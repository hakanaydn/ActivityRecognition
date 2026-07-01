#!/usr/bin/env python3
import sys
sys.path.insert(0, "/home/hakan/.local/lib/python3.8/site-packages")
import struct
import time
import csv
import threading
import os
os.environ["MPLBACKEND"] = "TkAgg"

import tkinter as tk
from tkinter import ttk, filedialog
from collections import deque
from datetime import datetime

import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.animation import FuncAnimation

import serial
import serial.tools.list_ports

PACKET_SIZE = 32
MAGIC_IMU   = 0x494D5550
MAGIC_DEBUG = 0x44424750
MAGIC_CMD   = 0x434D4430
MAGIC_ACK   = 0x41434B30
MAGIC_STAT  = 0x53544154

CMD_CALIBRATE     = 0x01
CMD_RESET         = 0x02
CMD_DEBUG_ON      = 0x03
CMD_DEBUG_OFF     = 0x04
CMD_TEST_DATA_ON  = 0x06
CMD_TEST_DATA_OFF = 0x07

ACCEL_SCALE = 1.0 / 16384.0
GYRO_SCALE  = 1.0 / 131.0

PLOT_WINDOW = 10
MAX_BUFFER  = 5000
UPDATE_MS   = 50
COM_BAUD    = 921600

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
    return crc & 0xFFFF

def parse_packet(data):
    if len(data) < PACKET_SIZE:
        return None
    magic, seq_nr, ts = struct.unpack_from("<III", data, 0)
    payload = data[12:30]
    pkt_crc = struct.unpack_from("<H", data, 30)[0]
    calc = crc16(data[:30])
    if calc != pkt_crc:
        return None
    return {"magic": magic, "seq": seq_nr, "timestamp": ts,
            "payload": payload}

class StatusBar:
    def __init__(self, parent):
        self.frame = ttk.LabelFrame(parent, text="Status", padding=5)
        self.frame.pack(fill=tk.X, padx=5, pady=5)

        row = ttk.Frame(self.frame)
        row.pack(fill=tk.X, pady=1)

        self.nrf_canvas = tk.Canvas(row, width=16, height=16,
                                     highlightthickness=0)
        self.nrf_canvas.pack(side=tk.LEFT, padx=(0, 4))
        self.nrf_dot = self.nrf_canvas.create_oval(2, 2, 14, 14,
                                                    fill="gray",
                                                    outline="gray")
        self.nrf_label = ttk.Label(row, text="nRF24: --")
        self.nrf_label.pack(side=tk.LEFT)

        row2 = ttk.Frame(self.frame)
        row2.pack(fill=tk.X, pady=1)
        self.uart_canvas = tk.Canvas(row2, width=16, height=16,
                                      highlightthickness=0)
        self.uart_canvas.pack(side=tk.LEFT, padx=(0, 4))
        self.uart_dot = self.uart_canvas.create_oval(2, 2, 14, 14,
                                                      fill="gray",
                                                      outline="gray")
        self.uart_label = ttk.Label(row2, text="UART: --")
        self.uart_label.pack(side=tk.LEFT)

        row3 = ttk.Frame(self.frame)
        row3.pack(fill=tk.X, pady=1)
        self.data_canvas = tk.Canvas(row3, width=16, height=16,
                                      highlightthickness=0)
        self.data_canvas.pack(side=tk.LEFT, padx=(0, 4))
        self.data_dot = self.data_canvas.create_oval(2, 2, 14, 14,
                                                      fill="gray",
                                                      outline="gray")
        self.data_label = ttk.Label(row3, text="Data: --")
        self.data_label.pack(side=tk.LEFT)

    def set_nrf(self, status):
        if status == 0:
            color = "#cc0000"
            text = "nRF24: Init FAILED"
        elif status == 1:
            color = "#ffaa00"
            text = "nRF24: Waiting for link"
        elif status == 2:
            color = "#00cc00"
            text = "nRF24: Connected"
        else:
            color = "gray"
            text = "nRF24: --"
        self.nrf_canvas.itemconfig(self.nrf_dot, fill=color, outline=color)
        self.nrf_label.config(text=text)

    def set_nrf_unknown(self):
        self.nrf_canvas.itemconfig(self.nrf_dot, fill="gray", outline="gray")
        self.nrf_label.config(text="nRF24: --")

    def set_uart(self, connected):
        color = "#00cc00" if connected else "#cc0000"
        text = "UART: Connected" if connected else "UART: Disconnected"
        self.uart_canvas.itemconfig(self.uart_dot, fill=color, outline=color)
        self.uart_label.config(text=text)

    def set_data_active(self, active):
        color = "#00ff00" if active else "#888888"
        self.data_canvas.itemconfig(self.data_dot, fill=color, outline=color)
        self.data_label.config(text="Data: Active" if active else "Data: Idle")

class Notification(tk.Toplevel):
    def __init__(self, parent, title, msg, color="#333", duration=3000):
        super().__init__(parent)
        self.overrideredirect(True)
        self.attributes("-topmost", True)
        f = ttk.Frame(self, padding=12)
        f.pack()
        ttk.Label(f, text=title, font=("", 10, "bold"),
                  foreground=color).pack()
        ttk.Label(f, text=msg).pack(pady=(4, 0))
        self.update_idletasks()
        px = parent.winfo_rootx() + parent.winfo_width() // 2 - self.winfo_width() // 2
        py = parent.winfo_rooty() + 60
        self.geometry(f"+{px}+{py}")
        self.after(duration, self.destroy)

class IMUGUI:
    def __init__(self):
        self.ser = None
        self.running = True
        self.pkt_count = 0
        self.lost_count = 0
        self.last_seq = 0
        self.start_time = time.time()
        self._rx_buf = b""
        self._reader_th = None
        self._last_imu_time = 0
        self._last_stat_time = 0
        self._nrf_status = -1
        self._crc_errors = 0
        self._t0 = time.time()
        self._frozen = True

        self.buf_ax = deque(maxlen=MAX_BUFFER)
        self.buf_ay = deque(maxlen=MAX_BUFFER)
        self.buf_az = deque(maxlen=MAX_BUFFER)
        self.buf_gx = deque(maxlen=MAX_BUFFER)
        self.buf_gy = deque(maxlen=MAX_BUFFER)
        self.buf_gz = deque(maxlen=MAX_BUFFER)
        self.debug_lines = deque(maxlen=200)
        self.pkt_log = deque(maxlen=500)
        self._pktlog_win = None

        self._init_gui()

    def _list_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def _connect_port(self):
        port = self.port_var.get()
        if not port:
            return
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.ser = serial.Serial(port, COM_BAUD, timeout=0)
            self.ser.reset_input_buffer()
            self._rx_buf = b""
            self._clear_plot_data()
            self._t0 = time.time()
            self._set_frozen(False)
            self.conn_status.config(text=f"\u2713 STM32 connected ({port})",
                                    foreground="green")
            self.status.set_uart(True)
            self.connect_btn.config(text="Disconnect",
                                    command=self._disconnect_port)
            self.port_combo.config(state=tk.DISABLED)
            self.refresh_btn.config(state=tk.DISABLED)
            print(f"Serial port {port} opened at {COM_BAUD} baud")
            self._reader_th = threading.Thread(target=self._reader,
                                               daemon=True)
            self._reader_th.start()
        except serial.SerialException as e:
            self.conn_status.config(text=f"Error: {e}", foreground="red")
            self.status.set_uart(False)

    def _disconnect_port(self):
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None
        self.conn_status.config(text="Disconnected", foreground="black")
        self.connect_btn.config(text="Connect", command=self._connect_port)
        self.port_combo.config(state=tk.NORMAL)
        self.refresh_btn.config(state=tk.NORMAL)
        self.status.set_uart(False)
        self.status.set_nrf_unknown()
        self.status.set_data_active(False)
        self._clear_plot_data()
        self.running = True

    def _refresh_ports(self):
        ports = self._list_ports()
        self.port_combo["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _init_gui(self):
        self.root = tk.Tk()
        self.root.title("IMU Receiver v2.0 — Serial")
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        top = ttk.Frame(self.root)
        top.pack(fill=tk.X, padx=5, pady=5)

        conn_f = ttk.LabelFrame(top, text="Connection", padding=5)
        conn_f.pack(fill=tk.X)
        row = ttk.Frame(conn_f)
        row.pack(fill=tk.X)
        ports = self._list_ports()
        self.port_var = tk.StringVar(value=ports[0] if ports else "")
        self.port_combo = ttk.Combobox(row, textvariable=self.port_var,
                                       values=ports, width=18)
        self.port_combo.pack(side=tk.LEFT, padx=(0, 4))
        self.refresh_btn = ttk.Button(row, text="R", width=2,
                                      command=self._refresh_ports)
        self.refresh_btn.pack(side=tk.LEFT)
        self.connect_btn = ttk.Button(conn_f, text="Connect",
                                      command=self._connect_port)
        self.connect_btn.pack(fill=tk.X, pady=(4, 0))
        self.conn_status = ttk.Label(conn_f, text="Not connected",
                                     foreground="gray")
        self.conn_status.pack(anchor=tk.W, pady=(2, 0))

        main = ttk.Frame(self.root)
        main.pack(fill=tk.BOTH, expand=True)

        plot_frame = ttk.Frame(main)
        plot_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        side = ttk.Frame(main, width=300)
        side.pack(side=tk.RIGHT, fill=tk.Y)
        side.pack_propagate(False)

        self.status = StatusBar(side)

        self.fig, (self.ax_accel, self.ax_gyro) = plt.subplots(
            2, 1, figsize=(8, 6))
        self.fig.suptitle("IMU Data — STM32 + nRF24L01+", fontsize=12)
        self.ax_accel.set_ylabel("Acceleration (g)")
        self.ax_accel.set_xlim(0, PLOT_WINDOW)
        self.ax_accel.set_ylim(-3, 3)
        self.ax_accel.grid(True, alpha=0.3)
        self.l_ax, = self.ax_accel.plot([], [], "r-", lw=1, label="Ax")
        self.l_ay, = self.ax_accel.plot([], [], "g-", lw=1, label="Ay")
        self.l_az, = self.ax_accel.plot([], [], "b-", lw=1, label="Az")
        self.ax_accel.legend(loc="upper right")
        self.ax_gyro.set_xlabel("Time (s)")
        self.ax_gyro.set_ylabel("Angular rate (\u00b0/s)")
        self.ax_gyro.set_xlim(0, PLOT_WINDOW)
        self.ax_gyro.set_ylim(-250, 250)
        self.ax_gyro.grid(True, alpha=0.3)
        self.l_gx, = self.ax_gyro.plot([], [], "c-", lw=1, label="Gx")
        self.l_gy, = self.ax_gyro.plot([], [], "m-", lw=1, label="Gy")
        self.l_gz, = self.ax_gyro.plot([], [], "y-", lw=1, label="Gz")
        self.ax_gyro.legend(loc="upper right")
        self.fig.tight_layout()

        canvas = FigureCanvasTkAgg(self.fig, master=plot_frame)
        toolbar = NavigationToolbar2Tk(canvas, plot_frame)
        toolbar.update()
        canvas.draw()
        canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        stats_f = ttk.LabelFrame(side, text="Stats", padding=5)
        stats_f.pack(fill=tk.X, padx=5, pady=5)
        self.lbl_pkts = ttk.Label(stats_f, text="Packets: 0")
        self.lbl_pkts.pack(anchor=tk.W)
        self.lbl_loss = ttk.Label(stats_f, text="Lost: 0 (0.0%)")
        self.lbl_loss.pack(anchor=tk.W)
        self.lbl_rate = ttk.Label(stats_f, text="Rate: 0 Hz")
        self.lbl_rate.pack(anchor=tk.W)
        self.lbl_crc = ttk.Label(stats_f, text="CRC errors: 0")
        self.lbl_crc.pack(anchor=tk.W)
        self.lbl_link = ttk.Label(stats_f, text="Link: --")
        self.lbl_link.pack(anchor=tk.W)

        dbg_f = ttk.LabelFrame(side, text="Debug Console", padding=5)
        dbg_f.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.txt_dbg = tk.Text(dbg_f, height=10, width=35,
                               state=tk.DISABLED, font=("Monospace", 8),
                               wrap=tk.WORD)
        self.txt_dbg.pack(fill=tk.BOTH, expand=True)
        dbg_scroll = ttk.Scrollbar(self.txt_dbg, command=self.txt_dbg.yview)
        self.txt_dbg.configure(yscrollcommand=dbg_scroll.set)
        dbg_scroll.pack(side=tk.RIGHT, fill=tk.Y)

        ctrl_f = ttk.LabelFrame(side, text="Commands", padding=5)
        ctrl_f.pack(fill=tk.X, padx=5, pady=5)
        ttk.Button(ctrl_f, text="Debug ON",
                   command=lambda: self._send_cmd(CMD_DEBUG_ON)
                   ).pack(fill=tk.X, pady=1)
        ttk.Button(ctrl_f, text="Debug OFF",
                   command=lambda: self._send_cmd(CMD_DEBUG_OFF)
                   ).pack(fill=tk.X, pady=1)
        ttk.Button(ctrl_f, text="Calibrate",
                   command=lambda: self._send_cmd(CMD_CALIBRATE)
                   ).pack(fill=tk.X, pady=1)
        ttk.Button(ctrl_f, text="Reset Sensor",
                   command=lambda: self._send_cmd(CMD_RESET)
                   ).pack(fill=tk.X, pady=1)
        ttk.Button(ctrl_f, text="Send Test Data",
                   command=lambda: self._send_cmd(CMD_TEST_DATA_ON)
                   ).pack(fill=tk.X, pady=1)
        ttk.Button(ctrl_f, text="Stop Test Data",
                   command=lambda: self._send_cmd(CMD_TEST_DATA_OFF)
                   ).pack(fill=tk.X, pady=1)
        ttk.Separator(ctrl_f, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=4)
        ttk.Button(ctrl_f, text="Packet Log",
                   command=self._open_pktlog
                   ).pack(fill=tk.X, pady=1)
        ttk.Button(ctrl_f, text="Clear Plot",
                   command=self._clear_plot
                   ).pack(fill=tk.X, pady=1)
        ttk.Button(ctrl_f, text="Save CSV",
                   command=self._save_as
                   ).pack(fill=tk.X, pady=1)
        ttk.Button(ctrl_f, text="Save PNG",
                   command=self._save_png
                   ).pack(fill=tk.X, pady=1)

        self.ani = FuncAnimation(self.fig, self._update_plot,
                                 interval=UPDATE_MS, blit=False,
                                 save_count=100)

        self.root.after(100, self._update_stats)
        print("Controls: Ctrl+S Save CSV")

    def _send_cmd(self, cmd_id):
        if not self.ser or not self.ser.is_open:
            return
        pkt = bytearray(PACKET_SIZE)
        struct.pack_into("<III", pkt, 0, MAGIC_CMD, 0, 0)
        pkt[12] = cmd_id
        crc = crc16(pkt[:30])
        struct.pack_into("<H", pkt, 30, crc)
        try:
            self.ser.write(pkt)
        except serial.SerialException:
            pass
        if cmd_id == CMD_TEST_DATA_ON:
            self._set_frozen(False)
        elif cmd_id == CMD_TEST_DATA_OFF:
            self._set_frozen(True)

    def _set_frozen(self, frozen):
        self._frozen = frozen
        if not frozen:
            self._clear_plot_data()
            self._t0 = time.time()
        self._log_debug("Plot " + ("frozen" if frozen else "running"))

    def _clear_plot_data(self):
        self.buf_ax.clear()
        self.buf_ay.clear()
        self.buf_az.clear()
        self.buf_gx.clear()
        self.buf_gy.clear()
        self.buf_gz.clear()

    def _clear_plot(self):
        self._clear_plot_data()
        self._log_debug("Plot cleared")

    def _save_csv(self, filepath=None):
        if not filepath:
            now = datetime.now().strftime("%Y%m%d_%H%M%S")
            filepath = f"imu_{now}.csv"
        start_abs = time.time() - self._t0
        with open(filepath, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["timestamp", "ax_g", "ay_g", "az_g",
                        "gx_dps", "gy_dps", "gz_dps"])
            items = list(zip(self.buf_ax, self.buf_ay, self.buf_az,
                             self.buf_gx, self.buf_gy, self.buf_gz))
            for vals in items:
                ts = datetime.fromtimestamp(vals[0][0] + start_abs).strftime(
                    "%H:%M:%S.%f")[:12]
                w.writerow([ts,
                            f"{vals[0][1]:.6f}", f"{vals[1][1]:.6f}",
                            f"{vals[2][1]:.6f}", f"{vals[3][1]:.2f}",
                            f"{vals[4][1]:.2f}", f"{vals[5][1]:.2f}"])
        self._log_debug(f"Saved {len(items)} samples to {filepath}")

    def _save_as(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
            initialfile=f"imu_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv")
        if path:
            self._save_csv(path)

    def _save_png(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".png",
            filetypes=[("PNG files", "*.png"), ("All files", "*.*")],
            initialfile=f"imu_{datetime.now().strftime('%Y%m%d_%H%M%S')}.png")
        if path:
            self.fig.savefig(path, dpi=150, bbox_inches="tight")
            self._log_debug(f"Plot saved to {path}")

    def _log_debug(self, msg):
        self.debug_lines.append(msg)
        self.txt_dbg.configure(state=tk.NORMAL)
        self.txt_dbg.insert(tk.END, msg + "\n")
        self.txt_dbg.see(tk.END)
        self.txt_dbg.configure(state=tk.DISABLED)

    def _open_pktlog(self):
        if self._pktlog_win is not None:
            try:
                self._pktlog_win.lift()
                return
            except tk.TclError:
                self._pktlog_win = None
        win = tk.Toplevel(self.root)
        win.title("Packet Log")
        win.geometry("800x400")
        self._pktlog_win = win
        txt = tk.Text(win, font=("Monospace", 9), state=tk.DISABLED)
        txt.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        scroll = ttk.Scrollbar(txt, command=txt.yview)
        txt.configure(yscrollcommand=scroll.set)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)

        def poll():
            if not self._pktlog_win:
                return
            try:
                self._pktlog_win.state()
            except tk.TclError:
                self._pktlog_win = None
                return
            txt.configure(state=tk.NORMAL)
            n = len(self.pkt_log)
            txt.delete("1.0", tk.END)
            for line in self.pkt_log:
                txt.insert(tk.END, line + "\n")
            txt.see(tk.END)
            txt.configure(state=tk.DISABLED)
            win.after(200, poll)

        def on_close():
            self._pktlog_win = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", on_close)
        win.after(200, poll)

    def _reader(self):
        while self.running:
            if not self.ser or not self.ser.is_open:
                time.sleep(0.05)
                continue
            try:
                data = self.ser.read(256)
            except serial.SerialException:
                self.root.after(0, self._on_serial_error)
                time.sleep(0.5)
                continue
            if not data:
                time.sleep(0.01)
                continue
            self._rx_buf += data
            while len(self._rx_buf) >= PACKET_SIZE:
                pkt = parse_packet(self._rx_buf[:PACKET_SIZE])
                if pkt is None:
                    self._rx_buf = self._rx_buf[1:]
                    continue
                self._rx_buf = self._rx_buf[PACKET_SIZE:]

                if pkt["magic"] == MAGIC_STAT:
                    pl = pkt["payload"]
                    self._nrf_status = pl[0]
                    self._crc_errors = pl[4] | (pl[5] << 8)
                    self._last_stat_time = time.time()
                    self.status.set_nrf(self._nrf_status)
                    continue

                self.pkt_count += 1
                if self.last_seq and pkt["seq"]:
                    gap = (pkt["seq"] - self.last_seq - 1) & 0xFFFFFFFF
                    if gap and gap < 1000:
                        self.lost_count += gap
                self.last_seq = pkt["seq"]

                if pkt["magic"] == MAGIC_IMU:
                    pl = pkt["payload"]
                    ax, ay, az, gx, gy, gz = struct.unpack_from("<hhhhhh",
                                                                  pl, 0)
                    t = time.time() - self._t0
                    self._last_imu_time = time.time()
                    raw_hex = pl[:18].hex().upper()
                    self.pkt_log.append(
                        f"seq={pkt['seq']:5d}  "
                        f"ax={ax*ACCEL_SCALE:+.4f}  ay={ay*ACCEL_SCALE:+.4f}  "
                        f"az={az*ACCEL_SCALE:+.4f}  "
                        f"gx={gx*GYRO_SCALE:+7.2f}  gy={gy*GYRO_SCALE:+7.2f}  "
                        f"gz={gz*GYRO_SCALE:+7.2f}  PL={raw_hex}")
                    self.buf_ax.append((t, ax * ACCEL_SCALE))
                    self.buf_ay.append((t, ay * ACCEL_SCALE))
                    self.buf_az.append((t, az * ACCEL_SCALE))
                    self.buf_gx.append((t, gx * GYRO_SCALE))
                    self.buf_gy.append((t, gy * GYRO_SCALE))
                    self.buf_gz.append((t, gz * GYRO_SCALE))
                elif pkt["magic"] == MAGIC_DEBUG:
                    pl = pkt["payload"]
                    dlen = min(pl[0], 17)
                    msg = pl[1:1 + dlen].decode("utf-8", errors="replace")
                    self._log_debug(msg)

    def _on_serial_error(self):
        if not self.ser:
            return
        Notification(self.root, "Connection Lost",
                     "STM32 disconnected!", "#cc0000", 4000)
        self.running = False
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
        except Exception:
            pass
        self.ser = None
        self.conn_status.config(text="Disconnected", foreground="black")
        self.status.set_uart(False)
        self.connect_btn.config(text="Connect", command=self._connect_port)
        self.port_combo.config(state=tk.NORMAL)
        self.refresh_btn.config(state=tk.NORMAL)
        self.status.set_nrf_unknown()
        self.status.set_data_active(False)
        self._clear_plot_data()
        self.running = True

    def _update_plot(self, frame):
        if not self._frozen:
            tmin = time.time() - self._t0 - PLOT_WINDOW
            tmax = time.time() - self._t0
            self.ax_accel.set_xlim(tmin, tmax)
            self.ax_gyro.set_xlim(tmin, tmax)
        else:
            tmin, tmax = self.ax_accel.get_xlim()

        lines = [self.l_ax, self.l_ay, self.l_az,
                 self.l_gx, self.l_gy, self.l_gz]
        bufs = [self.buf_ax, self.buf_ay, self.buf_az,
                self.buf_gx, self.buf_gy, self.buf_gz]

        for line, buf in zip(lines, bufs):
            items = [it for it in buf if tmin <= it[0] <= tmax]
            if items:
                line.set_data([it[0] for it in items],
                              [it[1] for it in items])
            else:
                line.set_data([], [])

        self.ax_accel.relim()
        self.ax_accel.autoscale_view(scalex=False)
        self.ax_gyro.relim()
        self.ax_gyro.autoscale_view(scalex=False)
        return lines

    def _update_stats(self):
        try:
            elapsed = time.time() - self.start_time
            rate = self.pkt_count / elapsed if elapsed > 0 else 0
            total = self.pkt_count + self.lost_count
            loss_pct = 100.0 * self.lost_count / total if total else 0
            self.lbl_pkts.config(text=f"Packets: {self.pkt_count}")
            self.lbl_loss.config(text=f"Lost: {self.lost_count} ({loss_pct:.1f}%)")
            self.lbl_rate.config(text=f"Rate: {rate:.0f} Hz")
            self.lbl_crc.config(text=f"CRC errors: {self._crc_errors}")

            now = time.time()
            stat_age = now - self._last_stat_time if self._last_stat_time else 999
            imu_age = now - self._last_imu_time if self._last_imu_time else 999

            if self._frozen:
                self.lbl_link.config(text="Plot: FROZEN")
            elif not self._last_stat_time:
                self.lbl_link.config(text="Link: waiting...")
                self.status.set_data_active(False)
            elif stat_age > 5:
                self.status.set_nrf_unknown()
                self.lbl_link.config(text="Link: no STAT packet")
                self.status.set_data_active(False)
            elif imu_age < 3:
                self.status.set_data_active(True)
                self.lbl_link.config(text=f"Data: {(imu_age)*1000:.0f}ms ago")
            else:
                self.status.set_data_active(False)
                self.lbl_link.config(text=f"Idle: {imu_age:.0f}s")

            self.root.after(500, self._update_stats)
        except:
            pass

    def _on_close(self):
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.root.quit()
        self.root.destroy()

    def run(self):
        self.root.bind("<Control-s>", lambda e: self._save_as())
        try:
            self.root.mainloop()
        except KeyboardInterrupt:
            self._on_close()

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else None
    gui = IMUGUI()
    if port:
        gui.port_var.set(port)
        gui._connect_port()
    gui.run()

if __name__ == "__main__":
    main()
