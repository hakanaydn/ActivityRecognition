###############################################################################
# Makefile - STM32F103C8T6 + MPU6050 + FreeRTOS + I2C DMA
# CubeMX stili Makefile
###############################################################################

# Project
TARGET = stm32-mpu6050

# Toolchain
PREFIX = /home/hakan/.local/opt/arm-none-eabi/usr/bin/arm-none-eabi-
CC     = $(PREFIX)gcc
CXX    = $(PREFIX)g++
LD     = $(PREFIX)gcc
AR     = $(PREFIX)ar
AS     = $(PREFIX)gcc -x assembler-with-cpp
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
SIZE   = $(PREFIX)size

# Paths
BUILD_DIR  = build
CORE_DIR   = Core
DRIVERS_DIR = Drivers
MIDDLEWARES_DIR = Middlewares

# CMSIS paths
CMSIS_DIR     = $(DRIVERS_DIR)/CMSIS
CMSIS_CORE    = $(CMSIS_DIR)/Core/Include
CMSIS_DEVICE  = $(CMSIS_DIR)/Device/ST/STM32F1xx/Include

# HAL paths
HAL_DIR       = $(DRIVERS_DIR)/STM32F1xx_HAL_Driver
HAL_INC       = $(HAL_DIR)/Inc
HAL_SRC       = $(HAL_DIR)/Src

# FreeRTOS paths
FREERTOS_DIR  = $(MIDDLEWARES_DIR)/FreeRTOS/Source
FREERTOS_INC  = $(FREERTOS_DIR)/include
FREERTOS_PORT = $(FREERTOS_DIR)/portable/GCC/ARM_CM3
FREERTOS_HEAP = $(FREERTOS_DIR)/portable/MemMang/heap_4.c

# MCU settings
MCU_FLAGS = -mcpu=cortex-m3 -mthumb -DSTM32F103xB -DUSE_HAL_DRIVER -DHSE_VALUE=8000000 -DHSI_VALUE=8000000 -DHSE_STARTUP_TIMEOUT=100 -DLSE_STARTUP_TIMEOUT=5000
FPU_FLAGS =
OPT_FLAGS = -Os -ffunction-sections -fdata-sections

# Include paths
INCLUDES = \
  -I$(CORE_DIR)/Inc \
  -I$(CMSIS_CORE) \
  -I$(CMSIS_DEVICE) \
  -I$(HAL_INC) \
  -I$(FREERTOS_INC) \
  -I$(FREERTOS_DIR)/portable/GCC/ARM_CM3

# Sources
C_SOURCES = \
  $(CORE_DIR)/Src/main.c \
  $(CORE_DIR)/Src/mpu6050.c \
  $(CORE_DIR)/Src/transport_uart.c \
  $(CORE_DIR)/Src/har_model.c \
  $(CORE_DIR)/Src/har_weights.c \
  $(CORE_DIR)/Src/stm32f1xx_it.c \
  $(CORE_DIR)/Src/system_stm32f1xx.c \
  $(HAL_SRC)/stm32f1xx_hal.c \
  $(HAL_SRC)/stm32f1xx_hal_i2c.c \
  $(HAL_SRC)/stm32f1xx_hal_gpio.c \
  $(HAL_SRC)/stm32f1xx_hal_rcc.c \
  $(HAL_SRC)/stm32f1xx_hal_cortex.c \
  $(HAL_SRC)/stm32f1xx_hal_dma.c \
  $(HAL_SRC)/stm32f1xx_hal_uart.c \
  $(FREERTOS_DIR)/tasks.c \
  $(FREERTOS_DIR)/queue.c \
  $(FREERTOS_DIR)/list.c \
  $(FREERTOS_DIR)/timers.c \
  $(FREERTOS_DIR)/event_groups.c \
  $(FREERTOS_DIR)/stream_buffer.c \
  $(FREERTOS_DIR)/portable/GCC/ARM_CM3/port.c \
  $(FREERTOS_DIR)/portable/MemMang/heap_4.c

ASM_SOURCES = \
  Startup/startup_stm32f103c8tx.s

# Compiler flags
CFLAGS   = $(MCU_FLAGS) $(FPU_FLAGS) $(OPT_FLAGS) $(INCLUDES) -Wall -Wno-unused-function -fmessage-length=0
LDFLAGS  = $(MCU_FLAGS) -TSTM32F103C8Tx_FLASH.ld -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -nostartfiles -L$(HOME)/.local/opt/arm-none-eabi/usr/lib/arm-none-eabi/newlib/thumb/v7-m/nofp -lc_nano -lnosys

# Objects
OBJECTS = $(addprefix $(BUILD_DIR)/, $(C_SOURCES:.c=.o) $(ASM_SOURCES:.s=.o))
OBJECTS := $(OBJECTS:.S=.o)

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))
vpath %.S $(sort $(dir $(ASM_SOURCES)))

# Default target
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# Link
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) | $(BUILD_DIR)
	$(LD) $(OBJECTS) $(LDFLAGS) -o $@
	$(SIZE) $@

# Compile C
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -std=c99 -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

# Compile ASM
$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Output formats
$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(OBJCOPY) -O binary $< $@

# Flash with OpenOCD (ST-Link)
flash: $(BUILD_DIR)/$(TARGET).bin
	openocd -f interface/stlink-v2.cfg -f target/stm32f1x.cfg -c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

# Clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean flash
