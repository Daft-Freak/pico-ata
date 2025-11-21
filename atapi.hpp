#pragma once
#include <cstdint>

#include "scsi.hpp"

namespace atapi
{
    void do_command(int device, int max_len, const uint8_t *command);
}