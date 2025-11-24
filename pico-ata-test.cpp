#include <cstdio>
#include <cmath>
#include <random>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "tusb.h"

#include "ata.hpp"
#include "atapi.hpp"
#include "identity.hpp"

static void print_identify_result(uint16_t data[256])
{
    ata::IdentityParser parser(data);

    char string_buf[41];

    bool is_atapi = parser.is_atapi();

    printf("IDENTIFY DEVICE:\n");

    if(is_atapi)
    {
        int periph_type = (data[0] >> 8) & 0x1F;
        int drq_time = parser.drq_response_time();

        static const char *drq_times[]{"3ms", "INTRQ", "50us", ""};

        printf("\ttype %x, DRQ %s, %sremovable, %scomplete response, packet size %i\n",
            periph_type, drq_times[drq_time], parser.is_removable() ? "" : "non-", (data[0] & (1 << 2)) ? "in" : "", parser.command_packet_size()
        );
    }
    else
    {
        printf("\t%sremovable, %scomplete response\n", parser.is_removable() ? "" : "non-", parser.is_response_incomplete() ? "in" : "");
        printf("\t%i cylinders\n", parser.num_cylinders());
    }

    switch(parser.specific_configuration())
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
        printf("\t%i heads\n", parser.num_heads());
        printf("\t%i bytes per track\n", parser.num_bytes_per_track());
        printf("\t%i bytes per sector\n", parser.num_bytes_per_sector());
        printf("\t%i sectors per track\n", parser.num_sectors_per_track());
        // 7-8 are for CF
    }

    parser.serial_number(string_buf);
    printf("\tserial no: \"%s\"\n", string_buf);

    // 20-21 are buffer related

    parser.firmware_revision(string_buf);
    printf("\tfirmware rev: \"%s\"\n", string_buf);
    parser.model_number(string_buf);
    printf("\tmodel no: \"%s\"\n", string_buf);

    if(!is_atapi)
        printf("\tmax sectors for r/w multi: %i\n", parser.max_read_write_multiple());

    if(is_atapi)
    {
        bool int_dma = parser.interleaved_dma_supported();
        bool queueing = parser.command_queueing_supported();
        bool overlap = parser.overlapped_operation_supported();
        bool iordy = parser.iordy_supported();
        bool iordy_dis = parser.can_disable_iordy();
        bool lba = parser.lba_supported();
        bool dma = parser.dma_supported();
        printf("\tinterleaved dma %ssupported, command queueing %ssupported, command overlap %ssupported, IORDY %ssupported, IORDY may %sbe disabled, LBA %ssupported, DMA %ssupported\n",
            int_dma   ? "" : "not ",
            queueing  ? "" : "not ",
            overlap   ? "" : "not ",
            iordy     ? "" : "may be ",
            iordy_dis ? "" : "not ",
            lba       ? "" : "not ",
            dma       ? "" : "not "
        );
    }
    else
    {
        bool std_timer = parser.standard_standby_timer_values_supported();
        bool iordy = parser.iordy_supported();
        bool iordy_dis = parser.can_disable_iordy();
        bool lba = parser.lba_supported();
        bool dma = parser.dma_supported();
        printf("\tstandard timer values %ssupported, IORDY %ssupported, IORDY may %sbe disabled, LBA %ssupported, DMA %ssupported\n",
            std_timer ? "" : "not ",
            iordy     ? "" : "may be ",
            iordy_dis ? "" : "not ",
            lba       ? "" : "not ",
            dma       ? "" : "not "
        );
    }

    // another cap bit in 50
    // 51-52 are obsolete timing modes

    if(!is_atapi)
    {
        if(parser.current_params_valid())
        {
            printf("\t%i cur cylinders\n", parser.num_current_cylinders());
            printf("\t%i cur heads\n", parser.num_current_heads());
            printf("\t%i cur sectors per track\n", parser.num_current_sectors_per_track());
            printf("\t%lu cur capacity in sectors\n", parser.current_capacity_sectors());
        }

        if(parser.rw_multiple_sector_setting_valid())
            printf("\tcur num sectors for multi: %i\n", parser.current_read_write_multiple());
        printf("\t%lu user addressable sectors\n", parser.total_user_addressable_sectors());

        // 62 is single word dma modes
    }

    printf("\tsupported multiword DMA modes: ");
    auto dma_modes = parser.multiword_dma_modes_supported();
    if(dma_modes & (1 << 2))
        printf("0-2\n");
    else if(dma_modes & (1 << 1))
        printf("0-1\n");
    else if(dma_modes & (1 << 0))
        printf("0\n");
    else
        printf("none\n");

    if(parser.timing_params_valid())
    {
        printf("\tsupported PIO modes: ");
        auto adv_pio_modes = parser.advanced_pio_modes_supported();
        if(adv_pio_modes & (1 << 1))
            printf("0-4\n");
        else if(adv_pio_modes & (1 << 0))
            printf("0-3\n");
        else
            printf("0-2\n");

        printf("\tmin multiword DMA cycle time: %ins\n", parser.min_mw_dma_cycle_time());
        printf("\trec multiword DMA cycle time: %ins\n", parser.rec_mw_dma_cycle_time());

        printf("\tmin PIO cycle time (no IORDY): %ins\n", parser.min_pio_cycle_time());
        printf("\tmin PIO cycle time (with IORDY): %ins\n", parser.min_pio_cycle_time_iordy());
    }

    // 71 ATAPI PACKET bus release time
    // 72 ATAPI SERVICE bus release time

    printf("\tqueue depth: %i\n", parser.queue_depth());

    // 76-79 are for SATA

    printf("\tmajor version: ATA-%i\n", parser.major_version());
    // 81 is minor version

    if(parser.command_set_notification_supported())
    {
        printf("\tsupported features:\n");
        if(parser.smart_supported())
            printf("\t\tSMART\n");
        if(parser.security_supported())
            printf("\t\tsecurity mode\n");
        if(parser.removable_supported())
            printf("\t\tremovable media\n");
        if(parser.power_management_supported())
            printf("\t\tpower management\n");
        if(parser.packet_supported())
            printf("\t\tpacket\n");
        if(parser.write_cache_supported())
            printf("\t\twrite cache\n");
        if(parser.look_ahead_supported())
            printf("\t\tlook-ahead\n");
        if(parser.release_interrupt_supported())
            printf("\t\trelease interrupt\n");
        if(parser.service_interrupt_supported())
            printf("\t\tservice interrupt\n");
        if(parser.device_reset_supported())
            printf("\t\tdevice reset\n");
        if(parser.host_protected_area_supported())
            printf("\t\thost protected area\n");
        if(parser.write_buffer_supported())
            printf("\t\twrite buffer\n");
        if(parser.read_buffer_supported())
            printf("\t\tread buffer\n");
        if(parser.nop_supported())
            printf("\t\tNOP\n");

        if(parser.download_microcode_supported())
            printf("\t\tdownload microcode\n");
        if(parser.rw_dma_queued_supported())
            printf("\t\tqueued DMA\n");
        if(parser.cfa_supported())
            printf("\t\tCFA\n");
        if(parser.apm_supported())
            printf("\t\tadvanced power management\n");
        if(parser.removable_media_status_supported())
            printf("\t\tremovable media status\n");
        if(parser.power_up_in_standby_supported())
            printf("\t\tpower up in standby\n");
        if(parser.set_features_required_to_spin_up())
            printf("\t\tSET FEATURES required for spin-up\n");
        //if(data[83] & (1 <<  7))
        //    printf("\t\t\n");
        if(parser.set_max_security_supported())
            printf("\t\tset max security\n");
        if(parser.auto_acoustic_management_supported())
            printf("\t\tautomatic acoustic management\n");
        if(parser.address_48bit_supported())
            printf("\t\t48-bit address\n");
        if(parser.device_config_overlay_supported())
            printf("\t\tdevice configuration overlay\n");
        if(parser.flush_cache_supported())
            printf("\t\tflush cache\n");
        if(parser.flush_cache_ext_supported())
            printf("\t\tflush cache ext\n");

        if(parser.smart_error_logging_supported())
            printf("\t\tSMART error logging\n");
        if(parser.smart_self_test_supported())
            printf("\t\tSMART self-test\n");
        if(parser.media_serial_number_supported())
            printf("\t\tmedia serial no\n");
        if(parser.media_card_pass_through_supported())
            printf("\t\tmedia card pass through\n");
        if(parser.general_purpose_logging_supported())
            printf("\t\tgeneral purpose logging\n");
        if(data[84] & (1 <<  6))
            printf("\t\tWRITE DMA/MULTIPLE FUA EXT\n");
        if(data[84] & (1 <<  7))
            printf("\t\tWRITE DMA QUEUED FUA EXT\n");
        if(data[84] & (1 <<  8))
            printf("\t\t64-bit world wide name\n");
        if(data[84] & (1 << 13))
            printf("\t\tIDLE IMMEDIATE with UNLOAD\n");
    }

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

    if(parser.ultra_dma_modes_valid())
    {
        auto udma_modes = parser.ultra_dma_modes_supported();
        printf("\tsupported Ultra DMA modes: ");
        if(udma_modes & (1 << 6))
            printf("0-6\n");
        else if(udma_modes & (1 << 5))
            printf("0-5\n");
        else if(udma_modes & (1 << 4))
            printf("0-4\n");
        else if(udma_modes & (1 << 3))
            printf("0-3\n");
        else if(udma_modes & (1 << 2))
            printf("0-2\n");
        else if(udma_modes & (1 << 1))
            printf("0-1\n");
        else if(udma_modes & (1 << 0))
            printf("0\n");
        else
            printf("none\n");
    }

    printf("\tsecurity erase time: ");
    auto erase_time = parser.security_erase_time();
    if(erase_time == 0)
        printf("not specified\n");
    else if(erase_time == 255)
        printf("> 508 minutes\n");
    else
        printf("%i minutes\n", erase_time * 2);
    
    printf("\tenhanced security erase time: ");
    erase_time = parser.enhanced_security_erase_time();
    if(erase_time == 0)
        printf("not specified\n");
    else if(erase_time == 255)
        printf("> 508 minutes\n");
    else
        printf("%i minutes\n", erase_time * 2);
    
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
    if(parser.checksum_valid())
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

static void test_ata(int device)
{
    using namespace ata;

    // make sure we're ready
    auto timeout = make_timeout_time_ms(10000);
    while(!check_ready())
    {
        if(time_reached(timeout))
        {
            printf("timeout waiting for ready on device %i\n", device);
            return;
        }
    }
        
    printf("ready\n");

    // identify
    uint16_t data[256];
    ata::identify_device(device, data);
    print_identify_result(data);

    IdentityParser parser(data);

    // set PIO mode if >= 3
    if(parser.timing_params_valid() && parser.advanced_pio_modes_supported())
    {
        int mode = (parser.advanced_pio_modes_supported() & (1 << 1)) ? 4 : 3;
        ata::set_features(device, ata::ATAFeature::SetTransferMode, 1 << 3/*PIO flow control mode*/ | mode);
    }

    // reconfigure for speed
    int min_cycle_time = 600;
    if(parser.timing_params_valid())
        min_cycle_time = parser.min_pio_cycle_time_iordy();

    // we're wrong for reg access in modes 1-2 (330-383ns cycle times)
    // (and the mode 2 cycle time for reg access is different...)
    // let's just hope nobody connects a drive that slow

    printf("adjusting for %ins cycle time\n", min_cycle_time);

    ata::adjust_for_min_cycle_time(min_cycle_time);
   
    // okay, lets try to read the MBR
    ata::read_sectors(device, 0, 1, data);

    if(((uint8_t *)data)[0x1BE + 4] == 0xEE)
    {
        printf("protective MBR, probably GPT...\n");
        ata::read_sectors(device, 1, 1, data);
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
        ata::read_sectors(device, sector, 256, buf);
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

    printf("\n");
}

static void test_atapi(int device)
{
    using namespace ata;

    // don't need to wait for ready

    // identify
    uint16_t data[1024];
    ata::identify_device(device, data, ATACommand::IDENTIFY_PACKET_DEVICE);

    print_identify_result(data);

    // now we should be ready
    while(!check_ready());
        
    printf("ready\n");

    // INQUIRY packet
    auto data8 = (uint8_t *)data;
    atapi::inquiry(device, data8);

    printf("INQUIRY:\n");
    
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
        if(!atapi::test_unit_ready(device))
        {
            auto sense_key = atapi::get_sense_key();

            printf("test unit ready sense key %X\n", int(sense_key));
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

    do
    {
        atapi::read(device, lba, 1, data8);

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

    for(int i = 0; i < 2; i++)
    {
        bool is_atapi = false;
        
        // attempt a DEVICE RESET to set the signature
        if(ata::device_reset(i))
        {
            // check signature
            uint8_t lba_mid = ata::read_register(ata::ATAReg::LBAMid);
            uint8_t lba_high = ata::read_register(ata::ATAReg::LBAHigh);
            is_atapi = lba_mid == 0x14 && lba_high == 0xEB;
        }

        if(is_atapi)
        {
            printf("Device %i is ATAPI\n", i);
            test_atapi(i);
        }
        else
        {
            printf("Device %i is not ATAPI\n", i);
            test_ata(i);
        }
    }

    while(true)
    {
       
    }

    return 0;
}
