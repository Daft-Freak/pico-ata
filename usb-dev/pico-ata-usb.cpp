#include "pico/stdlib.h"
#include "pico/time.h"

#include "tusb.h"

#include "ata.hpp"
#include "identity.hpp"

#include "usb-dev-config.h"

// USB MSC glue
static bool storageEjected = false;

void tud_mount_cb()
{
    storageEjected = false;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void) lun;

    const char vid[] = USB_VENDOR_STR;
    const char pid[] = USB_PRODUCT_STR " Storage";
    const char rev[] = "1.0";

    memcpy(vendor_id  , vid, strlen(vid));
    memcpy(product_id , pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if(storageEjected /*|| Filesystem::getNumSectors() == 0*/)
    {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
        return false;
    }

    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void) lun;

    uint16_t data[256];
    ata::identify_device(0, data);
    ata::IdentityParser parser(data);

    *block_size = 512; // TODO: ATAPI
    *block_count = parser.total_user_addressable_sectors();
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void) lun;
    (void) power_condition;

    if(load_eject)
    {
        if (start)
        {
        }
        else
            storageEjected = true;
    }

    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void) lun;

    // uh, ATA words, not ARM words
    auto word_buf = reinterpret_cast<uint16_t *>(buffer);
    ata::read_sectors(0, lba, bufsize / 512, word_buf);

    return bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void) lun;

    return -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
    uint16_t resplen = 0;

    switch (scsi_cmd[0])
    {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            // Host is about to read/write etc ... better not to disconnect disk
            resplen = 0;
        break;

        default:
            printf("SCSI cmd %02X\n", scsi_cmd[0]);
            // Set Sense = Invalid Command Operation
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

            // negative means error -> tinyusb could stall and/or response with failed status
            resplen = -1;
        break;
    }

    return resplen;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    return false;
}

static void setup_pio_timing()
{
    // this only covers "advanced" PIO modes (3-4)
    // modes 0-2 use word 51

    // identify
    uint16_t data[256];
    ata::identify_device(0, data);

    ata::IdentityParser parser(data);

    // set "advanced" PIO mode (with flow control)
    if(parser.timing_params_valid() && parser.advanced_pio_modes_supported())
    {
        int mode = (parser.advanced_pio_modes_supported() & (1 << 1)) ? 4 : 3;
        ata::set_features(0, ata::ATAFeature::SetTransferMode, 1 << 3/*PIO flow control mode*/ | mode);
    }

    // reconfigure for speed
    int min_cycle_time = 600;
    if(parser.timing_params_valid())
        min_cycle_time = parser.min_pio_cycle_time_iordy();

    ata::adjust_for_min_cycle_time(min_cycle_time);
}

int main()
{
    ata::init_io();

    tusb_init();

    stdio_init_all();

    ata::do_reset();

    setup_pio_timing();

    // TODO: check if ATAPI

    while(true)
    {
        tud_task();
    }

    return 0;
}