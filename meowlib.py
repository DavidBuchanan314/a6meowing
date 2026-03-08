"""
Client library for the usb_0xA1_2 protocol (should be ipwndfu-compatible)

Provides exec/memcpy/memset primitives over USB on a pwned DFU device.

Requires: pyusb (python3 -m pip install pyusb)
"""

import struct
import time

import usb.core

# Protocol constants
EXEC_MAGIC = struct.pack("<II", 0x65786563, 0x65786563)
DONE_MAGIC = struct.pack("<II", 0x646F6E65, 0x646F6E65)
MEMC_MAGIC = struct.pack("<II", 0x6D656D63, 0x6D656D63)
MEMS_MAGIC = struct.pack("<II", 0x6D656D73, 0x6D656D73)

DFU_MAX = 2048
USB_READ_LIMIT = 0x1000
CMD_TIMEOUT = 5000  # ms

# ARMv7: 4-byte words
WORD_SIZE = 4
WORD_FMT = "<I"
# Response data offset: after DONE_MAGIC(8) + padding(8)
RESP_DATA_OFFSET = 16
# Command data offset: after magic(8) + padding(8) + 3 args
CMD_DATA_OFFSET = 16 + 3 * WORD_SIZE


class PwnedDFUDevice:
    """Client for a pwned Apple DFU device with usb_0xA1_2 handler installed."""

    def __init__(self, load_address: int = 0x10020000, serial_number: str | None = None) -> None:
        """
        Args:
            load_address: DFU load address (e.g. 0x10020000 for s5l8950x w/ ROM remapped by a6meowing).
            serial_number: If set, only connect to a device with this serial.
        """
        self.load_address = load_address
        self._dev: usb.core.Device | None = None
        self._serial_number = serial_number
        self.connect()

    def connect(self) -> None:
        """Find and connect to an Apple DFU device."""
        if self._serial_number:
            self._dev = usb.core.find(
                idVendor=0x05AC, idProduct=0x1227,
                serial_number=self._serial_number,
            )
        else:
            self._dev = usb.core.find(idVendor=0x05AC, idProduct=0x1227)
        if self._dev is None:
            raise ConnectionError("No Apple DFU device found")
        self._serial_number = self._dev.serial_number

    @property
    def serial_number(self) -> str | None:
        return self._serial_number

    @property
    def pwnd(self) -> bool:
        """True if the device reports a PWND tag in its serial number."""
        return "PWND:" in (self._serial_number or "")

    # -- low-level transport ------------------------------------------------

    @property
    def dev(self) -> usb.core.Device:
        """Return the underlying USB device, raising if not connected."""
        if self._dev is None:
            raise ConnectionError("Not connected")
        return self._dev

    def _dfu_send(self, data: bytes) -> None:
        off = 0
        while off < len(data):
            chunk = data[off:off + DFU_MAX]
            self.dev.ctrl_transfer(0x21, 1, 0, 0, chunk, CMD_TIMEOUT)
            off += len(chunk)

    def command(self, request_data: bytes, response_length: int) -> bytes:
        """Send a command and read back a response.

        See usb_0xA1_2-protocol.md for the transport sequence.
        """
        if not (0 <= response_length <= USB_READ_LIMIT):
            raise ValueError(f"response_length {response_length} out of range 0..{USB_READ_LIMIT:#x}")
        dev = self.dev

        self._dfu_send(b"\x00" * 16)
        dev.ctrl_transfer(0x21, 1, 0, 0, b"", 100)
        dev.ctrl_transfer(0xA1, 3, 0, 0, 6, 100)
        dev.ctrl_transfer(0xA1, 3, 0, 0, 6, 100)
        time.sleep(0.001)  # ROM needs a moment after GETSTATUS
        self._dfu_send(request_data)

        if response_length == 0:
            resp = dev.ctrl_transfer(
                0xA1, 2, 0xFFFF, 0, response_length + 1, CMD_TIMEOUT,
            )
            return bytes(resp)[1:]
        resp = dev.ctrl_transfer(
            0xA1, 2, 0xFFFF, 0, response_length, CMD_TIMEOUT,
        )
        return bytes(resp)

    # -- command helpers ----------------------------------------------------

    def _data_address(self, index: int) -> int:
        return self.load_address + 16 + index * WORD_SIZE

    @staticmethod
    def _cmd_memcpy(dest: int, src: int, length: int) -> bytes:
        return struct.pack("<8s8xIII", MEMC_MAGIC, dest, src, length)

    @staticmethod
    def _cmd_memset(address: int, value: int, length: int) -> bytes:
        return struct.pack("<8s8xIII", MEMS_MAGIC, address, value, length)

    # -- public API ---------------------------------------------------------

    def read(self, address: int, length: int) -> bytes:
        """Read *length* bytes from device memory at *address*."""
        data = b""
        while len(data) < length:
            chunk_len = min(length - len(data), USB_READ_LIMIT - RESP_DATA_OFFSET)
            cmd = self._cmd_memcpy(
                self._data_address(0), address + len(data), chunk_len,
            )
            resp = self.command(cmd, RESP_DATA_OFFSET + chunk_len)
            if resp[:8] != DONE_MAGIC:
                raise RuntimeError(f"expected DONE_MAGIC, got {resp[:8].hex()}")
            data += resp[RESP_DATA_OFFSET:]
        return data

    def write(self, address: int, data: bytes) -> None:
        """Write *data* to device memory at *address*."""
        cmd = self._cmd_memcpy(
            address, self._data_address(3), len(data),
        ) + data
        self.command(cmd, 0)

    def memset(self, address: int, value: int, length: int) -> None:
        """Set *length* bytes at *address* to byte *value*."""
        cmd = self._cmd_memset(address, value, length)
        self.command(cmd, 0)

    def execute(self, func: int, args: list[int],
                response_length: int = 0, aux_data: bytes = b"") -> tuple[int, bytes]:
        """Call *func* with register/stack arguments, optional aux data, and response.

        Args:
            func: Function address (Thumb bit included).
            args: Up to 8 arguments (R0-R3 + 4 stack args).
            response_length: How many bytes of output data to read back.
            aux_data: Extra data appended after args in the command buffer.
                      Its address in device memory is self._data_address(len(args)).

        Returns (retval, response_data).
        """
        # exec layout: [8: EXEC_MAGIC] [4: func ptr] [4: pad] [4*N: args] [aux_data]
        payload = struct.pack(WORD_FMT, func)
        payload += b"\x00" * WORD_SIZE  # padding after func ptr (ARMv7)
        for arg in args:
            payload += struct.pack(WORD_FMT, arg)
        cmd = EXEC_MAGIC + payload + aux_data
        resp = self.command(cmd, RESP_DATA_OFFSET + response_length)
        done = resp[:8]
        if done != DONE_MAGIC:
            raise RuntimeError(f"expected DONE_MAGIC, got {done.hex()}")
        retval = struct.unpack("<Q", resp[8:16])[0]
        return retval, resp[RESP_DATA_OFFSET:RESP_DATA_OFFSET + response_length]

    def exec(self, func: int, *args: int) -> tuple[int, bytes]:
        """Shorthand for execute() with no aux data or response."""
        return self.execute(func, list(args))

    # -- convenience --------------------------------------------------------

    def read32(self, address: int) -> int:
        return struct.unpack("<I", self.read(address, 4))[0]

    def write32(self, address: int, value: int) -> None:
        self.write(address, struct.pack("<I", value))

    def read16(self, address: int) -> int:
        return struct.unpack("<H", self.read(address, 2))[0]

    def write16(self, address: int, value: int) -> None:
        self.write(address, struct.pack("<H", value))

    def read8(self, address: int) -> int:
        return self.read(address, 1)[0]

    def write8(self, address: int, value: int) -> None:
        self.write(address, struct.pack("<B", value))
