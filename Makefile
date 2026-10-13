CC = clang
LD = ld.lld

CFLAGS = -target x86_64-unknown-none-elf -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel -Itools/limine
LDFLAGS = -nostdlib -static -z max-page-size=0x1000 -T linker.ld

.PHONY: all clean run

all: educational-os.iso

OBJS = kernel/main.o kernel/pfa.o kernel/vmm.o kernel/heap.o

kernel/main.o: kernel/main.c kernel/pfa.h kernel/vmm.h kernel/heap.h tools/limine/limine.h
	$(CC) $(CFLAGS) -c kernel/main.c -o kernel/main.o

kernel/pfa.o: kernel/pfa.c kernel/pfa.h tools/limine/limine.h
	$(CC) $(CFLAGS) -c kernel/pfa.c -o kernel/pfa.o

kernel/vmm.o: kernel/vmm.c kernel/vmm.h kernel/pfa.h tools/limine/limine.h
	$(CC) $(CFLAGS) -c kernel/vmm.c -o kernel/vmm.o

kernel/heap.o: kernel/heap.c kernel/heap.h kernel/vmm.h kernel/pfa.h
	$(CC) $(CFLAGS) -c kernel/heap.c -o kernel/heap.o

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

run: educational-os.iso
	qemu-system-x86_64 -M q35 -m 256M -cdrom educational-os.iso -boot d -serial stdio -display none

clean:
	rm -f $(OBJS) kernel/kernel.elf educational-os.iso
