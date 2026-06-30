# Session Log — Wireless IMU System

> Last updated: 2026-06-28
> Resume: Read this file, then continue from **Next Steps** below.

---

## Project
Wireless IMU system: STM32F103C8T6 (client) + MPU6050 + nRF24L01+ → STM32F103C8T6 (server) → USB Bulk → PC GUI

All source: `/home/hakan/stm32-mpu6050/`
Remote: `git@github.com:hakanaydn/ActivityRecognition.git`

---

## Progress

### Done
- v1.0.0–v1.5.0: Full sensor stack (MPU6050 driver, FreeRTOS, I2C/UART DMA, fault handlers, calibration, SW LPF, gyro output)
- v1.6.0: nRF24L01+ register-level SPI driver, receiver bridge STM32, PC GUI, binary packet, commit + tag
- Folder restructured: `common/` (nRF24, packet, Drivers, Startup, Middlewares, linker), `client/`, `server/firmware/`, `server/pc-gui/`, `test/`
- **Packet**: 32-byte frames (`common/packet.h` + `common/packet.c`) — magic (IMU/DEBUG/CMD/ACK), seq counter, timestamp, 18B payload, CRC16-CCITT
- **nRF24 driver** (`common/nrf24/`): `NRF24_TransmitAck()` (TX + wait for ACK payload), `NRF24_SendAckPayload()` (pre-load ACK payload)
- **USB Bulk** (`server/firmware/Core/Src/usb_bulk.c`): register-level, EP0 control, EP1 IN 64B, EP2 OUT 64B, VID=0x0483 PID=0x5711
- **Server main** (`server/firmware/Core/Src/main.c`): register-level clock (HSE→PLL 72 MHz), nRF24 RX→USB Bulk IN, USB Bulk OUT→ACK payload via nRF24
- **Client main** (`client/Core/Src/main.c`): `debug_puts()`, `my_strcpy()`, debug ring buffer (16×48 chars), `sensor_task` uses `NRF24_TransmitAck`, `handle_ack_command()` for CMD_DEBUG_ON/OFF/CALIBRATE/RESET
- Both builds succeed with zero errors/warnings:
  - Server: 3320 text, 4 data, 4276 bss (7.6 KB)
  - Client: 22364 text, 36 data, 7756 bss (30.1 KB)
- `test/mock_server.py` + `test/mock_gui.py`: TCP-based mock (localhost:12345) simulating nRF24↔USB pipeline
- Headless mock test: 125 packets @ 50 Hz, 0 CRC errors, 0 lost, command round-trip verified
- CRC16-CCITT cross-validated C ↔ Python (bit-exact)
- tkinter/Pillow fixed: `pip install --user --force-reinstall Pillow`

### Verified Working
- Mock GUI connects to Mock Server, plots 6-axis sine data, stats show rate/loss
- Commands (Debug ON/OFF, Calibrate, Reset) send CMD packets, server responds with ACK
- Save CSV exports buffered data

### Blocked / Not Tested
- **Hardware flashing not yet attempted** (no ST-Link connected in session)
- **USB enumeration** not verified on PC
- **nRF24 range/connectivity** not tested with physical hardware
- **tkinter display** only works when user runs with X11 display

---

## Build Commands

```bash
# Build client firmware
cd ~/stm32-mpu6050/client && make -j4

# Build server firmware
cd ~/stm32-mpu6050/server/firmware && make -j4

# Flash (via ST-Link)
make flash    # run from respective directory

# Headless mock test
cd ~/stm32-mpu6050/test && python3 -c "
import sys; sys.path.insert(0,'.')
from mock_gui import *
import struct, time, socket, threading

server = socket.socket()
server.bind(('127.0.0.1', 12346))
server.listen(1)

def serve():
    conn, _ = server.accept()
    buf = b''
    for i in range(125):
        pkt = build_imu_packet(i+1, i*0.02)
        conn.sendall(pkt)
        time.sleep(0.005)
    conn.close()

t = threading.Thread(target=serve, daemon=True); t.start()
sock = socket.socket(); sock.connect(('127.0.0.1', 12346))
sock.settimeout(2)
ok = err = 0
while True:
    try:
        d = sock.recv(4096)
        if not d: break
        for off in range(0, len(d), 32):
            p = parse_packet(d[off:off+32])
            if p: ok+=1; last_p=p
            else: err+=1
    except: break
print(f'OK={ok} ERR={err}')
sock.close(); server.close()
"
```

---

## Next Steps (Resume Here)
1. **Flash server firmware**: `cd ~/stm32-mpu6050/server/firmware && make flash`, verify USB enumeration (`lsusb` → VID=0x0483 PID=0x5711)
2. **Flash client firmware**: `cd ~/stm32-mpu6050/client && make flash`
3. **Test real hardware**: `cd ~/stm32-mpu6050/server/pc-gui && python3 gui.py`
4. If USB not detected, adjust D+ pull-up timing in `USB_Bulk_Init()` (PA12 LOW duration)
5. Run mock GUI test (tkinter available):

   ```bash
   # Terminal 1:
   cd ~/stm32-mpu6050 && python3 test/mock_server.py
   # Terminal 2:
   cd ~/stm32-mpu6050 && python3 test/mock_gui.py
   ```

---

## Key Technical Details

| Item | Value |
|------|-------|
| MCU | STM32F103C8T6 (Cortex-M3, 64 KB flash, 20 KB RAM) |
| Toolchain | `/home/hakan/.local/opt/arm-none-eabi/usr/bin/` |
| No `<string.h>` | newlib not installed; use manual loops |
| ST-Link | OpenOCD; occasional retry needed |
| nRF24 addr | `{0xAA, 0x00, 0x00, 0x00, 0x01}` ch78 2Mbps ESB ACK |
| Packet size | 32 bytes (4 magic + 4 seq + 4 ts + 18 payload + 2 crc) |
| CRC16 | CCITT over bytes 4–29, polynomial 0x1021 |
| USB | FS 12 Mbps, vendor-specific, EP1 IN 64B, EP2 OUT 64B |
| IMU rate | 50 Hz |
| DLPF | SW EMA α=1/8 (gyro hardware DLPF non-functional) |
| Gyro Z bias | ≈ -0.5 °/s residual |
| Server flash | 7.6 KB |
| Client flash | 30.1 KB |
| Startup vector | `USB_LP_CAN1_RX0_IRQHandler` at position 20 (F103xB) |
| Board | Blue Pill, D+ pull-up R10=4.7 kΩ (non-spec 1.5 kΩ) |
