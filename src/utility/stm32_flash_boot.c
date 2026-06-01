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

#ifdef ARDUINO_ARCH_STM32

#include "stm32_flash_boot.h"
#if defined(STM32F0xx)
#include <stm32f0xx_hal_flash_ex.h> // FLASH_PAGE_SIZE is in hal.h
#elif defined(STM32F1xx)
#include <stm32f1xx_hal_flash_ex.h> // FLASH_PAGE_SIZE is in hal.h
#elif defined(STM32F2xx)
#include <stm32f2xx.h>
#elif defined(STM32F3xx)
#include <stm32f3xx_hal_flash_ex.h> // FLASH_PAGE_SIZE is in hal.h
#elif defined(STM32F4xx)
#include "stm32f4xx.h"
#elif defined(STM32F7xx)
#include <stm32f7xx.h>
#define SMALL_SECTOR_SIZE 0x8000 // sectors 0 to 3
#define LARGE_SECTOR_SIZE 0x40000 // from sector 5
#endif

#if defined(STM32F2xx) || defined(STM32F4xx)
#define SMALL_SECTOR_SIZE 0x4000 // sectors 0 to 3
#define LARGE_SECTOR_SIZE 0x20000 // from sector 5
#endif

static inline __attribute__ ((always_inline)) void nota_wait_flash_ready(void) {
  while (FLASH->SR & FLASH_SR_BSY);
}

#define IWDG_KEY_RELOAD 0xAAAAU

static inline __attribute__ ((always_inline)) void nota_iwdg_reload(void) {
  IWDG->KR = IWDG_KEY_RELOAD;
}

static inline __attribute__ ((always_inline)) void nota_prepare_flash_programming(void) {
#ifdef FLASH_CR_PSIZE
  CLEAR_BIT(FLASH->CR, FLASH_CR_PSIZE);
  FLASH->CR |= 0x00000100U;
#endif
  FLASH->CR |= FLASH_CR_PG;
}

static inline __attribute__ ((always_inline)) void nota_finish_flash_programming(void) {
  CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
}

static inline __attribute__ ((always_inline)) __attribute__((noreturn)) void nota_system_reset(void) {
  FLASH->CR |= FLASH_CR_LOCK;
  __DSB();
  SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
  __DSB();
  for (;;) {
    __NOP();
  }
}

static inline __attribute__ ((always_inline)) void nota_erase_destination(uint32_t flash_offs, uint32_t count) {
  uint32_t page_address = flash_offs;

#ifdef FLASH_PAGE_SIZE
  SET_BIT(FLASH->CR, FLASH_CR_PER);
  for (uint16_t i = 0; i < count / FLASH_PAGE_SIZE; i++) {
    WRITE_REG(FLASH->AR, page_address);
    SET_BIT(FLASH->CR, FLASH_CR_STRT);
    page_address += FLASH_PAGE_SIZE;
    nota_wait_flash_ready();
    nota_iwdg_reload();
  }
  CLEAR_BIT(FLASH->CR, FLASH_CR_PER);
#else
  uint8_t startSector = (flash_offs == FLASH_BASE) ? 0 : 1;
  uint8_t endSector = 1 + startSector + ((count - 1) / SMALL_SECTOR_SIZE);
  if (endSector > 4) {
    if (endSector < 8) {
      endSector = 5;
    } else {
      endSector = 5 + (((count - 1) + (startSector * SMALL_SECTOR_SIZE)) / LARGE_SECTOR_SIZE);
    }
  }
  for (uint8_t sector = startSector; sector < endSector; sector++) {
    nota_iwdg_reload();
    CLEAR_BIT(FLASH->CR, FLASH_CR_SNB);
    FLASH->CR |= FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    nota_wait_flash_ready();
  }
  CLEAR_BIT(FLASH->CR, (FLASH_CR_SER | FLASH_CR_SNB));
#endif
}

static inline __attribute__ ((always_inline)) void nota_program_halfword(uint32_t address, uint16_t value) {
  *(volatile uint16_t*) address = value;
  nota_wait_flash_ready();
}

static inline __attribute__ ((always_inline)) void nota_spi_wait_txe(SPI_TypeDef* spi) {
  while (!(spi->SR & SPI_SR_TXE));
}

static inline __attribute__ ((always_inline)) void nota_spi_wait_rxne(SPI_TypeDef* spi) {
  while (!(spi->SR & SPI_SR_RXNE));
}

static inline __attribute__ ((always_inline)) void nota_spi_drain_rx(SPI_TypeDef* spi) {
  while (spi->SR & SPI_SR_RXNE) {
    (void) *((volatile uint8_t*) &spi->DR);
  }
  (void) spi->SR;
}

static inline __attribute__ ((always_inline)) uint8_t nota_spi_transfer_byte(SPI_TypeDef* spi, uint8_t value) {
  nota_spi_wait_txe(spi);
  *((volatile uint8_t*) &spi->DR) = value;
  nota_spi_wait_rxne(spi);
  return *((volatile uint8_t*) &spi->DR);
}

static inline __attribute__ ((always_inline)) void nota_spi_select(GPIO_TypeDef* gpio, uint32_t mask) {
  gpio->BSRR = (mask << 16U);
}

static inline __attribute__ ((always_inline)) void nota_spi_deselect(GPIO_TypeDef* gpio, uint32_t mask) {
  gpio->BSRR = mask;
}

void copy_flash_pages_nota(uint32_t flash_offs, const uint8_t *data, uint32_t count, uint8_t reset) {
  uint32_t page_address = flash_offs;
  uint16_t* ptr = (uint16_t*) data;

  FLASH->KEYR = 0x45670123U;
  FLASH->KEYR = 0xCDEF89ABU;

  nota_wait_flash_ready();
#ifdef FLASH_CR_PSIZE
  CLEAR_BIT(FLASH->CR, FLASH_CR_PSIZE);
  FLASH->CR |= 0x00000200U;
  FLASH->CR |= FLASH_CR_PG;
#endif
  nota_erase_destination(flash_offs, count);

  page_address = flash_offs;
  nota_wait_flash_ready();
  nota_prepare_flash_programming();
  while (count) {
    nota_program_halfword(page_address, *ptr);
    page_address += 2;
    count -= 2;
    ptr++;
  }
  nota_finish_flash_programming();

  if (reset) {
    nota_system_reset();
  }
}

void copy_flash_pages_from_spi_nota(uint32_t flash_offs, uintptr_t spi_base, uintptr_t cs_gpio_base, uint32_t cs_mask, uint32_t spi_flash_address, uint32_t count, uint8_t reset) {
  uint32_t page_address = flash_offs;
  SPI_TypeDef* spi = (SPI_TypeDef*) spi_base;
  GPIO_TypeDef* cs_gpio = (GPIO_TypeDef*) cs_gpio_base;

  nota_iwdg_reload();

  FLASH->KEYR = 0x45670123U;
  FLASH->KEYR = 0xCDEF89ABU;

  nota_wait_flash_ready();
  FLASH->SR = FLASH->SR;
  nota_erase_destination(flash_offs, count);
  nota_iwdg_reload();

  nota_spi_deselect(cs_gpio, cs_mask);
  nota_spi_drain_rx(spi);
  nota_spi_select(cs_gpio, cs_mask);
  nota_spi_transfer_byte(spi, 0x03);
  nota_spi_transfer_byte(spi, (spi_flash_address >> 16) & 0xFF);
  nota_spi_transfer_byte(spi, (spi_flash_address >> 8) & 0xFF);
  nota_spi_transfer_byte(spi, spi_flash_address & 0xFF);

  nota_wait_flash_ready();
  nota_prepare_flash_programming();

  uint32_t progress = 0;
  while (count) {
    uint16_t value = nota_spi_transfer_byte(spi, 0xFF);
    if (count > 1) {
      value |= ((uint16_t) nota_spi_transfer_byte(spi, 0xFF)) << 8;
    } else {
      value |= 0xFF00;
    }
    nota_program_halfword(page_address, value);
    page_address += 2;
    count = (count > 1) ? (count - 2) : 0;

    progress += 2;
    if ((progress & 0x7FFF) == 0) {
      nota_iwdg_reload();
    }
  }
  nota_finish_flash_programming();
  nota_spi_deselect(cs_gpio, cs_mask);
  nota_iwdg_reload();

  if (reset) {
    nota_system_reset();
  }
}

#endif
