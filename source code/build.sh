#!/bin/bash
set -e

echo "[1/6] Assembling boot sector..."
nasm -f bin boot.asm -o boot.bin
stat -c %s boot.bin

echo "[2/6] Assembling stage2..."
nasm -f bin stage2.asm -o stage2.bin
stat -c %s stage2.bin

echo "[3/6] Assembling entry.asm (NASM, elf32)..."
nasm -f elf32 entry.asm -o entry.o
stat -c %s entry.o

echo "[3b/6] Compiling kernel.c (clang, i686-elf)..."
clang --target=i686-elf -ffreestanding -nostdlib \
      -fno-pic -fno-pie -mno-sse -mno-mmx \
      -O2 -Wall -Wextra \
      -ffunction-sections -fdata-sections \
      -c kernel.c -o kernel.o
stat -c %s kernel.o

echo "[3c/6]Assembling idt.asm..."
nasm -f elf32 idt.asm -o idt.o

echo "[4/6] Linking..."
ld.lld -T linker.ld --gc-sections -o kernel.elf entry.o idt.o kernel.o
llvm-objcopy -O binary kernel.elf kernel.bin
stat -c %s kernel.bin

echo "[5/6] Creating disk image..."
dd if=/dev/zero of=disk.img bs=512 count=20480 2>/dev/null

echo "[6/6] Writing images to disk..."
dd if=boot.bin   of=disk.img conv=notrunc 2>/dev/null
dd if=stage2.bin of=disk.img bs=512 seek=1 conv=notrunc 2>/dev/null
dd if=kernel.bin of=disk.img bs=512 seek=3 conv=notrunc 2>/dev/null

echo "Done: disk.img"
stat -c %s disk.img
