#include "Cartridge.h"
#include <fstream>
#include <iostream>

struct iNES_Header {
    char name[4];
    uint8_t prg_rom_chunks;
    uint8_t chr_rom_chunks;
    uint8_t mapper1;
    uint8_t mapper2;
    uint8_t prg_ram_size;
    uint8_t tv_system1;
    uint8_t tv_system2;
    char unused[5];
};

Cartridge::Cartridge(const std::string& fileName) {
    std::ifstream ifs(fileName, std::ios::binary);
    if (!ifs.is_open()) return;

    iNES_Header header;
    ifs.read((char*)&header, sizeof(iNES_Header));

    if (header.name[0] == 'N' && header.name[1] == 'E' && header.name[2] == 'S' && header.name[3] == 0x1A) {
        if (header.mapper1 & 0x04) ifs.seekg(512, std::ios_base::cur);

        mapper_id = ((header.mapper2 >> 4) << 4) | (header.mapper1 >> 4);
        mirror = (header.mapper1 & 0x01) ? VERTICAL : HORIZONTAL;

        prg_banks = header.prg_rom_chunks;
        chr_banks = header.chr_rom_chunks;

        // --- FIX: Prevent Modulo Zero Crashes from bad dumps! ---
        if (prg_banks == 0) {
            prg_banks = 1;
        }

        prg_memory.resize(prg_banks * 16384);
        ifs.read((char*)prg_memory.data(), prg_memory.size());

        if (chr_banks > 0) {
            chr_memory.resize(chr_banks * 8192);
            ifs.read((char*)chr_memory.data(), chr_memory.size());
        } else {
            chr_memory.resize(8192); 
        }

        // --- FIX: Map Initial Banks for Mappers 0, 1, 2, 3 ---
        prg_offsets[0] = 0; 
        prg_offsets[1] = (prg_banks - 1) * 16384; // Mapper 2 uses this
        chr_offsets[0] = 0; 
        chr_offsets[1] = 4096;
        
        for (int i = 0; i < 8192; i++) prg_ram[i] = 0x00;

        if (mapper_id == 4) Update_MMC3_Offsets(); 

        loaded = true;
    }
    ifs.close();
}

Cartridge::~Cartridge() {}
bool Cartridge::isLoaded() const { return loaded; }
bool Cartridge::irqState() const { return irq_active; }

void Cartridge::scanline() {
    if (mapper_id != 4) return;
    if (irq_counter == 0 || irq_reload) { irq_counter = irq_latch; irq_reload = false; } 
    else { irq_counter--; }
    if (irq_counter == 0 && irq_enable) { irq_active = true; }
}

void Cartridge::reset() {
    if (mapper_id == 1) {
        load_register = 0x00; load_count = 0x00; control_register = 0x1C;
        chr_bank_0 = 0x00; chr_bank_1 = 0x00; prg_bank = 0x00;
        Update_MMC1_Offsets();
    } 
    else if (mapper_id == 2) { // UxROM Reset
        prg_offsets[0] = 0;
        prg_offsets[1] = (prg_banks - 1) * 16384;
    }
    else if (mapper_id == 3) { // CNROM Reset
        chr_offsets[0] = 0;
    }
    else if (mapper_id == 4) {
        mmc3_target_reg = 0; mmc3_prg_mode = false; mmc3_chr_mode = false;
        for (int i = 0; i < 8; i++) mmc3_registers[i] = 0;
        irq_latch = 0; irq_counter = 0; irq_enable = false; irq_reload = false; irq_active = false;
        Update_MMC3_Offsets();
    }
}

bool Cartridge::cpuRead(uint16_t addr, uint8_t& data) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (mapper_id == 1 || mapper_id == 4) {
            data = prg_ram[addr & 0x1FFF]; 
            return true;
        }
        return false; 
    }
    if (addr >= 0x8000) {
        // --- FIX: Add Mappers 2 and 3 natively + Modulo Protection ---
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 3) {
            if (addr <= 0xBFFF) data = prg_memory[(prg_offsets[0] + (addr & 0x3FFF)) % prg_memory.size()];
            else                data = prg_memory[(prg_offsets[1] + (addr & 0x3FFF)) % prg_memory.size()];
        } 
        else if (mapper_id == 4) { 
            uint8_t bank = (addr - 0x8000) / 0x2000;
            data = prg_memory[(mmc3_prg_offsets[bank] + (addr & 0x1FFF)) % prg_memory.size()];
        }
        return true;
    }
    return false;
}

bool Cartridge::cpuWrite(uint16_t addr, uint8_t data) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (mapper_id == 1 || mapper_id == 4) { prg_ram[addr & 0x1FFF] = data; return true; }
        return false; 
    }
    if (addr >= 0x8000) {
        if (mapper_id == 1) MMC1_Write(addr, data); 
        else if (mapper_id == 4) MMC3_Write(addr, data); 
        else if (mapper_id == 2) { // Mapper 2: UxROM
            prg_offsets[0] = (data & 0x0F) * 16384; 
        }
        else if (mapper_id == 3) { // Mapper 3: CNROM
            chr_offsets[0] = (data & 0x03) * 8192; 
        }
        else if (mapper_id == 7) { // Mapper 7: AxROM (Battletoads, Marble Madness)
            prg_offsets[0] = (data & 0x07) * 32768;
            prg_offsets[1] = prg_offsets[0] + 16384;
            mirror = (data & 0x10) ? ONESCREEN_HI : ONESCREEN_LO;
        }
        else if (mapper_id == 66) { // Mapper 66: GxROM (SMB/Duck Hunt)
            prg_offsets[0] = ((data >> 4) & 0x03) * 32768;
            prg_offsets[1] = prg_offsets[0] + 16384;
            chr_offsets[0] = (data & 0x03) * 8192;
        }
        return true;
    }
    return false;
}

bool Cartridge::ppuRead(uint16_t addr, uint8_t& data) {
    if (addr <= 0x1FFF) {
        // --- FIX: Add Mappers 2 and 3 natively + Modulo Protection ---
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 7) {
            if (addr < 0x1000) data = chr_memory[(chr_offsets[0] + (addr & 0x0FFF)) % chr_memory.size()];
            else               data = chr_memory[(chr_offsets[1] + (addr & 0x0FFF)) % chr_memory.size()];
        } 
        else if (mapper_id == 3) {
            data = chr_memory[(chr_offsets[0] + (addr & 0x1FFF)) % chr_memory.size()];
        }
        else if (mapper_id == 4) { 
            uint8_t bank = addr >> 10; 
            data = chr_memory[(mmc3_chr_offsets[bank] + (addr & 0x03FF)) % chr_memory.size()];
        }
        return true;
    }
    return false;
}

bool Cartridge::ppuWrite(uint16_t addr, uint8_t data) {
    if (chr_banks == 0 && addr <= 0x1FFF) {
        // --- FIX: Add Mappers 2 and 3 natively + Modulo Protection ---
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 3 || mapper_id == 7) {
            if (addr < 0x1000) chr_memory[(chr_offsets[0] + (addr & 0x0FFF)) % chr_memory.size()] = data;
            else               chr_memory[(chr_offsets[1] + (addr & 0x0FFF)) % chr_memory.size()] = data;
        } 
        else if (mapper_id == 4) {
            uint8_t bank = addr >> 10; 
            chr_memory[(mmc3_chr_offsets[bank] + (addr & 0x03FF)) % chr_memory.size()] = data;
        }
        return true;
    }
    return false;
}

void Cartridge::MMC1_Write(uint16_t addr, uint8_t data) {
    if (data & 0x80) { 
        load_register = 0x00; load_count = 0; control_register |= 0x0C; Update_MMC1_Offsets();
    } else {
        load_register >>= 1; load_register |= (data & 0x01) << 4; load_count++;
        if (load_count == 5) {
            uint8_t target_reg = (addr >> 13) & 0x03;
            if (target_reg == 0) control_register = load_register & 0x1F;
            else if (target_reg == 1) chr_bank_0 = load_register & 0x1F;
            else if (target_reg == 2) chr_bank_1 = load_register & 0x1F;
            else if (target_reg == 3) prg_bank = load_register & 0x0F;
            Update_MMC1_Offsets(); load_register = 0x00; load_count = 0;
        }
    }
}
void Cartridge::Update_MMC1_Offsets() {
    switch (control_register & 0x03) { case 0: mirror = ONESCREEN_LO; break; case 1: mirror = ONESCREEN_HI; break; case 2: mirror = VERTICAL; break; case 3: mirror = HORIZONTAL; break; }
    uint8_t prg_mode = (control_register >> 2) & 0x03;
    if (prg_mode <= 1) { prg_offsets[0] = (prg_bank & 0xFE) * 16384; prg_offsets[1] = prg_offsets[0] + 16384; } 
    else if (prg_mode == 2) { prg_offsets[0] = 0; prg_offsets[1] = prg_bank * 16384; } 
    else if (prg_mode == 3) { prg_offsets[0] = prg_bank * 16384; prg_offsets[1] = (prg_banks - 1) * 16384; }
    if (((control_register >> 4) & 0x01) == 0) { chr_offsets[0] = (chr_bank_0 & 0xFE) * 4096; chr_offsets[1] = chr_offsets[0] + 4096; } 
    else { chr_offsets[0] = chr_bank_0 * 4096; chr_offsets[1] = chr_bank_1 * 4096; }
}

void Cartridge::MMC3_Write(uint16_t addr, uint8_t data) {
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        if (!(addr & 0x0001)) { mmc3_target_reg = data & 0x07; mmc3_prg_mode = data & 0x40; mmc3_chr_mode = data & 0x80; } 
        else { mmc3_registers[mmc3_target_reg] = data; }
        Update_MMC3_Offsets();
    } 
    else if (addr >= 0xA000 && addr <= 0xBFFF) {
        if (!(addr & 0x0001)) { mirror = (data & 0x01) ? HORIZONTAL : VERTICAL; }
    } 
    else if (addr >= 0xC000 && addr <= 0xDFFF) {
        if (!(addr & 0x0001)) { irq_latch = data; } 
        else { irq_reload = true; }
    } 
    else if (addr >= 0xE000 && addr <= 0xFFFF) {
        if (!(addr & 0x0001)) { irq_enable = false; irq_active = false; } 
        else { irq_enable = true; }
    }
}
void Cartridge::Update_MMC3_Offsets() {
    uint32_t prg_mask = (prg_banks * 2) - 1;
    if (prg_banks == 0) prg_mask = 0; 

    if (!mmc3_prg_mode) {
        mmc3_prg_offsets[0] = (mmc3_registers[6] & prg_mask) * 8192;
        mmc3_prg_offsets[1] = (mmc3_registers[7] & prg_mask) * 8192;
        mmc3_prg_offsets[2] = (prg_mask - 1) * 8192;
        mmc3_prg_offsets[3] = prg_mask * 8192;
    } else {
        mmc3_prg_offsets[0] = (prg_mask - 1) * 8192;
        mmc3_prg_offsets[1] = (mmc3_registers[7] & prg_mask) * 8192;
        mmc3_prg_offsets[2] = (mmc3_registers[6] & prg_mask) * 8192;
        mmc3_prg_offsets[3] = prg_mask * 8192;
    }

    uint32_t chr_mask = (chr_banks * 8) - 1;
    if (chr_banks == 0) chr_mask = 7; 

    if (!mmc3_chr_mode) {
        mmc3_chr_offsets[0] = ((mmc3_registers[0] & 0xFE) & chr_mask) * 1024;
        mmc3_chr_offsets[1] = ((mmc3_registers[0] | 0x01) & chr_mask) * 1024;
        mmc3_chr_offsets[2] = ((mmc3_registers[1] & 0xFE) & chr_mask) * 1024;
        mmc3_chr_offsets[3] = ((mmc3_registers[1] | 0x01) & chr_mask) * 1024;
        mmc3_chr_offsets[4] = (mmc3_registers[2] & chr_mask) * 1024;
        mmc3_chr_offsets[5] = (mmc3_registers[3] & chr_mask) * 1024;
        mmc3_chr_offsets[6] = (mmc3_registers[4] & chr_mask) * 1024;
        mmc3_chr_offsets[7] = (mmc3_registers[5] & chr_mask) * 1024;
    } else {
        mmc3_chr_offsets[4] = ((mmc3_registers[0] & 0xFE) & chr_mask) * 1024;
        mmc3_chr_offsets[5] = ((mmc3_registers[0] | 0x01) & chr_mask) * 1024;
        mmc3_chr_offsets[6] = ((mmc3_registers[1] & 0xFE) & chr_mask) * 1024;
        mmc3_chr_offsets[7] = ((mmc3_registers[1] | 0x01) & chr_mask) * 1024;
        mmc3_chr_offsets[0] = (mmc3_registers[2] & chr_mask) * 1024;
        mmc3_chr_offsets[1] = (mmc3_registers[3] & chr_mask) * 1024;
        mmc3_chr_offsets[2] = (mmc3_registers[4] & chr_mask) * 1024;
        mmc3_chr_offsets[3] = (mmc3_registers[5] & chr_mask) * 1024;
    }
}
