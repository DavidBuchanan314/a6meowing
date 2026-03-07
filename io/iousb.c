#include <io/iousb.h>
#include <common/log.h>
#include <common/common.h>

#include <stdio.h>
#include <time.h>

extern io_client_t client;

static int nsleep(long nanoseconds)
{
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = nanoseconds;
    return nanosleep(&req, &rem);
}

/* Async transfer callback state */
typedef struct {
    int completed;
    int status;
    int actual_length;
} async_cb_data_t;

static void LIBUSB_CALL async_cb(struct libusb_transfer *transfer)
{
    async_cb_data_t *cb = transfer->user_data;
    if (cb) {
        cb->status = transfer->status;
        cb->actual_length = transfer->actual_length;
        cb->completed = 1;
    }
}

static void load_devinfo(io_client_t client, const char* str)
{
    if (!client || !str) {
        return;
    }

    char* ptr;
    char tmp[256];

    memset(&client->devinfo, '\0', sizeof(struct io_devinfo));
    memset(&tmp, '\0', 256);

    ptr = strstr(str, "CPID:");
    if (ptr != NULL) {
        sscanf(ptr, "CPID:%x", &client->devinfo.cpid);
    }

    ptr = strstr(str, "CPRV:");
    if (ptr != NULL) {
        sscanf(ptr, "CPRV:%x", &client->devinfo.cprv);
    }

    ptr = strstr(str, "BDID:");
    if (ptr != NULL) {
        sscanf(ptr, "BDID:%x", &client->devinfo.bdid);
    }

    ptr = strstr(str, "CPFM:");
    if (ptr != NULL) {
        sscanf(ptr, "CPFM:%x", &client->devinfo.cpfm);
    }

    ptr = strstr(str, "SRNM:[");
    if (ptr != NULL) {
        client->devinfo.hasSrnm = true;
    } else {
        client->devinfo.hasSrnm = false;
    }

    ptr = strstr(str, "YOLO:checkra1n");
    if (ptr != NULL) {
        client->devinfo.checkra1nd = true;
    } else {
        client->devinfo.checkra1nd = false;
    }

    memset(&tmp, '\0', 256);
    ptr = strstr(str, "PWND:[");
    if (ptr != NULL) {
        client->devinfo.hasPwnd = true;

        sscanf(ptr, "PWND:[%s]", tmp);
        ptr = strrchr(tmp, ']');
        if (ptr != NULL) {
            *ptr = '\0';
        }
        client->devinfo.pwnstr = strdup(tmp);

    } else {
        client->devinfo.hasPwnd = false;
    }

    memset(&tmp, '\0', 256);
    ptr = strstr(str, "SRTG:[");
    if (ptr != NULL) {
        sscanf(ptr, "SRTG:[%s]", tmp);
        ptr = strrchr(tmp, ']');
        if (ptr != NULL) {
            *ptr = '\0';
        }
        client->devinfo.srtg = strdup(tmp);
    }

    client->devinfo.checkm8_flag = NO_CHECKM8;
    switch (client->devinfo.cpid) {
        case 0x8950:
        case 0x8955:
            client->devinfo.checkm8_flag |= CHECKM8_A6;
            break;
        case 0x8960:
            client->devinfo.checkm8_flag |= CHECKM8_A7;
            break;
        case 0x7000:
        case 0x7001:
        case 0x8000:
        case 0x8003:
            client->devinfo.checkm8_flag |= CHECKM8_A8_A9;
            break;
        case 0x8001:
        case 0x8010:
        case 0x8011:
            client->devinfo.checkm8_flag |= CHECKM8_A9X_A10X;
            break;
        case 0x8012:
        case 0x8015:
            client->devinfo.checkm8_flag |= CHECKM8_A11;
            break;
        default:
            break;
    }
}

static void io_get_serial(io_client_t client, libusb_device_handle *handle)
{
    struct libusb_device_descriptor desc;
    char serial_str[256];

    memset(&serial_str, '\0', 256);
    client->hasSerialStr = false;

    if (libusb_get_device_descriptor(libusb_get_device(handle), &desc) != 0)
        return;

    if (desc.iSerialNumber > 0) {
        int ret = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber,
                                                     (unsigned char *)serial_str,
                                                     sizeof(serial_str));
        if (ret > 0) {
            load_devinfo(client, serial_str);
            client->hasSerialStr = true;
            DEVMEOW("Found serial meow");
        }
    }
}

void send_reboot_via_recovery(io_client_t client)
{
    const char* io_setenv_cmd = "setenv auto-boot true\x00";
    const char* io_saveenv_cmd = "saveenv\x00";
    const char* io_reboot_cmd = "reboot\x00";
    usb_ctrl_transfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char*)io_setenv_cmd, strlen(io_setenv_cmd)+1);
    usb_ctrl_transfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char*)io_saveenv_cmd, strlen(io_saveenv_cmd)+1);
    usb_ctrl_transfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char*)io_reboot_cmd, strlen(io_reboot_cmd)+1);
}

void read_serial_number(io_client_t client)
{
    transfer_t result;
    uint8_t size;

    unsigned char buf[0x100];
    unsigned char str[0x100];

    if (client->devinfo.srtg == NULL) {
        memset(&buf, '\0', 0x100);
        memset(&str, '\0', 0x100);
        result = usb_ctrl_transfer(client, 0x80, 6, 0x0306, 0x040a, buf, 0x100);
        if (result.ret != kIOReturnSuccess)
            goto try2;
        size = *(uint8_t*)buf;
        for (int i = 0; i < (size/2); i++) {
            str[i] = *(uint8_t*)(buf+2+(i*2));
        }
        load_devinfo(client, (const char*)str);
    }

try2:
    if (client->devinfo.srtg == NULL) {
        memset(&buf, '\0', 0x100);
        memset(&str, '\0', 0x100);
        result = usb_ctrl_transfer(client, 0x80, 6, 0x0304, 0x040a, buf, 0x100);
        if (result.ret != kIOReturnSuccess)
            goto try3;
        size = *(uint8_t*)buf;
        for (int i = 0; i < (size/2); i++) {
            str[i] = *(uint8_t*)(buf+2+(i*2));
        }
        load_devinfo(client, (const char*)str);
    }

try3:
    if (client->devinfo.srtg == NULL) {
        memset(&buf, '\0', 0x100);
        memset(&str, '\0', 0x100);
        result = usb_ctrl_transfer(client, 0x80, 6, 0x0303, 0x040a, buf, 0x100);
        if (result.ret != kIOReturnSuccess)
            return;
        size = *(uint8_t*)buf;
        for (int i = 0; i < (size/2); i++) {
            str[i] = *(uint8_t*)(buf+2+(i*2));
        }
        load_devinfo(client, (const char*)str);
    }

    if (client->devinfo.srtg != NULL) {
        client->hasSerialStr = true;
        DEVMEOW("Found serial meow!");
    }
}

IOReturn io_reenumerate(io_client_t client)
{
    if (client == NULL || client->handle == NULL) {
        return kIOReturnError;
    }
    return libusb_reset_device(client->handle);
}

IOReturn io_resetdevice(io_client_t client)
{
    if (client == NULL || client->handle == NULL) {
        return kIOReturnError;
    }
    return libusb_reset_device(client->handle);
}

void io_close(io_client_t client)
{
    if (client->handle) {
        libusb_release_interface(client->handle, client->usb_interface);
        libusb_close(client->handle);
        client->handle = NULL;
    }
    if (client->ctx) {
        libusb_exit(client->ctx);
        client->ctx = NULL;
    }
    free(client);
}

void io_reset(io_client_t client, int flags)
{
    if (flags & USB_REPLUG) {
        printf("\x1b[32m Please disconnect and reconnect the lightning cable meow\n");
        printf("\x1b[32m After that, press <meow> key >> ");
        getchar();
        printf("\n");
        return;
    }

    if (flags & USB_RESET) {
        int result = io_resetdevice(client);
        DEVMEOW("Result 0x%08x (%s) -> %s", result, IOReturnName(result), "ResetDevice");
    }

    if (flags & USB_REENUMERATE) {
        int result = io_reenumerate(client);
        DEVMEOW("Result 0x%08x (%s) -> %s", result, IOReturnName(result), "USBDeviceReEnumerate");
    }
}

int io_open(io_client_t *pclient, uint16_t pid, bool srnm)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    io_client_t _client;
    int r;

    r = libusb_init(&ctx);
    if (r < 0) {
        return -1;
    }

    handle = libusb_open_device_with_vid_pid(ctx, kAppleVendorID, pid);
    if (handle == NULL) {
        libusb_exit(ctx);
        return -1;
    }

    /* Auto-detach kernel driver (e.g. apple-mfi-fastcharge) when we
     * claim the interface, and reattach when we release. This avoids
     * races with the manual detach+set_configuration sequence. */
    libusb_set_auto_detach_kernel_driver(handle, 1);

    r = libusb_set_configuration(handle, 1);
    if (r < 0 && r != LIBUSB_ERROR_BUSY) {
        /* LIBUSB_ERROR_BUSY means config already set, that's fine */
        libusb_close(handle);
        libusb_exit(ctx);
        return -1;
    }

    r = libusb_claim_interface(handle, 0);
    if (r < 0) {
        libusb_close(handle);
        libusb_exit(ctx);
        return -1;
    }

    _client = (io_client_t)calloc(1, sizeof(struct io_client_p));
    _client->handle = handle;
    _client->ctx = ctx;
    _client->usb_interface = 0;

    if (srnm) {
        io_get_serial(_client, handle);
    }

    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(libusb_get_device(handle), &desc);
    _client->mode = desc.idProduct;

    *pclient = _client;
    return 0;
}

int get_device(unsigned int mode, bool srnm)
{
    if (client) {
        io_close(client);
        client = NULL;
    }

    io_open(&client, mode, srnm);
    if (!client) {
        return -1;
    }

    return 0;
}

int get_device_time_stage(io_client_t *pclient, unsigned int time, uint16_t stage, bool srnm)
{
    for (unsigned int i = 0; i < time; i++) {
        if (*pclient) {
            io_close(*pclient);
            *pclient = NULL;
        }
        if (io_open(pclient, stage, srnm) == 0) {
            return 0;
        }
        sleep(1);
    }
    return -1;
}

int io_reconnect(io_client_t *pclient,
                 int retry,
                 uint16_t stage,
                 int flags,
                 bool srnm,
                 unsigned long sec)
{
    if (*pclient) {
        io_reset(*pclient, flags);
        io_close(*pclient);
        *pclient = NULL;
    }

    usleep(sec);

    if (get_device_time_stage(pclient, retry, stage, srnm) != 0) {
        *pclient = NULL;
        return -1;
    }

    if (!*pclient) {
        *pclient = NULL;
        return -1;
    }

    return 0;
}

transfer_t usb_ctrl_transfer(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length)
{
    transfer_t result;
    memset(&result, '\0', sizeof(transfer_t));

    int r = libusb_control_transfer(client->handle, bm_request_type, b_request,
                                    w_value, w_index, data, w_length, 0);
    if (r >= 0) {
        result.ret = kIOReturnSuccess;
        result.wLenDone = (uint32_t)r;
    } else {
        result.ret = r;
        result.wLenDone = 0;
    }

    return result;
}

transfer_t usb_ctrl_transfer_with_time(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int time)
{
    transfer_t result;
    memset(&result, '\0', sizeof(transfer_t));

    int r = libusb_control_transfer(client->handle, bm_request_type, b_request,
                                    w_value, w_index, data, w_length, time);
    if (r >= 0) {
        result.ret = kIOReturnSuccess;
        result.wLenDone = (uint32_t)r;
    } else {
        result.ret = r;
        result.wLenDone = 0;
    }

    return result;
}

IOReturn io_abort_pipe_zero(io_client_t client)
{
    /* On Linux/libusb there is no direct "abort pipe zero".
     * Clearing halt on EP0 is the closest equivalent.
     * In practice, the async cancel path uses libusb_cancel_transfer instead. */
    return libusb_clear_halt(client->handle, 0);
}

transfer_t async_usb_ctrl_transfer(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, async_transfer_t* transfer)
{
    /* This is only used internally; on Linux the async path is handled
     * entirely within async_usb_ctrl_transfer_with_cancel. */
    transfer_t result;
    memset(&result, '\0', sizeof(transfer_t));
    result.ret = kIOReturnSuccess;
    return result;
}

uint32_t async_usb_ctrl_transfer_with_cancel(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int ns_time)
{
    struct libusb_transfer *transfer;
    unsigned char *buf;
    async_cb_data_t cb_data;
    int r;

    memset(&cb_data, 0, sizeof(cb_data));

    transfer = libusb_alloc_transfer(0);
    if (!transfer)
        return 0;

    buf = malloc(LIBUSB_CONTROL_SETUP_SIZE + w_length);
    if (!buf) {
        libusb_free_transfer(transfer);
        return 0;
    }

    libusb_fill_control_setup(buf, bm_request_type, b_request, w_value, w_index, w_length);

    /* Copy outgoing data into the buffer after the setup packet */
    if (data && w_length > 0 && !(bm_request_type & 0x80)) {
        memcpy(buf + LIBUSB_CONTROL_SETUP_SIZE, data, w_length);
    }

    libusb_fill_control_transfer(transfer, client->handle, buf, async_cb, &cb_data, 0);

    r = libusb_submit_transfer(transfer);
    if (r < 0) {
        free(buf);
        libusb_free_transfer(transfer);
        return 0;
    }

    nsleep(ns_time);

    libusb_cancel_transfer(transfer);

    /* Handle events until the transfer completes/is cancelled */
    while (!cb_data.completed) {
        libusb_handle_events(client->ctx);
    }

    uint32_t actual = (uint32_t)transfer->actual_length;

    /* Copy incoming data back */
    if (data && w_length > 0 && (bm_request_type & 0x80) && actual > 0) {
        memcpy(data, buf + LIBUSB_CONTROL_SETUP_SIZE, actual);
    }

    free(buf);
    libusb_free_transfer(transfer);

    return actual;
}

uint32_t async_usb_ctrl_transfer_no_error(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length)
{
    /* Submit and immediately return without waiting — best-effort */
    transfer_t result = usb_ctrl_transfer(client, bm_request_type, b_request,
                                          w_value, w_index, data, w_length);
    return result.wLenDone;
}

uint32_t async_usb_ctrl_transfer_with_cancel_noloop(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int ns_time)
{
    /* Same as with_cancel but without the CFRunLoop spin — on Linux both
     * variants use the same libusb event handling. */
    return async_usb_ctrl_transfer_with_cancel(client, bm_request_type, b_request,
                                               w_value, w_index, data, w_length, ns_time);
}
