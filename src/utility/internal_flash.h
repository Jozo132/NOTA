#pragma once

#include <Arduino.h>
#include "stm32_flash_boot.h"

uint32_t program_memory_address = 0x08000000;
uint32_t program_ota_address = 0x08040000;
uint32_t program_ota_max_size = 0x00040000;
uint32_t ota_sector = 6;
uint32_t ota_sector_count = 2;

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
            if (!didUnlock) return 2;
        }

        program_ota_index = 0;
        data = 0;
        data_idx = 0;

        if (!erase()) return 3;
        return 0;
    }

    bool erase() {
        if (!unlocked) unlock();
        FLASH_EraseInitTypeDef EraseInitStruct;
        EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
        EraseInitStruct.Sector = ota_sector;
        EraseInitStruct.NbSectors = ota_sector_count;
        EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        uint32_t pageError = 0;
        HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&EraseInitStruct, &pageError);
        int reties = 3;
        while (status != HAL_OK) {
            reties--;
            if (reties == 0) break;
            delay(1);
            status = HAL_FLASHEx_Erase(&EraseInitStruct, &pageError);
        }
        return status == HAL_OK;
    }
    bool write(uint8_t b) {
        if (!unlocked) unlock();
        if (data_idx == 0) data = 0;
        data |= b << (data_idx * 8);
        data_idx++;
        if (data_idx == 4) {
            int retries = 3;
            HAL_StatusTypeDef status = HAL_OK;
            while (true) {
                status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, program_ota_address + program_ota_index, data);
                if (status == HAL_OK) break;
                retries--;
                if (retries == 0) break;
                delay(1);
            }
            if (status != HAL_OK) return false;
            program_ota_index += 4;
            data_idx = 0;
        }
        return true;
    }

    bool close() { return lock(); }

    void apply() {
        if (!unlocked) unlock();
        noInterrupts();
        copy_flash_pages_nota(program_memory_address, (uint8_t*) program_ota_address, program_ota_max_size, true);
    }
} InternalStorage;