#!/bin/bash
# Flash STM32F103C8T6 (Blue Pill) with OpenOCD and ST-Link
# Usage: ./flash.sh [elf|hex|bin]
#        ./flash.sh          # default: .elf
#        ./flash.sh hex
#        ./flash.sh bin

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
TARGET="stm32-mpu6050"
OPENOCD=${OPENOCD:-openocd}
OCD_IF=${OCD_IF:-interface/stlink-v2.cfg}
OCD_TARGET=${OCD_TARGET:-target/stm32f1x.cfg}

FMT="${1:-elf}"

case "$FMT" in
    elf)  FILE="${BUILD_DIR}/${TARGET}.elf"  ;;
    hex)  FILE="${BUILD_DIR}/${TARGET}.hex"  ;;
    bin)  FILE="${BUILD_DIR}/${TARGET}.bin"  ;;
    *)
        echo "Usage: $0 [elf|hex|bin]"
        exit 1
        ;;
esac

if [ ! -f "$FILE" ]; then
    echo "Error: $FILE not found. Run 'make' first."
    exit 1
fi

echo "Flashing ${FILE}..."
${OPENOCD} -f ${OCD_IF} -f ${OCD_TARGET} \
    -c "program ${FILE} verify reset exit"
