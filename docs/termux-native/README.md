# Native Termux build and flash (CH32V003A4M6, ch32fun, minichlink)

Build and program firmware_d entirely on an Android phone: no PC, no PlatformIO, no proot.
Target: CH32V003A4M6 (rv32ec / ilp32e, 16 KB flash, 2 KB RAM). Programmer: ESP32-S2 running minichlink.

## Commands (run from firmware_d/)
- Build: `./build.sh` (clean rebuild: `./build.sh clean && ./build.sh`)
- Detect programmer: `./detect.sh`
- Upload: `./flash.sh` (checks size, prints target, asks you to type FLASH)
- Rebuild minichlink: `./build_minichlink.sh`

## Status
Proven on the phone (POCO F6, Termux, clang/lld 21.1.8):
- Full build and link: 10440 of 16320 flash bytes, 1300 of 2048 RAM bytes, ELF/BIN/HEX/map produced.
- Vector table at 0x0, reset jumps to handle_reset, SysTick points to the app handler.
- minichlink builds and starts with the libusb backend.

NOT yet proven (needs the OTG cable and hardware):
- The phone seeing the ESP32-S2 (expect 303a:4004) through /dev/bus/usb as root.
- Any programming. flash.sh has never been run. First flash is a low-voltage bench test only.
- That the firmware behaves identically to a GCC build. A clean link is not a functional test.

## How it works
- tools/riscv32-unknown-elf-{gcc,objdump,objcopy}: thin wrappers so ch32fun.mk runs unchanged on clang/lld/llvm.
- tools/include/{string,stdio,stdlib}.h and build_native/libc_shim.c: minimal declarations and weak fallbacks (Termux has no newlib).
- build_native/patch_ch32fun.py: clang rejects C statements in naked functions. It patches a temporary copy
  (build_native/ch32fun_clang.c) at 3 spots: NMI_RCC_CSS handler, SysTick enable in handle_reset, a stray
  __builtin_unreachable. The original ch32fun-master is never modified.
- build_native/Makefile + fw.c (includes ../src/main.c): builds src/*.c without touching them.
- build.sh generates nvm.ld from ch32fun.ld with FLASH LENGTH = 0x3FC0, so the image cannot reach the NVM block
  at 0x08003FC0 (nvm_flash.c, last 64 bytes). It fails if the image exceeds 16320 bytes.
- minichlink: the ESP32-S2 programmer is a USB HID device (303a:4004). Stock hidapi uses hidraw + libudev, which Termux
  lacks. build_minichlink.sh copies minichlink, switches hidapi.c to its libusb backend, and adds the hidapi 0.15.0 headers.
  It runs under su so libusb can open /dev/bus/usb.

## PlatformIO
firmware_d/platformio.ini is kept but is not part of this flow. The PlatformIO route on Android failed
(platform ch32v unresolved, no AArch64 toolchain, pyserial/Android issues) and nothing here depends on it.

## Requirements
- Termux packages: clang, lld, make, libusb, python, curl; root (KernelSU) for USB.
- ch32fun checked out as ../ch32fun-master (relative to firmware_d), from cnlohr/ch32fun.

## Safety
Compressor protection firmware. Programming and testing only at low voltage: never connect mains to the
board, the phone or the programmer while programming.
