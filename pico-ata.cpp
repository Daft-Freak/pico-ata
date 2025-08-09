#include <cstdio>

#include "pico/stdlib.h"
#include "pico/time.h"

#include "tusb.h"

#include "config.h"

// includes CS and addr
enum class ATAReg
{
    Data        = 2 << 3 | 0,
    SectorCount = 2 << 3 | 2,
    LBALow      = 2 << 3 | 3,
    LBAMid      = 2 << 3 | 4,
    LBAHigh     = 2 << 3 | 5,
    Device      = 2 << 3 | 6,
    Status      = 2 << 3 | 7, // read-only
    Command     = 2 << 3 | 7, // write-only
};

enum ATAStatus
{
    Status_ERR  = 1 << 0, // error
    Status_DRQ  = 1 << 3, // data request
    Status_DF   = 1 << 5, // device fault
    Status_DRDY = 1 << 6, // device ready
    Status_BSY  = 1 << 7, // busy
};

enum class ATACommand
{
    READ_SECTOR     = 0x20,
    IDENTIFY_DEVICE = 0xEC,
};

static void init_io()
{
    // setup all the IO
    gpio_init_mask(ATA_IO_MASK);

    // init the active low control signals
    auto mask = ATA_READ_PIN_MASK | ATA_WRITE_PIN_MASK | ATA_RESET_PIN_MASK | ATA_CS_PIN_MASK;
    gpio_put_masked(mask, mask);

    // also set address pins to output
    mask |= ATA_ADDR_PIN_MASK;
    gpio_set_dir_out_masked(mask);
}

static uint16_t read_register(ATAReg reg)
{
    // the slowest cycle time we could possibly need here is 660ns

    // set address
    gpio_put_masked(ATA_CS_PIN_MASK | ATA_ADDR_PIN_MASK, static_cast<int>(reg) >> 3 << ATA_CS_PIN_BASE | (static_cast<int>(reg) & 7) << ATA_ADDR_PIN_BASE);

    sleep_us(1); // 70ns worst case

    // assert IOR
    gpio_put(ATA_READ_PIN, false);
    sleep_us(1); // 290ns for mode 0-2

    uint16_t data = gpio_get_all() >> ATA_DATA_PIN_BASE;

    gpio_put(ATA_READ_PIN, true);

    return data;
}

static void write_register(ATAReg reg, uint16_t data)
{
    // the slowest cycle time we could possibly need here is 660ns

    // set address
    gpio_put_masked(ATA_CS_PIN_MASK | ATA_ADDR_PIN_MASK, static_cast<int>(reg) >> 3 << ATA_CS_PIN_BASE | (static_cast<int>(reg) & 7) << ATA_ADDR_PIN_BASE);

    sleep_us(1); // 70ns worst case

    // assert IOW
    gpio_put(ATA_WRITE_PIN, false);
    sleep_us(1); // 290ns for mode 0-2

    // put data
    gpio_put_masked(ATA_DATA_PIN_MASK, data << ATA_DATA_PIN_BASE);
    gpio_set_dir_out_masked(ATA_DATA_PIN_MASK);
    sleep_us(1); // 60ns before end of IOW pulse (worst case)

    gpio_put(ATA_WRITE_PIN, true);
    gpio_set_dir_in_masked(ATA_DATA_PIN_MASK);
}

// tiny helper
static void write_command(ATACommand command)
{
    write_register(ATAReg::Command, static_cast<int>(command));
}

static bool check_ready()
{
    auto status = read_register(ATAReg::Status);

    // !BSY && DRDY
    return !(status & Status_BSY) && (status & Status_DRDY);
}

static bool check_data_request()
{
    auto status = read_register(ATAReg::Status);

    // !BSY && DRQ
    return !(status & Status_BSY) && (status & Status_DRQ);
}

static void do_reset()
{
    // assert reset
    gpio_put(ATA_RESET_PIN, false);
    sleep_us(25);

    // now wait a bit
    gpio_put(ATA_RESET_PIN, true);
    sleep_ms(2);

    // wait for reset
    while(true)
    {
        auto status = read_register(ATAReg::Status);

        // check for !BSY
        if(!(status & Status_BSY))
            break;
    }
}

static void do_pio_read(uint16_t *data, int count)
{
    // TODO: this could be a lot more efficient
    while(!check_data_request());

    for(int i = 0; i < count; i++)
        data[i] = read_register(ATAReg::Data);
}

static void read_sectors(int device, uint32_t lba, int num_sectors, uint16_t *data)
{
    // TODO: error checking
    // TODO: timeout?

    assert(device < 2);
    assert(num_sectors <= 256);
    assert(lba < 0x10000000); // TODO: LBA48

    while(!check_ready());

    write_register(ATAReg::SectorCount, num_sectors & 0xFF); // 0 == 256, so just throw away the high bit
    write_register(ATAReg::LBALow, lba & 0xFF);
    write_register(ATAReg::LBAMid, (lba >> 8) & 0xFF);
    write_register(ATAReg::LBAMid, (lba >> 16) & 0xFF);
    write_register(ATAReg::Device, 1 << 6 /*LBA*/ | device << 4 /*device id*/ | ((lba >> 24) & 0xF));
    write_command(ATACommand::READ_SECTOR);

    for(int sector = 0; sector < num_sectors; sector++)
    {
        // 512 bytes per sector
        do_pio_read(data + sector * 256, 256);
    }
}

static void print_identify_result(uint16_t data[256])
{
    auto get_string = [](uint16_t *in_ptr, char *out_ptr, int len)
    {
        for(int i = 0; i < len; i += 2)
        {
            *out_ptr++ = *in_ptr >> 8;
            *out_ptr++ = *in_ptr++ & 0xFF;
        }
        *out_ptr++ = 0;
    };

    char string_buf[41];

    printf("IDENTIFY DEVICE:\n");

    // bit 15 should be 0
    printf("\t%sremovable, %scomplete response\n", (data[0] & (1 << 7)) ? "" : "non-", (data[0] & (1 << 2)) ? "in" : "");
    printf("\t%i cylinders\n", data[1]);

    switch(data[2])
    {
        case 0x37C8:
            printf("\trequires spin-up, response incomplete\n");
            break;

        case 0x738C:
            printf("\trequires spin-up, response complete\n");
            break;

        case 0x8C73:
            printf("\tdoes not require spin-up, response incomplete\n");
            break;

        case 0xC837:
            printf("\tdoes not require spin-up, response complete\n");
            break;
    }

    printf("\t%i heads\n", data[3]);
    printf("\t%i bytes per track\n", data[4]);
    printf("\t%i bytes per sector\n", data[5]);
    printf("\t%i sectors per track\n", data[6]);

    // 7-8 are for CF

    get_string(data + 10, string_buf, 20);
    printf("\tserial no: \"%s\"\n", string_buf);

    // 20-21 are buffer related

    get_string(data + 23, string_buf, 8);
    printf("\tfirmware rev: \"%s\"\n", string_buf);
    get_string(data + 27, string_buf, 40);
    printf("\tmodel no: \"%s\"\n", string_buf);
    printf("\tmax sectors for r/w multi: %i\n", data[47] & 0xFF);

    printf("\tstandard timer values %ssupported, IORDY %ssupported, IORDY may %sbe disabled, LBA %ssupported, DMA %ssupported\n",
        (data[49] & (1 << 13)) ? "" : "not ",
        (data[49] & (1 << 11)) ? "" : "may be ",
        (data[49] & (1 << 10)) ? "" : "not ",
        (data[49] & (1 <<  9)) ? "" : "not ",
        (data[49] & (1 <<  8)) ? "" : "not "
    );

    // another cap bit in 50
    // 51-52 are obsolete timing modes

    bool w54_58_valid = (data[53] & (1 << 0));
    bool w64_70_valid = (data[53] & (1 << 1));
    bool w88_valid = (data[53] & (1 << 2));

    if(w54_58_valid)
    {
        printf("\t%i cur cylinders\n", data[54]);
        printf("\t%i cur heads\n", data[55]);
        printf("\t%i cur sectors per track\n", data[56]);
        printf("\t%i cur capacity in sectors\n", data[57] | data[58] << 16);
    }

    printf("\tcur num sectors for multi: %i\n", data[59]);
    printf("\t%i user addressable sectors\n", data[60] | data[61] << 16);

    // 62 is single word dma modes

    printf("\tsupported multiword DMA modes: ");
    if(data[63] & (1 << 2))
        printf("0-2\n");
    else if(data[63] & (1 << 1))
        printf("0-1\n");
    else if(data[63] & (1 << 0))
        printf("0\n");
    else
        printf("none\n");

    if(w64_70_valid)
    {
        printf("\tsupported PIO modes: ");
        if(data[64] & (1 << 1))
            printf("0-4\n");
        else if(data[64] & (1 << 0))
            printf("0-3\n");
        else
            printf("0-2\n");

        printf("\tmin multiword DMA cycle time: %ins\n", data[65]);
        printf("\trec multiword DMA cycle time: %ins\n", data[66]);

        printf("\tmin PIO cycle time (no IORDY): %ins\n", data[67]);
        printf("\tmin PIO cycle time (with IORDY): %ins\n", data[68]);
    }

    printf("\tqueue depth: %i\n", data[75] + 1);

    printf("\tmajor version: ATA-%i\n", 31 - __builtin_clz(data[80]));
    // 81 is minor version

    printf("\tsupported features:\n");
    if(data[82] & (1 <<  0))
        printf("\t\tSMART\n");
    if(data[82] & (1 <<  1))
        printf("\t\tsecurity mode\n");
    if(data[82] & (1 <<  2))
        printf("\t\tremovable media\n");
    if(data[82] & (1 <<  3))
        printf("\t\tpower management\n");
    if(data[82] & (1 <<  4))
        printf("\t\tpacket\n");
    if(data[82] & (1 <<  5))
        printf("\t\twrite cache\n");
    if(data[82] & (1 <<  6))
        printf("\t\tlook-ahead\n");
    if(data[82] & (1 <<  7))
        printf("\t\trelease interrupt\n");
    if(data[82] & (1 <<  8))
        printf("\t\tservice interrupt\n");
    if(data[82] & (1 <<  9))
        printf("\t\tdevice reset\n");
    if(data[82] & (1 << 10))
        printf("\t\thost protected area\n");
    if(data[82] & (1 << 12))
        printf("\t\twrite buffer\n");
    if(data[82] & (1 << 13))
        printf("\t\tread buffer\n");
    if(data[82] & (1 << 15))
        printf("\t\tNOP\n");

    if(data[83] & (1 <<  0))
        printf("\t\tdownload microcode\n");
    if(data[83] & (1 <<  1))
        printf("\t\tqueued DMA\n");
    if(data[83] & (1 <<  2))
        printf("\t\tCFA\n");
    if(data[83] & (1 <<  3))
        printf("\t\tadvanced power management\n");
    if(data[83] & (1 <<  4))
        printf("\t\tremovable media status\n");
    if(data[83] & (1 <<  5))
        printf("\t\tpower up in standby\n");
    if(data[83] & (1 <<  6))
        printf("\t\tSET FEATURES required for spin-up\n");
    //if(data[83] & (1 <<  7))
    //    printf("\t\t\n");
    if(data[83] & (1 <<  8))
        printf("\t\tset max security\n");
    if(data[83] & (1 <<  9))
        printf("\t\tautomatic acoustic management\n");
    if(data[83] & (1 << 10))
        printf("\t\t48-bit address\n");
    if(data[83] & (1 << 11))
        printf("\t\tdevice configuration overlay\n");
    if(data[83] & (1 << 12))
        printf("\t\tflush cache\n");
    if(data[83] & (1 << 13))
        printf("\t\tflush cache ext\n");

    if(data[84] & (1 <<  0))
        printf("\t\tSMART error logging\n");
    if(data[84] & (1 <<  1))
        printf("\t\tSMART self-test\n");
    if(data[84] & (1 <<  2))
        printf("\t\tmedia serial no\n");
    if(data[84] & (1 <<  3))
        printf("\t\tmedia card pass through\n");
    if(data[84] & (1 <<  5))
        printf("\t\tgeneral purpose logging\n");

    // 85-87 are enabled features

    if(w88_valid)
    {
        printf("\tsupported Ultra DMA modes: ");
        if(data[88] & (1 << 5))
            printf("0-5\n");
        else if(data[88] & (1 << 4))
            printf("0-4\n");
        else if(data[88] & (1 << 3))
            printf("0-3\n");
        else if(data[88] & (1 << 2))
            printf("0-2\n");
        else if(data[88] & (1 << 1))
            printf("0-1\n");
        else if(data[88] & (1 << 0))
            printf("0\n");
        else
            printf("none\n");
    }

    printf("\tsecurity erase time: ");
    if(data[89] == 0)
        printf("not specified\n");
    else if(data[89] == 255)
        printf("> 508 minutes\n");
    else
        printf("%i minutes\n", data[89] * 2);
    
    printf("\tenhanced security erase time: ");
    if(data[90] == 0)
        printf("not specified\n");
    else if(data[90] == 255)
        printf("> 508 minutes\n");
    else
        printf("%i minutes\n", data[90] * 2);
    
    // 91 is apm level
    // 92 is password revision

    if((data[93] >> 14) == 1)
    {
        // test results
    }

    // 94 is acoustic management

    // verify checksum if signature correct
    if((data[255] & 0xFF) == 0xA5)
    {
        uint8_t sum = 0;
        for(int i = 0; i < 256; i++)
            sum += (data[i] & 0xFF) + (data[i] >> 8);

        printf("checksum: %s\n", sum == 0 ? "good" : "bad");
    }

    printf("\nraw data:\n");
    for(int i = 0; i < 256; i++)
    {
        printf("%04X%c", data[i], i % 8 == 7 ? '\n' : ' ');
    }
    printf("\n");
}

int main()
{
    init_io();

    stdio_init_all();

    printf("starting...\n");

    auto start = get_absolute_time();
    do_reset();
    auto end = get_absolute_time();

    auto reset_time = absolute_time_diff_us(start, end);
    printf("Device reset done in %llius\n", reset_time);

    // make sure we're ready
    while(!check_ready());
        
    printf("ready\n");

    // identify
    write_register(ATAReg::Device, 0 << 4); // device id 0
    write_command(ATACommand::IDENTIFY_DEVICE);

    uint16_t data[256];
    do_pio_read(data, 256);

    print_identify_result(data);
   
    // okay, lets try to read the MBR
    read_sectors(0, 0, 1, data);

    // check boot signature
    if(data[255] == 0xAA55)
    {
        auto byteData = reinterpret_cast<uint8_t *>(data);
        for(int i = 0; i < 4; i++)
        {
            int offset = 0x1BE + i * 16;

            bool active = byteData[offset + 0] & 0x80;
            int startHead = byteData[offset + 1];
            int startSector = byteData[offset + 2] & 0x3F;
            int startCylinder = byteData[offset + 3] | (byteData[offset + 2] & 0xC0) << 2;
            int type = byteData[offset + 4];
            int endHead = byteData[offset + 5];
            int endSector = byteData[offset + 6] & 0x3F;
            int endCylinder = byteData[offset + 7] | (byteData[offset + 6] & 0xC0) << 2;

            uint32_t lbaStart = byteData[offset + 8] | byteData[offset + 9] << 8 | byteData[offset + 10] << 16 | byteData[offset + 11] << 24;
            uint32_t numSectors = byteData[offset + 12] | byteData[offset + 13] << 8 | byteData[offset + 14] << 16 | byteData[offset + 15] << 24;

            if(!type)
                continue; // skip empty

            printf("partition %i type %02X active %i CHS %4i %3i %2i - %4i %3i %2i LBA %lu count %lu\n",
                i, type, active, startCylinder, startHead, startSector, endCylinder, endHead, endSector, lbaStart, numSectors
            );
        }
    }

    while(true)
    {
       
    }

    return 0;
}
