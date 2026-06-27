# Session Özeti — stm32-mpu6050

> **Her konuşma sonunda güncellenir.** Yeni bir opencode oturumunda önce bu dosyayı bana ilet, kaldığım yerden devam edeyim.

## Son Oturum (2026-06-27)

### Yapılanlar
- **v1.6.0**: nRF24L01+ wireless link + receiver bridge STM32 + PC GUI
- **nRF24L01 driver** (`nrf24/nrf24.c`): register-level SPI1 (PA5,6,7), CSN=PB0, CE=PB1, Enhanced ShockBurst, TX/RX, 2Mbps, channel 78, AA:00:00:00:01 adres
- **STM32 #1** (sensor): Her 20ms'de bir binary IMU paketi (16 bayt: uint32 index + 6×int16 raw) gönderiyor, UART CSV çıktısı korundu
- **STM32 #2** (receiver/bridge): Minimal firmware, nRF24 RX → UART TX (115200) binary forward, LED blink her pakette
- **PC GUI** (`pc-gui/gui.py`): matplotlib+TkAgg, real-time 6-axis plot, keyboard save-to-CSV ([s]), command send ([r]), pyserial
- **Proje yapısı**: Ortak `nrf24/` driver, her proje kendi `Makefile`'ı ile bağımsız build

### Sonraki Adım (önerilen)
- İlk test: ST-Link ile sensor node flash, sonra receiver bridge flash
- Seri kablolama: sensor node → nRF24 TX, receiver → nRF24 RX, USB-UART → PC
- PC GUI test et: `python3 pc-gui/gui.py`
- HAR inference motoru nRF24 üzerinden gönderilecek şekilde güncelle
- Madgwick filter ekle (orientation)

### Çalıştırma
```bash
cd /home/hakan/stm32-mpu6050
make -j4 && make flash       # sensor node
cd receiver && make -j4 && make flash   # receiver bridge
python3 pc-gui/gui.py         # PC GUI
cd /home/hakan/stm32-mpu6050 && make clean && git add -A && git commit -m "v1.6.0" && git tag v1.6.0 && git push && git push --tags
```

---

## Proje
STM32F103C8T6 (Blue Pill) + MPU6050 IMU + nRF24L01+ wireless, FreeRTOS, HAL, I2C DMA, UART DMA TX

## Ortam
- **Toolchain**: `/home/hakan/.local/opt/arm-none-eabi/usr/bin/` (arm-none-eabi)
- **Programmer**: ST-Link V2 → OpenOCD
- **Git remote**: `git@github.com:hakanaydn/ActivityRecognition.git`
- **Proje dizini**: `/home/hakan/stm32-mpu6050`
- **Build sensor**: `make -j4 && make flash` (proje kökünde)
- **Build receiver**: `cd receiver && make -j4 && make flash`

## Versiyon Geçmişi

| Tag | Açıklama |
|-----|----------|
| v1.0.0 | İlk scaffold, MPU6050 driver, HAR inference, blink task |
| v1.1.0 | Fault handlers (HardFault, MemManage, BusFault, UsageFault), reset cause |
| v1.2.0 | Physical unit output (g, °/s), HAR removed from callback, blink cleanup |
| v1.3.0 | Byte-order fix, clone MPU6050 (WHO_AM_I=0x70), CLKSEL=1, notification-based sensor_task@50Hz |
| v1.4.0 | Smart calibration + progress bar `[==== ...]` + retry loop |
| v1.4.1 | Gyro output enabled + gyro cal verification (geri alındı) |
| v1.5.0 | Gyro output + SW LPF (α=1/8) + init fix |
| **v1.6.0** | **Son versiyon** — nRF24L01+ wireless link + receiver bridge + PC GUI |

## v1.6.0 Durumu

### Çalışan Özellikler
- **Accel**: Mükemmel kalibre (X≈0, Y≈0, Z≈1.00 g, ±0.02 g)
- **Gyro**: Yazılımsal LPF ile ±1-2 °/s noise (clone MPU6050 DLPF çalışmıyor)
- **Çıktı formatı**: CSV `ax,ay,az,gx,gy,gz` @ 50 Hz (UART) + binary packet (nRF24)
- **Wireless**: nRF24L01+ SPI1 (register-level), 2Mbps, ESB TX, ACK payloads
- **Receiver bridge**: 2. STM32 nRF24 RX → USB-UART (115200) binary forward
- **PC GUI**: Real-time matplotlib plot, CSV save, command send
- **Kalibrasyon**: Smart wait + progress bar + retry (sadece accel control)

### Bilinen Sorunlar
1. Clone MPU6050 (WHO_AM_I=0x70) gyro DLPF'si donanımsal çalışmıyor → SW LPF ile çözüldü
2. ST-Link ara sıra bağlantı kesiyor → `make flash` tekrar dene
3. Gyro'da küçük bias kalıyor (özellikle Z'de ≈ -0.5 °/s)
4. nRF24L01 henüz saha testi yapılmadı — kablolama + ilk çalıştırma bekleniyor

### Devam Etmek İçin
```bash
cd /home/hakan/stm32-mpu6050
# Sensor node flash:
make -j4 && make flash
# Receiver bridge flash:
cd receiver && make -j4 && make flash
# PC GUI:
python3 pc-gui/gui.py
```

### Kilometre Taşları
- [x] MPU6050 init & temel okuma
- [x] I2C DMA + UART DMA TX
- [x] FreeRTOS task'lar
- [x] Smart calibration + progress bar
- [x] Gyro output + yazılımsal LPF
- [x] nRF24L01+ wireless link (TX/RX)
- [x] Receiver bridge STM32
- [x] PC GUI (matplotlib real-time plot)
- [ ] HAR (Human Activity Recognition) nRF24 üzerinden
- [ ] Madgwick/Complementary filter (orientation)
- [ ] Daha iyi gyro bias tracking (stationary detection)

### Önemli Kod Lokasyonları
| Dosya | Ne var? |
|-------|---------|
| `Core/Src/main.c` | `sensor_task` (notif-based, 50 Hz, SW LPF, nRF24 TX), `format_imu_line`, `prog_bar`, kalibrasyon |
| `Core/Src/mpu6050.c` | `MPU6050_Init` (CLKSEL=1, clone WHO_AM_I), `MPU6050_Calibrate`, `MPU6050_Correct` |
| `Core/Inc/mpu6050.h` | Register defines, calib struct, progress callback |
| `Core/Src/transport_uart.c` | `Output_Write` (256B ring buffer + DMA TX) |
| `Core/Inc/version.h` | `FW_VERSION` |
| `nrf24/nrf24.c` | nRF24L01 driver (register-level SPI, ESB TX/RX) |
| `nrf24/nrf24.h` | nRF24 API |
| `receiver/Core/Src/main.c` | Receiver bridge: nRF24 RX → UART TX binary forward |
| `pc-gui/gui.py` | PC GUI: matplotlib real-time 6-axis IMU plot |
| `receiver/Makefile` | Receiver projesi build (ortak nrf24/ ve Drivers/ kullanır) |

### Mimari Kararlar
- nRF24L01: register-level SPI (HAL SPI source yok), 2Mbps, ESB, kanal 78
- nRF24 adres: `AA:00:00:00:01`
- Binary packet: 16 bayt (uint32 index + 6×int16 raw), ~1 KB/s @ 50 Hz
- Receiver bridge: minimal super loop, FreeRTOS yok, basit forward
- PC GUI: matplotlib+TkAgg (PyQt5 yok ortamda), pyserial

### Donanım Bağlantıları
```
Sensor Node (STM32 #1):         Receiver Bridge (STM32 #2):
  PA5 (SPI1 SCK)  → nRF24 SCK    PA5 (SPI1 SCK)  → nRF24 SCK
  PA6 (SPI1 MISO) → nRF24 MISO   PA6 (SPI1 MISO) → nRF24 MISO
  PA7 (SPI1 MOSI) → nRF24 MOSI   PA7 (SPI1 MOSI) → nRF24 MOSI
  PB0 (GPIO)      → nRF24 CSN    PB0 (GPIO)      → nRF24 CSN
  PB1 (GPIO)      → nRF24 CE     PB1 (GPIO)      → nRF24 CE
  3.3V            → nRF24 VCC    3.3V            → nRF24 VCC
  GND             → nRF24 GND    GND             → nRF24 GND
                                  PA9 (USART1 TX) → USB-UART RX
                                  PA10 (USART1 RX) → USB-UART TX
```
