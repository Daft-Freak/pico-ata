#include <cmath>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"

#include "ata.hpp"

#include "config.h"

#include "ata.pio.h"

static const PIO ata_pio = pio0;
static int ata_read_pio_sm = -1, ata_write_pio_sm = -1;

static int calculate_clkdiv(int target_cycle_time)
{
    double clock_ns = 1000000000.0 / clock_get_hz(clk_sys);
    int target_ns = target_cycle_time / 6; // 6 instructions for the loop
    int clkdiv = ceil(target_ns / clock_ns);

    return clkdiv;
}

namespace ata
{
    void init_io()
    {
        // setup all the IO
        gpio_init_mask(ATA_IO_MASK);

        // init the active low control signals
        auto mask = ATA_RESET_PIN_MASK | ATA_CS_PIN_MASK;
        gpio_put_masked(mask, mask);

        // also set address pins to output
        mask |= ATA_ADDR_PIN_MASK;
        gpio_set_dir_out_masked(mask);

        // PIO init
        int read_program_offset = pio_add_program(pio0, &pio_read_program);
        int write_program_offset = pio_add_program(pio0, &pio_write_program);
        ata_read_pio_sm = pio_claim_unused_sm(ata_pio, true);
        ata_write_pio_sm = pio_claim_unused_sm(ata_pio, true);

        // setup read/write pins
        uint32_t rw_mask = ATA_READ_PIN_MASK | ATA_WRITE_PIN_MASK;
        pio_sm_set_pins_with_mask(ata_pio, ata_read_pio_sm, rw_mask, rw_mask);
        pio_sm_set_pindirs_with_mask(ata_pio, ata_read_pio_sm, rw_mask, rw_mask);
        pio_gpio_init(ata_pio, ATA_READ_PIN);
        pio_gpio_init(ata_pio, ATA_WRITE_PIN);

        // setup data bus
        pio_sm_set_pindirs_with_mask(ata_pio, ata_read_pio_sm, 0, ATA_DATA_PIN_MASK);
        for(int i = 0; i < 16; i++)
            pio_gpio_init(ata_pio, ATA_DATA_PIN_BASE + i);

        // configure read program
        pio_sm_config c = pio_read_program_get_default_config(read_program_offset);

        sm_config_set_in_shift(&c, false, true, 16); // data
        sm_config_set_out_shift(&c, false, true, 16); // read count

        sm_config_set_in_pins(&c, ATA_DATA_PIN_BASE);
        sm_config_set_sideset_pins(&c, ATA_READ_PIN);
        sm_config_set_jmp_pin(&c, ATA_IORDY_PIN);

        // calc clkdiv
        int clkdiv = calculate_clkdiv(600); // PIO mode 0 cycle time
        sm_config_set_clkdiv_int_frac8(&c, clkdiv, 0);

        pio_sm_init(ata_pio, ata_read_pio_sm, read_program_offset, &c);

        // configure write program
        c = pio_write_program_get_default_config(write_program_offset);

        sm_config_set_out_shift(&c, false, false, 16); // data

        sm_config_set_out_pins(&c, ATA_DATA_PIN_BASE, 16);
        sm_config_set_sideset_pins(&c, ATA_WRITE_PIN);
        sm_config_set_jmp_pin(&c, ATA_IORDY_PIN);

        sm_config_set_clkdiv_int_frac8(&c, clkdiv, 0);

        pio_sm_init(ata_pio, ata_write_pio_sm, write_program_offset, &c);

        // start
        pio_set_sm_mask_enabled(ata_pio, 1 << ata_read_pio_sm | 1 << ata_write_pio_sm, true);
    }

    void adjust_for_min_cycle_time(int min_cycle_time)
    {
        // we're wrong for reg access in modes 1-2 (330-383ns cycle times)
        // (and the mode 2 cycle time for reg access is different...)
        // let's just hope nobody connects a drive that slow
        int clkdiv = calculate_clkdiv(min_cycle_time);

        pio_set_sm_mask_enabled(ata_pio, 1 << ata_read_pio_sm | 1 << ata_write_pio_sm, false);

        pio_sm_set_clkdiv_int_frac8(ata_pio, ata_read_pio_sm, clkdiv, 0);
        pio_sm_set_clkdiv_int_frac8(ata_pio, ata_write_pio_sm, clkdiv, 0);

        pio_set_sm_mask_enabled(ata_pio, 1 << ata_read_pio_sm | 1 << ata_write_pio_sm, true);
    }

    void do_reset()
    {
        // assert reset
        gpio_put(ATA_RESET_PIN, false);
        sleep_us(25);

        // now wait a bit
        gpio_put(ATA_RESET_PIN, true);
        sleep_ms(2);

        // wait for reset
        // TODO: add timeout
        while(true)
        {
            auto status = read_register(ATAReg::Status);

            // check for !BSY
            if(!(status & Status_BSY))
                break;
        }
    }

    uint16_t read_register(ATAReg reg)
    {
        // set address
        gpio_put_masked(ATA_CS_PIN_MASK | ATA_ADDR_PIN_MASK, static_cast<int>(reg) >> 3 << ATA_CS_PIN_BASE | (static_cast<int>(reg) & 7) << ATA_ADDR_PIN_BASE);

        uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + ata_read_pio_sm);

        // count = 1
        pio_sm_put_blocking(ata_pio, ata_read_pio_sm, 0);
        ata_pio->fdebug |= stall_mask;

        // get result
        uint16_t data = pio_sm_get_blocking(ata_pio, ata_read_pio_sm);

        // wait for stall
        while(!(ata_pio->fdebug & stall_mask));

        return data;
    }

    void write_register(ATAReg reg, uint16_t data)
    {
        // set address
        gpio_put_masked(ATA_CS_PIN_MASK | ATA_ADDR_PIN_MASK, static_cast<int>(reg) >> 3 << ATA_CS_PIN_BASE | (static_cast<int>(reg) & 7) << ATA_ADDR_PIN_BASE);

        uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + ata_write_pio_sm);

        pio_sm_put_blocking(ata_pio, ata_write_pio_sm, data << 16);

        // wait for stall
        ata_pio->fdebug |= stall_mask;
        while(!(ata_pio->fdebug & stall_mask));
    }

    bool check_ready()
    {
        auto status = read_register(ATAReg::Status);

        // !BSY && DRDY
        return !(status & Status_BSY) && (status & Status_DRDY);
    }

    bool do_pio_read(uint16_t *data, int count)
    {
        // poll status
        // TODO: timeout
        while(true)
        {
            auto status = read_register(ATAReg::Status);

            // ignore bsy
            if(status & Status_BSY)
                continue;

            // done if !BSY && DRQ
            if(status & Status_DRQ)
                break;

            // fail if error
            if(status & Status_ERR)
                return false;
        }

        assert(count > 0);
        assert(count <= 0x10000);

        // set address
        auto reg = ATAReg::Data;
        gpio_put_masked(ATA_CS_PIN_MASK | ATA_ADDR_PIN_MASK, static_cast<int>(reg) >> 3 << ATA_CS_PIN_BASE | (static_cast<int>(reg) & 7) << ATA_ADDR_PIN_BASE);

        uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + ata_read_pio_sm);

        pio_sm_put_blocking(ata_pio, ata_read_pio_sm, (count - 1) << 16);
        ata_pio->fdebug |= stall_mask;

        // TODO: DMA?
        for(int i = 0; i < count; i++)
            data[i] = pio_sm_get_blocking(ata_pio, ata_read_pio_sm);

        // wait for stall
        while(!(ata_pio->fdebug & stall_mask));

        return true;
    }

    bool do_pio_write(const uint16_t *data, int count)
    {
        // poll status
        while(true)
        {
            auto status = read_register(ATAReg::Status);

            // ignore bsy
            if(status & Status_BSY)
                continue;

            // done if !BSY && DRQ
            if(status & Status_DRQ)
                break;

            // fail if error
            if(status & Status_ERR)
                return false;
        }

        // set address
        auto reg = ATAReg::Data;
        gpio_put_masked(ATA_CS_PIN_MASK | ATA_ADDR_PIN_MASK, static_cast<int>(reg) >> 3 << ATA_CS_PIN_BASE | (static_cast<int>(reg) & 7) << ATA_ADDR_PIN_BASE);

        uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + ata_write_pio_sm);

        // TODO: DMA?
        for(int i = 0; i < count; i++)
            pio_sm_put_blocking(ata_pio, ata_write_pio_sm, data[i] << 16);

        // wait for stall
        ata_pio->fdebug |= stall_mask;
        while(!(ata_pio->fdebug & stall_mask));

        return true;
    }

    bool device_reset(int device)
    {
        write_register(ATAReg::Device, device << 4 /*device id*/);
        write_command(ATACommand::DEVICE_RESET);

        sleep_us(1);

        // wait for either !BSY or ERR
        while(true)
        {
            auto status = read_register(ATAReg::Status);

            // check for !BSY
            if(!(status & Status_BSY))
                return !(status & Status_ERR); // return true if no error
        }
    }

    void read_sectors(int device, uint32_t lba, int num_sectors, uint16_t *data)
    {
        // TODO: error checking
        // TODO: timeout?

        assert(device < 2);
        assert(num_sectors <= 256);
        assert(lba < 0x10000000); // TODO: LBA48

        while(!check_ready());

        write_register(ATAReg::SectorCount, num_sectors & 0xFF); // 0 == 256, so just throw away the high bit
        write_register(ATAReg::LBALow, lba & 0xFF);
        write_register(ATAReg::LBAMid, (lba >> 8) & 0xFF);
        write_register(ATAReg::LBAHigh, (lba >> 16) & 0xFF);
        write_register(ATAReg::Device, 1 << 6 /*LBA*/ | device << 4 /*device id*/ | ((lba >> 24) & 0xF));
        write_command(ATACommand::READ_SECTOR);

        for(int sector = 0; sector < num_sectors; sector++)
        {
            // 512 bytes per sector
            do_pio_read(data + sector * 256, 256);
        }
    }

    void identify_device(int device, uint16_t data[256], ATACommand command)
    {
        // does not wait for ready as ATAPI devices aren't ready at this point

        write_register(ATAReg::Device, device << 4);
        write_command(command);

        do_pio_read(data, 256);
    }

    // sector count meaning depends on the feature
    void set_features(int device, ATAFeature feature, uint8_t sectorCount)
    {
        while(!check_ready());

        write_register(ATAReg::Features, static_cast<uint16_t>(feature));
        write_register(ATAReg::SectorCount, sectorCount);
        write_register(ATAReg::Device, device << 4);
        write_command(ATACommand::SET_FEATURES);

        // TODO: check for errors
    }
}