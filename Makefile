CC		= gcc

CFLAGS		= -I./include -Wall
CFLAGS		+= -O0
CFLAGS		+= $(shell pkg-config --cflags libusb-1.0)

LDFLAGS		= $(shell pkg-config --libs libusb-1.0)

OBJ		= a6meowing

SOURCE		= io/iousb.c common/common.c

EXPLOIT		= a6meow.c

.PHONY: all clean shellcode

all: shellcode
	$(CC) $(CFLAGS) $(SOURCE) main.c $(EXPLOIT) $(LDFLAGS) -o $(OBJ)

shellcode:
	@if [ ! -f shellcode/payload.h ]; then \
		echo "shellcode/payload.h not found — rebuild requires macOS iOS SDK"; \
		exit 1; \
	fi

clean:
	cd shellcode && make clean
	-$(RM) $(OBJ)
