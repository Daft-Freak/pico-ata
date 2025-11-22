#pragma once
#include <cstdint>

namespace ata
{
    // IDENTIFY (PACKET) DEVICE parsing
    // mostly complete up to ATA-6

    class IdentityParser final
    {
    public:
        IdentityParser(const uint16_t *data) : data(data) {}

        bool is_atapi() const {return (data[0] >> 14) == 2;}

        // ATA-5 (soft sectored in ATA-1)
        bool is_response_incomplete() const {return data[0] & (1 << 2);}

        bool is_removable() const {return data[0] & (1 << 7);}

        // "obsolete" from ATA-6
        uint16_t num_cylinders() const {return data[1];}
        uint16_t num_heads() const {return data[3];}
        uint16_t num_sectors_per_track() const {return data[6];}

        // ATA-5
        uint16_t specific_configuration() const {return data[2];}

        // "obsolete" from ATA-2
        uint16_t num_bytes_per_track() const {return data[4];}
        uint16_t num_bytes_per_sector() const {return data[5];}

        // 7-9 are vendor-specific until ATA-4 (retired)
        // 7-8 are reserved for CF from ATA-5

        // 20 chars
        void serial_number(char *out) const {get_string(10, out, 20);}
        // 8 chars
        void firmware_revision(char *out) const {get_string(23, out, 8);}
        // 40 chars
        void model_number(char *out) const {get_string(27, out, 40);}

        // 20-21 are buffer-related in ATA-1
        // 22 is vendor specific bytes for READ/WRITE long (obsolete in ATA-4)

        uint8_t max_read_write_multiple() const {return data[47] & 0xFF;}

        // 48 is reserved (can perform doubleword IO in ATA-1)

        // capabilities
        // ATA-1 (obsolete in ATA-3, restored for ATAPI-4)
        bool dma_supported() const {return data[49] & (1 << 8);}
        bool lba_supported() const {return data[49] & (1 << 9);}
        // ATA-2
        bool can_disable_iordy() const {return data[49] & (1 << 10);}
        bool iordy_supported() const {return data[49] & (1 << 11);}
        bool standard_standby_timer_values_supported() const {return data[49] & (1 << 13);}
        // bits 12 is reserved for ATAPI, but also obsolete?

        // ATAPI-4
        bool overlapped_operation_supported() const {return data[49] & (1 << 13);}
        bool command_queueing_supported() const {return data[49] & (1 << 14);}
        bool interleaved_dma_supported() const {return data[49] & (1 << 15);}

        // ATA-4 adds another bit in word 50
        // 51 is PIO data transfer cycle timing mode (obsolete in ATA-5)
        // 52 is DMA data transfer cycle timing mode (obsolete in ATA-3)

        // obsolete in ATA-6
        bool current_params_valid() const {return data[53] & (1 << 0);} // fields 54-58
        uint16_t num_current_cylinders() const {return data[54];}
        uint16_t num_current_heads() const {return data[55];}
        uint16_t num_current_sectors_per_track() const {return data[56];}
        uint32_t current_capacity_sectors() const {return data[57] | data[58] << 16;}

        bool rw_multiple_sector_setting_valid() const {return data[59] & (1 << 8);}
        uint8_t current_read_write_multiple() const {return data[59] & 0xFF;}

        uint32_t total_user_addressable_sectors() const {return data[60] | data[61] << 16;}

        // ATA-2
        // 62 is single word DMA modes (obsolete in ATA-3)
        uint8_t multiword_dma_modes_supported() const {return data[63] & 0xFF;}

        bool timing_params_valid() const {return data[53] & (1 << 1);} // fields 64-70
        uint8_t advanced_pio_modes_supported() const {return data[64] & 0xFF;} // modes 3-4 (lower modes require checking word 51)
        uint16_t min_mw_dma_cycle_time() const {return data[65];}
        uint16_t rec_mw_dma_cycle_time() const {return data[66];}
        uint16_t min_pio_cycle_time() const {return data[67];}
        uint16_t min_pio_cycle_time_iordy() const {return data[68];}

        // ATA-3
        int major_version() const {return 31 - __builtin_clz(data[80]);};
        uint16_t minor_version() const {return data[81];}
        // 128 is security status

        // ATA-4
        int queue_depth() const {return (data[75] & 0x1F) + 1;}

        bool ultra_dma_modes_valid() const {return data[53] & (1 << 2);}
        uint8_t ultra_dma_modes_supported() const {return data[88] & 0xFF;}

        uint16_t security_erase_time() const {return data[89];}
        uint16_t enhanced_security_erase_time() const {return data[90];}
        // 91 is cur APM value
        // 127 is removable media status feature set

        // ATAPI-4
        // 71 PACKET to bus release time
        // 72 SERVICE to BSY=0 time

        // ATA-5
        // 92 is master password revision code
        // 93 is hardware test results
        // 160 is CFA power mode 1
        bool checksum_valid() const {return (data[255] & 0xFF) == 0xA5;}
        uint8_t get_checksum() const {return data[255] >> 8;}

        // ATA-6
        // 94 is acoustic management
        // 100-103 is max LBA for 48bit
        // 176-205 is media serial number

        // command sets
        bool command_set_notification_supported() const {return (data[82] != 0 && data[82] != 0xFFFF) || (data[83] != 0 && data[83] != 0xFFFF);}

        // ATA-3
        bool smart_supported() const {return data[82] & (1 << 0);}
        bool security_supported() const {return data[82] & (1 << 1);}
        bool removable_supported() const {return data[82] & (1 << 2);}
        bool power_management_supported() const {return data[82] & (1 << 3);}

        // ATA-4
        bool packet_supported() const {return data[82] & (1 << 4);}
        bool write_cache_supported() const {return data[82] & (1 << 5);}
        bool look_ahead_supported() const {return data[82] & (1 << 6);}
        bool release_interrupt_supported() const {return data[82] & (1 << 7);}
        bool service_interrupt_supported() const {return data[82] & (1 << 8);}
        bool device_reset_supported() const {return data[82] & (1 << 9);}
        bool host_protected_area_supported() const {return data[82] & (1 << 10);}
        // bit 11 is obsolete in ATA-4 and reserved before that?
        bool write_buffer_supported() const {return data[82] & (1 << 12);}
        bool read_buffer_supported() const {return data[82] & (1 << 13);}
        bool nop_supported() const {return data[82] & (1 << 14);}
        // bit 15 is similar to 11

        bool download_microcode_supported() const {return data[83] & (1 << 0);}
        bool rw_dma_queued_supported() const {return data[83] & (1 << 1);}
        bool cfa_supported() const {return data[83] & (1 << 2);}
        bool apm_supported() const {return data[83] & (1 << 3);}
        bool removable_media_status_supported() const {return data[83] & (1 << 4);}

        // ATA-5
        bool power_up_in_standby_supported() const {return data[83] & (1 << 5);}
        bool set_features_required_to_spin_up() const {return data[83] & (1 << 6);}
        // bit 7 is "address offset reserved area boot"
        bool set_max_security_supported() const {return data[83] & (1 << 8);}

        // ATA-6
        bool auto_acoustic_management_supported() const {return data[83] & (1 << 9);}
        bool address_48bit_supported() const {return data[83] & (1 << 10);}
        bool device_config_overlay_supported() const {return data[83] & (1 << 11);}
        bool flush_cache_supported() const {return data[83] & (1 << 12);}
        bool flush_cache_ext_supported() const {return data[83] & (1 << 13);}

        bool smart_error_logging_supported() const {return data[84] & (1 << 0);}
        bool smart_self_test_supported() const {return data[84] & (1 << 1);}
        bool media_serial_number_supported() const {return data[84] & (1 << 2);}
        bool media_card_pass_through_supported() const {return data[84] & (1 << 3);}
        bool general_purpose_logging_supported() const {return data[84] & (1 << 5);}

        // 85-87 are enabled commands/feature sets

        // ATAPI-4
        int command_packet_size() const
        {
            static const int packet_sizes[]{12, 16, 0, 0};
            return packet_sizes[data[0] & 3];
        }
        int drq_response_time() const {return (data[0] >> 5) & 3;}
        int packet_command_set() const {return (data[0] >> 8) & 0x1F;}

    private:
        const uint16_t *data;

        void get_string(int offset, char *out_ptr, int len) const
        {
            auto in_ptr = data + offset;
            for(int i = 0; i < len; i += 2)
            {
                *out_ptr++ = *in_ptr >> 8;
                *out_ptr++ = *in_ptr++ & 0xFF;
            }
            *out_ptr++ = 0;
        }
    };
}