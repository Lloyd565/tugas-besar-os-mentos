# Compiler & linker
ASM     = nasm
CC      = gcc
LD      = ld
LIN 	= ld
# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386

# Target rules
all: run
build: iso

disk:
	@mkdir -p $(OUTPUT_FOLDER)
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

run: iso
	@echo "Running OS in QEMU..."
	@qemu-system-i386 -s \
		-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
		-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso \
		-audiodev sdl,id=audio0 \
		-machine pcspk-audiodev=audio0 2>/dev/null || \
	qemu-system-i386 -s \
		-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
		-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

kernel:
	@mkdir -p $(OUTPUT_FOLDER)
	@echo "Compiling assembly and C files..."
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/text/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/speaker.c -o $(OUTPUT_FOLDER)/speaker.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@echo "Linking object files and generating ELF32 kernel..."
	@$(LD) $(LFLAGS) \
		$(OUTPUT_FOLDER)/kernel-entrypoint.o \
		$(OUTPUT_FOLDER)/intsetup.o \
		$(OUTPUT_FOLDER)/kernel.o \
		$(OUTPUT_FOLDER)/gdt.o \
		$(OUTPUT_FOLDER)/string.o \
		$(OUTPUT_FOLDER)/framebuffer.o \
		$(OUTPUT_FOLDER)/portio.o \
		$(OUTPUT_FOLDER)/idt.o \
		$(OUTPUT_FOLDER)/interrupt.o \
		$(OUTPUT_FOLDER)/keyboard.o \
		$(OUTPUT_FOLDER)/speaker.o \
		$(OUTPUT_FOLDER)/ext2.o \
		$(OUTPUT_FOLDER)/disk.o \
		$(OUTPUT_FOLDER)/paging.o \
		-o $(OUTPUT_FOLDER)/kernel

iso: kernel
	@echo "Creating ISO image..."
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst $(OUTPUT_FOLDER)/iso/boot/grub/
	@cd $(OUTPUT_FOLDER) && genisoimage -R \
		-b boot/grub/grub1 \
		-no-emul-boot \
		-boot-load-size 4 \
		-A os \
		-input-charset utf8 \
		-boot-info-table \
		-o $(ISO_NAME).iso \
		iso
	@rm -rf $(OUTPUT_FOLDER)/iso/

inserter:
	@$(CC) -Wno-builtin-declaration-mismatch -g -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/stdlib/string.c \
		$(SOURCE_FOLDER)/ext2.c \
		$(SOURCE_FOLDER)/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

clean:
	@echo "Cleaning up..."
	@rm -rf $(OUTPUT_FOLDER)/*

user-shell:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/user-shell.c -o user-shell.o
	@$(CC)  $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o string.o
	@$(CC)  $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/cpu/portio.c -o portio.o
	@$(CC)  $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/driver/speaker.c -o speaker.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o user-shell.o string.o portio.o speaker.o -o $(OUTPUT_FOLDER)/shell
	@echo Linking object shell object files and generate ELF32 for debugging...
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=elf32-i386 \
		crt0.o user-shell.o string.o portio.o speaker.o -o $(OUTPUT_FOLDER)/shell_elf
	@echo Linking object shell object files and generate ELF32 for debugging...
	@size --target=binary $(OUTPUT_FOLDER)/shell
	@rm -f *.o

insert-shell: inserter user-shell
	@echo Inserting shell into root directory... 
	@cd $(OUTPUT_FOLDER); ./inserter shell 2 $(DISK_NAME).bin
	@echo Inserting music file...
	@cp music.txt bin/music.txt
	@cd $(OUTPUT_FOLDER); ./inserter music.txt 2 $(DISK_NAME).bin

run-pulse: iso
	@echo "Running OS in QEMU with PulseAudio..."
	@qemu-system-i386 -s \
		-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
		-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso \
		-audiodev pa,id=audio0 \
		-machine pcspk-audiodev=audio0 || true

run-alsa: iso
	@echo "Running OS in QEMU with ALSA..."
	@qemu-system-i386 -s \
		-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
		-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso \
		-audiodev alsa,id=audio0 \
		-machine pcspk-audiodev=audio0 || true

run-file: iso
	@echo "Running OS in QEMU and saving audio to /tmp/qemu-audio.wav..."
	@qemu-system-i386 -s \
		-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
		-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso \
		-audiodev wav,id=audio0,path=/tmp/qemu-audio.wav \
		-machine pcspk-audiodev=audio0
	@echo "Audio saved to /tmp/qemu-audio.wav - you can play it with: paplay /tmp/qemu-audio.wav"

.PHONY: all

all: clean disk user-shell insert-shell run

run-with-music:
	@if [ -z "$(MUSIC_FILE)" ]; then echo "Usage: make run-with-music MUSIC_FILE=agartha.txt"; exit 1; fi
	@$(MAKE) clean
	@$(MAKE) disk
	@$(MAKE) user-shell
	@$(MAKE) insert-shell
	@$(MAKE) insert-music MUSIC_FILE=$(MUSIC_FILE)
	@$(MAKE) run

insert-music:
	@echo "Usage: make insert-music MUSIC_FILE=path/to/file.txt"
	@echo "Example: make insert-music MUSIC_FILE=agartha.txt"
	@if [ -z "$(MUSIC_FILE)" ]; then echo "Error: MUSIC_FILE not specified"; exit 1; fi
	@if [ ! -f "$(MUSIC_FILE)" ]; then echo "Error: File not found: $(MUSIC_FILE)"; exit 1; fi
	@BASENAME=$$(basename "$(MUSIC_FILE)"); \
	echo "Copying $$BASENAME to bin/"; \
	cp "$(MUSIC_FILE)" "bin/$$BASENAME"; \
	echo "Inserting $$BASENAME into disk..."; \
	if [ ! -f "bin/inserter" ]; then $(MAKE) inserter; fi; \
	if [ ! -f "bin/storage.bin" ]; then $(MAKE) disk; fi; \
	cd bin && ./inserter "$$BASENAME" 2 storage.bin
	@echo "=== SELESAI SEMUA TASK ==="
