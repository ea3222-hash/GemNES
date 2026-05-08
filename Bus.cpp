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
        for (int i = 0; i < 2048; i++) {
            cpuRam[i] = (i & 0x04) ? 0xFF : 0x00;
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
        return apu.cpuRead(addr, current_open_bus); 
    }
    else if (addr == 0x4016 || addr == 0x4017) {
        uint8_t index = addr & 0x0001;

        if (index == 1 && zapper_enabled) {
            // --- ZAPPER GUN ON PORT 2 ---
            bool light_sensed = false;
            
            // Check a 5x5 pixel radius around the mouse pointer
            if (zapper_x >= 0 && zapper_x < 256 && zapper_y >= 0 && zapper_y < 240) {
                int radius = 2; 
                for (int dy = -radius; dy <= radius && !light_sensed; dy++) {
                    for (int dx = -radius; dx <= radius && !light_sensed; dx++) {
                        int px = zapper_x + dx;
                        int py = zapper_y + dy;
                        if (px >= 0 && px < 256 && py >= 0 && py < 240) {
                            // Extract RGB from the live PPU screen buffer
                            uint32_t pixel = ppu.screen[py * 256 + px];
                            uint8_t r = (pixel >> 16) & 0xFF;
                            uint8_t g = (pixel >> 8) & 0xFF;
                            uint8_t b = pixel & 0xFF;
                            
                            // If the pixel is bright (white target), light is detected
                            if (r > 100 && g > 100 && b > 100) {
                                light_sensed = true;
                            }
                        }
                    }
                }
            }
            
            uint8_t d3 = light_sensed ? 0x00 : 0x08;   // Bit 3: 0 = Light, 1 = Dark
            uint8_t d4 = zapper_trigger ? 0x10 : 0x00; // Bit 4: 1 = Pulled, 0 = Released
            
            data = (current_open_bus & 0xE0) | d4 | d3;

        } else {
            // --- STANDARD CONTROLLER ---
            data = (controller_state[index] & 0x80) > 0;
            if (strobe) {
                controller_state[index] = controller[index];
            } else {
                controller_state[index] <<= 1;
                controller_state[index] |= 1; 
            }
            data |= (current_open_bus & 0xE0); 
        }
    }
    else if (addr >= 0x4000 && addr <= 0x5FFF) {
        return current_open_bus;
    }

    return data;
}

void Bus::cpuWrite(uint16_t addr, uint8_t data) {
    cpu.open_bus = data; 

    if (cart && cart->cpuWrite(addr, data)) { } 
    else if (addr >= 0x0000 && addr <= 0x1FFF) { cpuRam[addr & 0x07FF] = data; } 
    else if (addr >= 0x2000 && addr <= 0x3FFF) { ppu.cpuWrite(addr & 0x2007, data); }
    else if (addr == 0x4014) {
        uint16_t dma_page = data << 8;
        
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
            cpu.open_bus = dma_data;
            if (!cpu.fceux_mode) { ppu.step(); ppu.step(); ppu.step(); apu.step(); } 
            cpu.cycles++; 
            cpu.total_cycles++;
            
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
        strobe = data & 1;
        if (strobe) {
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
