"""Extra build flags for the STM32F407:
- turn on the Cortex-M4 hardware FPU for every compiled file and the link (FreeRTOS ARM_CM4F port);
- compile the C++ part (player.cpp, which uses the lab's song.h / song_def.h) without exceptions/RTTI.
"""
Import("env")

FPU_FLAGS = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]

for scope in (env, DefaultEnvironment()):
    scope.Append(CCFLAGS=FPU_FLAGS, ASFLAGS=FPU_FLAGS, LINKFLAGS=FPU_FLAGS)
    scope.Append(CXXFLAGS=["-fno-exceptions", "-fno-rtti"])
