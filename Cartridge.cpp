#include "Cartridge.h"
#include <fstream>
#include <iostream>
#include <algorithm>

const uint8_t mmc5_length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

struct iNES_Header { char name[4]; uint8_t prg_rom_chunks; uint8_t chr_rom_chunks; uint8_t mapper1; uint8_t mapper2; uint8_t prg_ram_size; uint8_t tv_system1; uint8_t tv_system2; char unused[5]; };

Cartridge::Cartridge(const std::string& fileName) {
    std::ifstream ifs(fileName, std::ios::binary);
    if (!ifs.is_open()) return;
    iNES_Header header;
    ifs.read((char*)&header, sizeof(iNES_Header));

    if (header.name[0] == 'N' && header.name[1] == 'E' && header.name[2] == 'S' && header.name[3] == 0x1A) {
        if (header.mapper1 & 0x04) ifs.seekg(512, std::ios_base::cur);

        is_nes20 = ((header.tv_system1 & 0x0C) == 0x08);
        mapper_id = ((header.mapper2 >> 4) << 4) | (header.mapper1 >> 4);
        mirror = (header.mapper1 & 0x01) ? VERTICAL : HORIZONTAL;

        prg_banks = header.prg_rom_chunks;
        chr_banks = header.chr_rom_chunks;

        if (is_nes20) {
            mapper_id |= (header.prg_ram_size & 0x0F) << 8;
            submapper_id = (header.prg_ram_size >> 4);
            prg_banks |= ((header.tv_system2 & 0x0F) << 8);
            chr_banks |= ((header.tv_system2 & 0xF0) << 4);
        }

        if (prg_banks == 0) prg_banks = 1;
        prg_memory.resize(prg_banks * 16384);
        ifs.read((char*)prg_memory.data(), prg_memory.size());

        if (chr_banks > 0) { chr_memory.resize(chr_banks * 8192); ifs.read((char*)chr_memory.data(), chr_memory.size()); } 
        else { chr_memory.resize(8192); }

        prg_offsets[0] = 0; prg_offsets[1] = (prg_banks - 1) * 16384; 
        chr_offsets[0] = 0; chr_offsets[1] = 4096;
        
        for (int i = 0; i < 131072; i++) prg_ram[i] = 0x00;
        for (int i = 0; i < 1024; i++) mmc5_exram[i] = 0x00;
        for (int i = 0; i < 2048; i++) mmc5_ciram[i] = 0x00;

        if (mapper_id == 4) Update_MMC3_Offsets(); 
        if (mapper_id == 5) reset();
        loaded = true;
    }
    ifs.close();
}

std::string Cartridge::getROMInfo() const {
    std::string info = "Format: " + std::string(is_nes20 ? "NES 2.0" : "iNES 1.0") + "\n";
    info += "Mapper ID: " + std::to_string(mapper_id) + "\n";
    info += "PRG ROM: " + std::to_string(prg_banks * 16) + " KB\n";
    if (chr_banks > 0) info += "CHR ROM: " + std::to_string(chr_banks * 8) + " KB\n";
    else info += "CHR RAM: 8 KB\n";
    return info;
}

Cartridge::~Cartridge() {}
bool Cartridge::isLoaded() const { return loaded; }
bool Cartridge::irqState() const { return irq_active; }

void Cartridge::SaveSRAM(const std::string& path) { std::ofstream ofs(path, std::ios::binary); if (ofs.is_open()) { ofs.write((char*)prg_ram, sizeof(prg_ram)); ofs.close(); } }
void Cartridge::LoadSRAM(const std::string& path) { std::ifstream ifs(path, std::ios::binary); if (ifs.is_open()) { ifs.read((char*)prg_ram, sizeof(prg_ram)); ifs.close(); } }
void Cartridge::ppuCtrlWrite(uint8_t data) { if (mapper_id == 5) mmc5_8x16_mode = (data & 0x20) != 0; }

void Cartridge::stepAudio(int cycles) {
    if (mapper_id != 5) return;
    for (int i = 0; i < cycles; i++) {
        mmc5_audio_divider++;
        if (mmc5_audio_divider % 2 == 0) {
            if (mmc5_p1.timer > 0) mmc5_p1.timer--; else { mmc5_p1.timer = mmc5_p1.timer_reload; mmc5_p1.duty_pos = (mmc5_p1.duty_pos + 1) % 8; }
            if (mmc5_p2.timer > 0) mmc5_p2.timer--; else { mmc5_p2.timer = mmc5_p2.timer_reload; mmc5_p2.duty_pos = (mmc5_p2.duty_pos + 1) % 8; }
        }
    }
}

double Cartridge::getAudioSample() {
    if (mapper_id != 5) return 0.0;
    static const uint8_t duty_table[4][8] = { {0,1,0,0,0,0,0,0}, {0,1,1,0,0,0,0,0}, {0,1,1,1,1,0,0,0}, {1,0,0,1,1,1,1,1} };
    double p1 = (mmc5_p1.enable && mmc5_p1.length > 0 && mmc5_p1.timer_reload > 8 && duty_table[mmc5_p1.duty][mmc5_p1.duty_pos]) ? mmc5_p1.volume : 0;
    double p2 = (mmc5_p2.enable && mmc5_p2.length > 0 && mmc5_p2.timer_reload > 8 && duty_table[mmc5_p2.duty][mmc5_p2.duty_pos]) ? mmc5_p2.volume : 0;
    return ((p1 + p2) * 0.005) + ((mmc5_pcm - 128.0) * 0.0005);
}

void Cartridge::scanline() {
    if (mapper_id == 4) {
        if (irq_counter == 0 || irq_reload) { irq_counter = irq_latch; irq_reload = false; } 
        else { irq_counter--; }
        if (irq_counter == 0 && irq_enable) { irq_active = true; }
    }
    else if (mapper_id == 5) {
        if (mmc5_ppu_in_frame) {
            mmc5_scanline_counter++;
            if (mmc5_scanline_counter == mmc5_irq_scanline) {
                mmc5_irq_pending = true;
                if (mmc5_irq_enable) irq_active = true;
            }
        }
        if (mmc5_scanline_counter % 60 == 0) {
            if (mmc5_p1.length > 0 && !mmc5_p1.halt) mmc5_p1.length--;
            if (mmc5_p2.length > 0 && !mmc5_p2.halt) mmc5_p2.length--;
        }
    }
    else if (mapper_id == 69) {
        if (fme7_irq_counter_enable) { if (fme7_irq_counter <= 114) { if (fme7_irq_enable) irq_active = true; } fme7_irq_counter -= 114; }
    }
    else if (mapper_id == 90) { if (map90_irq_enable) { map90_irq_counter--; if (map90_irq_counter == 0) irq_active = true; } }
}

void Cartridge::reset() {
    if (mapper_id == 1) { load_register = 0x00; load_count = 0x00; control_register = 0x1C; chr_bank_0 = 0x00; chr_bank_1 = 0x00; prg_bank = 0x00; Update_MMC1_Offsets(); } 
    else if (mapper_id == 2) { prg_offsets[0] = 0; prg_offsets[1] = (prg_banks - 1) * 16384; }
    else if (mapper_id == 3) { chr_offsets[0] = 0; }
    else if (mapper_id == 4) { mmc3_target_reg = 0; mmc3_prg_mode = false; mmc3_chr_mode = false; irq_latch = 0; irq_counter = 0; irq_enable = false; irq_reload = false; irq_active = false; for (int i = 0; i < 8; i++) mmc3_registers[i] = 0; Update_MMC3_Offsets(); }
    else if (mapper_id == 5) {
        mmc5_prg_mode = 3; mmc5_chr_mode = 0; mmc5_exram_mode = 0; mmc5_nt_mapping = 0; mmc5_prg_protect1 = 0; mmc5_prg_protect2 = 0; mmc5_chr_upper = 0; mmc5_last_chr_reg = 0; mmc5_irq_enable = false; irq_active = false; mmc5_irq_pending = false; mmc5_ppu_in_frame = false; mmc5_last_exram = 0;
        mmc5_p1.enable = false; mmc5_p2.enable = false; mmc5_pcm = 128;
        for (int i=0; i<4; i++) mmc5_prg_banks[i] = 0; mmc5_prg_banks[4] = 0xFF; for (int i=0; i<12; i++) mmc5_chr_banks[i] = 0;
    }
    else if (mapper_id == 69) { fme7_command = 0; fme7_irq_counter = 0; fme7_irq_enable = false; fme7_irq_counter_enable = false; fme7_prg_ram_enable = false; fme7_prg_ram_rom = false; fme7_prg_ram_offset = 0; fme7_prg_offsets[0] = 0; fme7_prg_offsets[1] = 8192 % prg_memory.size(); fme7_prg_offsets[2] = 16384 % prg_memory.size(); fme7_prg_offsets[3] = (((prg_banks * 2) - 1) * 8192) % prg_memory.size(); for (int i = 0; i < 8; i++) fme7_chr_offsets[i] = (i * 1024) % chr_memory.size(); }
}

uint32_t Cartridge::GetPrgRomAddr(int bank8k, int offset) { if (prg_memory.empty()) return 0; return ((bank8k * 8192) + offset) % prg_memory.size(); }
uint32_t Cartridge::GetPrgRamAddr(int bank, int offset) { return (((bank & 0x07) * 8192) + offset) % sizeof(prg_ram); }
bool Cartridge::IsPrgRamWritable() { return mmc5_prg_protect1 == 0x02 && mmc5_prg_protect2 == 0x01; }

bool Cartridge::cpuRead(uint16_t addr, uint8_t& data) {
    if (mapper_id == 5) {
        if (addr >= 0xFFFA && addr <= 0xFFFB) { mmc5_ppu_in_frame = false; mmc5_scanline_counter = 0; mmc5_irq_pending = false; irq_active = false; }
        if (mmc5_ppu_idle_counter > 0) { mmc5_ppu_idle_counter--; if (mmc5_ppu_idle_counter == 0) { mmc5_ppu_in_frame = false; } }
    }
    if (addr >= 0x5000 && addr <= 0x5FFF) {
        if (mapper_id == 5) {
            if (addr == 0x5015) { data = (mmc5_p1.length > 0 ? 1 : 0) | (mmc5_p2.length > 0 ? 2 : 0); return true; }
            if (addr == 0x5204) { data = (mmc5_irq_pending ? 0x80 : 0x00) | (mmc5_ppu_in_frame ? 0x40 : 0x00); mmc5_irq_pending = false; irq_active = false; return true; }
            if (addr == 0x5205) { data = (mmc5_mult_a * mmc5_mult_b) & 0xFF; return true; }
            if (addr == 0x5206) { data = ((mmc5_mult_a * mmc5_mult_b) >> 8) & 0xFF; return true; }
            if (addr >= 0x5C00 && addr <= 0x5FFF) { 
                if (mmc5_exram_mode <= 1) {
                    data = mmc5_ppu_in_frame ? 0x00 : mmc5_exram[addr - 0x5C00];
                } else {
                    data = mmc5_exram[addr - 0x5C00];
                }
                return true; 
            }
        }
        if (mapper_id == 90) { if (addr == 0x5800) { data = (map90_mul1 * map90_mul2) & 0xFF; return true; } if (addr == 0x5801) { data = ((map90_mul1 * map90_mul2) >> 8) & 0xFF; return true; } }
    }
    else if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (mapper_id == 1 || mapper_id == 4 || mapper_id == 90) { data = prg_ram[addr & 0x1FFF]; return true; }
        else if (mapper_id == 5) { data = prg_ram[GetPrgRamAddr(mmc5_prg_banks[0], addr & 0x1FFF)]; return true; }
        else if (mapper_id == 69) { if (fme7_prg_ram_enable) { if (fme7_prg_ram_rom) data = prg_memory[(fme7_prg_ram_offset + (addr & 0x1FFF)) % prg_memory.size()]; else data = prg_ram[addr & 0x1FFF]; return true; } }
        return false; 
    }
    else if (addr >= 0x8000) {
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 3 || mapper_id == 7 || mapper_id == 66) { if (addr <= 0xBFFF) data = prg_memory[(prg_offsets[0] + (addr & 0x3FFF)) % prg_memory.size()]; else data = prg_memory[(prg_offsets[1] + (addr & 0x3FFF)) % prg_memory.size()]; } 
        else if (mapper_id == 4) { uint8_t bank = (addr - 0x8000) / 0x2000; data = prg_memory[(mmc3_prg_offsets[bank] + (addr & 0x1FFF)) % prg_memory.size()]; }
        else if (mapper_id == 5) {
            int offset;
            switch(mmc5_prg_mode) {
                case 0: offset = addr - 0x8000; data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[4] & 0x7C, offset)]; break;
                case 1: if (addr < 0xC000) { offset = addr - 0x8000; if ((mmc5_prg_banks[2] & 0x80) == 0) data = prg_ram[GetPrgRamAddr(mmc5_prg_banks[2], offset & 0x3FFF)]; else data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[2] & 0x7E, offset)]; } else { offset = addr - 0xC000; data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[4] & 0x7E, offset)]; } break;
                case 2: if (addr < 0xC000) { offset = addr - 0x8000; if ((mmc5_prg_banks[2] & 0x80) == 0) data = prg_ram[GetPrgRamAddr(mmc5_prg_banks[2], offset & 0x3FFF)]; else data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[2] & 0x7E, offset)]; } else if (addr < 0xE000) { offset = addr - 0xC000; if ((mmc5_prg_banks[3] & 0x80) == 0) data = prg_ram[GetPrgRamAddr(mmc5_prg_banks[3], offset)]; else data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[3] & 0x7F, offset)]; } else { offset = addr - 0xE000; data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[4] & 0x7F, offset)]; } break;
                case 3: if (addr < 0xA000) { offset = addr - 0x8000; if ((mmc5_prg_banks[1] & 0x80) == 0) data = prg_ram[GetPrgRamAddr(mmc5_prg_banks[1], offset)]; else data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[1] & 0x7F, offset)]; } else if (addr < 0xC000) { offset = addr - 0xA000; if ((mmc5_prg_banks[2] & 0x80) == 0) data = prg_ram[GetPrgRamAddr(mmc5_prg_banks[2], offset)]; else data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[2] & 0x7F, offset)]; } else if (addr < 0xE000) { offset = addr - 0xC000; if ((mmc5_prg_banks[3] & 0x80) == 0) data = prg_ram[GetPrgRamAddr(mmc5_prg_banks[3], offset)]; else data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[3] & 0x7F, offset)]; } else { offset = addr - 0xE000; data = prg_memory[GetPrgRomAddr(mmc5_prg_banks[4] & 0x7F, offset)]; } break;
            }
        }
        else if (mapper_id == 69) { uint8_t bank = (addr - 0x8000) / 0x2000; data = prg_memory[(fme7_prg_offsets[bank] + (addr & 0x1FFF)) % prg_memory.size()]; }
        else if (mapper_id == 90) { uint8_t bank = (addr - 0x8000) / 0x2000; data = prg_memory[(map90_prg_offsets[bank] + (addr & 0x1FFF)) % prg_memory.size()]; }
        return true;
    }
    return false;
}

bool Cartridge::cpuWrite(uint16_t addr, uint8_t data) {
    if (mapper_id == 5) {
        if (mmc5_ppu_idle_counter > 0) { mmc5_ppu_idle_counter--; if (mmc5_ppu_idle_counter == 0) { mmc5_ppu_in_frame = false; } }
    }

    if (addr >= 0x5000 && addr <= 0x5FFF) {
        if (mapper_id == 5) {
            if (addr == 0x5000) { mmc5_p1.duty = (data >> 6) & 3; mmc5_p1.halt = data & 0x20; mmc5_p1.volume = data & 0x0F; return true; }
            if (addr == 0x5002) { mmc5_p1.timer_reload = (mmc5_p1.timer_reload & 0xFF00) | data; return true; }
            if (addr == 0x5003) { mmc5_p1.timer_reload = (mmc5_p1.timer_reload & 0x00FF) | ((data & 0x07) << 8); mmc5_p1.timer = mmc5_p1.timer_reload; mmc5_p1.length = mmc5_length_table[(data >> 3) & 0x1F]; return true; }
            if (addr == 0x5004) { mmc5_p2.duty = (data >> 6) & 3; mmc5_p2.halt = data & 0x20; mmc5_p2.volume = data & 0x0F; return true; }
            if (addr == 0x5006) { mmc5_p2.timer_reload = (mmc5_p2.timer_reload & 0xFF00) | data; return true; }
            if (addr == 0x5007) { mmc5_p2.timer_reload = (mmc5_p2.timer_reload & 0x00FF) | ((data & 0x07) << 8); mmc5_p2.timer = mmc5_p2.timer_reload; mmc5_p2.length = mmc5_length_table[(data >> 3) & 0x1F]; return true; }
            if (addr == 0x5011) { mmc5_pcm = data; return true; }
            if (addr == 0x5015) { mmc5_p1.enable = data & 0x01; mmc5_p2.enable = data & 0x02; return true; }

            if (addr == 0x5100) { mmc5_prg_mode = data & 0x03; return true; }
            if (addr == 0x5101) { mmc5_chr_mode = data & 0x03; return true; }
            if (addr == 0x5102) { mmc5_prg_protect1 = data & 0x03; return true; }
            if (addr == 0x5103) { mmc5_prg_protect2 = data & 0x03; return true; }
            if (addr == 0x5104) { mmc5_exram_mode = data & 0x03; return true; }
            if (addr == 0x5105) { mmc5_nt_mapping = data; return true; }
            if (addr == 0x5106) { mmc5_fill_tile = data; return true; }
            if (addr == 0x5107) { mmc5_fill_color = data & 0x03; return true; }
            
            if (addr >= 0x5113 && addr <= 0x5117) { mmc5_prg_banks[addr - 0x5113] = data; return true; }
            if (addr >= 0x5120 && addr <= 0x5127) { mmc5_chr_banks[addr - 0x5120] = data | (mmc5_chr_upper << 8); mmc5_last_chr_reg = addr; return true; }
            if (addr >= 0x5128 && addr <= 0x512B) { mmc5_chr_banks[8 + (addr - 0x5128)] = data | (mmc5_chr_upper << 8); mmc5_last_chr_reg = addr; return true; }
            if (addr == 0x5130) { mmc5_chr_upper = data & 0x03; return true; }
            
            if (addr == 0x5203) { mmc5_irq_scanline = data; return true; } 
            if (addr == 0x5204) { mmc5_irq_enable = (data & 0x80) != 0; if (!mmc5_irq_enable) irq_active = false; else if (mmc5_irq_pending) irq_active = true; return true; }
            if (addr == 0x5205) { mmc5_mult_a = data; return true; }
            if (addr == 0x5206) { mmc5_mult_b = data; return true; }
            
            if (addr >= 0x5C00 && addr <= 0x5FFF) { 
                if (mmc5_exram_mode <= 1) {
                    mmc5_exram[addr - 0x5C00] = mmc5_ppu_in_frame ? 0x00 : data;
                } else {
                    mmc5_exram[addr - 0x5C00] = data;
                }
                return true; 
            }
        }
        else if (mapper_id == 90) { if (addr == 0x5800) { map90_mul1 = data; return true; } if (addr == 0x5801) { map90_mul2 = data; return true; } }
    }
    else if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (mapper_id == 1 || mapper_id == 4 || mapper_id == 90) { prg_ram[addr & 0x1FFF] = data; return true; }
        else if (mapper_id == 5) { if (IsPrgRamWritable()) { prg_ram[GetPrgRamAddr(mmc5_prg_banks[0], addr & 0x1FFF)] = data; } return true; }
        else if (mapper_id == 69) { if (fme7_prg_ram_enable && !fme7_prg_ram_rom) { prg_ram[addr & 0x1FFF] = data; return true; } }
        return false; 
    }
    else if (addr >= 0x8000) {
        // --- WORSTNES: PRG ROM OVERWRITE TRAP ---
        // Allows direct writing to ROM memory, failing the ROM writable test!
        if (worst_nes && !prg_memory.empty()) {
            if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 3 || mapper_id == 7 || mapper_id == 66) { 
                if (addr <= 0xBFFF) prg_memory[(prg_offsets[0] + (addr & 0x3FFF)) % prg_memory.size()] = data;
                else prg_memory[(prg_offsets[1] + (addr & 0x3FFF)) % prg_memory.size()] = data;
            } 
            else if (mapper_id == 4) { 
                uint8_t bank = (addr - 0x8000) / 0x2000; 
                prg_memory[(mmc3_prg_offsets[bank] + (addr & 0x1FFF)) % prg_memory.size()] = data;
            } 
        }

        if (mapper_id == 1) MMC1_Write(addr, data); 
        else if (mapper_id == 4) MMC3_Write(addr, data); 
        else if (mapper_id == 2) { prg_offsets[0] = (data & 0x0F) * 16384; }
        else if (mapper_id == 3) { chr_offsets[0] = (data & 0x03) * 8192; }
        else if (mapper_id == 5) {
            int offset;
            switch(mmc5_prg_mode) {
                case 0: break;
                case 1: if (addr < 0xC000 && (mmc5_prg_banks[2] & 0x80) == 0 && IsPrgRamWritable()) { prg_ram[GetPrgRamAddr(mmc5_prg_banks[2], (addr - 0x8000) & 0x3FFF)] = data; } break;
                case 2: if (addr < 0xC000 && (mmc5_prg_banks[2] & 0x80) == 0 && IsPrgRamWritable()) { prg_ram[GetPrgRamAddr(mmc5_prg_banks[2], (addr - 0x8000) & 0x3FFF)] = data; } else if (addr >= 0xC000 && addr < 0xE000 && (mmc5_prg_banks[3] & 0x80) == 0 && IsPrgRamWritable()) { prg_ram[GetPrgRamAddr(mmc5_prg_banks[3], addr - 0xC000)] = data; } break;
                case 3: if (addr < 0xA000 && (mmc5_prg_banks[1] & 0x80) == 0 && IsPrgRamWritable()) { prg_ram[GetPrgRamAddr(mmc5_prg_banks[1], addr - 0x8000)] = data; } else if (addr >= 0xA000 && addr < 0xC000 && (mmc5_prg_banks[2] & 0x80) == 0 && IsPrgRamWritable()) { prg_ram[GetPrgRamAddr(mmc5_prg_banks[2], addr - 0xA000)] = data; } else if (addr >= 0xC000 && addr < 0xE000 && (mmc5_prg_banks[3] & 0x80) == 0 && IsPrgRamWritable()) { prg_ram[GetPrgRamAddr(mmc5_prg_banks[3], addr - 0xC000)] = data; } break;
            }
            return true;
        }
        else if (mapper_id == 7) { prg_offsets[0] = (data & 0x07) * 32768; prg_offsets[1] = prg_offsets[0] + 16384; mirror = (data & 0x10) ? ONESCREEN_HI : ONESCREEN_LO; }
        else if (mapper_id == 66) { prg_offsets[0] = ((data >> 4) & 0x03) * 32768; prg_offsets[1] = prg_offsets[0] + 16384; chr_offsets[0] = (data & 0x03) * 8192; }
        return true;
    }
    return false;
}

bool Cartridge::ppuRead(uint16_t addr, uint8_t& data, bool is_sprite) {
    if (mapper_id == 5) {
        mmc5_ppu_idle_counter = 3; 
        if (!mmc5_ppu_in_frame) {
            mmc5_ppu_in_frame = true;
            mmc5_scanline_counter = -1; 
        }

        if (addr >= 0x2000 && addr <= 0x3EFF) {
            uint16_t reduced = addr & 0x0FFF; int nt = 0;
            if (reduced < 0x0400) nt = mmc5_nt_mapping & 3; else if (reduced < 0x0800) nt = (mmc5_nt_mapping >> 2) & 3; else if (reduced < 0x0C00) nt = (mmc5_nt_mapping >> 4) & 3; else nt = (mmc5_nt_mapping >> 6) & 3;
            uint16_t nt_idx = reduced & 0x03FF;
            bool is_attribute = nt_idx >= 0x03C0;

            if (is_attribute) {
                if (mmc5_exram_mode == 1 && nt != 3) { uint8_t pal = (mmc5_last_exram & 0xC0) >> 6; data = pal | (pal << 2) | (pal << 4) | (pal << 6); return true; }
            } else {
                if (mmc5_exram_mode == 1) mmc5_last_exram = mmc5_exram[nt_idx];
            }

            if (nt == 0) data = mmc5_ciram[nt_idx]; else if (nt == 1) data = mmc5_ciram[0x0400 + nt_idx]; else if (nt == 2) { if (mmc5_exram_mode < 2) data = mmc5_exram[nt_idx]; else data = 0x00; } else if (nt == 3) { if (is_attribute) { uint8_t c = mmc5_fill_color & 3; data = c | (c << 2) | (c << 4) | (c << 6); } else { data = mmc5_fill_tile; } }
            return true;
        }

        if (addr < 0x2000) {
            if (chr_banks > 0) {
                if (mmc5_exram_mode == 1 && !is_sprite) {
                    uint32_t bank = (mmc5_last_exram & 0x3F) | (mmc5_chr_upper << 6);
                    data = chr_memory[((bank * 4096) + (addr & 0x0FFF)) % chr_memory.size()];
                } else {
                    bool use_a = !mmc5_8x16_mode || is_sprite || (!mmc5_ppu_in_frame && mmc5_last_chr_reg <= 0x5127);
                    int bank = 0;
                    if (mmc5_chr_mode == 0) { bank = use_a ? mmc5_chr_banks[7] : mmc5_chr_banks[11]; data = chr_memory[((bank * 8192) + (addr & 0x1FFF)) % chr_memory.size()]; } 
                    else if (mmc5_chr_mode == 1) { bank = use_a ? mmc5_chr_banks[(addr < 0x1000) ? 3 : 7] : mmc5_chr_banks[11]; data = chr_memory[((bank * 4096) + (addr & 0x0FFF)) % chr_memory.size()]; } 
                    else if (mmc5_chr_mode == 2) { if (use_a) { bank = mmc5_chr_banks[((addr >> 11) & 0x03) * 2 + 1]; } else { bank = mmc5_chr_banks[((addr >> 11) & 0x02) * 2 + 9]; } data = chr_memory[((bank * 2048) + (addr & 0x07FF)) % chr_memory.size()]; } 
                    else { if (use_a) bank = mmc5_chr_banks[(addr >> 10) & 0x07]; else bank = mmc5_chr_banks[8 + ((addr >> 10) & 0x03)]; data = chr_memory[((bank * 1024) + (addr & 0x03FF)) % chr_memory.size()]; }
                }
            } else { data = chr_memory[addr & 0x1FFF]; }
            return true;
        }
    }

    if (addr <= 0x1FFF) {
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 7) { if (addr < 0x1000) data = chr_memory[(chr_offsets[0] + (addr & 0x0FFF)) % chr_memory.size()]; else data = chr_memory[(chr_offsets[1] + (addr & 0x0FFF)) % chr_memory.size()]; } 
        else if (mapper_id == 3) { data = chr_memory[(chr_offsets[0] + (addr & 0x1FFF)) % chr_memory.size()]; } else if (mapper_id == 4) { uint8_t bank = addr >> 10; data = chr_memory[(mmc3_chr_offsets[bank] + (addr & 0x03FF)) % chr_memory.size()]; } else if (mapper_id == 69) { uint8_t bank = addr >> 10; data = chr_memory[(fme7_chr_offsets[bank] + (addr & 0x03FF)) % chr_memory.size()]; } return true;
    }
    return false;
}

bool Cartridge::ppuWrite(uint16_t addr, uint8_t data) {
    if (mapper_id == 5) {
        if (addr >= 0x2000 && addr <= 0x3EFF) {
            uint16_t reduced = addr & 0x0FFF; int nt = 0;
            if (reduced < 0x0400) nt = mmc5_nt_mapping & 3; else if (reduced < 0x0800) nt = (mmc5_nt_mapping >> 2) & 3; else if (reduced < 0x0C00) nt = (mmc5_nt_mapping >> 4) & 3; else nt = (mmc5_nt_mapping >> 6) & 3;
            uint16_t nt_idx = reduced & 0x03FF;
            if (nt == 0) mmc5_ciram[nt_idx] = data; else if (nt == 1) mmc5_ciram[0x0400 + nt_idx] = data; else if (nt == 2) { if (mmc5_exram_mode == 0 || mmc5_exram_mode == 2) mmc5_exram[nt_idx] = data; }
            return true;
        }
    }
    
    // WORSTNES DIABOLICAL TRAP
    // If WorstNES is on, we let CHR ROM writes pass perfectly fine. Test ROMs will hate this.
    if ((worst_nes || chr_banks == 0) && addr <= 0x1FFF) {
        if (mapper_id == 0 || mapper_id == 1 || mapper_id == 2 || mapper_id == 3 || mapper_id == 7 || mapper_id == 5) { if (addr < 0x1000) chr_memory[(chr_offsets[0] + (addr & 0x0FFF)) % chr_memory.size()] = data; else chr_memory[(chr_offsets[1] + (addr & 0x0FFF)) % chr_memory.size()] = data; } 
        else if (mapper_id == 4) { uint8_t bank = addr >> 10; chr_memory[(mmc3_chr_offsets[bank] + (addr & 0x03FF)) % chr_memory.size()] = data; } return true;
    }
    return false;
}

void Cartridge::MMC1_Write(uint16_t addr, uint8_t data) { if (data & 0x80) { load_register = 0x00; load_count = 0; control_register |= 0x0C; Update_MMC1_Offsets(); } else { load_register >>= 1; load_register |= (data & 0x01) << 4; load_count++; if (load_count == 5) { uint8_t target_reg = (addr >> 13) & 0x03; if (target_reg == 0) control_register = load_register & 0x1F; else if (target_reg == 1) chr_bank_0 = load_register & 0x1F; else if (target_reg == 2) chr_bank_1 = load_register & 0x1F; else if (target_reg == 3) prg_bank = load_register & 0x0F; Update_MMC1_Offsets(); load_register = 0x00; load_count = 0; } } }
void Cartridge::Update_MMC1_Offsets() { switch (control_register & 0x03) { case 0: mirror = ONESCREEN_LO; break; case 1: mirror = ONESCREEN_HI; break; case 2: mirror = VERTICAL; break; case 3: mirror = HORIZONTAL; break; } uint8_t prg_mode = (control_register >> 2) & 0x03; if (prg_mode <= 1) { prg_offsets[0] = (prg_bank & 0xFE) * 16384; prg_offsets[1] = prg_offsets[0] + 16384; } else if (prg_mode == 2) { prg_offsets[0] = 0; prg_offsets[1] = prg_bank * 16384; } else if (prg_mode == 3) { prg_offsets[0] = prg_bank * 16384; prg_offsets[1] = (prg_banks - 1) * 16384; } if (((control_register >> 4) & 0x01) == 0) { chr_offsets[0] = (chr_bank_0 & 0xFE) * 4096; chr_offsets[1] = chr_offsets[0] + 4096; } else { chr_offsets[0] = chr_bank_0 * 4096; chr_offsets[1] = chr_bank_1 * 4096; } }
void Cartridge::MMC3_Write(uint16_t addr, uint8_t data) { if (addr >= 0x8000 && addr <= 0x9FFF) { if (!(addr & 0x0001)) { mmc3_target_reg = data & 0x07; mmc3_prg_mode = data & 0x40; mmc3_chr_mode = data & 0x80; } else { mmc3_registers[mmc3_target_reg] = data; } Update_MMC3_Offsets(); } else if (addr >= 0xA000 && addr <= 0xBFFF) { if (!(addr & 0x0001)) { mirror = (data & 0x01) ? HORIZONTAL : VERTICAL; } } else if (addr >= 0xC000 && addr <= 0xDFFF) { if (!(addr & 0x0001)) { irq_latch = data; } else { irq_reload = true; } } else if (addr >= 0xE000 && addr <= 0xFFFF) { if (!(addr & 0x0001)) { irq_enable = false; irq_active = false; } else { irq_enable = true; } } }
void Cartridge::Update_MMC3_Offsets() { uint32_t prg_mask = (prg_banks * 2) - 1; if (prg_banks == 0) prg_mask = 0; if (!mmc3_prg_mode) { mmc3_prg_offsets[0] = (mmc3_registers[6] & prg_mask) * 8192; mmc3_prg_offsets[1] = (mmc3_registers[7] & prg_mask) * 8192; mmc3_prg_offsets[2] = (prg_mask - 1) * 8192; mmc3_prg_offsets[3] = prg_mask * 8192; } else { mmc3_prg_offsets[0] = (prg_mask - 1) * 8192; mmc3_prg_offsets[1] = (mmc3_registers[7] & prg_mask) * 8192; mmc3_prg_offsets[2] = (mmc3_registers[6] & prg_mask) * 8192; mmc3_prg_offsets[3] = prg_mask * 8192; } uint32_t chr_mask = (chr_banks * 8) - 1; if (chr_banks == 0) chr_mask = 7; if (!mmc3_chr_mode) { mmc3_chr_offsets[0] = ((mmc3_registers[0] & 0xFE) & chr_mask) * 1024; mmc3_chr_offsets[1] = ((mmc3_registers[0] | 0x01) & chr_mask) * 1024; mmc3_chr_offsets[2] = ((mmc3_registers[1] & 0xFE) & chr_mask) * 1024; mmc3_chr_offsets[3] = ((mmc3_registers[1] | 0x01) & chr_mask) * 1024; mmc3_chr_offsets[4] = (mmc3_registers[2] & chr_mask) * 1024; mmc3_chr_offsets[5] = (mmc3_registers[3] & chr_mask) * 1024; mmc3_chr_offsets[6] = (mmc3_registers[4] & chr_mask) * 1024; mmc3_chr_offsets[7] = (mmc3_registers[5] & chr_mask) * 1024; } else { mmc3_chr_offsets[4] = ((mmc3_registers[0] & 0xFE) & chr_mask) * 1024; mmc3_chr_offsets[5] = ((mmc3_registers[0] | 0x01) & chr_mask) * 1024; mmc3_chr_offsets[6] = ((mmc3_registers[1] & 0xFE) & chr_mask) * 1024; mmc3_chr_offsets[7] = ((mmc3_registers[1] | 0x01) & chr_mask) * 1024; mmc3_chr_offsets[0] = (mmc3_registers[2] & chr_mask) * 1024; mmc3_chr_offsets[1] = (mmc3_registers[3] & chr_mask) * 1024; mmc3_chr_offsets[2] = (mmc3_registers[4] & chr_mask) * 1024; mmc3_chr_offsets[3] = (mmc3_registers[5] & chr_mask) * 1024; } }
