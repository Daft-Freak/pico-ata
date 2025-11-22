#include <cstdio>
#include <cmath>
#include <random>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "tusb.h"

#include "ata.hpp"
#include "atapi.hpp"

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

    bool is_atapi = (data[0] >> 14) == 2;

    printf("IDENTIFY DEVICE:\n");

    if(is_atapi)
    {
        int periph_type = (data[0] >> 8) & 0x1F;
        int drq_time = (data[0] >> 5) & 3;
        int packet_size = data[0] & 3;

        static const char *drq_times[]{"3ms", "INTRQ", "50us", ""};
        static const int packet_sizes[]{12, 16, 0, 0};

        printf("\ttype %x, DRQ %s, %sremovable, %scomplete response, packet size %i\n",
            periph_type, drq_times[drq_time], (data[0] & (1 << 7)) ? "" : "non-", (data[0] & (1 << 2)) ? "in" : "", packet_sizes[packet_size]
        );
    }
    else
    {
        printf("\t%sremovable, %scomplete response\n", (data[0] & (1 << 7)) ? "" : "non-", (data[0] & (1 << 2)) ? "in" : "");
        printf("\t%i cylinders\n", data[1]);
    }

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

    if(!is_atapi)
    {
        printf("\t%i heads\n", data[3]);
        printf("\t%i bytes per track\n", data[4]);
        printf("\t%i bytes per sector\n", data[5]);
        printf("\t%i sectors per track\n", data[6]);
        // 7-8 are for CF
    }

    get_string(data + 10, string_buf, 20);
    printf("\tserial no: \"%s\"\n", string_buf);

    // 20-21 are buffer related

    get_string(data + 23, string_buf, 8);
    printf("\tfirmware rev: \"%s\"\n", string_buf);
    get_string(data + 27, string_buf, 40);
    printf("\tmodel no: \"%s\"\n", string_buf);

    if(!is_atapi)
        printf("\tmax sectors for r/w multi: %i\n", data[47] & 0xFF);

    if(is_atapi)
    {
        printf("\tinterleaved dma %ssupported, command queueing %ssupported, command overlap %ssupported, IORDY %ssupported, IORDY may %sbe disabled, LBA %ssupported, DMA %ssupported\n",
            (data[49] & (1 << 15)) ? "" : "not ",
            (data[49] & (1 << 14)) ? "" : "not ",
            (data[49] & (1 << 13)) ? "" : "not ",
            (data[49] & (1 << 11)) ? "" : "may be ",
            (data[49] & (1 << 10)) ? "" : "not ",
            (data[49] & (1 <<  9)) ? "" : "not ",
            (data[49] & (1 <<  8)) ? "" : "not "
        );
    }
    else
    {
        printf("\tstandard timer values %ssupported, IORDY %ssupported, IORDY may %sbe disabled, LBA %ssupported, DMA %ssupported\n",
            (data[49] & (1 << 13)) ? "" : "not ",
            (data[49] & (1 << 11)) ? "" : "may be ",
            (data[49] & (1 << 10)) ? "" : "not ",
            (data[49] & (1 <<  9)) ? "" : "not ",
            (data[49] & (1 <<  8)) ? "" : "not "
        );
    }

    // another cap bit in 50
    // 51-52 are obsolete timing modes

    bool w54_58_valid = (data[53] & (1 << 0));
    bool w64_70_valid = (data[53] & (1 << 1));
    bool w88_valid = (data[53] & (1 << 2));

    if(!is_atapi)
    {
        if(w54_58_valid)
        {
            printf("\t%i cur cylinders\n", data[54]);
            printf("\t%i cur heads\n", data[55]);
            printf("\t%i cur sectors per track\n", data[56]);
            printf("\t%i cur capacity in sectors\n", data[57] | data[58] << 16);
        }

        printf("\tcur num sectors for multi: %i\n", data[59] & 0xFF);
        printf("\t%i user addressable sectors\n", data[60] | data[61] << 16);

        // 62 is single word dma modes
    }

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

    // 71 ATAPI PACKET bus release time
    // 72 ATAPI SERVICE bus release time

    printf("\tqueue depth: %i\n", data[75] + 1);

    // 76-79 are for SATA

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
    if(data[82] & (1 << 14))
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
    if(data[84] & (1 <<  6))
        printf("\t\tWRITE DMA/MULTIPLE FUA EXT\n");
    if(data[84] & (1 <<  7))
        printf("\t\tWRITE DMA QUEUED FUA EXT\n");
    if(data[84] & (1 <<  8))
        printf("\t\t64-bit world wide name\n");
    if(data[84] & (1 << 13))
        printf("\t\tIDLE IMMEDIATE with UNLOAD\n");

    if((data[119] >> 14) == 1)
    {
        if(data[119] & (1 <<  1))
            printf("\t\twrite-read-verify\n");
        if(data[119] & (1 <<  2))
            printf("\t\tWRITE UNCORRECTABLE EXT\n");
        if(data[119] & (1 <<  3))
            printf("\t\tREAD/WRITE LOG DMA EXT\n");
        if(data[119] & (1 <<  4))
            printf("\t\tDOWNLOAD MICROCODE offset transfer\n");
        if(data[119] & (1 <<  5))
            printf("\t\tfree-fall control\n");
    }

    // 85-87 are enabled features

    if(w88_valid)
    {
        printf("\tsupported Ultra DMA modes: ");
        if(data[88] & (1 << 6))
            printf("0-6\n");
        else if(data[88] & (1 << 5))
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

    // 95-99 are for streaming

    if((data[106] >> 14) == 1)
    {
        if(data[106] & (1 << 13))
            printf("\t%i logical sectors per physical sector", 1 << (data[106] & 0xF));
        else
            printf("\t1 logical sector per physical sector");

        printf(", logical sector is%s longer than 256 words\n", data[106] & (1 << 12) ? "" : " not");

        // 117-118 are logical sector size (valid if bit 12)

        // 209 is sector alignment (valid if bit 13)
    }

    // 107 is for acoustic testing

    // 108-111 are world wide name

    // 119-120 are more features

    // 125 ATAPI byte count=0

    // 214-216, 219 are for cache

    // 217 is rotation rate

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

static void print_mbr(uint16_t data[256])
{
    // check boot signature
    if(data[255] == 0xAA55)
    {
        auto byte_data = reinterpret_cast<uint8_t *>(data);
        for(int i = 0; i < 4; i++)
        {
            int offset = 0x1BE + i * 16;

            bool active = byte_data[offset + 0] & 0x80;
            int start_head = byte_data[offset + 1];
            int start_sector = byte_data[offset + 2] & 0x3F;
            int start_cylinder = byte_data[offset + 3] | (byte_data[offset + 2] & 0xC0) << 2;
            int type = byte_data[offset + 4];
            int end_head = byte_data[offset + 5];
            int end_sector = byte_data[offset + 6] & 0x3F;
            int end_cylinder = byte_data[offset + 7] | (byte_data[offset + 6] & 0xC0) << 2;

            uint32_t lba_start = byte_data[offset + 8] | byte_data[offset + 9] << 8 | byte_data[offset + 10] << 16 | byte_data[offset + 11] << 24;
            uint32_t num_sectors = byte_data[offset + 12] | byte_data[offset + 13] << 8 | byte_data[offset + 14] << 16 | byte_data[offset + 15] << 24;

            if(!type)
                continue; // skip empty

            printf("partition %i type %02X active %i CHS %4i %3i %2i - %4i %3i %2i LBA %lu count %lu\n",
                i, type, active, start_cylinder, start_head, start_sector, end_cylinder, end_head, end_sector, lba_start, num_sectors
            );

            // check for extended partition
            auto ext_type = type;
            uint32_t ext_lba_start = 0;
            int ext_index = 0;
            while(ext_type == 0x5 || ext_type == 0xF || ext_type == 0x85)
            {
                // technically some of these should be using CHS addressing...
                uint16_t ebr_data[256];
                ata::read_sectors(0, lba_start + ext_lba_start, 1, ebr_data);

                if(ebr_data[255] == 0xAA55)
                {
                    auto ebr_byte_data = reinterpret_cast<uint8_t *>(ebr_data);
                    offset = 0x1BE;

                    bool active = ebr_byte_data[offset + 0] & 0x80;
                    int start_head = ebr_byte_data[offset + 1];
                    int start_sector = ebr_byte_data[offset + 2] & 0x3F;
                    int start_cylinder = ebr_byte_data[offset + 3] | (ebr_byte_data[offset + 2] & 0xC0) << 2;
                    int type = ebr_byte_data[offset + 4];
                    int end_head = ebr_byte_data[offset + 5];
                    int end_sector = ebr_byte_data[offset + 6] & 0x3F;
                    int end_cylinder = ebr_byte_data[offset + 7] | (ebr_byte_data[offset + 6] & 0xC0) << 2;

                    uint32_t lba_start = ebr_byte_data[offset + 8] | ebr_byte_data[offset + 9] << 8 | ebr_byte_data[offset + 10] << 16 | ebr_byte_data[offset + 11] << 24;
                    uint32_t num_sectors = ebr_byte_data[offset + 12] | ebr_byte_data[offset + 13] << 8 | ebr_byte_data[offset + 14] << 16 | ebr_byte_data[offset + 15] << 24;

                    printf(" extended %i type %02X active %i CHS %4i %3i %2i - %4i %3i %2i LBA %lu count %lu\n",
                        ext_index, type, active, start_cylinder, start_head, start_sector, end_cylinder, end_head, end_sector, lba_start, num_sectors
                    );

                    // setup for next extended partition
                    ext_type = ebr_byte_data[0x1CE + 4];
                    ext_lba_start = ebr_byte_data[0x1CE + 8] | ebr_byte_data[0x1CE + 9] << 8 | ebr_byte_data[0x1CE + 10] << 16 | ebr_byte_data[0x1CE + 11] << 24;
                    ext_index++;
                }
                else
                    ext_type = 0;
            }
        }
    }
}

static void print_gpt(uint16_t data[256])
{
    auto print_guid = [](uint8_t *guid)
    {
        printf("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            guid[3], guid[2], guid[1], guid[0],
            guid[5], guid[4],
            guid[7], guid[6],
            guid[8], guid[9],
            guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]
        );
    };

    uint64_t array_start_lba = data[36] | data[37] << 16 | uint64_t(data[38]) << 32 | uint64_t(data[39]) << 48;
    uint32_t num_partitions = data[40] | data[41] << 16;
    uint32_t partition_entry_size = data[42] | data[43] << 16;
    printf("GPT id ");
    print_guid((uint8_t *)&data[28]);
    printf(" %lu partitions, size %lu\n", num_partitions, partition_entry_size);

    const int sector_size = 512; // assuming
    uint8_t sector_buf[sector_size];

    for(uint32_t partition = 0; partition < num_partitions; partition++)
    {
        auto byte_offset = partition * partition_entry_size;

        if(byte_offset % sector_size == 0)
            ata::read_sectors(0, array_start_lba + byte_offset / sector_size, 1, (uint16_t *)sector_buf);

        auto part_data = sector_buf + (byte_offset % sector_size);

        // ignore zeroed partition type (unused)
        bool is_zero = true;

        for(int i = 0; i < 16; i++)
        {
            if(part_data[i])
                is_zero = false;
        }

        if(is_zero)
            continue;

        printf("\ttype ");
        print_guid(part_data);
        printf(" id ");
        print_guid(part_data + 16);

        auto start = *(uint64_t *)(part_data + 32);
        auto end = *(uint64_t *)(part_data + 40);
        auto attribs = *(uint64_t *)(part_data + 48);
        // UTF-16 name at 56

        printf(" LBA %llu - %llu attribs %llx\n", start, end, attribs);
    }
}

static void print_iso9660_volume_descriptor(uint8_t data[2048])
{
    if(memcmp(data + 1, "CD001", 5) != 0)
        return;
    
    printf("volume descriptor type %i\n", data[0]);

    // primary volume descriptor
    if(data[0] == 1)
    {
        printf("\tsystem identifier: \"%.32s\"\n", data + 8);
        printf("\tvolume identifier: \"%.32s\"\n", data + 40);

        uint32_t val32 = data[80] | data[81] << 8 | data[82] << 16 | data[83] << 24;
        printf("\tvolume space size: %lu\n", val32);
        
        uint16_t val16 = data[120] | data[121] << 8;
        printf("\tvolume set size: %u\n", val16);
        val16 = data[124] | data[125] << 8;
        printf("\tvolume sequence number: %u\n", val16);
        val16 = data[128] | data[129] << 8;
        printf("\tlogical block size: %u\n", val16);

        val32 = data[132] | data[133] << 8 | data[134] << 16 | data[135] << 24;
        printf("\tpath table size: %lu\n", val32);

        val32 = data[140] | data[141] << 8 | data[142] << 16 | data[143] << 24;
        printf("\tpath table lba: %lu (le)", val32);
        val32 = data[151] | data[150] << 8 | data[149] << 16 | data[148] << 24;
        printf(" / %lu (be)\n", val32);

        val32 = data[144] | data[145] << 8 | data[146] << 16 | data[147] << 24;
        printf("\topt path table lba: %lu (le)", val32);
        val32 = data[155] | data[154] << 8 | data[153] << 16 | data[152] << 24;
        printf(" / %lu (be)\n", val32);

        // root dir...

        printf("\tvolume set identifier   : \"%.128s\"\n", data + 190);
        printf("\tpublisher identifier    : \"%.128s\"\n", data + 318);
        printf("\tdata preparer identifier: \"%.128s\"\n", data + 446);
        printf("\tapplication identifier  : \"%.128s\"\n", data + 574);

        printf("\tcopyright file identifier    : \"%.37s\"\n", data + 702);
        printf("\tabstract file identifier     : \"%.37s\"\n", data + 739);
        printf("\tbibliographic file identifier: \"%.37s\"\n", data + 776);

        printf("\tvolume creation date    : \"%.16s\" %i\n", data + 813, int8_t(data[829]));
        printf("\tvolume modification date: \"%.16s\" %i\n", data + 830, int8_t(data[846]));
        printf("\tvolume expiration date  : \"%.16s\" %i\n", data + 847, int8_t(data[863]));
        printf("\tvolume effective date   : \"%.16s\" %i\n", data + 864, int8_t(data[880]));
    }
}

// for benchmark
static uint16_t buf[256 * 512 / 2];

static void test_ata()
{
    using namespace ata;

    // make sure we're ready
    while(!check_ready());
        
    printf("ready\n");

    // identify
    uint16_t data[256];
    ata::identify_device(0, data);
    print_identify_result(data);

    // set PIO mode if >= 3
    if((data[53] & (1 << 1)) && (data[64] & (1 << 0)))
    {
        while(!check_ready());

        int mode = (data[64] & (1 << 1)) ? 4 : 3;

        write_register(ATAReg::Features, 3);
        write_register(ATAReg::SectorCount, 1 << 3/*PIO flow control mode*/ | mode);
        write_register(ATAReg::Device, 0 << 4); // device id 0
        write_command(ATACommand::SET_FEATURES);
    }

    // reconfigure for speed
    int min_cycle_time = 600;
    if(data[53] & (1 << 1))
        min_cycle_time = data[68];

    // we're wrong for reg access in modes 1-2 (330-383ns cycle times)
    // (and the mode 2 cycle time for reg access is different...)
    // let's just hope nobody connects a drive that slow

    printf("adjusting for %ins cycle time\n", min_cycle_time);

    ata::adjust_for_min_cycle_time(min_cycle_time);
   
    // okay, lets try to read the MBR
    ata::read_sectors(0, 0, 1, data);

    if(((uint8_t *)data)[0x1BE + 4] == 0xEE)
    {
        printf("protective MBR, probably GPT...\n");
        ata::read_sectors(0, 1, 1, data);
        if(memcmp(data, "EFI PART", 8) == 0)
            print_gpt(data);
    }
    else
        print_mbr(data);

    // little benchmark
   
    auto format_speed = [](int num_sectors, int64_t time_us, const char *&unit)
    {
        float speed = float(num_sectors * 512) / (float(time_us) / 1000000.0f);
        unit = "B";

        if(speed >= 1000.0f)
        {
            speed /= 1000.0f;
            unit = "kB";
        }

        if(speed >= 1000.0f)
        {
            speed /= 1000.0f;
            unit = "MB";
        }

        return speed;
    };

    auto start = get_absolute_time();
    int count = 0;
    for(int sector = 0; sector < 10 * 1024 * 1024 / 512; sector += 256, count++)
    {
        printf(".");
        ata::read_sectors(0, sector, 256, buf);
    }
    auto end = get_absolute_time();

    auto time_us = absolute_time_diff_us(start, end);
    const char *unit;
    float speed = format_speed(256 * count, time_us, unit);
    
    printf("\nread %ix256 sectors in %llius %3.3f%s/s", count, time_us, speed, unit);

    // random reads
    std::mt19937 gen;
    std::uniform_int_distribution<> distrib(0, 1000000);

    start = get_absolute_time();
    count = 1000;
    for(int i = 0; i < count; i++)
    {
        int lba = distrib(gen);

        ata::read_sectors(0, lba, 1, buf);
    }
    end = get_absolute_time();

    time_us = absolute_time_diff_us(start, end);
    speed = format_speed(count * 1, time_us, unit);
    
    printf("\nread %ix1 random sectors in %llius %3.3f%s/s", count, time_us, speed, unit);
}

static void test_atapi()
{
    using namespace ata;

    // don't need to wait for ready

    // identify
    uint16_t data[1024];
    ata::identify_device(0, data, ATACommand::IDENTIFY_PACKET_DEVICE);

    print_identify_result(data);

    // now we should be ready
    while(!check_ready());
        
    printf("ready\n");

    // INQUIRY packet
    int data_len = 36; // min

    // assuming 12-byte
    uint8_t command[12]{};
    command[0] = int(SCSICommand::INQUIRY);
    command[1] = 0;
    command[2] = 0; // page
    command[3] = data_len >> 8;
    command[4] = data_len & 0xFF;
    command[5] = 0; // control
    atapi::do_command(0, data_len, command);

    // now the response
    do_pio_read(data, data_len / 2);

    printf("INQUIRY:\n");
    auto data8 = (uint8_t *)data;
    int qualifier = data8[0] >> 5;
    int type = data[0] & 0x1F;
    printf("\tqualifier %i, type %x\n", qualifier, type);
    printf("\tremovable? %s\n", (data8[1] & 0x80) ? "yes" : "no");
    // some other bits...
    printf("\tvendor: \"%.8s\"\n", data8 + 8);
    printf("\tproduct: \"%.16s\"\n", data8 + 16);
    printf("\tversion: \"%.4s\"\n", data8 + 32);

    // test ready
    bool ready = false;
    for(int i = 0; i < 20; i++)
    {
        command[0] = int(SCSICommand::TEST_UNIT_READY);
        command[1] = command[2] = command[3] = command[4] = 0;
        command[5] = 0; // control
        atapi::do_command(0, 0, command);

        while(read_register(ATAReg::Status) & Status_BSY);
        uint8_t status = read_register(ATAReg::Status);

        if(status & Status_ERR) // for ATAPI, this is "check condition"
        {
            uint8_t err = read_register(ATAReg::Error);

            printf("test unit ready sense key %X\n", err >> 4);
        }
        else
        {
            printf("test unit ready okay\n");
            ready = true;
            break;
        }

        sleep_ms(500);
    }

    if(!ready)
        return;

    // attempt some reading
    uint32_t lba = 16;
    data_len = 2048;

    do
    {

        command[0] = int(SCSICommand::READ_10);
        command[1] = 0; // FUA, DPO, RDPROTECT...
        command[2] = lba >> 24;
        command[3] = lba >> 16;
        command[4] = lba >> 8;
        command[5] = lba & 0xFF;
        command[6] = 0; // group number
        command[7] = 0; // len high
        command[8] = 1; // len low
        command[9] = 0; // control
        atapi::do_command(0, data_len, command);

        do_pio_read(data, data_len / 2);

        print_iso9660_volume_descriptor(data8);
        lba++;
    } while(data8[0] != 255);
}

int main()
{
    ata::init_io();

    stdio_init_all();

    printf("starting...\n");

    auto start = get_absolute_time();
    ata::do_reset();
    auto end = get_absolute_time();

    auto reset_time = absolute_time_diff_us(start, end);
    printf("Device reset done in %llius\n", reset_time);
    // check signature
    uint8_t lba_mid = ata::read_register(ata::ATAReg::LBAMid);
    uint8_t lba_high = ata::read_register(ata::ATAReg::LBAHigh);
    bool is_atapi = lba_mid == 0x14 && lba_high == 0xEB;

    if(is_atapi)
    {
        printf("ATAPI device detected\n");
        test_atapi();
    }
    else
        test_ata();

    while(true)
    {
       
    }

    return 0;
}
