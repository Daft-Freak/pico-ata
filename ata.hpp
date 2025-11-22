#pragma once
#include <cstdint>

namespace ata
{
    // includes CS and addr
    enum class ATAReg
    {
        AltStatus   = 1 << 3 | 6,
        Data        = 2 << 3 | 0,
        Error       = 2 << 3 | 1, // read-only
        Features    = 2 << 3 | 1, // write-only
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
        READ_SECTOR            = 0x20,
        PACKET                 = 0xA0,
        IDENTIFY_PACKET_DEVICE = 0xA1,
        IDENTIFY_DEVICE        = 0xEC,
        SET_FEATURES           = 0xEF,
    };

    // initialisation
    void init_io();

    void adjust_for_min_cycle_time(int min_cycle_time);

    void do_reset();

    // register access
    uint16_t read_register(ATAReg reg);
    void write_register(ATAReg reg, uint16_t data);

    // helper for convenience
    inline void write_command(ATACommand command)
    {
        write_register(ATAReg::Command, static_cast<int>(command));
    }

    // status helpers
    bool check_ready();
    bool check_data_request();

    // PIO transfers
    void do_pio_read(uint16_t *data, int count);
    void do_pio_write(const uint16_t *data, int count);

    // higher level commands
    void read_sectors(int device, uint32_t lba, int num_sectors, uint16_t *data);

    void identify_device(int device, uint16_t data[256], ATACommand command = ATACommand::IDENTIFY_DEVICE);
}