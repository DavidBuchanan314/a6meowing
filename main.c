/*
 * a6meowing - main.c
 *
 * Copyright (c) 2021 - 2023 kok3shidoll
 *
 */

#include <io/iousb.h>
#include <common/common.h>
#include <common/log.h>

#include <getopt.h>

#define remap_rom_to_sram   (1 << 0)
#define enable_demotion     (1 << 1)
#define use_usb_0xA1_2_handler (1 << 2)

io_client_t client;
bool debug_enabled = false;

int a6meowing(io_client_t *pclient, uint16_t flag);

static void meow_list(void)
{
    printf("Device list:\n");
    printf("\t\x1b[36ms5l8950x\x1b[39m - \x1b[35mApple A6\x1b[39m\n");
}

static void meow_usage(char** argv)
{
    printf("Usage: %s [option]\n", argv[0]);
    
    printf("  -h, --help\t\t\t\x1b[36mmeow usage\x1b[39m\n");
    printf("  -l, --list\t\t\t\x1b[36mmeow list of supported devices\x1b[39m\n");
    printf("  -c, --cleandfu\t\t\x1b[36mmeow cleandfu\x1b[39m\n");
    printf("  -n, --no-handler\t\t\x1b[36mdon't install usb_0xA1_2 handler\x1b[39m\n");
    printf("  -d, --debug\t\t\t\x1b[36menable meow log\x1b[39m\n");
    printf("\n");
}

int main(int argc, char** argv)
{
    
    bool useRecovery = false;
    bool noHandler = false;

    MEOW_NOFUNC("================================");
    MEOW_NOFUNC("::");
    MEOW_NOFUNC(":: a6meowing v1.0.0 for linux");
    MEOW_NOFUNC("::");
    MEOW_NOFUNC(":: (c) 2020-2023 kok3shidoll");
    MEOW_NOFUNC("::");
    MEOW_NOFUNC(":: BUILD_STYLE: %s", "RELEASE");
    MEOW_NOFUNC("::");
    MEOW_NOFUNC(":: ---- made by ----");
    MEOW_NOFUNC(":: kok3shidoll (meow)");
    MEOW_NOFUNC(":: ---- with changes by ----");
    MEOW_NOFUNC(":: retr0id");
    MEOW_NOFUNC(":: ---- thanks to ----");
    MEOW_NOFUNC(":: checkra1n");
    MEOW_NOFUNC("================================");
    MEOW_NOFUNC("");
    
    int opt = 0;
    static struct option longopts[] = {
        { "help",           no_argument,       NULL, 'h' },
        { "list",           no_argument,       NULL, 'l' },
        { "cleandfu",       no_argument,       NULL, 'c' },
        { "no-handler",     no_argument,       NULL, 'n' },
        { "debug",          no_argument,       NULL, 'd' },
        { NULL, 0, NULL, 0 }
    };
    
    const char *opsStr = "hlcnd";
    
    while ((opt = getopt_long(argc, argv, opsStr, longopts, NULL)) > 0) {
        switch (opt) {
            case 'h':
                meow_usage(argv);
                return 0;
                
            case 'l':
                meow_list();
                return 0;
                
            case 'd':
                debug_enabled = true;
                DEVMEOW("Enabled meowing log");
                break;
                
            case 'c':
                useRecovery = true;
                break;
                
            case 'n':
                noHandler = true;
                break;
                
            default:
                meow_usage(argv);
                return -1;
        }
    }
    
    if(useRecovery) {
        if(enter_dfu_via_recovery(client)) {
            return -1;
        }
    }
    
    MEOWWWW("Waiting for DFU meow device");
    while(get_device(DEVICE_DFU, true)) {
        sleep(1);
    }
    
    MEOWWWW("Found DFU meow device");
    sleep(1);
    
    if(client->hasSerialStr == false) {
        read_serial_number(client); // For iOS 10 and lower
    }
    
    DEVMEOW("CPID: 0x%02x, CPRV:0x%02x ", client->devinfo.cpid, client->devinfo.cprv);
    
    if(client->hasSerialStr != true) {
        ERROR("Serial number was not meow");
        return -1;
    }
    
    int attempt = 0;
    while(1) {
        attempt++;
        MEOWWWW("Exploit attempt %d", attempt);

        a6meowing(&client, noHandler ? remap_rom_to_sram : remap_rom_to_sram | use_usb_0xA1_2_handler);

        /* a6meowing() propagates the last reconnected handle back through
         * &client. get_device() will close it before opening a fresh one. */
        usleep(500000);

        MEOWWWW("Waiting for DFU meow device");
        while(get_device(DEVICE_DFU, true)) {
            sleep(1);
        }

        if(client->hasSerialStr && client->devinfo.hasPwnd) {
            MEOWWWW("PWND:[%s] — exploit succeeded on attempt %d", client->devinfo.pwnstr, attempt);
            break;
        }

        MEOWWWW("Not pwned yet, retrying...");
        sleep(1);
    }

    return 0;
}

