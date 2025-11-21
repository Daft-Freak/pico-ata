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
}