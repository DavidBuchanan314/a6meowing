CC		= arm-none-eabi-gcc
OBJCOPY	= arm-none-eabi-objcopy

CFLAGS	= -Iinclude/ -march=armv7-a -mthumb -mthumb-interwork
CFLAGS	+= -Wall -fno-stack-protector -nostdlib -nostartfiles -ffreestanding -fno-builtin
CFLAGS	+= -T entry.ld -Wl,--nmagic -Wl,--gc-sections

OBJ		= payload

ENTRY_SRC	= entry.S entryp.c entry.ld
PATCH_SRC	= patch/entry.S patch/patch.c patch/patch.ld
JUMP_SRC	= jump/entry.S jump/jump.c jump/jump.ld

.PHONY: clean

$(OBJ).h: pre_$(OBJ).bin patch/$(OBJ).bin marge
	./marge pre_$(OBJ).bin patch/$(OBJ).bin $(OBJ).bin
	xxd -i $(OBJ).bin $(OBJ).h

pre_$(OBJ).bin: $(ENTRY_SRC) jump/jump.h
	$(CC) entry.S entryp.c $(CFLAGS) -o pre_$(OBJ).elf
	$(OBJCOPY) -O binary pre_$(OBJ).elf pre_$(OBJ).bin
	-$(RM) pre_$(OBJ).elf

patch/$(OBJ).bin: $(PATCH_SRC)
	cd patch && $(MAKE)

jump/jump.h: $(JUMP_SRC) jump/cut.c
	cd jump && $(MAKE)

marge: marge.c
	gcc marge.c -o marge

clean:
	cd jump && make clean
	cd patch && make clean
	-$(RM) marge
	-$(RM) $(OBJ).bin
	-$(RM) $(OBJ).h
	-$(RM) pre_$(OBJ).elf
	-$(RM) pre_$(OBJ).bin

