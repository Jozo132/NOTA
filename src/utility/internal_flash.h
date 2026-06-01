#pragma once

#include <Arduino.h>
#include "stm32_flash_boot.h"

// Define NOTA_EXT_FLASH_OTA=1 (e.g. in platformio.ini build_flags) to stage OTA firmware
// on an external SPI flash chip via SPIMemory library. This allows full MCU flash updates
// because the staging area is not limited to the upper half of internal flash.
// Default (0): stage directly in the upper sectors of internal MCU flash (classic approach,
// firmware size limited to ~half of internal flash).
#ifndef NOTA_EXT_FLASH_OTA
#define NOTA_EXT_FLASH_OTA 0
#endif

uint32_t program_memory_address = 0x08000000;

#if NOTA_EXT_FLASH_OTA

// ============================================================================
// External SPI-flash OTA backend
// Requires: SPIMemory library, and an SPIFlash instance named `flash` plus
// a bool `flash_initialized` and spi_select(SPI_Flash/SPI_None) in scope
// (provided by host firmware code), plus SPI/CS register metadata for the
// RAM-resident apply step.
// ============================================================================

#include <SPIMemory.h>

#ifndef NOTA_EXT_FLASH_SPI_BASE
#error "Define NOTA_EXT_FLASH_SPI_BASE to the SPI peripheral base address when NOTA_EXT_FLASH_OTA=1"
#endif

#ifndef NOTA_EXT_FLASH_CS_GPIO_BASE
#error "Define NOTA_EXT_FLASH_CS_GPIO_BASE to the chip-select GPIO port base address when NOTA_EXT_FLASH_OTA=1"
#endif

#ifndef NOTA_EXT_FLASH_CS_MASK
#error "Define NOTA_EXT_FLASH_CS_MASK to the chip-select GPIO bit mask when NOTA_EXT_FLASH_OTA=1"
#endif

#ifndef NOTA_EXT_FLASH_OTA_ADDRESS
#define NOTA_EXT_FLASH_OTA_ADDRESS 0x00010000UL
#endif

#ifndef NOTA_EXT_FLASH_OTA_FROM_END
#define NOTA_EXT_FLASH_OTA_FROM_END 0
#endif

#ifndef NOTA_INTERNAL_FLASH_SIZE
#if defined(FLASH_END) && defined(FLASH_BASE)
#define NOTA_INTERNAL_FLASH_SIZE ((uint32_t) ((FLASH_END + 1UL) - FLASH_BASE))
#elif defined(STM32F411xE)
#define NOTA_INTERNAL_FLASH_SIZE 0x00080000UL
#else
#define NOTA_INTERNAL_FLASH_SIZE 0x00040000UL
#endif
#endif

#ifndef NOTA_EXT_FLASH_PAGE_SIZE
#define NOTA_EXT_FLASH_PAGE_SIZE 256U
#endif

#ifndef NOTA_EXT_FLASH_ERASE_SIZE
#define NOTA_EXT_FLASH_ERASE_SIZE 4096U
#endif

uint32_t program_ota_address = NOTA_EXT_FLASH_OTA_ADDRESS;
uint32_t program_ota_max_size = NOTA_INTERNAL_FLASH_SIZE;
uint32_t ota_sector = 6;
uint32_t ota_sector_count = 2;

struct OTAStorage {
    uint32_t program_ota_index = 0;
    uint32_t staged_size = 0;
    uint32_t erase_start = NOTA_EXT_FLASH_OTA_ADDRESS;
    uint32_t erase_size = 0;
    uint8_t data[NOTA_EXT_FLASH_PAGE_SIZE];
    uint32_t data_idx = 0;

    uint32_t align_down(uint32_t value, uint32_t alignment) {
        return value - (value % alignment);
    }

    uint32_t align_up(uint32_t value, uint32_t alignment) {
        if (value == 0) return 0;
        uint32_t remainder = value % alignment;
        return remainder == 0 ? value : (value + alignment - remainder);
    }

    bool compute_erase_window(uint32_t address, uint32_t size, uint32_t &start, uint32_t &length) {
        if (size == 0) return false;
        start = align_down(address, NOTA_EXT_FLASH_ERASE_SIZE);
        uint32_t end = align_up(address + size, NOTA_EXT_FLASH_ERASE_SIZE);
        if (end < address) return false;
        length = end - start;
        return length >= size;
    }

    void select_flash() {
        spi_select(SPI_Flash);
    }

    void release_flash() {
        spi_select(SPI_None);
    }

    // Re-initialize if there is any error state, even if already initialized.
    // A stale error flag left by another SPI peripheral (e.g. Ethernet) after a shared-SPI
    // bus transaction causes flash.error() to be nonzero, which makes writes fail silently.
    bool ensure_flash_ready() {
        select_flash();
        if (!flash_initialized || flash.error()) {
            flash.begin();
            if (flash.error()) {
                release_flash();
                return false;
            }
            flash_initialized = true;
        }
        bool ready = !flash.error();
        release_flash();
        return ready;
    }

    uint32_t resolve_storage_address(uint32_t size) {
#if NOTA_EXT_FLASH_OTA_FROM_END
        select_flash();
        uint32_t flash_capacity = flash.getCapacity();
        release_flash();
        if (size > flash_capacity) return 0xFFFFFFFFUL;
        // Align down to page boundary so every flush_page() write is page-aligned.
        // Without this, the first write starts mid-page and SPIMemory wraps within the
        // same page, corrupting data.
        uint32_t ota_address = align_down(flash_capacity - size, NOTA_EXT_FLASH_PAGE_SIZE);
        if (ota_address < NOTA_EXT_FLASH_OTA_ADDRESS) return 0xFFFFFFFFUL;
        return ota_address;
#else
        (void) size;
        return NOTA_EXT_FLASH_OTA_ADDRESS;
#endif
    }

    bool has_capacity(uint32_t size) {
        uint32_t ota_address = resolve_storage_address(size);
        if (ota_address == 0xFFFFFFFFUL) return false;
        uint32_t required_erase_start = 0;
        uint32_t required_erase_size = 0;
        if (!compute_erase_window(ota_address, size, required_erase_start, required_erase_size)) return false;
        if (required_erase_start < NOTA_EXT_FLASH_OTA_ADDRESS) return false;
        select_flash();
        uint32_t flash_capacity = flash.getCapacity();
        release_flash();
        return (required_erase_start + required_erase_size) <= flash_capacity;
    }

    uint32_t maxSize() {
        return program_ota_max_size;
    }

    int open(uint32_t size) {
        if (size == 0 || size > program_ota_max_size) return 1;
        if (!ensure_flash_ready()) return 2;
        program_ota_address = resolve_storage_address(size);
        if (!has_capacity(size)) return 4;
        if (!compute_erase_window(program_ota_address, size, erase_start, erase_size)) return 4;
        program_ota_index = 0;
        staged_size = size;
        data_idx = 0;
        memset(data, 0xFF, sizeof(data));
        if (!erase()) return 3;
        return 0;
    }

    bool erase() {
        // SPIMemory 3.4.0 eraseSection() never advances _currentAddress between block-erase
        // iterations, so only the first 64 KB of the staging region ever gets erased.
        // Iterate eraseSector() (4 KB each) manually to cover the full window.
        select_flash();
        bool ok = true;
        for (uint32_t addr = erase_start; addr < erase_start + erase_size && ok; addr += NOTA_EXT_FLASH_ERASE_SIZE) {
            ok = flash.eraseSector(addr);
        }
        release_flash();
        return ok;
    }

    bool flush_page() {
        if (data_idx == 0) return true;
        uint32_t page_address = program_ota_address + program_ota_index - data_idx;
        select_flash();
        bool ok = flash.writeByteArray(page_address, data, data_idx);
        release_flash();
        memset(data, 0xFF, sizeof(data));
        data_idx = 0;
        return ok;
    }

    bool write(uint8_t b) {
        if (program_ota_index >= staged_size) return false;
        data[data_idx++] = b;
        program_ota_index++;
        if (data_idx == sizeof(data)) {
            return flush_page();
        }
        return true;
    }

    bool close() {
        return flush_page();
    }

    void apply() {
        release_flash();
        noInterrupts();
        copy_flash_pages_from_spi_nota(
            program_memory_address,
            NOTA_EXT_FLASH_SPI_BASE,
            NOTA_EXT_FLASH_CS_GPIO_BASE,
            NOTA_EXT_FLASH_CS_MASK,
            program_ota_address,
            staged_size,
            true);
    }
} InternalStorage;

#else // !NOTA_EXT_FLASH_OTA

// ============================================================================
// Internal-flash OTA backend (classic)
// Stages firmware in the upper sectors of internal MCU flash using HAL APIs.
// Firmware size is limited to roughly half of internal flash.
// ============================================================================

uint32_t program_ota_address = 0x08040000;
uint32_t program_ota_max_size = 0x00040000;
uint32_t ota_sector = 6;
uint32_t ota_sector_count = 2;

// ─── RAM-resident flash helpers ──────────────────────────────────────────────
// On STM32F411 (single-bank flash) the CPU cannot fetch instructions from flash
// while an erase or program operation is in progress — it stalls completely.
// By placing these polling loops in RAM the CPU keeps running and can actively
// wait for BSY to clear.  NO flash-resident code may be called from here
// (no HAL, no Serial, no delay).
// ─────────────────────────────────────────────────────────────────────────────

__attribute__((long_call, noinline, section(".RamFunc*")))
static int _flash_erase_sector_ram(uint32_t sector, uint32_t voltage_range) {
    uint32_t psize = voltage_range << FLASH_CR_PSIZE_Pos;

    // Wait for any previous operation
    while (FLASH->SR & FLASH_SR_BSY) {}

    // Clear error flags (write-1-to-clear)
    FLASH->SR = FLASH_SR_EOP  | FLASH_SR_OPERR  | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    // Configure erase: PSIZE + sector + SER
    uint32_t cr = FLASH->CR;
    cr &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB | FLASH_CR_MER | FLASH_CR_PG);
    cr |= psize | FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos);
    FLASH->CR = cr;

    // Start
    FLASH->CR |= FLASH_CR_STRT;
    __DSB();

    // Poll BSY — this loop executes from RAM, so the CPU does NOT stall
    while (FLASH->SR & FLASH_SR_BSY) {}

    // Clear SER + sector bits
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);

    // Check for errors
    uint32_t sr = FLASH->SR;
    if (sr & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
              FLASH_SR_PGPERR | FLASH_SR_PGSERR)) {
        return (int)sr;
    }

    // Clear EOP
    if (sr & FLASH_SR_EOP) FLASH->SR = FLASH_SR_EOP;
    return 0;
}

__attribute__((long_call, noinline, section(".RamFunc*")))
static int _flash_program_word_ram(uint32_t address, uint32_t word) {
    // Wait for any previous operation
    while (FLASH->SR & FLASH_SR_BSY) {}

    // Clear error flags
    FLASH->SR = FLASH_SR_EOP  | FLASH_SR_OPERR  | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    // Set PG + PSIZE = word (0b10)
    uint32_t cr = FLASH->CR;
    cr &= ~(FLASH_CR_PSIZE | FLASH_CR_SER | FLASH_CR_MER | FLASH_CR_SNB);
    cr |= FLASH_CR_PG | FLASH_CR_PSIZE_1;
    FLASH->CR = cr;

    // Write the word — this triggers flash programming
    *(__IO uint32_t*)address = word;
    __DSB();

    // Wait for completion from RAM
    while (FLASH->SR & FLASH_SR_BSY) {}

    // Clear PG
    FLASH->CR &= ~FLASH_CR_PG;

    // Check for errors
    uint32_t sr = FLASH->SR;
    if (sr & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
              FLASH_SR_PGPERR | FLASH_SR_PGSERR)) {
        return -1;
    }
    if (sr & FLASH_SR_EOP) FLASH->SR = FLASH_SR_EOP;
    return 0;
}

// ── RAM-resident apply (erase + copy + reset) ───────────────────────────────
// Complete firmware apply: erases destination sectors, copies firmware from
// OTA area, then resets the MCU.  This function NEVER returns.
// Must not call any flash-resident code (no HAL, no Serial, nothing).
// Fixes the original copy_flash_pages_nota bug where PG and SER were both
// set in FLASH_CR during sector erase — violating STM32F4 spec.
// ─────────────────────────────────────────────────────────────────────────────

__attribute__((long_call, noinline, section(".RamFunc*")))
static void _flash_apply_update_ram(uint32_t dest_addr,
                                    volatile const uint32_t* src,
                                    uint32_t word_count,
                                    uint32_t num_sectors) {
    // Wait for any previous flash operation
    while (FLASH->SR & FLASH_SR_BSY) {}

    // Clear all error flags (write-1-to-clear)
    FLASH->SR = FLASH_SR_EOP  | FLASH_SR_OPERR  | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    // ── Erase destination sectors ───────────────────────────────────
    // CRITICAL: PG must NOT be set during erase (STM32F4 reference manual)
    FLASH->CR &= ~(FLASH_CR_PG | FLASH_CR_MER);

    for (uint32_t sector = 0; sector < num_sectors; sector++) {
        // Set PSIZE = WORD (3.3 V), SER, and sector number
        FLASH->CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
        FLASH->CR |= FLASH_CR_PSIZE_1 | FLASH_CR_SER
                   | (sector << FLASH_CR_SNB_Pos);

        // Start erase
        FLASH->CR |= FLASH_CR_STRT;
        __DSB();

        // Poll BSY from RAM — CPU does NOT stall on single-bank flash
        while (FLASH->SR & FLASH_SR_BSY) {}

        // Clear EOP flag
        if (FLASH->SR & FLASH_SR_EOP) FLASH->SR = FLASH_SR_EOP;
    }

    // Clear SER bits
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);

    // ── Program word-by-word ────────────────────────────────────────
    // Now set PG + PSIZE = WORD (erase is done, PG is safe)
    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PG | FLASH_CR_PSIZE_1;

    volatile uint32_t* dst = (volatile uint32_t*)dest_addr;

    for (uint32_t i = 0; i < word_count; i++) {
        // Read source word — flash is idle at this point
        uint32_t w = src[i];

        // Write to destination — triggers flash programming
        dst[i] = w;
        __DSB();

        // Wait for programming to complete (from RAM)
        while (FLASH->SR & FLASH_SR_BSY) {}

        // Clear EOP
        if (FLASH->SR & FLASH_SR_EOP) FLASH->SR = FLASH_SR_EOP;
    }

    // Clear PG, lock flash
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;

    // ── Reset MCU ───────────────────────────────────────────────────
    __DSB();
    SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    for (;;) {} // never reached
}

// ─────────────────────────────────────────────────────────────────────────────

struct OTAStorage {
    uint32_t program_ota_index = 0;
    uint32_t data = 0;
    int data_idx = 0;
    bool unlocked = false;
    bool unlock() {
        HAL_StatusTypeDef status = HAL_FLASH_Unlock();
        int retries = 3;
        while (status != HAL_OK) {
            retries--;
            if (retries == 0) break;
            delay(1);
            status = HAL_FLASH_Unlock();
        }
        if (status == HAL_OK) {
            unlocked = true;
            return true;
        }
        return false;
    }
    bool lock() {
        HAL_StatusTypeDef status = HAL_FLASH_Lock();
        int retries = 3;
        while (status != HAL_OK) {
            retries--;
            if (retries == 0) break;
            delay(1);
            status = HAL_FLASH_Lock();
        }
        if (status == HAL_OK) {
            unlocked = false;
            return true;
        }
        return false;
    }

    uint32_t maxSize() {
        return program_ota_max_size;
    }

    int open(uint32_t size) {
        if (size > program_ota_max_size) return 1;

        if (!unlocked) {
            bool didUnlock = unlock();
            if (!didUnlock) {
                Serial.println("[FLASH] Unlock failed");
                Serial.flush();
                return 2;
            }
        }
        Serial.println("[FLASH] Unlocked OK");
        Serial.flush();

        program_ota_index = 0;
        data = 0;
        data_idx = 0;

        if (!erase()) return 3;
        return 0;
    }

    bool erase() {
        if (!unlocked) unlock();

        // Clear any residual error flags
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                               FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                               FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

        Serial.printf("[FLASH] FLASH->SR before erase: 0x%08lX\n", FLASH->SR);
        Serial.flush();

        for (uint32_t s = 0; s < ota_sector_count; s++) {
            uint32_t sector = ota_sector + s;
            Serial.printf("[FLASH] Erasing sector %lu (from RAM) ...\n", sector);
            Serial.flush();

            // Call the RAM-resident erase — CPU will NOT stall
            int result = _flash_erase_sector_ram(sector, FLASH_VOLTAGE_RANGE_3);

            if (result != 0) {
                Serial.printf("[FLASH] Sector %lu FAILED: SR=0x%08X\n", sector, result);
                Serial.flush();
                return false;
            }
            Serial.printf("[FLASH] Sector %lu erased OK\n", sector);
            Serial.flush();
        }
        return true;
    }

    bool write(uint8_t b) {
        if (!unlocked) unlock();
        if (data_idx == 0) data = 0;
        data |= b << (data_idx * 8);
        data_idx++;
        if (data_idx == 4) {
            int retries = 3;
            int result = 0;
            while (true) {
                // RAM-resident word program — CPU will NOT stall
                result = _flash_program_word_ram(program_ota_address + program_ota_index, data);
                if (result == 0) break;
                retries--;
                if (retries == 0) break;
            }
            if (result != 0) return false;
            program_ota_index += 4;
            data_idx = 0;
        }
        return true;
    }

    bool close() { return lock(); }

    void apply() {
        if (!unlocked) unlock();
        Serial.println("[OTA] Applying update...");
        Serial.flush();
        noInterrupts();
        // Use our RAM-resident apply instead of copy_flash_pages_nota
        // which had a PG+SER conflict bug in FLASH_CR.
        // Erases sectors 0..(ota_sector-1), programs from OTA area, resets.
        _flash_apply_update_ram(
            program_memory_address,
            (volatile const uint32_t*)program_ota_address,
            program_ota_max_size / 4,
            ota_sector
        );
        // Never reaches here — MCU resets inside the function
    }
} InternalStorage;

#endif // NOTA_EXT_FLASH_OTA