#!/usr/bin/env python3
import struct
import time
import csv
import socket
import threading
import os
os.environ["MPLBACKEND"] = "TkAgg"

import tkinter as tk
from tkinter import ttk
from collections import deque
from datetime import datetime

import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.animation import FuncAnimation

PACKET_SIZE = 32
PACKET_PAYLOAD_LEN = 18
MAGIC_IMU   = 0x494D5550
MAGIC_DEBUG = 0x44424750
MAGIC_CMD   = 0x434D4430
MAGIC_ACK   = 0x41434B30

CMD_CALIBRATE  = 0x01
CMD_RESET      = 0x02
CMD_DEBUG_ON   = 0x03
CMD_DEBUG_OFF  = 0x04

ACCEL_SCALE = 1.0 / 16384.0
GYRO_SCALE  = 1.0 / 131.0

PLOT_WINDOW = 200
MAX_BUFFER  = 5000
UPDATE_MS   = 50

TCP_HOST = "127.0.0.1"
TCP_PORT = 12345

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
    calc = crc16(data[4:30])
    if calc != pkt_crc:
        return None
    return {"magic": magic, "seq": seq_nr, "timestamp": ts,
            "payload": payload}

class IMUGUI:
    def __init__(self):
        self.sock = None
        self.running = True
        self.pkt_count = 0
        self.lost_count = 0
        self.last_seq = 0
        self.start_time = time.time()
        self.last_frame_time = time.time()

        self.buf_ax = deque(maxlen=MAX_BUFFER)
        self.buf_ay = deque(maxlen=MAX_BUFFER)
        self.buf_az = deque(maxlen=MAX_BUFFER)
        self.buf_gx = deque(maxlen=MAX_BUFFER)
        self.buf_gy = deque(maxlen=MAX_BUFFER)
        self.buf_gz = deque(maxlen=MAX_BUFFER)
        self.debug_lines = deque(maxlen=200)

        self._init_tcp()
        self._init_gui()

    def _init_tcp(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.sock.connect((TCP_HOST, TCP_PORT))
            print(f"Connected to mock server at {TCP_HOST}:{TCP_PORT}")
        except ConnectionRefusedError:
            print(f"Could not connect to mock server at {TCP_HOST}:{TCP_PORT}")
            print("Start mock_server.py first.")
            exit(1)

    def _init_gui(self):
        self.root = tk.Tk()
        self.root.title("IMU Receiver v2.0 (MOCK)")
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        main = ttk.Frame(self.root)
        main.pack(fill=tk.BOTH, expand=True)

        plot_frame = ttk.Frame(main)
        plot_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        side = ttk.Frame(main, width=300)
        side.pack(side=tk.RIGHT, fill=tk.Y)
        side.pack_propagate(False)

        self.fig, (self.ax_accel, self.ax_gyro) = plt.subplots(
            2, 1, figsize=(8, 6))
        self.fig.suptitle("IMU Data — STM32 + nRF24L01+ (MOCK)", fontsize=12)
        self.ax_accel.set_ylabel("Acceleration (g)")
        self.ax_accel.set_xlim(0, PLOT_WINDOW)
        self.ax_accel.set_ylim(-3, 3)
        self.ax_accel.grid(True, alpha=0.3)
        self.l_ax, = self.ax_accel.plot([], [], "r-", lw=1, label="Ax")
        self.l_ay, = self.ax_accel.plot([], [], "g-", lw=1, label="Ay")
        self.l_az, = self.ax_accel.plot([], [], "b-", lw=1, label="Az")
        self.ax_accel.legend(loc="upper right")
        self.ax_gyro.set_xlabel("Sample")
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
        ttk.Button(ctrl_f, text="Save CSV",
                   command=self._save_csv
                   ).pack(fill=tk.X, pady=1)

        self.ani = FuncAnimation(self.fig, self._update_plot,
                                 interval=UPDATE_MS, blit=False)

        self.reader_th = threading.Thread(target=self._reader, daemon=True)
        self.reader_th.start()

        self.root.after(100, self._update_stats)

        print("Controls: [s] Save CSV")

    def _send_cmd(self, cmd_id):
        pkt = bytearray(PACKET_SIZE)
        struct.pack_into("<III", pkt, 0, MAGIC_CMD, 0, 0)
        pkt[12] = cmd_id
        crc = crc16(pkt[4:30])
        struct.pack_into("<H", pkt, 30, crc)
        try:
            self.sock.sendall(pkt)
        except OSError as e:
            pass

    def _save_csv(self):
        now = datetime.now().strftime("%Y%m%d_%H%M%S")
        fname = f"imu_{now}.csv"
        with open(fname, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["index", "ax_g", "ay_g", "az_g",
                        "gx_dps", "gy_dps", "gz_dps"])
            items = list(zip(self.buf_ax, self.buf_ay, self.buf_az,
                             self.buf_gx, self.buf_gy, self.buf_gz))
            for vals in items:
                w.writerow([vals[0][0],
                            vals[0][1], vals[1][1], vals[2][1],
                            vals[3][1], vals[4][1], vals[5][1]])
        self._log_debug(f"Saved {len(items)} samples to {fname}")

    def _log_debug(self, msg):
        self.debug_lines.append(msg)
        self.txt_dbg.configure(state=tk.NORMAL)
        self.txt_dbg.insert(tk.END, msg + "\n")
        self.txt_dbg.see(tk.END)
        self.txt_dbg.configure(state=tk.DISABLED)

    def _reader(self):
        buf = bytearray()
        while self.running:
            try:
                chunk = self.sock.recv(1024)
            except (socket.timeout, ConnectionResetError, BrokenPipeError, OSError):
                continue
            if not chunk:
                continue
            buf.extend(chunk)
            while len(buf) >= PACKET_SIZE:
                pkt = parse_packet(bytes(buf[:PACKET_SIZE]))
                buf = buf[PACKET_SIZE:]
                if pkt is None:
                    continue
                self.pkt_count += 1
                if self.last_seq and pkt["seq"]:
                    gap = (pkt["seq"] - self.last_seq - 1) & 0xFFFFFFFF
                    if gap and gap < 1000:
                        self.lost_count += gap
                self.last_seq = pkt["seq"]
                if pkt["magic"] == MAGIC_IMU:
                    pl = pkt["payload"]
                    ax, ay, az, gx, gy, gz = struct.unpack_from("<hhhhhh", pl, 0)
                    self.buf_ax.append((pkt["seq"], ax * ACCEL_SCALE))
                    self.buf_ay.append((pkt["seq"], ay * ACCEL_SCALE))
                    self.buf_az.append((pkt["seq"], az * ACCEL_SCALE))
                    self.buf_gx.append((pkt["seq"], gx * GYRO_SCALE))
                    self.buf_gy.append((pkt["seq"], gy * GYRO_SCALE))
                    self.buf_gz.append((pkt["seq"], gz * GYRO_SCALE))
                elif pkt["magic"] == MAGIC_DEBUG:
                    pl = pkt["payload"]
                    dlen = min(pl[0], 17)
                    msg = pl[1:1 + dlen].decode("utf-8", errors="replace")
                    self._log_debug(msg)
                elif pkt["magic"] == MAGIC_ACK:
                    self._log_debug(f"ACK cmd=0x{pkt['payload'][0]:02X}")
                self.last_frame_time = time.time()

    def _update_plot(self, frame):
        def get_y(buf):
            items = list(buf)[-PLOT_WINDOW:]
            if not items:
                return [], []
            x = [it[0] % PLOT_WINDOW for it in items]
            y = [it[1] for it in items]
            return x, y
        for i, buf in enumerate([
            self.buf_ax, self.buf_ay, self.buf_az,
            self.buf_gx, self.buf_gy, self.buf_gz
        ]):
            x, y = get_y(buf)
            [self.l_ax, self.l_ay, self.l_az,
             self.l_gx, self.l_gy, self.l_gz][i].set_data(x, y)
        self.ax_accel.relim()
        self.ax_accel.autoscale_view(scalex=False)
        self.ax_gyro.relim()
        self.ax_gyro.autoscale_view(scalex=False)
        return (self.l_ax, self.l_ay, self.l_az,
                self.l_gx, self.l_gy, self.l_gz)

    def _update_stats(self):
        elapsed = time.time() - self.start_time
        rate = self.pkt_count / elapsed if elapsed > 0 else 0
        total = self.pkt_count + self.lost_count
        loss_pct = 100.0 * self.lost_count / total if total else 0
        self.lbl_pkts.config(text=f"Packets: {self.pkt_count}")
        self.lbl_loss.config(text=f"Lost: {self.lost_count} ({loss_pct:.1f}%)")
        self.lbl_rate.config(text=f"Rate: {rate:.0f} Hz")
        self.root.after(500, self._update_stats)

    def _on_close(self):
        self.running = False
        self.root.quit()
        self.root.destroy()

    def run(self):
        self.root.bind("<KeyPress-s>", lambda e: self._save_csv())
        try:
            self.root.mainloop()
        except KeyboardInterrupt:
            self._on_close()

def main():
    gui = IMUGUI()
    gui.run()

if __name__ == "__main__":
    main()
