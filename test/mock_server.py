#!/usr/bin/env python3
import socket
import struct
import time
import threading
import math

PACKET_SIZE = 32
PACKET_PAYLOAD_LEN = 18
MAGIC_IMU   = 0x494D5550
MAGIC_DEBUG = 0x44424750
MAGIC_CMD   = 0x434D4430
MAGIC_ACK   = 0x41434B30

HOST = "127.0.0.1"
PORT = 12345

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
    return crc & 0xFFFF

def build_imu_packet(seq, t):
    ax = int(16384 * 0.5 * math.sin(2 * math.pi * 0.5 * t))
    ay = int(16384 * 0.8 * math.sin(2 * math.pi * 1.0 * t + 0.5))
    az = int(16384 * 1.0 * math.sin(2 * math.pi * 2.0 * t + 1.2))
    gx = int(131 * 30 * math.sin(2 * math.pi * 0.3 * t))
    gy = int(131 * 20 * math.sin(2 * math.pi * 0.7 * t + 0.8))
    gz = int(131 * 10 * math.sin(2 * math.pi * 1.5 * t + 2.1))
    lpf_enabled = 1
    batt_mv = 3300
    payload = struct.pack("<hhhhhh", ax, ay, az, gx, gy, gz)
    payload += struct.pack("<BH", lpf_enabled, batt_mv)
    payload += b"\x00\x00\x00"

    pkt = bytearray(PACKET_SIZE)
    struct.pack_into("<III", pkt, 0, MAGIC_IMU, seq, 0)
    pkt[12:30] = payload
    crc = crc16(pkt[:30])
    struct.pack_into("<H", pkt, 30, crc)
    return pkt

def build_ack_packet(cmd_id, seq):
    pkt = bytearray(PACKET_SIZE)
    struct.pack_into("<III", pkt, 0, MAGIC_ACK, seq, 0)
    pkt[12] = cmd_id
    crc = crc16(pkt[:30])
    struct.pack_into("<H", pkt, 30, crc)
    return pkt

def handle_client(conn, addr):
    print(f"[+] Client connected: {addr}")
    seq = 1
    t0 = time.time()
    running = True
    conn.settimeout(0.01)

    def reader():
        nonlocal running, seq
        buf = bytearray()
        while running:
            try:
                chunk = conn.recv(1024)
            except socket.timeout:
                continue
            except (ConnectionResetError, BrokenPipeError, OSError):
                running = False
                break
            if not chunk:
                running = False
                break
            buf.extend(chunk)
            while len(buf) >= PACKET_SIZE:
                pkt = bytes(buf[:PACKET_SIZE])
                buf = buf[PACKET_SIZE:]
                magic, cmd_seq, ts = struct.unpack_from("<III", pkt, 0)
                if magic == MAGIC_CMD:
                    cmd_id = pkt[12]
                    print(f"  [CMD] seq={cmd_seq}, cmd_id=0x{cmd_id:02X}")
                    try:
                        ack = build_ack_packet(cmd_id, cmd_seq)
                        conn.sendall(ack)
                    except OSError:
                        running = False

    rt = threading.Thread(target=reader, daemon=True)
    rt.start()

    try:
        while running:
            t = time.time() - t0
            pkt = build_imu_packet(seq, t)
            try:
                conn.sendall(pkt)
            except (BrokenPipeError, ConnectionResetError, OSError):
                break
            seq += 1
            time.sleep(0.02)
    finally:
        running = False
        conn.close()
        print(f"[-] Client disconnected: {addr}")

def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(1)
    print(f"[*] Mock IMU server listening on {HOST}:{PORT}")
    print("    Sending sine-wave IMU packets at 50 Hz")
    print("    Press Ctrl+C to stop")
    try:
        while True:
            conn, addr = server.accept()
            handle_client(conn, addr)
    except KeyboardInterrupt:
        print("\n[*] Shutting down")
    finally:
        server.close()

if __name__ == "__main__":
    main()
