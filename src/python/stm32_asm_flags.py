#
# STM32 builds pass `-D __FILE__='""'` (see env_common_stm32) to keep source paths out of the
# image, together with -Wno-builtin-macro-redefined. PlatformIO routes -W flags from build_flags to
# CCFLAGS only, but -D goes to every preprocessed source, including the framework's startup .S
# file, which then warns that __FILE__ was redefined. Give the assembler the same suppression.
#
Import("env")

env.Append(ASPPFLAGS=["-Wno-builtin-macro-redefined"])
