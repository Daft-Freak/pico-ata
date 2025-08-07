#include <cstdio>

#include "pico/stdlib.h"
#include "pico/time.h"

#include "tusb.h"

#include "config.h"

// includes CS and addr
enum class ATAReg
{
    Status = 2 << 3 | 7,
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
        if(!(status & (1 << 7)))
            break;
    }
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

    // identify

    while(true)
    {
       
    }

    return 0;
}
