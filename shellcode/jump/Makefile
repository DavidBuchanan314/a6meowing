CC		= arm-none-eabi-gcc
OBJCOPY	= arm-none-eabi-objcopy

CFLAGS	= -Iinclude/ -march=armv7-a -mthumb -mthumb-interwork
CFLAGS	+= -Wall -fno-stack-protector -nostdlib -nostartfiles -ffreestanding -fno-builtin
CFLAGS	+= -T jump.ld -Wl,--nmagic -Wl,--gc-sections

OBJ		= jump

jump.h: jump.bin
	xxd -i $(OBJ).bin $(OBJ).h

jump.bin: jump.prebin cut
	./cut $(OBJ).prebin $(OBJ).bin
	-$(RM) $(OBJ).prebin

jump.prebin: entry.S jump.c jump.ld
	$(CC) entry.S jump.c $(CFLAGS) -o $(OBJ).elf
	$(OBJCOPY) -O binary $(OBJ).elf $(OBJ).prebin
	-$(RM) $(OBJ).elf

cut: cut.c
	gcc cut.c -o cut

.PHONY: clean
clean:
	-$(RM) $(OBJ).elf
	-$(RM) $(OBJ).bin
	-$(RM) $(OBJ).prebin
	-$(RM) $(OBJ).h
	-$(RM) cut

