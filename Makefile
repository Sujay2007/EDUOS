CC = clang
LD = ld.lld

CFLAGS = -target x86_64-unknown-none-elf -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel -Itools/limine
LDFLAGS = -nostdlib -static -z max-page-size=0x1000 -T linker.ld

.PHONY: all clean run

all: educational-os.iso disk.img

OBJS = kernel/main.o kernel/pfa.o kernel/vmm.o kernel/heap.o \
       kernel/gdt.o kernel/gdt_flush.o kernel/idt.o kernel/idt_stubs.o \
       kernel/pic.o kernel/pit.o \
       kernel/serial.o kernel/keyboard.o kernel/terminal.o kernel/shell.o \
       kernel/thread.o kernel/thread_test.o \
       kernel/syscall.o kernel/usermode.o kernel/usermode_entry.o \
       kernel/process.o kernel/process_test.o kernel/process_test_blobs.o \
       kernel/block.o kernel/ata.o kernel/fs.o kernel/fs_test.o

kernel/block.o: kernel/block.c kernel/block.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/ata.o: kernel/ata.c kernel/block.h kernel/io.h kernel/idt.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/fs.o: kernel/fs.c kernel/fs.h kernel/block.h kernel/serial.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/fs_test.o: kernel/fs_test.c kernel/fs.h kernel/block.h kernel/serial.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/main.o kernel/shell.o: kernel/fs.h kernel/block.h

kernel/main.o: kernel/main.c kernel/io.h kernel/serial.h kernel/pfa.h kernel/vmm.h kernel/heap.h kernel/gdt.h kernel/idt.h kernel/pit.h kernel/keyboard.h kernel/terminal.h kernel/shell.h kernel/thread.h kernel/thread_test.h kernel/syscall.h kernel/usermode.h kernel/process.h kernel/process_test.h tools/limine/limine.h
	$(CC) $(CFLAGS) -c kernel/main.c -o kernel/main.o

kernel/serial.o: kernel/serial.c kernel/serial.h kernel/io.h
	$(CC) $(CFLAGS) -c kernel/serial.c -o kernel/serial.o

kernel/keyboard.o: kernel/keyboard.c kernel/keyboard.h kernel/io.h kernel/pic.h kernel/idt.h kernel/serial.h
	$(CC) $(CFLAGS) -c kernel/keyboard.c -o kernel/keyboard.o

kernel/terminal.o: kernel/terminal.c kernel/terminal.h kernel/serial.h
	$(CC) $(CFLAGS) -c kernel/terminal.c -o kernel/terminal.o

kernel/shell.o: kernel/shell.c kernel/shell.h kernel/terminal.h kernel/keyboard.h kernel/pfa.h kernel/vmm.h kernel/heap.h kernel/pit.h kernel/io.h kernel/idt.h kernel/thread.h kernel/process.h kernel/process_test.h kernel/usermode.h
	$(CC) $(CFLAGS) -c kernel/shell.c -o kernel/shell.o

kernel/thread.o: kernel/thread.c kernel/thread.h kernel/heap.h kernel/gdt.h kernel/pit.h kernel/serial.h kernel/process.h
	$(CC) $(CFLAGS) -c kernel/thread.c -o kernel/thread.o

kernel/thread_test.o: kernel/thread_test.c kernel/thread_test.h kernel/thread.h kernel/pit.h kernel/serial.h
	$(CC) $(CFLAGS) -c kernel/thread_test.c -o kernel/thread_test.o

kernel/syscall.o: kernel/syscall.c kernel/syscall.h kernel/vmm.h kernel/serial.h kernel/thread.h kernel/process.h kernel/idt.h
	$(CC) $(CFLAGS) -c kernel/syscall.c -o kernel/syscall.o

kernel/usermode.o: kernel/usermode.c kernel/usermode.h kernel/syscall.h kernel/vmm.h kernel/pfa.h kernel/heap.h kernel/gdt.h kernel/serial.h
	$(CC) $(CFLAGS) -c kernel/usermode.c -o kernel/usermode.o

kernel/usermode_entry.o: kernel/usermode_entry.S
	$(CC) $(CFLAGS) -c kernel/usermode_entry.S -o kernel/usermode_entry.o

kernel/process.o: kernel/process.c kernel/process.h kernel/thread.h kernel/vmm.h kernel/pfa.h kernel/heap.h kernel/serial.h
	$(CC) $(CFLAGS) -c kernel/process.c -o kernel/process.o

kernel/process_test.o: kernel/process_test.c kernel/process_test.h kernel/process.h kernel/thread.h kernel/vmm.h kernel/pfa.h kernel/pit.h kernel/serial.h kernel/syscall.h
	$(CC) $(CFLAGS) -c kernel/process_test.c -o kernel/process_test.o

kernel/process_test_blobs.o: kernel/process_test_blobs.S
	$(CC) $(CFLAGS) -c kernel/process_test_blobs.S -o kernel/process_test_blobs.o

kernel/pfa.o: kernel/pfa.c kernel/pfa.h tools/limine/limine.h
	$(CC) $(CFLAGS) -c kernel/pfa.c -o kernel/pfa.o

kernel/vmm.o: kernel/vmm.c kernel/vmm.h kernel/pfa.h tools/limine/limine.h
	$(CC) $(CFLAGS) -c kernel/vmm.c -o kernel/vmm.o

kernel/heap.o: kernel/heap.c kernel/heap.h kernel/vmm.h kernel/pfa.h
	$(CC) $(CFLAGS) -c kernel/heap.c -o kernel/heap.o

kernel/gdt.o: kernel/gdt.c kernel/gdt.h
	$(CC) $(CFLAGS) -c kernel/gdt.c -o kernel/gdt.o

kernel/gdt_flush.o: kernel/gdt_flush.S
	$(CC) $(CFLAGS) -c kernel/gdt_flush.S -o kernel/gdt_flush.o

kernel/idt.o: kernel/idt.c kernel/idt.h kernel/gdt.h kernel/pic.h kernel/serial.h kernel/thread.h kernel/process.h
	$(CC) $(CFLAGS) -c kernel/idt.c -o kernel/idt.o

kernel/idt_stubs.o: kernel/idt_stubs.S
	$(CC) $(CFLAGS) -c kernel/idt_stubs.S -o kernel/idt_stubs.o

kernel/pic.o: kernel/pic.c kernel/pic.h
	$(CC) $(CFLAGS) -c kernel/pic.c -o kernel/pic.o

kernel/pit.o: kernel/pit.c kernel/pit.h kernel/pic.h kernel/idt.h
	$(CC) $(CFLAGS) -c kernel/pit.c -o kernel/pit.o

kernel/kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel/kernel.elf $(OBJS)

educational-os.iso: kernel/kernel.elf limine.conf
	mkdir -p iso_root/boot
	cp kernel/kernel.elf iso_root/boot/kernel.elf
	cp limine.conf iso_root/limine.conf
	cp tools/limine/limine-bios.sys iso_root/
	cp tools/limine/limine-bios-cd.bin iso_root/
	cp tools/limine/limine-uefi-cd.bin iso_root/
	xorriso -as mkisofs -R -r -J \
		-b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
		--protective-msdos-label iso_root -o educational-os.iso
	tools/limine/limine bios-install educational-os.iso

disk.img:
	python3 tools/mkfs.py $@ samples

run: educational-os.iso disk.img
	qemu-system-x86_64 -M q35 -m 256M -cdrom educational-os.iso -boot d -serial stdio -display none \
		-device piix3-ide,id=eduide \
		-drive if=none,id=edudisk,file=disk.img,format=raw \
		-device ide-hd,drive=edudisk,bus=eduide.0

clean:
	rm -f $(OBJS) kernel/kernel.elf educational-os.iso
