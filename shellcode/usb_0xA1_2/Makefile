CC		= arm-none-eabi-gcc
OBJCOPY	= arm-none-eabi-objcopy

CFLAGS	= -march=armv7-a -mthumb -mthumb-interwork
CFLAGS	+= -Wall -fno-stack-protector -nostdlib -nostartfiles -ffreestanding -fno-builtin
CFLAGS	+= -T handler.ld -Wl,--nmagic -Wl,--gc-sections

OBJ		= handler

handler.h: handler.bin
	xxd -i $(OBJ).bin $(OBJ).h

handler.bin: handler.S handler.ld
	$(CC) handler.S $(CFLAGS) -o $(OBJ).elf
	$(OBJCOPY) -O binary $(OBJ).elf $(OBJ).bin
	-$(RM) $(OBJ).elf

.PHONY: clean
clean:
	-$(RM) $(OBJ).elf
	-$(RM) $(OBJ).bin
	-$(RM) $(OBJ).h
