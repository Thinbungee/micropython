/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2022 Robert Hammelrath
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Based on spiflash.py by Christopher Arndt and the EEPROM library of Peter Hinch
 *
 * https://github.com/SpotlightKid/micropython-stm-lib/tree/master/spiflash
 * https://github.com/peterhinch/micropython_eeprom.git
 */

#include <stdint.h>
#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "extmod/machine_spi.h"
#include "extmod/virtpin.h"
#include "modmachine.h"

#if MICROPY_HW_SPIFLASH

#define _READ_INDEX (0)
#define _PROGRAM_PAGE_INDEX (1)
#define _SECTOR_ERASE_INDEX (2)

const uint8_t _CMDS3BA[] = {0x03, 0x02, 0x20};  // CMD_READ CMD_PROGRAM_PAGE CMD_ERASE_4K
const uint8_t _CMDS4BA[] = {0x13, 0x12, 0x21};  // CMD_READ CMD_PROGRAM_PAGE CMD_ERASE_4K

#define CMD_JEDEC_ID (0x9F)
#define CMD_READ_STATUS (0x05)
#define CMD_WRITE_ENABLE (0x06)
#define CMD_READ_UID (0x4B)
#define CMD_READ_SFDP (0x5A)
#define PAGE_SIZE (256)
#define SECTOR_SIZE (4096)

typedef struct _spiflash_obj_t {
    mp_obj_base_t base;
    mp_obj_base_t *spi;
    mp_obj_base_t *cs;
    bool addr4b;
    uint16_t pagesize;
    uint16_t sectorsize;
    const uint8_t *commands;
    uint32_t size;
} spiflash_obj_t;

extern const mp_obj_type_t spiflash_type;

static void spi_transfer(mp_obj_base_t *spi, size_t len, const uint8_t *src, uint8_t *dest) {
    mp_machine_spi_p_t *spi_p = (mp_machine_spi_p_t *)MP_OBJ_TYPE_GET_SLOT(spi->type, protocol);
    spi_p->transfer(spi, len, src, dest);
}

static void wait(spiflash_obj_t *self) {
    uint8_t msg[2];
    uint32_t timeout = 100000;

    // each loop takes at least about 5µs @ 120Mhz. So a timeout of
    // 100000 wait 500ms max. at 120Mhz. Sector erase lasts about
    // 100ms worst case, page write is < 1ms.
    do {
        msg[0] = CMD_READ_STATUS;
        mp_virtual_pin_write(self->cs, 0);
        spi_transfer((mp_obj_base_t *)self->spi, 2, msg, msg);
        mp_virtual_pin_write(self->cs, 1);
    } while (msg[1] != 0 && timeout-- > 0);
}

static void get_id(spiflash_obj_t *self, uint8_t id[3]) {
    uint8_t msg[1];

    msg[0] = CMD_JEDEC_ID;
    mp_virtual_pin_write(self->cs, 0);
    spi_transfer(self->spi, 1, msg, NULL);
    spi_transfer(self->spi, 3, id, id);
    mp_virtual_pin_write(self->cs, 1);
}
static void write_addr(spiflash_obj_t *self, uint8_t cmd, uint32_t addr) {
    uint8_t msg[5];
    uint8_t index = 1;
    msg[0] = cmd;
    if (self->addr4b) {
        msg[index++] = addr >> 24;
    }
    msg[index++] = (addr >> 16) & 0xff;
    msg[index++] = (addr >> 8) & 0xff;
    msg[index++] = addr & 0xff;
    mp_virtual_pin_write(self->cs, 0);
    spi_transfer(self->spi, self->addr4b ? 5 : 4, msg, msg);
}

static void write_enable(spiflash_obj_t *self) {
    uint8_t msg[1];

    msg[0] = CMD_WRITE_ENABLE;
    mp_virtual_pin_write(self->cs, 0);
    spi_transfer(self->spi, 1, msg, NULL);
    mp_virtual_pin_write(self->cs, 1);
}

static void get_sfdp(spiflash_obj_t *self, uint32_t addr, uint8_t *buffer, int size) {
    uint8_t dummy[1];
    write_addr(self, CMD_READ_SFDP, addr);
    spi_transfer(self->spi, 1, dummy, NULL);
    spi_transfer(self->spi, size, buffer, buffer);
    mp_virtual_pin_write(self->cs, 1);
}

STATIC mp_obj_t spiflash_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_spi, ARG_cs, ARG_addr4b, ARG_size, ARG_pagesize, ARG_sectorsize };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_spi, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        { MP_QSTR_cs, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        { MP_QSTR_addr4b, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_size, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_pagesize, MP_ARG_INT, {.u_int = PAGE_SIZE} },
        { MP_QSTR_sectorsize, MP_ARG_INT, {.u_int = SECTOR_SIZE} },
    };
    mp_arg_check_num(n_args, n_kw, 2, 3, true);
    // Parse the arguments.
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_obj_base_t *spi = args[ARG_spi].u_obj;
    mp_obj_base_t *cs = args[ARG_cs].u_obj;
    if (!((mp_obj_is_type(spi, &machine_spi_type) || mp_obj_is_type(spi, &mp_machine_soft_spi_type)) &&
          mp_obj_is_type(cs, &machine_pin_type))) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid argument type"));
    }
    // Set up the object
    spiflash_obj_t *self = mp_obj_malloc(spiflash_obj_t, &spiflash_type);
    self->spi = spi;
    self->cs = cs;
    self->addr4b = false; // initial setting.
    self->pagesize = args[ARG_pagesize].u_int;
    mp_virtual_pin_write(self->cs, 1);

    // Get the flash size
    wait(self);

    // If size is not provided, get it from the device ID (default)
    if (args[ARG_size].u_int == -1) {
        uint8_t id[3];
        get_id(self, id);
        if (id[1] == 0x84 && id[2] == 1) {  // Adesto
            self->size = 512 * 1024;
        } else if (id[1] == 0x1f && id[2] == 1) {  // Atmeĺ / Renesas
            self->size = 1024 * 1024;
        } else {
            self->size = 1 << id[2];
        }
    } else {
        self->size = args[ARG_size].u_int;
    }

    // Get the addr4b flag and the sector size
    uint8_t buffer[128];
    get_sfdp(self, 0, buffer, 16);  // get the header
    int len = MIN(buffer[11] * 4, sizeof(buffer));
    if (len >= 29) {
        int addr = buffer[12] + (buffer[13] << 8) + (buffer[14] << 16);
        get_sfdp(self, addr, buffer, len);  // Get the JEDEC mandatory table
        self->sectorsize = 1 << buffer[28];
        self->addr4b = ((buffer[2] >> 1) & 0x03) != 0;
    } else {
        self->sectorsize = args[ARG_sectorsize].u_int;
        self->addr4b = args[ARG_addr4b].u_int;
    }
    self->commands = self->addr4b ? _CMDS4BA : _CMDS3BA;

    return self;
}

STATIC mp_obj_t spiflash_size(mp_obj_t self_in) {
    spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(self->size);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(spiflash_size_obj, spiflash_size);

STATIC mp_obj_t spiflash_sectorsize(mp_obj_t self_in) {
    spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(self->sectorsize);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(spiflash_sectorsize_obj, spiflash_sectorsize);

STATIC mp_obj_t spiflash_read(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t buf_in) {
    spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t addr = mp_obj_get_int(addr_in);

    mp_buffer_info_t src;
    mp_get_buffer_raise(buf_in, &src, MP_BUFFER_READ);
    if (src.len > 0) {
        write_addr(self, self->commands[_READ_INDEX], addr);
        spi_transfer(self->spi, src.len, src.buf, src.buf);
        mp_virtual_pin_write(self->cs, 1);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(spiflash_read_obj, spiflash_read);

STATIC mp_obj_t spiflash_write(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t buf_in) {
    spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t addr = mp_obj_get_int(addr_in);

    mp_buffer_info_t src;
    mp_get_buffer_raise(buf_in, &src, MP_BUFFER_READ);
    uint32_t length = src.len;
    uint32_t pos = 0;
    uint8_t *buf = src.buf;

    while (pos < length) {
        uint16_t maxsize = self->pagesize - pos % self->pagesize;
        uint16_t size = (length - pos) > maxsize ? maxsize : length - pos;

        write_enable(self);
        write_addr(self, self->commands[_PROGRAM_PAGE_INDEX], addr);
        spi_transfer(self->spi, size, buf + pos, NULL);
        mp_virtual_pin_write(self->cs, 1);
        wait(self);

        addr += size;
        pos += size;
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(spiflash_write_obj, spiflash_write);

STATIC mp_obj_t spiflash_erase(mp_obj_t self_in, mp_obj_t addr_in) {
    spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t addr = mp_obj_get_int(addr_in);

    write_enable(self);
    write_addr(self, self->commands[_SECTOR_ERASE_INDEX], addr);
    mp_virtual_pin_write(self->cs, 1);
    wait(self);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(spiflash_erase_obj, spiflash_erase);

STATIC const mp_rom_map_elem_t spiflash_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_flash_size), MP_ROM_PTR(&spiflash_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_sectorsize), MP_ROM_PTR(&spiflash_sectorsize_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_read), MP_ROM_PTR(&spiflash_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_write), MP_ROM_PTR(&spiflash_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_erase), MP_ROM_PTR(&spiflash_erase_obj) },
};
STATIC MP_DEFINE_CONST_DICT(spiflash_locals_dict, spiflash_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    spiflash_type,
    MP_QSTR_SPI_flash,
    MP_TYPE_FLAG_NONE,
    make_new, spiflash_make_new,
    locals_dict, &spiflash_locals_dict
    );

#endif // #if MICROPY_HW_SPIFLASH
