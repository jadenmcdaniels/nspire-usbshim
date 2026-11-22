#!/bin/bash
# Tests for nspire-usbshim:
#  1. tests/shim_test.c (with newlib_shim.c from PocketSNES: the malloc family
#     the Ndless newlib leaves out) built with the calculator's compiler (ARMv5, ARM mode)
#     and run under qemu-arm, so the real trampoline and stub code executes
#     (match/attach/detach and completion callbacks, ARM and Thumb targets,
#     retiring, a full callback block; --wrap=malloc counts allocations).
#  2. usbshim.c built for the calculator against the Ndless headers, and its
#     object checked for instructions newer than ARMv5.
# Needs the Ndless toolchain (on PATH, or NSPIRE_ENV) and qemu-arm.
set -eu
cd "$(dirname "$0")/.."
command -v arm-none-eabi-gcc >/dev/null || . "${NSPIRE_ENV:?put the Ndless toolchain on PATH, or set NSPIRE_ENV to a script that does}"
QEMU=${QEMU:-$(command -v qemu-arm)}
OUT=build
mkdir -p $OUT

ARCH="-march=armv5te -mtune=arm926ej-s -marm"
arm-none-eabi-gcc $ARCH -O2 -Wall -Wextra -Werror -DUSBSHIM_TEST -specs=rdimon.specs \
	-Wl,--wrap=malloc usbshim.c tests/shim_test.c tests/newlib_shim.c -o $OUT/shim_test
"$QEMU" -cpu arm926 $OUT/shim_test

gcc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
	tests/status_test.c -o $OUT/status_test_native
./$OUT/status_test_native
arm-none-eabi-gcc $ARCH -O2 -Wall -Wextra -Werror -fno-builtin-malloc -specs=rdimon.specs \
	tests/status_test.c tests/newlib_shim.c -o $OUT/status_test
"$QEMU" -cpu arm926 $OUT/status_test

nspire-gcc -O2 -Wall -Wextra -Werror -c usbshim.c -o $OUT/usbshim.o
if arm-none-eabi-objdump -d $OUT/usbshim.o | grep -E -w \
	'(uxt|sxt|rev|ldrex|strex|pkh|usat|ssat|smlad|smlald|smlsd|smlsld|smuad|smusd|movw|movt|vld|vst|vmov)[a-z0-9.]*' ; then
	echo "usbshim.o: instructions newer than ARMv5"; exit 1
fi
echo "usbshim.o: ARMv5 only, $(stat -c %s $OUT/usbshim.o) bytes"
