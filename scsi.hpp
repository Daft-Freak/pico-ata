#pragma once

enum class SCSICommand
{
    TEST_UNIT_READY = 0x00,
    INQUIRY         = 0x12,
    READ_10         = 0x28,
};

enum class SCSISenseKey
{
    NO_SENSE        = 0x0,
    RECOVERED_ERROR = 0x1,
    NOT_READY       = 0x2,
    MEDIUM_ERROR    = 0x3,
    HARDWARE_ERROR  = 0x4,
    ILLEGAL_REQUEST = 0x5,
    UNIT_ATTENTION  = 0x6,
    DATA_PROTECT    = 0x7,
    BLANK_CHECK     = 0x8,
    VENDOR_SPECIFIC = 0x9,
    COPY_ABORTED    = 0xA,
    ABORTED_COMMAND = 0xB,
    VOLUME_OVERFLOW = 0xD,
    MISCOMPARE      = 0xE,
};