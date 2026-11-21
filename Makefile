##############################################################################
# STM32F103C8T6 SmartTone UI - GNU Make build
#
# VS Code / terminal:
#   mingw32-make ARM_TOOLCHAIN_PATH="C:/path/to/arm-none-eabi/bin"
#   mingw32-make clean
#
# On Linux/macOS the same Makefile works with make.
##############################################################################

TARGET        := STM32F103_TFT_ST7735
BUILD_DIR     ?= build
LDSCRIPT      := STM32F103C8TX_FLASH.ld

ARM_TOOLCHAIN_PATH ?=
ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

PREFIX := $(if $(ARM_TOOLCHAIN_PATH),$(ARM_TOOLCHAIN_PATH)/, )arm-none-eabi-
CC      := $(PREFIX)gcc$(EXE)
AS      := $(CC)
OBJCOPY := $(PREFIX)objcopy$(EXE)
SIZE    := $(PREFIX)size$(EXE)

MCU := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft

C_DEFS := -DUSE_HAL_DRIVER -DSTM32F103xB

C_INCLUDES := \
-I Core/Inc \
-I BSP/Inc \
-I BSP/Audio/01_audio_config \
-I BSP/Audio/02_audio_pitch \
-I BSP/Audio/03_audio_pwm \
-I BSP/Audio/04_audio_core \
-I BSP/Audio/05_audio_metronome \
-I BSP/Audio/06_audio_song \
-I BSP/Audio/07_audio_drum \
-I Drivers/STM32F1xx_HAL_Driver/Inc \
-I Drivers/CMSIS/Device/ST/STM32F1xx/Include \
-I Drivers/CMSIS/Include

OPT := -Os -g3 -std=gnu11
CFLAGS := $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) \
          -Wall -Wextra -Wno-unused-parameter \
          -ffunction-sections -fdata-sections -fno-common -MMD -MP

ASFLAGS := $(MCU) $(C_DEFS) $(C_INCLUDES) -g3 -x assembler-with-cpp

C_SOURCES := \
Core/Src/main.c \
Core/Src/stm32f1xx_hal_msp.c \
Core/Src/stm32f1xx_it.c \
Core/Src/system_stm32f1xx.c \
Core/Src/syscalls.c \
Core/Src/sysmem.c \
BSP/Src/bsp_tft.c \
BSP/Src/bsp_encoder.c \
BSP/Src/bsp_func.c \
BSP/Src/bsp_notes.c \
BSP/Src/bsp_audio_tim.c \
BSP/Audio/02_audio_pitch/audio_pitch.c \
BSP/Audio/03_audio_pwm/audio_pwm.c \
BSP/Audio/04_audio_core/audio.c \
BSP/Audio/05_audio_metronome/metronome.c \
BSP/Audio/06_audio_song/song.c \
BSP/Audio/06_audio_song/sample.c \
BSP/Audio/06_audio_song/song_builtin.c \
BSP/Audio/07_audio_drum/drum.c \
BSP/Src/boot_logo.c \
BSP/Src/record.c \
BSP/Src/font.c \
BSP/Src/ui_page.c \
BSP/Src/ui_font_ascii.c \
BSP/Src/rhythm_game.c \
BSP/Src/melody_maker.c \
BSP/Src/ds_room.c \
BSP/Src/rhythm_chart.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c

ASM_SOURCES := Core/Startup/startup_stm32f103c8tx.s

OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

LDFLAGS := $(MCU) -T$(LDSCRIPT) \
           -specs=nano.specs -specs=nosys.specs \
           -Wl,--gc-sections -Wl,--print-memory-usage \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map
LIBS := -lc -lm -lnosys

ifeq ($(OS),Windows_NT)
MKDIR = cmd /C if not exist "$(1)" mkdir "$(1)"
RMDIR = cmd /C rmdir /S /Q "$(1)"
else
MKDIR = mkdir -p "$(1)"
RMDIR = rm -rf "$(1)"
endif

.PHONY: all clean flash size

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR):
	@$(call MKDIR,$@)

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	@echo "CC  $<"
	@"$(CC)" -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	@echo "AS  $<"
	@"$(AS)" -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	@echo "LD  $@"
	@"$(CC)" $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@
	@"$(SIZE)" $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	@"$(OBJCOPY)" -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	@"$(OBJCOPY)" -O binary $< $@

size: $(BUILD_DIR)/$(TARGET).elf
	@"$(SIZE)" $<

flash: $(BUILD_DIR)/$(TARGET).hex
	STM32_Programmer_CLI -c port=SWD -w $< -v -rst

clean:
	-@$(call RMDIR,$(BUILD_DIR))

-include $(OBJECTS:.o=.d)
