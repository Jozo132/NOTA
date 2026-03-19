#pragma once

#include <Arduino.h>
#include "stm32_flash_boot.h"

uint32_t program_memory_address = 0x08000000;
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
            IWatchdog.reload();

            // Call the RAM-resident erase — CPU will NOT stall
            int result = _flash_erase_sector_ram(sector, FLASH_VOLTAGE_RANGE_3);

            IWatchdog.reload();

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