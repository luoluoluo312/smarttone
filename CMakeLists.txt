cmake_minimum_required(VERSION 3.20)

project(STM32F103_TFT_ST7735 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(TARGET_NAME STM32F103_TFT_ST7735)

set(MCU_FLAGS
  -mcpu=cortex-m3
  -mthumb
  -mfloat-abi=soft
)

add_compile_options(
  ${MCU_FLAGS}
  -ffunction-sections
  -fdata-sections
  -fno-common
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-missing-field-initializers
)

add_compile_definitions(
  USE_HAL_DRIVER
  STM32F103xB
)

include_directories(
  ${CMAKE_SOURCE_DIR}/Core/Inc
  ${CMAKE_SOURCE_DIR}/BSP/Inc
  ${CMAKE_SOURCE_DIR}/BSP/Audio/01_audio_config
  ${CMAKE_SOURCE_DIR}/BSP/Audio/02_audio_pitch
  ${CMAKE_SOURCE_DIR}/BSP/Audio/03_audio_pwm
  ${CMAKE_SOURCE_DIR}/BSP/Audio/04_audio_core
  ${CMAKE_SOURCE_DIR}/BSP/Audio/05_audio_metronome
  ${CMAKE_SOURCE_DIR}/BSP/Audio/06_audio_song
  ${CMAKE_SOURCE_DIR}/BSP/Audio/07_audio_drum
  ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc
  ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include
  ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
)

set(HAL_SOURCES
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c
  Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c
)

set(APP_SOURCES
  Core/Src/main.c
  Core/Src/stm32f1xx_hal_msp.c
  Core/Src/stm32f1xx_it.c
  Core/Src/system_stm32f1xx.c
  Core/Src/syscalls.c
  Core/Src/sysmem.c
  BSP/Src/bsp_tft.c
  BSP/Src/bsp_encoder.c
  BSP/Src/bsp_func.c
  BSP/Src/bsp_notes.c
  BSP/Src/bsp_audio_tim.c
  BSP/Audio/02_audio_pitch/audio_pitch.c
  BSP/Audio/03_audio_pwm/audio_pwm.c
  BSP/Audio/04_audio_core/audio.c
  BSP/Audio/05_audio_metronome/metronome.c
  BSP/Audio/06_audio_song/song.c
  BSP/Audio/06_audio_song/sample.c
  BSP/Audio/06_audio_song/song_builtin.c
  BSP/Audio/07_audio_drum/drum.c
  BSP/Src/boot_logo.c
  BSP/Src/record.c
  BSP/Src/font.c
  BSP/Src/ui_page.c
  BSP/Src/ui_font_ascii.c
  BSP/Src/rhythm_game.c
  BSP/Src/melody_maker.c
  BSP/Src/ds_room.c
  BSP/Src/rhythm_chart.c
)

add_executable(${TARGET_NAME}
  ${APP_SOURCES}
  ${HAL_SOURCES}
  Core/Startup/startup_stm32f103c8tx.s
)

target_compile_options(${TARGET_NAME} PRIVATE
  $<$<CONFIG:Debug>:-Og;-g3;-gdwarf-2>
  $<$<CONFIG:Release>:-Os;-g3>
)

target_link_options(${TARGET_NAME} PRIVATE
  ${MCU_FLAGS}
  -T${CMAKE_SOURCE_DIR}/STM32F103C8TX_FLASH.ld
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
  -specs=nano.specs
  -specs=nosys.specs
)

set_target_properties(${TARGET_NAME} PROPERTIES
  OUTPUT_NAME ${TARGET_NAME}
  SUFFIX ".elf"
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex
          $<TARGET_FILE:${TARGET_NAME}>
          ${CMAKE_BINARY_DIR}/${TARGET_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary
          $<TARGET_FILE:${TARGET_NAME}>
          ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
  COMMENT "Generating HEX/BIN and printing firmware size"
)

# A convenience target that does not need the debugger/probe.
add_custom_target(firmware ALL DEPENDS ${TARGET_NAME})
