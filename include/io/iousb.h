#ifndef IOUSB_H
#define IOUSB_H

#include <libusb-1.0/libusb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* IOReturn compatibility — maps macOS IOKit constants to libusb error codes
 * so that a6meow.c and common.c need minimal changes. */
typedef int IOReturn;

#define kIOReturnSuccess        LIBUSB_SUCCESS
#define kIOReturnError          LIBUSB_ERROR_OTHER
#define kIOReturnTimeout        LIBUSB_ERROR_TIMEOUT
#define kIOUSBTransactionTimeout LIBUSB_ERROR_TIMEOUT
#define kIOUSBPipeStalled       LIBUSB_ERROR_PIPE
#define kUSBHostReturnPipeStalled LIBUSB_ERROR_PIPE
#define kIOReturnNotResponding  LIBUSB_ERROR_OTHER
#define kIOReturnNoDevice       LIBUSB_ERROR_NO_DEVICE
#define kIOReturnAborted        LIBUSB_TRANSFER_CANCELLED

#define kAppleVendorID          0x05AC

#define USB_NO_RESET    (0)
#define USB_RESET       (1 << 1)
#define USB_REENUMERATE (1 << 2)
#define USB_REPLUG      (1 << 3)

#define NO_CHECKM8              (0)
#define CHECKM8_A6              (1 << 0)
#define CHECKM8_A7              (1 << 1)
#define CHECKM8_A8_A9           (1 << 2)
#define CHECKM8_A9X_A10X        (1 << 3)
#define CHECKM8_A11             (1 << 4)
#define USE_HEAP_SPRAY_A8_A9    (1 << 5)
#define NO_AUTOBOOT             (1 << 6)
#define APPLE_M1_WITH_USB_C     (1 << 7)

#define DEVICE_DFU              (0x1227)
#define DEVICE_STAGE2           (0x1338)
#define DEVICE_PONGO            (0x4141)
#define DEVICE_RECOVERY_MODE_1  (0x1280)
#define DEVICE_RECOVERY_MODE_2  (0x1281)
#define DEVICE_RECOVERY_MODE_3  (0x1282)
#define DEVICE_RECOVERY_MODE_4  (0x1283)

#define DFU_DNLOAD              (1)
#define DFU_GET_STATUS          (3)
#define DFU_CLR_STATUS          (4)
#define DFU_MAX_TRANSFER_SZ     (0x800)
#define EP0_MAX_PACKET_SZ       (0x40)

typedef struct io_client_p io_client_p;
typedef io_client_p* io_client_t;

struct io_devinfo {
    unsigned int cpid;
    unsigned int cprv;
    unsigned int bdid;
    unsigned int cpfm;
    bool hasSrnm;
    bool hasPwnd;
    bool checkra1nd;
    char* pwnstr;
    char* srtg;
    int checkm8_flag;
};

struct io_client_p {
    libusb_device_handle *handle;
    libusb_context *ctx;
    unsigned int mode;
    unsigned int vid;
    int usb_interface;
    int usb_alt_interface;
    struct io_devinfo devinfo;
    bool hasSerialStr;
    bool isDemotion;
};

typedef struct {
    unsigned char *stage1;
    unsigned int stage1_len;
    unsigned char *pongoOS;
    unsigned int pongoOS_len;
    unsigned char *overwrite;
    unsigned int overwrite_len;
    unsigned char *stage2;
    unsigned int stage2_len;
} checkra1n_payload_t;

typedef struct {
    uint32_t wLenDone;
    int ret;
} transfer_t;

typedef transfer_t async_transfer_t;

int get_device(unsigned int mode, bool srnm);
int get_device_time_stage(io_client_t *pclient, unsigned int time, uint16_t stage, bool snrm);
void send_reboot_via_recovery(io_client_t client);
void read_serial_number(io_client_t client);

void io_close(io_client_t client);
int io_open(io_client_t *pclient, uint16_t pid, bool srnm);
void io_reset(io_client_t client, int flags);
IOReturn io_reenumerate(io_client_t client);
IOReturn io_resetdevice(io_client_t client);
IOReturn io_abort_pipe_zero(io_client_t client);
int io_reconnect(io_client_t *pclient,
                 int retry,
                 uint16_t stage,
                 int flags,
                 bool srnm,
                 unsigned long sec);

transfer_t usb_ctrl_transfer(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length);
transfer_t usb_ctrl_transfer_with_time(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int time);
transfer_t async_usb_ctrl_transfer(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, async_transfer_t* transfer);
uint32_t async_usb_ctrl_transfer_with_cancel(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int ns_time);
uint32_t async_usb_ctrl_transfer_no_error(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length);
uint32_t async_usb_ctrl_transfer_with_cancel_noloop(io_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int ns_time);

#endif
