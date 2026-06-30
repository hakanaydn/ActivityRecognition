TARGET = stm32-receiver
PREFIX = /home/hakan/.local/opt/arm-none-eabi/usr/bin/arm-none-eabi-
CC     = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
SIZE   = $(PREFIX)size

BUILD_DIR  = build
COMMON_DIR = $(abspath ../..)/common

CMSIS_CORE    = $(COMMON_DIR)/Drivers/CMSIS/Core/Include
CMSIS_DEVICE  = $(COMMON_DIR)/Drivers/CMSIS/Device/ST/STM32F1xx/Include
HAL_INC       = $(COMMON_DIR)/Drivers/STM32F1xx_HAL_Driver/Inc
HAL_SRC       = $(COMMON_DIR)/Drivers/STM32F1xx_HAL_Driver/Src

FREERTOS_DIR  = $(COMMON_DIR)/Middlewares/FreeRTOS/Source
FREERTOS_INC  = $(FREERTOS_DIR)/include
FREERTOS_PORT = $(FREERTOS_DIR)/portable/GCC/ARM_CM3
FREERTOS_HEAP = $(FREERTOS_DIR)/portable/MemMang/heap_4.c

MCU_FLAGS = -mcpu=cortex-m3 -mthumb -DSTM32F103xB -DUSE_HAL_DRIVER -DNRF24_USE_HAL -DHSE_VALUE=8000000 -DHSI_VALUE=8000000 -DHSE_STARTUP_TIMEOUT=100 -DLSE_STARTUP_TIMEOUT=5000

INCLUDES = \
  -ICore/Inc \
  -I$(CMSIS_CORE) \
  -I$(CMSIS_DEVICE) \
  -I$(HAL_INC) \
  -I$(FREERTOS_INC) \
  -I$(FREERTOS_PORT) \
  -I$(COMMON_DIR)/nrf24 \
  -I$(COMMON_DIR)

C_SOURCES = \
  Core/Src/main.c \
  Core/Src/stm32f1xx_it.c \
  Core/Src/system_stm32f1xx.c \
  Core/Src/blink_task.c \
  Core/Src/nrf24_task.c \
  Core/Src/uart_com.c \
  $(COMMON_DIR)/packet.c \
  $(COMMON_DIR)/nrf24/nrf24.c \
  $(HAL_SRC)/stm32f1xx_hal.c \
  $(HAL_SRC)/stm32f1xx_hal_gpio.c \
  $(HAL_SRC)/stm32f1xx_hal_rcc.c \
  $(HAL_SRC)/stm32f1xx_hal_cortex.c \
  $(HAL_SRC)/stm32f1xx_hal_spi.c \
   $(HAL_SRC)/stm32f1xx_hal_uart.c \
   $(HAL_SRC)/stm32f1xx_hal_dma.c \
  $(FREERTOS_DIR)/tasks.c \
  $(FREERTOS_DIR)/queue.c \
  $(FREERTOS_DIR)/list.c \
  $(FREERTOS_DIR)/timers.c \
  $(FREERTOS_DIR)/event_groups.c \
  $(FREERTOS_PORT)/port.c \
  $(FREERTOS_HEAP)

ASM_SOURCES = \
  $(COMMON_DIR)/Startup/startup_stm32f103c8tx.s

CFLAGS   = $(MCU_FLAGS) -Os -ffunction-sections -fdata-sections $(INCLUDES) -Wall -Wno-unused-function -fmessage-length=0 -std=c99
LDFLAGS  = $(MCU_FLAGS) -T$(COMMON_DIR)/STM32F103C8Tx_FLASH.ld -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -nostartfiles -L$(HOME)/.local/opt/arm-none-eabi/usr/lib/arm-none-eabi/newlib/thumb/v7-m/nofp -lc_nano -lnosys

OBJECTS = $(addprefix $(BUILD_DIR)/, $(C_SOURCES:.c=.o) $(ASM_SOURCES:.s=.o))

vpath %.c $(sort $(dir $(C_SOURCES)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash: $(BUILD_DIR)/$(TARGET).elf
	openocd -f interface/stlink-v2.cfg -f target/stm32f1x.cfg -c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean flash
