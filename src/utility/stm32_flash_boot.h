/*
  Copyright (c) 2021 Juraj Andrassy

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _STM32_FLASH_BOOT_H
#define _STM32_FLASH_BOOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__ ((long_call, noinline, section (".RamFunc*")))
void copy_flash_pages_nota(uint32_t flash_offs, const uint8_t *data, uint32_t count, uint8_t reset);

#ifdef __cplusplus
}
#endif

#endif
