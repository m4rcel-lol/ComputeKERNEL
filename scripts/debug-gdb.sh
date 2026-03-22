#!/usr/bin/env sh
set -eu

echo "GDB debug helper:"
echo "1) Start qemu with -s -S in another terminal."
echo "2) Run: gdb -ex 'target remote :1234' -ex 'symbol-file out/kernel.elf'"

