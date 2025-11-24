#include "atapi.hpp"

#include "ata.hpp"

namespace atapi
{
    using namespace ata;

    void do_command(int device, int max_len, const uint8_t *command)
    {
        write_register(ATAReg::Features, 0);
        write_register(ATAReg::LBAMid, max_len & 0xFF);
        write_register(ATAReg::LBAHigh, (max_len >> 8) & 0xFF);
        write_register(ATAReg::Device, device << 4 /*device id*/);
        write_command(ATACommand::PACKET);

        // assuming 12-byte
        do_pio_write((uint16_t *)command, 6);

        // delay
        read_register(ATAReg::AltStatus);
    }

    SCSISenseKey get_sense_key()
    {
        return static_cast<SCSISenseKey>((read_register(ATAReg::Error) & 0xFF) >> 4);
    }

    bool test_unit_ready(int device)
    {
        uint8_t command[12]{};

        command[0] = int(SCSICommand::TEST_UNIT_READY);
        command[5] = 0; // control
        atapi::do_command(device, 0, command);

        while(read_register(ATAReg::Status) & Status_BSY);
    
        uint8_t status = read_register(ATAReg::Status);

        if(status & Status_ERR) // for ATAPI, this is "check condition"
            return false;
        
        return true;
    }

    void inquiry(int device, uint8_t *data, int data_len)
    {
        // assuming 12-byte
        uint8_t command[12]{};
        command[0] = int(SCSICommand::INQUIRY);
        command[1] = 0;
        command[2] = 0; // page
        command[3] = data_len >> 8;
        command[4] = data_len & 0xFF;
        command[5] = 0; // control
        atapi::do_command(device, data_len, command);

        // now the response
        do_pio_read(reinterpret_cast<uint16_t *>(data), data_len / 2);
    }

    void read(int device, uint32_t lba, uint16_t num_sectors, uint8_t *data, int sector_size)
    {
        // TODO: multiple sectors probably needs more work...
        int data_len = num_sectors * sector_size;

        uint8_t command[12]{};
        command[0] = int(SCSICommand::READ_10);
        command[1] = 0; // FUA, DPO, RDPROTECT...
        command[2] = lba >> 24;
        command[3] = lba >> 16;
        command[4] = lba >> 8;
        command[5] = lba & 0xFF;
        command[6] = 0; // group number
        command[7] = num_sectors >> 8; // len high
        command[8] = num_sectors; // len low
        command[9] = 0; // control
        atapi::do_command(device, data_len, command);

        do_pio_read(reinterpret_cast<uint16_t *>(data), data_len / 2);
    }
}