#pragma once
#include <cstdint>

#include "scsi.hpp"

namespace atapi
{
    void do_command(int device, int max_len, const uint8_t *command);

    SCSISenseKey get_sense_key();

    bool test_unit_ready(int device);

    void inquiry(int device, uint8_t *data, int data_len=36);
}