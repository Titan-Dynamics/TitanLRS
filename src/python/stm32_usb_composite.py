#
# Keep the stm32duino USBDevice sources out of STM32 builds.
#
# lib/USBComposite is a full replacement for that library: it exposes a CDC-ACM
# function *and* a CDC-NCM network function (the config API / MAVLink UDP) instead of CDC
# alone, so the two cannot both be compiled — they define the same symbols
# (SerialUSB, USBD_Desc, ep_def, the CDC queues, ...).
#
# `lib_ignore = USBDevice` is not enough on its own. The ststm32 framework build
# script calls env.BuildSources() on framework-arduinoststm32/libraries/USBDevice
# directly whenever USBD_USE_CDC is defined, which bypasses the library dependency
# finder entirely. So wrap BuildSources and drop that one directory.
#
# Everything else stays untouched: USBD_USE_CDC remains defined for every
# translation unit (the Arduino core needs it to bind Serial to SerialUSB), and
# lib/USBComposite is placed ahead of libraries/USBDevice/inc on the include path
# (see -I in env_common_stm32) so our replacement headers win.
#
Import("env")

import os

_EXCLUDED_DIR = os.path.join("libraries", "USBDevice")

# Unwrap to the plain function rather than keeping the bound method: this method
# is inherited by every clone of the environment (projenv, each library's env),
# and a bound method would silently build their sources against the wrong env.
_original_build_sources = env.BuildSources.method


def BuildSources(env, variant_dir, src_dir, src_filter=None):
    if os.path.normpath(src_dir).endswith(_EXCLUDED_DIR):
        print("USBComposite: skipping framework %s (replaced by lib/USBComposite)" % _EXCLUDED_DIR)
        return None
    return _original_build_sources(env, variant_dir, src_dir, src_filter)


env.AddMethod(BuildSources, "BuildSources")
