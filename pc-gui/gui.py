#!/usr/bin/env python3
"""
IMU Data Visualizer — PC GUI for nRF24L01 + STM32 receiver bridge.
Reads binary IMU packets from serial port and plots 6 axes in real-time.
"""

import sys
import struct
import time
import csv
import threading
from collections import deque
from datetime import datetime

import serial
import serial.tools.list_ports
import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

# Packet format: uint32_t index + int16_t ax, ay, az, gx, gy, gz = 16 bytes
PACKET_FMT = "<Ihhhhhh"
PACKET_SIZE = struct.calcsize(PACKET_FMT)
ACCEL_SCALE = 1.0 / 16384.0   # LSB → g
GYRO_SCALE  = 1.0 / 131.0     # LSB → °/s

# Plot config
PLOT_WINDOW = 200              # samples shown on screen
MAX_BUFFER  = 5000             # retained in memory for CSV save
UPDATE_MS   = 50               # plot refresh interval

# Buffers: each is a deque of (index, value) pairs
buf_ax = deque(maxlen=MAX_BUFFER)
buf_ay = deque(maxlen=MAX_BUFFER)
buf_az = deque(maxlen=MAX_BUFFER)
buf_gx = deque(maxlen=MAX_BUFFER)
buf_gy = deque(maxlen=MAX_BUFFER)
buf_gz = deque(maxlen=MAX_BUFFER)

ser = None
ser_lock = threading.Lock()
running = True
pkt_index = 0


def find_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = (p.description or "").lower()
        if "stlink" in desc or "stm32" in desc or "usb-uart" in desc:
            return p.device
    if ports:
        return ports[0].device
    return None


def open_serial(port=None, baud=115200):
    global ser
    if port is None:
        port = find_port()
        if port is None:
            print("No serial port found. Use --port <device>")
            sys.exit(1)
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
        ser.reset_input_buffer()
        print(f"Connected to {port} @ {baud} baud")
        return True
    except Exception as e:
        print(f"Serial error: {e}")
        return False


def parse_packet(data):
    """Parse 16-byte binary packet. Returns (index, [ax, ay, az, gx, gy, gz]) or None."""
    if len(data) < PACKET_SIZE:
        return None
    try:
        idx, ax, ay, az, gx, gy, gz = struct.unpack(PACKET_FMT, data[:PACKET_SIZE])
        return idx, [
            ax * ACCEL_SCALE,
            ay * ACCEL_SCALE,
            az * ACCEL_SCALE,
            gx * GYRO_SCALE,
            gy * GYRO_SCALE,
            gz * GYRO_SCALE,
        ]
    except struct.error:
        return None


def reader_thread():
    global pkt_index, running
    buf = bytearray()
    while running:
        if ser is None or not ser.is_open:
            time.sleep(0.1)
            continue
        try:
            with ser_lock:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting)
            if data:
                buf.extend(data)
        except serial.SerialException:
            time.sleep(0.1)
            continue
        except Exception:
            time.sleep(0.1)
            continue

        while len(buf) >= PACKET_SIZE:
            pkt = parse_packet(buf[:PACKET_SIZE])
            if pkt:
                idx, vals = pkt
                pkt_index = idx
                buf_ax.append((idx, vals[0]))
                buf_ay.append((idx, vals[1]))
                buf_az.append((idx, vals[2]))
                buf_gx.append((idx, vals[3]))
                buf_gy.append((idx, vals[4]))
                buf_gz.append((idx, vals[5]))
                buf = buf[PACKET_SIZE:]
            else:
                buf.pop(0)
        time.sleep(0.001)


def setup_plot():
    fig, (ax_accel, ax_gyro) = plt.subplots(2, 1, figsize=(12, 8))
    fig.suptitle("IMU Data — STM32 + nRF24L01", fontsize=14)

    ax_accel.set_ylabel("Acceleration (g)")
    ax_accel.set_xlim(0, PLOT_WINDOW)
    ax_accel.set_ylim(-3, 3)
    ax_accel.grid(True, alpha=0.3)
    line_ax, = ax_accel.plot([], [], "r-", label="Ax", lw=1)
    line_ay, = ax_accel.plot([], [], "g-", label="Ay", lw=1)
    line_az, = ax_accel.plot([], [], "b-", label="Az", lw=1)
    ax_accel.legend(loc="upper right")

    ax_gyro.set_xlabel("Sample")
    ax_gyro.set_ylabel("Angular rate (°/s)")
    ax_gyro.set_xlim(0, PLOT_WINDOW)
    ax_gyro.set_ylim(-250, 250)
    ax_gyro.grid(True, alpha=0.3)
    line_gx, = ax_gyro.plot([], [], "c-", label="Gx", lw=1)
    line_gy, = ax_gyro.plot([], [], "m-", label="Gy", lw=1)
    line_gz, = ax_gyro.plot([], [], "y-", label="Gz", lw=1)
    ax_gyro.legend(loc="upper right")

    fig.tight_layout()
    return fig, (line_ax, line_ay, line_az, line_gx, line_gy, line_gz), (ax_accel, ax_gyro)


def update(frame, lines, axes):
    def get_y(buf):
        # Return last PLOT_WINDOW values; align x by index
        items = list(buf)[-PLOT_WINDOW:]
        if not items:
            return [], []
        if len(items) < PLOT_WINDOW:
            x = list(range(len(items)))
        else:
            x = [it[0] % PLOT_WINDOW for it in items]
        y = [it[1] for it in items]
        return x, y

    for i, buf in enumerate([buf_ax, buf_ay, buf_az, buf_gx, buf_gy, buf_gz]):
        x, y = get_y(buf)
        lines[i].set_data(x, y)

    for ax in axes:
        ax.relim()
        ax.autoscale_view(scalex=False)
    return lines


def save_csv():
    now = datetime.now().strftime("%Y%m%d_%H%M%S")
    fname = f"imu_{now}.csv"
    with open(fname, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["index", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps"])
        items = list(zip(buf_ax, buf_ay, buf_az, buf_gx, buf_gy, buf_gz))
        for vals in items:
            w.writerow([vals[0][0],
                        vals[0][1], vals[1][1], vals[2][1],
                        vals[3][1], vals[4][1], vals[5][1]])
    print(f"Saved {len(items)} samples to {fname}")


def send_cmd(cmd_str):
    """Send command string to sensor via serial (→ receiver nRF24 → sensor)."""
    cmd = (cmd_str.strip() + "\n").encode()
    with ser_lock:
        if ser and ser.is_open:
            ser.write(cmd)
            print(f"Sent: {cmd_str}")


def main():
    import argparse
    parser = argparse.ArgumentParser(description="IMU Data Visualizer")
    parser.add_argument("--port", "-p", help="Serial port")
    parser.add_argument("--baud", "-b", type=int, default=115200)
    args = parser.parse_args()

    if not open_serial(args.port, args.baud):
        return

    # Start reader thread
    t = threading.Thread(target=reader_thread, daemon=True)
    t.start()

    fig, lines, axes = setup_plot()

    def on_key(event):
        if event.key == "s":
            save_csv()
        elif event.key == "r":
            send_cmd("reset")

    fig.canvas.mpl_connect("key_press_event", on_key)

    ani = FuncAnimation(fig, update, fargs=(lines, axes),
                        interval=UPDATE_MS, blit=False)

    print("\nControls:")
    print("  [s]  Save current buffer to CSV")
    print("  [r]  Send 'reset' command to sensor")
    print("  Close window to exit\n")

    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        global running
        running = False
        if ser and ser.is_open:
            ser.close()


if __name__ == "__main__":
    main()
