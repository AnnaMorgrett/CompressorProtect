# CompressorProtect: native Termux build and flash guide

Build and program `firmware_d` (CH32V003A4M6, ch32fun) **entirely on an Android phone**: no PC, no PlatformIO, no proot.

- Phone: POCO F6, Android, Termux, KernelSU root
- Compiler: Termux clang + lld 21.1.8 (no GCC needed)
- Programmer: ESP32-S2 running the ch32fun `minichlink` implementation
- Repo: https://github.com/AnnaMorgrett/CompressorProtect (docs in `docs/termux-native/`)

> **Safety.** This is compressor protection firmware. Program and test at low voltage only. Never connect mains to the board, the phone or the programmer while programming.

---

## 1. Daily commands

Run from `~/CompressorProtect/firmware_d`:

| Task | Command |
|---|---|
| Build | `./build.sh` |
| Clean rebuild | `./build.sh clean && ./build.sh` |
| Detect programmer | `./detect.sh` |
| Upload (asks you to type `FLASH`) | `./flash.sh` |
| Rebuild minichlink | `./build_minichlink.sh` |

Build outputs land in `build_native/` (`fw.elf`, `fw.bin`, `fw.hex`, `fw.map`) and are copied to `~/storage/shared/CompressorProtect/out/`.

## 2. Status

**Proven on the phone**

- Full compile and link. Flash: 10,440 of 16,320 bytes (64%). RAM: 1,300 of 2,048 bytes static (about 748 bytes left for the stack).
- Vector table at `0x0`, reset vector jumps to `handle_reset`, SysTick vector points at the application handler, all other vectors go to the default handler.
- minichlink compiles natively and starts using the libusb backend.

**Not yet proven (needs the OTG cable and hardware)**

- The phone seeing the ESP32-S2 (expected `303a:4004`) through `/dev/bus/usb` as root.
- Any programming. `flash.sh` has never been run.
- Functional equivalence with a GCC build. A clean link is not a functional test; the first flash is a bench test.

## 3. Flash and NVM protection

`nvm_flash.c` keeps its configuration in the last 64 bytes of flash (`NVM_ADDR = 0x08003FC0`).

- `build.sh` generates `nvm.ld` from `ch32fun.ld` with `FLASH LENGTH = 0x3FC0`, so the linker cannot place code or data in the NVM block.
- `build.sh` and `flash.sh` both refuse an image larger than 16,320 bytes.
- The image starts at `0x08000000` and ends far below `0x3FC0`; the NVM page is not written by the upload.
- The NVM address and format are unchanged.

## 4. How it works

| File | Purpose |
|---|---|
| `tools/riscv32-unknown-elf-{gcc,objdump,objcopy}` | Thin wrappers so `ch32fun.mk` runs unchanged on clang, lld and llvm |
| `tools/include/{string,stdio,stdlib}.h` | Minimal declarations (Termux has no newlib) |
| `build_native/libc_shim.c` | Weak fallbacks for `memcpy`, `memset`, `memcmp`, `strlen` and similar |
| `build_native/patch_ch32fun.py` | Patches a **temporary copy** of `ch32fun.c` for clang (see section 5) |
| `build_native/Makefile`, `fw.c` | Builds `src/*.c` unchanged; `fw.c` just includes `../src/main.c` |
| `build.sh` | Patch, generate linker script, build, check sizes, copy outputs |
| `detect.sh`, `flash.sh`, `build_minichlink.sh` | Programmer detection, guarded upload, minichlink rebuild |

Nothing in `firmware_d/src/` or `ch32fun-master/` is modified.

## 5. Problems met and how they were solved

1. **`string.h` / `stdio.h` not found.** Clang's bare-metal mode has no libc headers. Fixed with tiny local headers plus weak shim functions.
2. **`memchr` undeclared in `ch32fun.c`.** Added to the shim.
3. **"non-ASM statement in naked function" (3 places).** GCC allows C inside `naked` functions, clang does not. The temporary copy of `ch32fun.c` is patched at three spots:
   - the clock-security NMI handler (`RCC->INTR |= RCC_CSSC`) becomes inline asm ending in `mret` (HSI is used, so it should not fire);
   - the SysTick enable in `handle_reset` becomes an inline-asm store to `0xE000F000`;
   - a stray `__builtin_unreachable()` after `ret` in `longjmp` is removed.
   The script aborts if any pattern fails to match.
4. **minichlink: `libudev.h` not found.** `libudev` is not in the Termux repos. The ESP32-S2 programmer is a USB HID device (`303a:4004`) and stock hidapi uses `hidraw` + `libudev`. `hidapi.c` also contains a libusb backend, so `build_minichlink.sh` copies minichlink, disables the Linux `hidraw` branch (`#elif defined(__linux__)` becomes `#elif 0`), and adds the hidapi 0.15.0 headers. It runs under `su` so libusb can open `/dev/bus/usb`.
5. **Missing `which`.** Only used by the minichlink Makefile for a checksum tool. Bypassed by calling clang directly.

## 6. Requirements

- Termux packages: `clang`, `lld`, `make`, `libusb`, `python`, `curl`
- Root (KernelSU) for USB access
- ch32fun checked out as `~/CompressorProtect/ch32fun-master` (from cnlohr/ch32fun)

## 7. PlatformIO

`firmware_d/platformio.ini` is kept but is not part of this flow. On Android it failed: `platform = ch32v` could not be resolved, no AArch64 toolchain was available, and pyserial needed patching. Nothing here depends on it.

## 8. First flash checklist (when the cable arrives)

1. Connect the ESP32-S2 programmer to the phone over USB-C OTG, and the programmer to the CH32V003 board **with no mains anywhere**.
2. Run `./detect.sh` and confirm `303a:4004` is listed and minichlink reports a programmer.
3. Review the output before continuing.
4. Run `./flash.sh`, check the printed target, file size and start address (`0x08000000`), then type `FLASH`.
5. Treat the first run as a bench test and confirm behavior at low voltage.
