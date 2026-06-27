# Session Özeti — stm32-mpu6050

> **Her konuşma sonunda güncellenir.** Yeni bir opencode oturumunda önce bu dosyayı bana ilet, kaldığım yerden devam edeyim.

## Son Oturum (2026-06-27)

### Yapılanlar
- **v1.5.0** pushlandı: Gyro output açıldı, yazılımsal LPF (α=1/8 @ 50Hz) eklendi
- Kalibrasyon retry'den gyro kontrolü kaldırıldı (sadece accel kontrolü kalınca sorunsuz geçti)
- Gyor DLPF clone MPU6050'de çalışmadığı için SW LPF ile noise ±10 → ±1-2 °/s düşürüldü
- GYRO_CONFIG=0x03 triple-write + signal path reset eklendi
- Debug register dump (`G=0030 C04x0`) ve "F..." debug print'leri temizlendi
- `SESSION.md` oluşturuldu (oturum devamlılığı için)

### Sonraki Adım (önerilen)
- HAR (Human Activity Recognition) inference motorunu tekrar aktifleştir
- Veya Madgwick filter ekle (orientation estimation)
- Veya gyro bias tracking (stationary detection ile)

### Çalıştırma
```bash
cd /home/hakan/stm32-mpu6050
make -j4 && make flash       # derle + flash
make flash                   # sadece flash (tekrar dene, ST-Link bazen ilk seferde hata verir)
screen /dev/ttyUSB0 115200   # seri monitör
```

---

## Proje
STM32F103C8T6 (Blue Pill) + MPU6050 IMU, FreeRTOS, HAL, I2C DMA, UART DMA TX

## Ortam
- **Toolchain**: `/home/hakan/.local/opt/arm-none-eabi/usr/bin/` (arm-none-eabi)
- **Programmer**: ST-Link V2 → OpenOCD
- **Git remote**: `git@github.com:hakanaydn/ActivityRecognition.git`
- **Proje dizini**: `/home/hakan/stm32-mpu6050`
- **Build**: `make -j4 && make flash`

## Versiyon Geçmişi

| Tag | Açıklama |
|-----|----------|
| v1.0.0 | İlk scaffold, MPU6050 driver, HAR inference, blink task |
| v1.1.0 | Fault handlers (HardFault, MemManage, BusFault, UsageFault), reset cause |
| v1.2.0 | Physical unit output (g, °/s), HAR removed from callback, blink cleanup |
| v1.3.0 | Byte-order fix, clone MPU6050 (WHO_AM_I=0x70), CLKSEL=1, notification-based sensor_task@50Hz |
| v1.4.0 | Smart calibration + progress bar `[==== ...]` + retry loop |
| v1.4.1 | Gyro output enabled + gyro cal verification (geri alındı) |
| **v1.5.0** | **Son versiyon** — Gyro output + SW LPF (α=1/8) + init fix |

## v1.5.0 Durumu

### Çalışan Özellikler
- **Accel**: Mükemmel kalibre (X≈0, Y≈0, Z≈1.00 g, ±0.02 g)
- **Gyro**: Yazılımsal LPF ile ±1-2 °/s noise (clone MPU6050 DLPF çalışmıyor)
- **Çıktı formatı**: CSV `ax,ay,az,gx,gy,gz` @ 50 Hz
- **Kalibrasyon**: Smart wait (stabilite kontrolü) + progress bar + retry (sadece accel kontrol)
- **Kalibrasyon doğrulama**: 5 örnek okur, Z 12000-20000, |X|,|Y| < 5000 LSB kontrolü

### Bilinen Sorunlar
1. Clone MPU6050 (WHO_AM_I=0x70) gyro DLPF'si donanımsal çalışmıyor → SW LPF ile çözüldü
2. ST-Link ara sıra bağlantı kesiyor → `make flash` tekrar dene
3. Gyro'da küçük bias kalıyor (özellikle Z'de ≈ -0.5 °/s)

### Devam Etmek İçin
```bash
cd /home/hakan/stm32-mpu6050
# Kodu düzenle, derle ve flash:
make -j4 && make flash
# Seri monitör ile izle:
screen /dev/ttyUSB0 115200
```

### Kilometre Taşları
- [x] MPU6050 init & temel okuma
- [x] I2C DMA + UART DMA TX
- [x] FreeRTOS task'lar
- [x] Smart calibration + progress bar
- [x] Gyro output + yazılımsal LPF
- [ ] HAR (Human Activity Recognition) tekrar aktifleştirme
- [ ] Madgwick/Complementary filter (orientation)
- [ ] Daha iyi gyro bias tracking

### Önemli Kod Lokasyonları
| Dosya | Ne var? |
|-------|---------|
| `Core/Src/main.c` | `sensor_task` (notif-based, 50 Hz, SW LPF), `format_imu_line`, `prog_bar`, kalibrasyon döngüsü |
| `Core/Src/mpu6050.c` | `MPU6050_Init` (CLKSEL=1, clone WHO_AM_I), `MPU6050_Calibrate` (100 samples, progress cb), `MPU6050_Correct` |
| `Core/Inc/mpu6050.h` | Register defines, `MPU6050_Calib_t`, `MPU6050_ProgressCB` |
| `Core/Src/transport_uart.c` | `Output_Write` (ring buffer 256 bayt + DMA TX) |
| `Core/Inc/version.h` | `FW_VERSION` |

### Mimari Kararlar
- Byte order: MPU6050 big-endian, manuel `(buf[0]<<8)|buf[1]` ile okunur, LE yazılır
- CLKSEL=1 (PLL X-gyro) daha kararlı gyro için
- Gyro DLPF clone'da çalışmaz → yazılımsal EMA filtresi (α=1/8, ~1 Hz cutoff)
- Kalibrasyon: stabilite bekleme (20 örnek window, dx/dy/dz<500, Z>7000, timeout 500×20ms=10sn)
