set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Pass -DARM_TOOLCHAIN_PATH=C:/path/to/bin from the command line, or have
# arm-none-eabi-gcc on PATH.
set(ARM_TOOLCHAIN_PATH "" CACHE PATH "Directory containing arm-none-eabi-gcc")

if(ARM_TOOLCHAIN_PATH)
  set(_arm_prefix "${ARM_TOOLCHAIN_PATH}/arm-none-eabi-")
else()
  find_program(ARM_GCC_EXECUTABLE arm-none-eabi-gcc)
  if(NOT ARM_GCC_EXECUTABLE)
    message(FATAL_ERROR
      "arm-none-eabi-gcc was not found. Install GNU Arm Embedded / STM32CubeCLT "
      "or pass -DARM_TOOLCHAIN_PATH=<directory containing arm-none-eabi-gcc>.")
  endif()
  get_filename_component(_arm_bin "${ARM_GCC_EXECUTABLE}" DIRECTORY)
  set(_arm_prefix "${_arm_bin}/arm-none-eabi-")
endif()

if(CMAKE_HOST_WIN32)
  set(_exe ".exe")
else()
  set(_exe "")
endif()

set(CMAKE_C_COMPILER   "${_arm_prefix}gcc${_exe}")
set(CMAKE_ASM_COMPILER "${_arm_prefix}gcc${_exe}")
set(CMAKE_CXX_COMPILER "${_arm_prefix}g++${_exe}")

set(CMAKE_OBJCOPY "${_arm_prefix}objcopy${_exe}" CACHE FILEPATH "" FORCE)
set(CMAKE_SIZE    "${_arm_prefix}size${_exe}"    CACHE FILEPATH "" FORCE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
