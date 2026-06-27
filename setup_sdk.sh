#!/bin/bash
# setup_sdk.sh - STM32CubeF1 HAL + FreeRTOS SDK çekme scripti
# STM32CubeF1 GitHub mirror'dan gerekli dosyaları indirir

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SDK_URL="https://raw.githubusercontent.com/STMicroelectronics/STM32CubeF1/main"

echo "=== STM32CubeF1 SDK Setup ==="

# Create directories
mkdir -p Drivers/CMSIS/Core/Include
mkdir -p Drivers/CMSIS/Device/ST/STM32F1xx/Include
mkdir -p Drivers/STM32F1xx_HAL_Driver/Inc
mkdir -p Drivers/STM32F1xx_HAL_Driver/Src
mkdir -p Middlewares/FreeRTOS/Source/include
mkdir -p Middlewares/FreeRTOS/Source/portable/GCC/ARM_CM3
mkdir -p Middlewares/FreeRTOS/Source/portable/MemMang
mkdir -p Startup

echo "Downloading CMSIS core headers..."
for f in cmsis_compiler.h cmsis_gcc.h cmsis_armcc.h cmsis_iccarm.h core_cm3.h mpu_armv7.h; do
    echo "  $f"
    curl -sSL "${SDK_URL}/Drivers/CMSIS/Core/Include/${f}" -o "Drivers/CMSIS/Core/Include/${f}" || echo "    WARNING: ${f} failed"
done

echo "Downloading CMSIS device headers..."
for f in stm32f103xe.h stm32f1xx.h system_stm32f1xx.h; do
    echo "  $f"
    curl -sSL "${SDK_URL}/Drivers/CMSIS/Device/ST/STM32F1xx/Include/${f}" -o "Drivers/CMSIS/Device/ST/STM32F1xx/Include/${f}" || echo "    WARNING: ${f} failed"
done

echo "Downloading HAL headers..."
HAL_INC_FILES=(
    stm32f1xx_hal.h stm32f1xx_hal_def.h stm32f1xx_hal_i2c.h
    stm32f1xx_hal_gpio.h stm32f1xx_hal_rcc.h stm32f1xx_hal_cortex.h
    stm32f1xx_hal_dma.h stm32f1xx_hal_uart.h stm32f1xx_hal_pwr.h
    stm32f1xx_hal_flash.h stm32f1xx_hal_exti.h
)
for f in "${HAL_INC_FILES[@]}"; do
    echo "  $f"
    curl -sSL "${SDK_URL}/Drivers/STM32F1xx_HAL_Driver/Inc/${f}" -o "Drivers/STM32F1xx_HAL_Driver/Inc/${f}" || echo "    WARNING: ${f} failed"
done
curl -sSL "${SDK_URL}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy/stm32f1xx_hal_i2c_ex.h" -o "Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_i2c_ex.h" 2>/dev/null || true

echo "Downloading HAL sources..."
HAL_SRC_FILES=(
    stm32f1xx_hal.c stm32f1xx_hal_i2c.c stm32f1xx_hal_gpio.c
    stm32f1xx_hal_rcc.c stm32f1xx_hal_cortex.c stm32f1xx_hal_dma.c
    stm32f1xx_hal_uart.c stm32f1xx_hal_pwr.c stm32f1xx_hal_flash.c
    stm32f1xx_hal_exti.c stm32f1xx_hal_rcc_ex.c stm32f1xx_hal_flash_ex.c
    stm32f1xx_hal_gpio_ex.c
)
for f in "${HAL_SRC_FILES[@]}"; do
    echo "  $f"
    curl -sSL "${SDK_URL}/Drivers/STM32F1xx_HAL_Driver/Src/${f}" -o "Drivers/STM32F1xx_HAL_Driver/Src/${f}" || echo "    WARNING: ${f} failed"
done

echo "Downloading FreeRTOS headers..."
FREERTOS_INC_FILES=(
    FreeRTOS.h FreeRTOSConfig.h task.h queue.h list.h timers.h
    event_groups.h stream_buffer.h projdefs.h portable.h
    portmacro.h stack_macros.h croutine.h semphr.h deprecations.h message_buffer.h
    mpu_wrappers.h
)
for f in "${FREERTOS_INC_FILES[@]}"; do
    echo "  $f"
    curl -sSL "${SDK_URL}/Middlewares/Third_Party/FreeRTOS/Source/include/${f}" -o "Middlewares/FreeRTOS/Source/include/${f}" || echo "    WARNING: ${f} failed"
done

echo "Downloading FreeRTOS port files..."
curl -sSL "${SDK_URL}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3/portmacro.h" -o "Middlewares/FreeRTOS/Source/portable/GCC/ARM_CM3/portmacro.h" || echo "    WARNING: portmacro.h failed"
curl -sSL "${SDK_URL}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c" -o "Middlewares/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c" || echo "    WARNING: port.c failed"

echo "Downloading FreeRTOS heap_4..."
curl -sSL "${SDK_URL}/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c" -o "Middlewares/FreeRTOS/Source/portable/MemMang/heap_4.c" || echo "    WARNING: heap_4.c failed"

echo "Downloading FreeRTOS core sources..."
for f in tasks.c queue.c list.c timers.c event_groups.c stream_buffer.c; do
    echo "  $f"
    curl -sSL "${SDK_URL}/Middlewares/Third_Party/FreeRTOS/Source/${f}" -o "Middlewares/FreeRTOS/Source/${f}" || echo "    WARNING: ${f} failed"
done

echo "Downloading startup file..."
curl -sSL "${SDK_URL}/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xe.s" -o "Startup/startup_stm32f103c8tx.s" || echo "    WARNING: startup failed"

echo ""
echo "=== SDK setup complete ==="
echo "NOTE: Some files may have failed due to GitHub raw URL changes."
echo "If needed, open in CubeMX and regenerate to get missing files."
