#include <cstdlib>
#include "Bus.h"

Bus::Bus() {
    cpu.ConnectBus(this);
    for (int i = 0; i < 2048; i++) cpuRam[i] = 0x00;
    
    controller[0] = 0x00; controller[1] = 0x00;
    controller_state[0] = 0x00; controller_state[1] = 0x00;
}

Bus::~Bus() {}

void Bus::insertCartridge(const std::shared_ptr<Cartridge>& cartridge) {
    cart = cartridge;
    ppu.ConnectCartridge(cartridge);
}

void Bus::reset(bool hard, bool fceux_mode) {
    cpu.reset();
    ppu.reset(fceux_mode); 
    apu.reset();
    if (cart) cart->reset(); 
    
    if (hard) {
        if (fceux_mode) {
            for (int i = 0; i < 2048; i++) {
                cpuRam[i] = (i & 0x04) ? 0xFF : 0x00;
            }
        } else {
            for (int i = 0; i < 2048; i++) {
                cpuRam[i] = rand() % 256; 
            }
        }
    }
    
    cpu.open_bus = 0x00;
    controller_state[0] = 0x00;
    controller_state[1] = 0x00;
    strobe = 0;
}

uint8_t Bus::cpuRead(uint16_t addr, uint8_t current_open_bus) {
    uint8_t data = current_open_bus; 

    if (cart && cart->cpuRead(addr, data)) { } 
    else if (addr >= 0x0000 && addr <= 0x1FFF) { data = cpuRam[addr & 0x07FF]; } 
    else if (addr >= 0x2000 && addr <= 0x3FFF) { 
        data = ppu.cpuRead(addr & 0x2007, current_open_bus); 
    }
    else if (addr == 0x4015) { 
        data = apu.cpuRead(addr, current_open_bus); 
        return data; 
    }
    else if (addr == 0x4016 || addr == 0x4017) {
        uint8_t index = addr & 0x0001;
        if (strobe & 1) {
            data = (controller[index] & 0x80) > 0;
        } else {
            data = (controller_state[index] & 0x80) > 0;
            controller_state[index] <<= 1;
            controller_state[index] |= 1; 
        }
        data |= (current_open_bus & 0xE0); 
    }
    else if (addr >= 0x4000 && addr <= 0x5FFF) {
        return current_open_bus;
    }

    cpu.open_bus = data; 
    return data;
}

void Bus::cpuWrite(uint16_t addr, uint8_t data) {
    cpu.open_bus = data; 

    if (cart && cart->cpuWrite(addr, data)) { } 
    else if (addr >= 0x0000 && addr <= 0x1FFF) { cpuRam[addr & 0x07FF] = data; } 
    else if (addr >= 0x2000 && addr <= 0x3FFF) { ppu.cpuWrite(addr & 0x2007, data); }
    else if (addr == 0x4014) {
        uint16_t dma_page = data << 8;
        
        // --- FIX: The Golden DMA Hardware Implementation ---
        int dummy_cycles = (cpu.total_cycles % 2 == 1) ? 2 : 1;
        for (int i = 0; i < dummy_cycles; i++) {
            uint8_t dummy_data = cpuRead(cpu.PC, cpu.open_bus); 
            cpu.open_bus = dummy_data;
            if (!cpu.fceux_mode) { ppu.step(); ppu.step(); ppu.step(); apu.step(); }
            cpu.cycles++;
            cpu.total_cycles++;
        }

        for (int i = 0; i < 256; i++) {
            uint8_t dma_data = cpuRead(dma_page | i, cpu.open_bus);
            if (!cpu.fceux_mode) { ppu.step(); ppu.step(); ppu.step(); apu.step(); } 
            cpu.cycles++; 
            cpu.total_cycles++;
            
            cpu.open_bus = dma_data;
            ppu.cpuWrite(0x2004, dma_data);
            
            if (!cpu.fceux_mode) { ppu.step(); ppu.step(); ppu.step(); apu.step(); } 
            cpu.cycles++; 
            cpu.total_cycles++;
        }
    }
    else if ((addr >= 0x4000 && addr <= 0x4013) || addr == 0x4015 || addr == 0x4017) {
        apu.cpuWrite(addr, data);
    }
    else if (addr == 0x4016) {
        uint8_t prev_strobe = strobe;
        strobe = data & 1;
        if (prev_strobe == 1 && strobe == 0) {
            controller_state[0] = controller[0];
            controller_state[1] = controller[1];
        }
    }
}
void Bus::clearCheats() { cheats.clear(); }
void Bus::addCheat(const std::string& code) {
    if (code.length() != 6 && code.length() != 8) return;
    auto charToVal = [](char c) -> int {
        switch(toupper(c)) {
            case 'A': return 0x0; case 'P': return 0x1; case 'Z': return 0x2; case 'L': return 0x3;
            case 'G': return 0x4; case 'I': return 0x5; case 'T': return 0x6; case 'Y': return 0x7;
            case 'E': return 0x8; case 'O': return 0x9; case 'X': return 0xA; case 'U': return 0xB;
            case 'K': return 0xC; case 'S': return 0xD; case 'V': return 0xE; case 'R': return 0xF;
            default: return 0;
        }
    };
    int n[8] = {0};
    for (size_t i = 0; i < code.length(); i++) n[i] = charToVal(code[i]);
    CheatCode cheat;
    cheat.address = 0x8000 + (((n[3] & 7) << 12) | ((n[5] & 7) << 8) | ((n[4] & 8) << 8) | 
                             ((n[2] & 7) << 4) | ((n[1] & 8) << 4) | (n[4] & 7) | (n[3] & 8));

    if (code.length() == 6) {
        cheat.data = ((n[1] & 7) << 4) | ((n[0] & 8) << 4) | (n[0] & 7) | (n[5] & 8);
        cheat.requires_compare = false;
    } else { 
        cheat.data = ((n[1] & 7) << 4) | ((n[0] & 8) << 4) | (n[0] & 7) | (n[7] & 8);
        cheat.compare = ((n[7] & 7) << 4) | ((n[6] & 8) << 4) | (n[6] & 7) | (n[5] & 8);
        cheat.requires_compare = true;
    }
    cheats.push_back(cheat);
}
