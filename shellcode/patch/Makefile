CC		= arm-none-eabi-gcc
OBJCOPY	= arm-none-eabi-objcopy

CFLAGS	= -Iinclude/ -march=armv7-a -mthumb -mthumb-interwork
CFLAGS	+= -Wall -fno-stack-protector -nostdlib -nostartfiles -ffreestanding -fno-builtin
CFLAGS	+= -T patch.ld -Wl,--nmagic -Wl,--gc-sections

OBJ		= payload

payload.bin: entry.S patch.c patch.ld
	$(CC) entry.S patch.c $(CFLAGS) -o $(OBJ).elf
	$(OBJCOPY) -O binary $(OBJ).elf $(OBJ).bin
	-$(RM) $(OBJ).elf

.PHONY: clean
clean:
	-$(RM) $(OBJ).elf
	-$(RM) $(OBJ).bin

