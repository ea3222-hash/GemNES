#include "Cartridge.h"
#include <fstream>
#include <iostream>
#include <algorithm>

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

        if (prg_banks == 0) prg_banks = 1;

        prg_memory.resize(prg_banks * 16384);
        ifs.read((char*)prg_memory.data(), prg_memory.size());

        if (chr_banks > 0) {
            chr_memory.resize(chr_banks * 8192);
            ifs.read((char*)chr_memory.data(), chr_memory.size());
        } else {
            chr_memory.resize(8192); 
        }

        prg_offsets[0] = 0; prg_offsets[1] = (prg_banks - 1) * 16384; 
        chr_offsets[0] = 0; chr_offsets[1] = 4096;
        
        for (int i = 0; i < 131072; i++) prg_ram[i] = 0x00;
        for (int i = 0; i < 1024; i++) mmc5_exram[i] = 0x00;
        for (int i = 0; i < 2048; i++) mmc5_ciram[i] = 0x00;

        if (mapper_id == 4) Update_MMC3_Offsets(); 
        if (mapper_id == 5) { Update_MMC5_PRG(); Update_MMC5_CHR(); }

        loaded = true;
    }
    ifs.close();
}

Cartridge::~Cartridge() {}
bool Cartridge::isLoaded() const { return loaded; }
bool Cartridge::irqState() const { return irq_active; }

void Cartridge::scanline() {
    if (mapper_id == 4) {
        if (irq_counter == 0 || irq_reload) { irq_counter = irq_latch; irq_reload = false; } 
        else { irq_counter--; }
        if (irq_counter == 0 && irq_enable) { irq_active = true; }
    }
    else if (mapper_id == 5) {
        mmc5_sprite_fetches = 16; // Guarantee next 16 fetches use Sprite Bank (Set A)
        if (mmc5_in_frame) {
            mmc5_irq_counter++;
            if (mmc5_irq_counter == mmc5_irq_scanline && mmc5_irq_enable) {
                irq_active = true;
            }
        }
    }
    else if (mapper_id == 69) {
        if (fme7_irq_counter_enable) {
            if (fme7_irq_counter <= 114) { if (fme7_irq_enable) irq_active = true; }
            fme7_irq_counter -= 114; 
        }
    }
    else if (mapper_id == 90) {
        if (map90_irq_enable) { map90_irq_counter--; if (map90_irq_counter == 0) irq_active = true; }
    }
}

void Cartridge::reset() {
    if (mapper_id == 1) {
        load_register = 0x00; load_count = 0x00; control_register = 0x1C;
        chr_bank_0 = 0x00; chr_bank_1 = 0x00; prg_bank = 0x00; Update_MMC1_Offsets();
    } 
    else if (mapper_id == 2) { prg_offsets[0] = 0; prg_offsets[1] = (prg_banks - 1) * 16384; }
    else if (mapper_id == 3) { chr_offsets[0] = 0; }
    else if (mapper_id == 4) {
        mmc3_target_reg = 0; mmc3_prg_mode = false; mmc3_chr_mode = false; irq_latch = 0; irq_counter = 0; irq_enable = false; irq_reload = false; irq_active = false;
        for (int i = 0; i < 8; i++) mmc3_registers[i] = 0; Update_MMC3_Offsets();
    }
    else if (mapper_id == 5) {
        mmc5_prg_mode = 3; mmc5_chr_mode = 0; mmc5_irq_enable = false; irq_active = false;
        for (int i=0; i<4; i++) mmc5_prg_banks[i] = 0xFF; // Point to Reset Vector
        mmc5_exram_mode = 0; mmc5_nt_mapping = 0; mmc5_in_frame = false;
        Update_MMC5_PRG(); Update_MMC5_CHR();
    }
    else if (mapper_id == 69) {
        fme7_command = 0; fme7_irq_counter = 0; fme7_irq_enable = false; fme7_irq_counter_enable = false;
        fme7_prg_ram_enable = false; fme7_prg_ram_rom = false; fme7_prg_ram_offset = 0;
        fme7_prg_offsets[0] = 0; fme7_prg_offsets[1] = 8192 % prg_memory.size(); fme7_prg_offsets[2] = 16384 % prg_memory.size(); fme7_prg_offsets[3] = (((prg_banks * 2) - 1) * 8192) % prg_memory.size();
        for (int i = 0; i < 8; i++) fme7_chr_offsets[i] = (i * 1024) % chr_memory.size();
    }
}

bool Cartridge::cpuRead(uint16_t addr, uint8_t& data) {
    if (addr >= 0x5000 && addr <= 0x5FFF) {
        if (mapper_id == 5) {
            if (addr == 0x5204) { 
                // VBlank Heuristic: If CPU spams $5204 without any PPU activity, we are in VBlank!
                if (mmc5_ppu_reads > 0) {
                    mmc5_in_frame = true;
                    mmc5_ppu_reads = 0;
                    mmc5_cpu_reads = 0;
                } else {
                    mmc5_cpu_reads++;
                    if (mmc5_cpu_reads > 3) { // 3 consecutive reads = ~27 silent PPU cycles
                        mmc5_in_frame = false;
                        mmc5_irq_counter = 0;
                    }
                }
                data = (irq_active ? 0x80 : 0x00) | (mmc5_in_frame ? 0x40 : 0x00); 
                irq_active = false; 
                return true; 
            }
            if (addr == 0x5205) { data = (mmc5_mult_a * mmc5_mult_b) & 0xFF; return true; }
            if (addr == 0x5206) { data = ((mmc5_mult_a * mmc5_mult_b) >> 8) & 0xFF; return true; }
            if (addr >= 0x5C00 && addr <= 0x5FFF) { data = mmc5_exram[addr - 0x5C00]; return true; }
        }
        if (mapper_id == 90) { 
            if (addr == 0x5800) { data = (map90_mul1 * map90_mul2) & 0xFF; return true; }
            if (addr == 0x5801) { data = ((map90_mul1 * map90_mul2) >> 8) & 0xFF; return true; }
        }
    }
    else if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (mapper_id == 1 || mapper_id == 4 || mapper_id == 90) { data = prg_ram[addr & 0x1FFF]; return true; }
        else if (mapper_id == 5) { data = prg_ram[(mmc5_prg_ram_bank * 8192 + (addr & 0x1FFF)) % 131072]; return true; }
        else if (mapper_id == 69) {
            if (fme7_prg_ram_enable) {
                if (fme7_prg_ram_rom) data = prg_memory[(fme7_prg_ram_offset + (addr & 0x1FFF)) % prg_memory.size()];
                else data = prg_ram[addr & 0x1FFF];
                return true;
            }
        }
        return false; 
    }
    else if (addr >= 0x8000) {
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 3 || mapper_id == 7 || mapper_id == 66) {
            if (addr <= 0xBFFF) data = prg_memory[(prg_offsets[0] + (addr & 0x3FFF)) % prg_memory.size()];
            else                data = prg_memory[(prg_offsets[1] + (addr & 0x3FFF)) % prg_memory.size()];
        } 
        else if (mapper_id == 4) { 
            uint8_t bank = (addr - 0x8000) / 0x2000; data = prg_memory[(mmc3_prg_offsets[bank] + (addr & 0x1FFF)) % prg_memory.size()];
        }
        else if (mapper_id == 5) {
            uint8_t bank = (addr - 0x8000) / 0x2000;
            if (mmc5_prg_is_ram[bank]) data = prg_ram[mmc5_prg_offsets[bank] % 131072];
            else data = prg_memory[mmc5_prg_offsets[bank] % prg_memory.size()];
        }
        else if (mapper_id == 69) {
            uint8_t bank = (addr - 0x8000) / 0x2000; data = prg_memory[(fme7_prg_offsets[bank] + (addr & 0x1FFF)) % prg_memory.size()];
        }
        else if (mapper_id == 90) {
            uint8_t bank = (addr - 0x8000) / 0x2000; data = prg_memory[(map90_prg_offsets[bank] + (addr & 0x1FFF)) % prg_memory.size()];
        }
        return true;
    }
    return false;
}

bool Cartridge::cpuWrite(uint16_t addr, uint8_t data) {
    if (mapper_id == 5 && addr == 0x2000) { mmc5_8x16_mode = (data & 0x20) != 0; return false; } // Snoop

    if (addr >= 0x5000 && addr <= 0x5FFF) {
        if (mapper_id == 5) {
            if (addr == 0x5100) { mmc5_prg_mode = data & 0x03; Update_MMC5_PRG(); return true; }
            if (addr == 0x5101) { mmc5_chr_mode = data & 0x03; Update_MMC5_CHR(); return true; }
            if (addr == 0x5102) { mmc5_ram_protect1 = data; return true; }
            if (addr == 0x5103) { mmc5_ram_protect2 = data; return true; }
            if (addr == 0x5104) { mmc5_exram_mode = data & 0x03; return true; }
            if (addr == 0x5105) { mmc5_nt_mapping = data; return true; }
            if (addr == 0x5106) { mmc5_fill_tile = data; return true; }
            if (addr == 0x5107) { mmc5_fill_color = data; return true; }
            if (addr == 0x5113) { mmc5_prg_ram_bank = data & 0x0F; return true; }
            if (addr >= 0x5114 && addr <= 0x5117) { mmc5_prg_banks[addr - 0x5114] = data; Update_MMC5_PRG(); return true; }
            if (addr >= 0x5120 && addr <= 0x5127) { mmc5_chr_banks_A[addr - 0x5120] = data | (mmc5_chr_high_bits << 8); Update_MMC5_CHR(); return true; }
            if (addr >= 0x5128 && addr <= 0x512B) { mmc5_chr_banks_B[addr - 0x5128] = data | (mmc5_chr_high_bits << 8); Update_MMC5_CHR(); return true; }
            if (addr == 0x5130) { mmc5_chr_high_bits = data & 0x03; return true; }
            if (addr == 0x5203) { mmc5_irq_scanline = data; return true; } 
            if (addr == 0x5204) { mmc5_irq_enable = (data & 0x80); return true; }
            if (addr == 0x5205) { mmc5_mult_a = data; return true; }
            if (addr == 0x5206) { mmc5_mult_b = data; return true; }
            if (addr >= 0x5C00 && addr <= 0x5FFF) { mmc5_exram[addr - 0x5C00] = data; return true; }
        }
        else if (mapper_id == 90) {
            if (addr == 0x5800) { map90_mul1 = data; return true; }
            if (addr == 0x5801) { map90_mul2 = data; return true; }
        }
    }
    else if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (mapper_id == 1 || mapper_id == 4 || mapper_id == 90) { prg_ram[addr & 0x1FFF] = data; return true; }
        else if (mapper_id == 5) {
            if (mmc5_ram_protect1 == 0x02 && mmc5_ram_protect2 == 0x01) { prg_ram[(mmc5_prg_ram_bank * 8192 + (addr & 0x1FFF)) % 131072] = data; }
            return true;
        }
        else if (mapper_id == 69) {
            if (fme7_prg_ram_enable && !fme7_prg_ram_rom) { prg_ram[addr & 0x1FFF] = data; return true; }
        }
        return false; 
    }
    else if (addr >= 0x8000) {
        if (mapper_id == 1) MMC1_Write(addr, data); 
        else if (mapper_id == 4) MMC3_Write(addr, data); 
        else if (mapper_id == 2) { prg_offsets[0] = (data & 0x0F) * 16384; }
        else if (mapper_id == 3) { chr_offsets[0] = (data & 0x03) * 8192; }
        else if (mapper_id == 5) {
            uint8_t bank = (addr - 0x8000) / 0x2000;
            if (mmc5_prg_is_ram[bank] && mmc5_ram_protect1 == 0x02 && mmc5_ram_protect2 == 0x01) { prg_ram[mmc5_prg_offsets[bank] % 131072] = data; }
            return true;
        }
        else if (mapper_id == 7) { 
            prg_offsets[0] = (data & 0x07) * 32768; prg_offsets[1] = prg_offsets[0] + 16384;
            mirror = (data & 0x10) ? ONESCREEN_HI : ONESCREEN_LO;
        }
        else if (mapper_id == 66) { 
            prg_offsets[0] = ((data >> 4) & 0x03) * 32768; prg_offsets[1] = prg_offsets[0] + 16384; chr_offsets[0] = (data & 0x03) * 8192;
        }
        return true;
    }
    return false;
}

bool Cartridge::ppuRead(uint16_t addr, uint8_t& data) {
    if (mapper_id == 5) {
        mmc5_ppu_reads++; // PPU Activity detected
        
        if (addr >= 0x2000 && addr <= 0x3EFF) {
            uint16_t reduced_addr = addr & 0x0FFF;
            uint8_t mapping = (mmc5_nt_mapping >> ((reduced_addr >> 10) * 2)) & 0x03;

            if ((reduced_addr & 0x03FF) < 0x03C0) {
                if (mmc5_exram_mode == 1) mmc5_last_exram = mmc5_exram[reduced_addr & 0x03FF];
            } else {
                if (mmc5_exram_mode == 1 && mapping != 3) {
                    uint8_t pal = (mmc5_last_exram & 0xC0) >> 6; data = pal | (pal << 2) | (pal << 4) | (pal << 6); return true;
                }
            }
            if (mapping == 0) { data = mmc5_ciram[reduced_addr & 0x03FF]; return true; }
            if (mapping == 1) { data = mmc5_ciram[0x0400 + (reduced_addr & 0x03FF)]; return true; }
            if (mapping == 2) { if (mmc5_exram_mode < 2) data = mmc5_exram[reduced_addr & 0x03FF]; return true; }
            if (mapping == 3) {
                if ((reduced_addr & 0x03FF) < 0x03C0) data = mmc5_fill_tile;
                else { uint8_t c = mmc5_fill_color & 0x03; data = c | (c << 2) | (c << 4) | (c << 6); }
                return true;
            }
        }
        else if (addr <= 0x1FFF) {
            if (mmc5_sprite_fetches > 0) {
                mmc5_sprite_fetches--;
                uint8_t bank = (addr >> 10) & 0x07;
                data = chr_memory[(mmc5_chr_offsets_A[bank] + (addr & 0x03FF)) % chr_memory.size()];
                return true;
            } else {
                if (mmc5_exram_mode == 1) {
                    uint32_t bank = (mmc5_last_exram & 0x3F) | (mmc5_chr_high_bits << 6);
                    data = chr_memory[(bank * 4096 + (addr & 0x0FFF)) % chr_memory.size()];
                    return true;
                }
                uint8_t bank = (addr >> 10) & 0x07;
                if (mmc5_8x16_mode) data = chr_memory[(mmc5_chr_offsets_B[bank & 0x03] + (addr & 0x03FF)) % chr_memory.size()];
                else data = chr_memory[(mmc5_chr_offsets_A[bank] + (addr & 0x03FF)) % chr_memory.size()];
                return true;
            }
        }
    }

    if (addr <= 0x1FFF) {
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 7) {
            if (addr < 0x1000) data = chr_memory[(chr_offsets[0] + (addr & 0x0FFF)) % chr_memory.size()];
            else               data = chr_memory[(chr_offsets[1] + (addr & 0x0FFF)) % chr_memory.size()];
        } 
        else if (mapper_id == 3) { data = chr_memory[(chr_offsets[0] + (addr & 0x1FFF)) % chr_memory.size()]; }
        else if (mapper_id == 4) { uint8_t bank = addr >> 10; data = chr_memory[(mmc3_chr_offsets[bank] + (addr & 0x03FF)) % chr_memory.size()]; }
        else if (mapper_id == 69) { uint8_t bank = addr >> 10; data = chr_memory[(fme7_chr_offsets[bank] + (addr & 0x03FF)) % chr_memory.size()]; }
        return true;
    }
    return false;
}

bool Cartridge::ppuWrite(uint16_t addr, uint8_t data) {
    if (mapper_id == 5) {
        mmc5_ppu_reads++; // PPU Activity detected
        if (addr >= 0x2000 && addr <= 0x3EFF) {
            uint16_t reduced_addr = addr & 0x0FFF;
            uint8_t mapping = (mmc5_nt_mapping >> ((reduced_addr >> 10) * 2)) & 0x03;
            if (mapping == 0) { mmc5_ciram[reduced_addr & 0x03FF] = data; return true; }
            if (mapping == 1) { mmc5_ciram[0x0400 + (reduced_addr & 0x03FF)] = data; return true; }
            if (mapping == 2) { if (mmc5_exram_mode == 0 || mmc5_exram_mode == 2) { mmc5_exram[reduced_addr & 0x03FF] = data; } return true; }
            return true;
        }
    }

    if (chr_banks == 0 && addr <= 0x1FFF) {
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 3 || mapper_id == 7) {
            if (addr < 0x1000) chr_memory[(chr_offsets[0] + (addr & 0x0FFF)) % chr_memory.size()] = data;
            else               chr_memory[(chr_offsets[1] + (addr & 0x0FFF)) % chr_memory.size()] = data;
        } 
        else if (mapper_id == 4) { uint8_t bank = addr >> 10; chr_memory[(mmc3_chr_offsets[bank] + (addr & 0x03FF)) % chr_memory.size()] = data; }
        else if (mapper_id == 5) { uint8_t bank = addr >> 10; chr_memory[(mmc5_chr_offsets_A[bank] + (addr & 0x03FF)) % chr_memory.size()] = data; }
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// MMC5 Pattern and Banking Setup
// -----------------------------------------------------------------------------

void Cartridge::Update_MMC5_PRG() {
    uint32_t prg_mask = (prg_banks * 2) - 1; if (prg_banks == 0) prg_mask = 0;
    for (int i = 0; i < 4; i++) { mmc5_prg_is_ram[i] = (mmc5_prg_banks[i] & 0x80) == 0; }

    if (mmc5_prg_mode == 3) {
        for (int i = 0; i < 4; i++) {
            if (mmc5_prg_is_ram[i]) mmc5_prg_offsets[i] = (mmc5_prg_banks[i] & 0x0F) * 8192;
            else mmc5_prg_offsets[i] = (mmc5_prg_banks[i] & 0x7F & prg_mask) * 8192;
        }
    }
    else if (mmc5_prg_mode == 2) {
        if (mmc5_prg_is_ram[1]) { mmc5_prg_offsets[0] = ((mmc5_prg_banks[1] & 0x0E)) * 8192; mmc5_prg_offsets[1] = (((mmc5_prg_banks[1] & 0x0E) | 1)) * 8192; } 
        else { mmc5_prg_offsets[0] = ((mmc5_prg_banks[1] & 0x7E) & prg_mask) * 8192; mmc5_prg_offsets[1] = (((mmc5_prg_banks[1] & 0x7E) | 1) & prg_mask) * 8192; }
        for (int i = 2; i < 4; i++) {
            if (mmc5_prg_is_ram[i]) mmc5_prg_offsets[i] = (mmc5_prg_banks[i] & 0x0F) * 8192;
            else mmc5_prg_offsets[i] = (mmc5_prg_banks[i] & 0x7F & prg_mask) * 8192;
        }
    }
    else if (mmc5_prg_mode == 1) {
        if (mmc5_prg_is_ram[1]) { mmc5_prg_offsets[0] = ((mmc5_prg_banks[1] & 0x0E)) * 8192; mmc5_prg_offsets[1] = (((mmc5_prg_banks[1] & 0x0E) | 1)) * 8192; } 
        else { mmc5_prg_offsets[0] = ((mmc5_prg_banks[1] & 0x7E) & prg_mask) * 8192; mmc5_prg_offsets[1] = (((mmc5_prg_banks[1] & 0x7E) | 1) & prg_mask) * 8192; }
        if (mmc5_prg_is_ram[3]) { mmc5_prg_offsets[2] = ((mmc5_prg_banks[3] & 0x0E)) * 8192; mmc5_prg_offsets[3] = (((mmc5_prg_banks[3] & 0x0E) | 1)) * 8192; } 
        else { mmc5_prg_offsets[2] = ((mmc5_prg_banks[3] & 0x7E) & prg_mask) * 8192; mmc5_prg_offsets[3] = (((mmc5_prg_banks[3] & 0x7E) | 1) & prg_mask) * 8192; }
    }
    else if (mmc5_prg_mode == 0) {
        if (mmc5_prg_is_ram[3]) { for (int i = 0; i < 4; i++) mmc5_prg_offsets[i] = (((mmc5_prg_banks[3] & 0x0C) | i)) * 8192; } 
        else { for (int i = 0; i < 4; i++) mmc5_prg_offsets[i] = (((mmc5_prg_banks[3] & 0x7C) | i) & prg_mask) * 8192; }
    }
}

void Cartridge::Update_MMC5_CHR() {
    uint32_t chr_mask = (chr_banks * 8) - 1; if (chr_banks == 0) chr_mask = 7; 
    auto updateSet = [&](uint32_t* offsets, uint16_t* banks, int max) {
        if (mmc5_chr_mode == 3) { for(int i=0; i<max; i++) offsets[i] = (banks[i] & chr_mask) * 1024; } 
        else if (mmc5_chr_mode == 2) { for(int i=0; i<max; i+=2) { offsets[i] = ((banks[i] & 0xFFFE) & chr_mask) * 1024; offsets[i+1] = (((banks[i] & 0xFFFE) | 1) & chr_mask) * 1024; } } 
        else if (mmc5_chr_mode == 1) { for(int i=0; i<max; i+=4) { offsets[i] = ((banks[i] & 0xFFFC) & chr_mask) * 1024; offsets[i+1] = (((banks[i] & 0xFFFC) | 1) & chr_mask) * 1024; offsets[i+2] = (((banks[i] & 0xFFFC) | 2) & chr_mask) * 1024; offsets[i+3] = (((banks[i] & 0xFFFC) | 3) & chr_mask) * 1024; } } 
        else if (mmc5_chr_mode == 0) { for(int i=0; i<max; i++) offsets[i] = (((banks[max-1] & 0xFFF8) | i) & chr_mask) * 1024; }
    };
    updateSet(mmc5_chr_offsets_A, mmc5_chr_banks_A, 8);
    updateSet(mmc5_chr_offsets_B, mmc5_chr_banks_B, 4);
}

// -----------------------------------------------------------------------------
// MMC1 and MMC3 Methods Below (Unchanged)
// -----------------------------------------------------------------------------
void Cartridge::MMC1_Write(uint16_t addr, uint8_t data) {
    if (data & 0x80) { load_register = 0x00; load_count = 0; control_register |= 0x0C; Update_MMC1_Offsets(); } 
    else { load_register >>= 1; load_register |= (data & 0x01) << 4; load_count++; if (load_count == 5) { uint8_t target_reg = (addr >> 13) & 0x03; if (target_reg == 0) control_register = load_register & 0x1F; else if (target_reg == 1) chr_bank_0 = load_register & 0x1F; else if (target_reg == 2) chr_bank_1 = load_register & 0x1F; else if (target_reg == 3) prg_bank = load_register & 0x0F; Update_MMC1_Offsets(); load_register = 0x00; load_count = 0; } }
}
void Cartridge::Update_MMC1_Offsets() {
    switch (control_register & 0x03) { case 0: mirror = ONESCREEN_LO; break; case 1: mirror = ONESCREEN_HI; break; case 2: mirror = VERTICAL; break; case 3: mirror = HORIZONTAL; break; }
    uint8_t prg_mode = (control_register >> 2) & 0x03; if (prg_mode <= 1) { prg_offsets[0] = (prg_bank & 0xFE) * 16384; prg_offsets[1] = prg_offsets[0] + 16384; } else if (prg_mode == 2) { prg_offsets[0] = 0; prg_offsets[1] = prg_bank * 16384; } else if (prg_mode == 3) { prg_offsets[0] = prg_bank * 16384; prg_offsets[1] = (prg_banks - 1) * 16384; }
    if (((control_register >> 4) & 0x01) == 0) { chr_offsets[0] = (chr_bank_0 & 0xFE) * 4096; chr_offsets[1] = chr_offsets[0] + 4096; } else { chr_offsets[0] = chr_bank_0 * 4096; chr_offsets[1] = chr_bank_1 * 4096; }
}
void Cartridge::MMC3_Write(uint16_t addr, uint8_t data) {
    if (addr >= 0x8000 && addr <= 0x9FFF) { if (!(addr & 0x0001)) { mmc3_target_reg = data & 0x07; mmc3_prg_mode = data & 0x40; mmc3_chr_mode = data & 0x80; } else { mmc3_registers[mmc3_target_reg] = data; } Update_MMC3_Offsets(); } 
    else if (addr >= 0xA000 && addr <= 0xBFFF) { if (!(addr & 0x0001)) { mirror = (data & 0x01) ? HORIZONTAL : VERTICAL; } } 
    else if (addr >= 0xC000 && addr <= 0xDFFF) { if (!(addr & 0x0001)) { irq_latch = data; } else { irq_reload = true; } } 
    else if (addr >= 0xE000 && addr <= 0xFFFF) { if (!(addr & 0x0001)) { irq_enable = false; irq_active = false; } else { irq_enable = true; } }
}
void Cartridge::Update_MMC3_Offsets() {
    uint32_t prg_mask = (prg_banks * 2) - 1; if (prg_banks == 0) prg_mask = 0; 
    if (!mmc3_prg_mode) { mmc3_prg_offsets[0] = (mmc3_registers[6] & prg_mask) * 8192; mmc3_prg_offsets[1] = (mmc3_registers[7] & prg_mask) * 8192; mmc3_prg_offsets[2] = (prg_mask - 1) * 8192; mmc3_prg_offsets[3] = prg_mask * 8192; } else { mmc3_prg_offsets[0] = (prg_mask - 1) * 8192; mmc3_prg_offsets[1] = (mmc3_registers[7] & prg_mask) * 8192; mmc3_prg_offsets[2] = (mmc3_registers[6] & prg_mask) * 8192; mmc3_prg_offsets[3] = prg_mask * 8192; }
    uint32_t chr_mask = (chr_banks * 8) - 1; if (chr_banks == 0) chr_mask = 7; 
    if (!mmc3_chr_mode) { mmc3_chr_offsets[0] = ((mmc3_registers[0] & 0xFE) & chr_mask) * 1024; mmc3_chr_offsets[1] = ((mmc3_registers[0] | 0x01) & chr_mask) * 1024; mmc3_chr_offsets[2] = ((mmc3_registers[1] & 0xFE) & chr_mask) * 1024; mmc3_chr_offsets[3] = ((mmc3_registers[1] | 0x01) & chr_mask) * 1024; mmc3_chr_offsets[4] = (mmc3_registers[2] & chr_mask) * 1024; mmc3_chr_offsets[5] = (mmc3_registers[3] & chr_mask) * 1024; mmc3_chr_offsets[6] = (mmc3_registers[4] & chr_mask) * 1024; mmc3_chr_offsets[7] = (mmc3_registers[5] & chr_mask) * 1024; } 
    else { mmc3_chr_offsets[4] = ((mmc3_registers[0] & 0xFE) & chr_mask) * 1024; mmc3_chr_offsets[5] = ((mmc3_registers[0] | 0x01) & chr_mask) * 1024; mmc3_chr_offsets[6] = ((mmc3_registers[1] & 0xFE) & chr_mask) * 1024; mmc3_chr_offsets[7] = ((mmc3_registers[1] | 0x01) & chr_mask) * 1024; mmc3_chr_offsets[0] = (mmc3_registers[2] & chr_mask) * 1024; mmc3_chr_offsets[1] = (mmc3_registers[3] & chr_mask) * 1024; mmc3_chr_offsets[2] = (mmc3_registers[4] & chr_mask) * 1024; mmc3_chr_offsets[3] = (mmc3_registers[5] & chr_mask) * 1024; }
}
