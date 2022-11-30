/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
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
 */

#include <stdio.h>

#include "py/runtime.h"
#include "extmod/vfs.h"
#include "samd_soc.h"
#include "hal_flash.h"

// ASF 4
#include "hal_flash.h"
#include "hal_init.h"
#include "hpl_gclk_base.h"

#if defined(MCU_SAMD21)
#include "lib/asf4/samd21/hpl/pm/hpl_pm_base.h"
#elif defined(MCU_SAMD51)
#include "lib/asf4/samd51/hpl/pm/hpl_pm_base.h"
#include "lib/asf4/samd51/hri/hri_mclk_d51.h"
#endif

static struct flash_descriptor flash_desc;
STATIC mp_int_t BLOCK_SIZE = VFS_BLOCK_SIZE_BYTES; // Board specific: mpconfigboard.h
extern const mp_obj_type_t samd_mcuflash_type;

typedef struct _samd_mcuflash_obj_t {
    mp_obj_base_t base;
    uint32_t flash_base;
    uint32_t flash_size;
} samd_mcuflash_obj_t;

extern uint8_t _oflash_fs, _sflash_fs;

// Build a Flash storage at top.
STATIC samd_mcuflash_obj_t samd_mcuflash_obj = {
    .base = { &samd_mcuflash_type },
    .flash_base = (uint32_t)&_oflash_fs, // Get from MCU-Specific loader script.
    .flash_size = (uint32_t)&_sflash_fs, // Get from MCU-Specific loader script.
};

// FLASH stuff
STATIC mp_obj_t samd_mcuflash_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    // No args required. bdev=Flash(). Start Addr & Size defined in samd_mcuflash_obj.
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    #ifdef SAMD51
    hri_mclk_set_AHBMASK_NVMCTRL_bit(MCLK);
    #endif
    #ifdef SAMD21
    _pm_enable_bus_clock(PM_BUS_APBB, NVMCTRL);
    #endif

    flash_init(&flash_desc, NVMCTRL);

    // Return singleton object.
    return MP_OBJ_FROM_PTR(&samd_mcuflash_obj);
}

// Function for ioctl.
STATIC mp_obj_t samd_mcuflash_erase(mp_obj_t self_in, mp_obj_t addr_in) {
    // Destination address aligned with page start to be erased.
    uint32_t DEST_ADDR = mp_obj_get_int(addr_in) + samd_mcuflash_obj.flash_base;
    mp_int_t PAGE_SIZE = flash_get_page_size(&flash_desc); // adf4 API call

    flash_erase(&flash_desc, DEST_ADDR, (BLOCK_SIZE / PAGE_SIZE));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(samd_mcuflash_erase_obj, samd_mcuflash_erase);

STATIC mp_obj_t samd_mcuflash_size(mp_obj_t self_in) {
    samd_mcuflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(self->flash_size);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(samd_mcuflash_size_obj, samd_mcuflash_size);

STATIC mp_obj_t samd_mcuflash_sectorsize(mp_obj_t self_in) {
    return MP_OBJ_NEW_SMALL_INT(BLOCK_SIZE);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(samd_mcuflash_sectorsize_obj, samd_mcuflash_sectorsize);

STATIC mp_obj_t samd_mcuflash_read(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t buf_in) {
    samd_mcuflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t offset = mp_obj_get_int(addr_in) + self->flash_base;
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    // Read data to flash (asf4 API)
    flash_read(&flash_desc, offset, bufinfo.buf, bufinfo.len);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(samd_mcuflash_read_obj, samd_mcuflash_read);

STATIC mp_obj_t samd_mcuflash_write(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t buf_in) {
    samd_mcuflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t offset = mp_obj_get_int(addr_in) + self->flash_base;
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    // Write data to flash (asf4 API)
    flash_write(&flash_desc, offset, bufinfo.buf, bufinfo.len);
    // TODO check return value
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(samd_mcuflash_write_obj, samd_mcuflash_write);

STATIC const mp_rom_map_elem_t samd_mcuflash_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_flash_size), MP_ROM_PTR(&samd_mcuflash_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_sectorsize), MP_ROM_PTR(&samd_mcuflash_sectorsize_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_read), MP_ROM_PTR(&samd_mcuflash_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_write), MP_ROM_PTR(&samd_mcuflash_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_erase), MP_ROM_PTR(&samd_mcuflash_erase_obj) },
};
STATIC MP_DEFINE_CONST_DICT(samd_mcuflash_locals_dict, samd_mcuflash_locals_dict_table);


MP_DEFINE_CONST_OBJ_TYPE(
    samd_mcuflash_type,
    MP_QSTR_MCU_flash,
    MP_TYPE_FLAG_NONE,
    make_new, samd_mcuflash_make_new,
    locals_dict, &samd_mcuflash_locals_dict
    );
