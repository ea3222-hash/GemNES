#ifndef BUS_H
#define BUS_H

#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include "CPU.h"
#include "PPU.h"
#include "Cartridge.h"
#include "APU.h"

struct CheatCode {
    uint16_t address;
    uint8_t data;
    uint8_t compare;
    bool requires_compare;
};

class Bus {
public:
    Bus();
    ~Bus();

    CPU cpu;
    PPU ppu;
    APU apu;
    std::shared_ptr<Cartridge> cart;
    
    uint8_t cpuRam[2048];
    uint8_t controller[2]; 
    
    uint8_t cpuRead(uint16_t addr, uint8_t current_open_bus);
    void cpuWrite(uint16_t addr, uint8_t data);

    void insertCartridge(const std::shared_ptr<Cartridge>& cartridge);
    void reset(bool hard = true, bool fceux_mode = false);

    std::vector<CheatCode> cheats;
    void addCheat(const std::string& code);
    void clearCheats();

    bool zapper_enabled = false;
    int zapper_x = 0;
    int zapper_y = 0;
    bool zapper_trigger = false;

    // --- DIABOLICAL MODE ---
    bool worst_nes_mode = false;

private:
    uint8_t controller_state[2]; 
    uint8_t strobe = 0;
};

#endif // BUS_H
