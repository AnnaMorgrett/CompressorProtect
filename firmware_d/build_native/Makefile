all : build
TARGET:=fw
SYSTEM_C:=ch32fun_clang.c
TARGET_MCU:=CH32V003
PREFIX:=riscv32-unknown-elf
NEWLIB:=$(CURDIR)/../tools/include
CFLAGS:=-g -Os -ffunction-sections -fdata-sections -msmall-data-limit=8
ADDITIONAL_C_FILES:=$(filter-out ../src/main.c,$(wildcard ../src/*.c)) libc_shim.c
EXTRA_CFLAGS:=-I../src
LINKER_SCRIPT:=nvm.ld
include ../../ch32fun-master/ch32fun/ch32fun.mk
