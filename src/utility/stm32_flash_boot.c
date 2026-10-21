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
#elif defined(STM32H723xx)
#include <stm32h7xx.h>
#define NOTA_STM32H7
#endif

#if defined(STM32F2xx) || defined(STM32F4xx)
#define SMALL_SECTOR_SIZE 0x4000 // sectors 0 to 3
#define LARGE_SECTOR_SIZE 0x20000 // from sector 5
#endif

static inline __attribute__ ((always_inline)) void nota_wait_flash_ready(void) {
#if defined(NOTA_STM32H7)
  while (FLASH->SR1 & FLASH_SR_BSY);
#else
  while (FLASH->SR & FLASH_SR_BSY);
#endif
}

#define IWDG_KEY_RELOAD 0xAAAAU

static inline __attribute__ ((always_inline)) void nota_iwdg_reload(void) {
#if defined(NOTA_STM32H7)
  IWDG1->KR = IWDG_KEY_RELOAD;
#else
  IWDG->KR = IWDG_KEY_RELOAD;
#endif
}

static inline __attribute__ ((always_inline)) void nota_prepare_flash_programming(void) {
#if defined(NOTA_STM32H7)
  SET_BIT(FLASH->CR1, FLASH_CR_PG);
#else
#ifdef FLASH_CR_PSIZE
  CLEAR_BIT(FLASH->CR, FLASH_CR_PSIZE);
  FLASH->CR |= 0x00000100U;
#endif
  FLASH->CR |= FLASH_CR_PG;
#endif
}

static inline __attribute__ ((always_inline)) void nota_finish_flash_programming(void) {
#if defined(NOTA_STM32H7)
  CLEAR_BIT(FLASH->CR1, FLASH_CR_PG);
#else
  CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
#endif
}

static inline __attribute__ ((always_inline)) __attribute__((noreturn)) void nota_system_reset(void) {
#if defined(NOTA_STM32H7)
  FLASH->CR1 |= FLASH_CR_LOCK;
#else
  FLASH->CR |= FLASH_CR_LOCK;
#endif
  __DSB();
  SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
  __DSB();
  for (;;) {
    __NOP();
  }
}

static inline __attribute__ ((always_inline)) void nota_erase_destination(uint32_t flash_offs, uint32_t count) {
#if defined(NOTA_STM32H7)
  const uint32_t sector_size = 0x20000U;
  uint32_t start_sector = (flash_offs - FLASH_BASE) / sector_size;
  uint32_t end_sector = (flash_offs - FLASH_BASE + count + sector_size - 1U) / sector_size;

  for (uint32_t sector = start_sector; sector < end_sector; sector++) {
    nota_iwdg_reload();
    nota_wait_flash_ready();
    FLASH->CCR1 = FLASH->SR1;
    MODIFY_REG(FLASH->CR1, FLASH_CR_SNB, (sector << FLASH_CR_SNB_Pos));
    SET_BIT(FLASH->CR1, FLASH_CR_SER | FLASH_CR_START);
    nota_wait_flash_ready();
  }
  CLEAR_BIT(FLASH->CR1, FLASH_CR_SER | FLASH_CR_SNB);
#else
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
#endif
}

#if defined(NOTA_STM32H7)
static inline __attribute__ ((always_inline)) void nota_program_flash_word(uint32_t address, const uint8_t *data) {
  const uint32_t *words = (const uint32_t *) data;
  volatile uint32_t *destination = (volatile uint32_t *) address;

  for (uint32_t index = 0; index < 8U; index++) {
    destination[index] = words[index];
  }
  nota_wait_flash_ready();
}
#else
static inline __attribute__ ((always_inline)) void nota_program_halfword(uint32_t address, uint16_t value) {
  *(volatile uint16_t*) address = value;
  nota_wait_flash_ready();
}
#endif

static inline __attribute__ ((always_inline)) void nota_spi_wait_txe(SPI_TypeDef* spi) {
#if defined(NOTA_STM32H7)
  while (!(spi->SR & SPI_SR_TXP));
#else
  while (!(spi->SR & SPI_SR_TXE));
#endif
}

static inline __attribute__ ((always_inline)) void nota_spi_wait_rxne(SPI_TypeDef* spi) {
#if defined(NOTA_STM32H7)
  while (!(spi->SR & SPI_SR_RXP));
#else
  while (!(spi->SR & SPI_SR_RXNE));
#endif
}

static inline __attribute__ ((always_inline)) void nota_spi_drain_rx(SPI_TypeDef* spi) {
#if defined(NOTA_STM32H7)
  while (spi->SR & SPI_SR_RXP) {
    (void) spi->RXDR;
  }
#else
  while (spi->SR & SPI_SR_RXNE) {
    (void) *((volatile uint8_t*) &spi->DR);
  }
  (void) spi->SR;
#endif
}

static inline __attribute__ ((always_inline)) uint8_t nota_spi_transfer_byte(SPI_TypeDef* spi, uint8_t value) {
  nota_spi_wait_txe(spi);
#if defined(NOTA_STM32H7)
  *((volatile uint8_t *) &spi->TXDR) = value;
#else
  *((volatile uint8_t*) &spi->DR) = value;
#endif
  nota_spi_wait_rxne(spi);
#if defined(NOTA_STM32H7)
  return *((volatile uint8_t *) &spi->RXDR);
#else
  return *((volatile uint8_t*) &spi->DR);
#endif
}

static inline __attribute__ ((always_inline)) void nota_spi_select(GPIO_TypeDef* gpio, uint32_t mask) {
  gpio->BSRR = (mask << 16U);
}

static inline __attribute__ ((always_inline)) void nota_spi_deselect(GPIO_TypeDef* gpio, uint32_t mask) {
  gpio->BSRR = mask;
}

void copy_flash_pages_nota(uint32_t flash_offs, const uint8_t *data, uint32_t count, uint8_t reset) {
#if defined(NOTA_STM32H7)
  if (flash_offs < FLASH_BASE || (flash_offs & 0x1FU) != 0U || count == 0U ||
      ((uint64_t) flash_offs + count) > ((uint64_t) FLASH_BASE + 0x100000U)) {
    return;
  }

  __disable_irq();
  FLASH->KEYR1 = 0x45670123U;
  FLASH->KEYR1 = 0xCDEF89ABU;
  nota_erase_destination(flash_offs, count);
  nota_prepare_flash_programming();

  uint32_t address = flash_offs;
  while (count) {
    uint8_t flash_word[32];
    uint32_t chunk = (count > sizeof(flash_word)) ? sizeof(flash_word) : count;
    for (uint32_t index = 0; index < sizeof(flash_word); index++) {
      flash_word[index] = (index < chunk) ? data[index] : 0xFFU;
    }
    nota_program_flash_word(address, flash_word);
    address += sizeof(flash_word);
    data += chunk;
    count -= chunk;
  }
  nota_finish_flash_programming();

  if (reset) {
    nota_system_reset();
  }
  __enable_irq();
#else
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
#endif
}

void copy_flash_pages_from_spi_nota(uint32_t flash_offs, uintptr_t spi_base, uintptr_t cs_gpio_base, uint32_t cs_mask, uint32_t spi_flash_address, uint32_t count, uint8_t reset) {
  uint32_t page_address = flash_offs;
  SPI_TypeDef* spi = (SPI_TypeDef*) spi_base;
  GPIO_TypeDef* cs_gpio = (GPIO_TypeDef*) cs_gpio_base;

  nota_iwdg_reload();

#if defined(NOTA_STM32H7)
  if (flash_offs < FLASH_BASE || (flash_offs & 0x1FU) != 0U || count == 0U ||
      ((uint64_t) flash_offs + count) > ((uint64_t) FLASH_BASE + 0x100000U)) {
    return;
  }

  __disable_irq();
  FLASH->KEYR1 = 0x45670123U;
  FLASH->KEYR1 = 0xCDEF89ABU;
  nota_erase_destination(flash_offs, count);
  nota_iwdg_reload();

  nota_spi_deselect(cs_gpio, cs_mask);
  nota_spi_drain_rx(spi);
  nota_spi_select(cs_gpio, cs_mask);
  nota_spi_transfer_byte(spi, 0x03);
  nota_spi_transfer_byte(spi, (spi_flash_address >> 16) & 0xFFU);
  nota_spi_transfer_byte(spi, (spi_flash_address >> 8) & 0xFFU);
  nota_spi_transfer_byte(spi, spi_flash_address & 0xFFU);

  nota_prepare_flash_programming();
  uint32_t progress = 0;
  while (count) {
    uint8_t flash_word[32];
    uint32_t chunk = (count > sizeof(flash_word)) ? sizeof(flash_word) : count;
    for (uint32_t index = 0; index < sizeof(flash_word); index++) {
      flash_word[index] = (index < chunk) ? nota_spi_transfer_byte(spi, 0xFFU) : 0xFFU;
    }
    nota_program_flash_word(page_address, flash_word);
    page_address += sizeof(flash_word);
    count -= chunk;
    progress += chunk;
    if ((progress & 0x7FFFU) == 0U) {
      nota_iwdg_reload();
    }
  }
  nota_finish_flash_programming();
  nota_spi_deselect(cs_gpio, cs_mask);
  nota_iwdg_reload();

  if (reset) {
    nota_system_reset();
  }
  __enable_irq();
  return;
#else
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
#endif
}

#endif
