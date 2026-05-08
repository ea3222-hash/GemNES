#ifndef PPU_H
#define PPU_H

#include <cstdint>
#include <memory>
#include "Cartridge.h"

class PPU {
public:
    PPU();
    ~PPU();

    void ConnectCartridge(const std::shared_ptr<Cartridge>& cartridge);
    uint8_t cpuRead(uint16_t addr, uint8_t open_bus);
    void cpuWrite(uint16_t addr, uint8_t data);
    void step();
    void reset(bool fceux_mode = false);

    uint32_t screen[256 * 240]; 
    bool frame_complete = false; 
    bool nmi = false; 
    bool nmi_output = false; 
    
    bool nmi_edge_latched = false;
    bool nmi_suppressed = false; 
    void update_nmi();                 

    uint8_t OAM[256];
    uint8_t oam_addr = 0x00;
    uint8_t eval_oam_addr = 0x00;

private:
    std::shared_ptr<Cartridge> cart;

    uint8_t nameTable[2][1024]; 
    uint8_t paletteTable[32];   
    uint8_t ppuRead(uint16_t addr);
    void ppuWrite(uint16_t addr, uint8_t data);

    int scanline = 0;
    int cycle = 0;

    int latch_decay_timer = 0;

    uint8_t status = 0x00;
    uint8_t control = 0x00;
    uint8_t mask = 0x00;
    
    uint8_t ppu_data_buffer = 0x00;
    uint8_t ppu_data_latch = 0x00;

    uint16_t v = 0x0000; 
    uint16_t t = 0x0000; 
    uint8_t x = 0x00;    
    uint8_t w = 0x00;    

    bool vblank_suppress = false; 
    bool is_odd_frame = false;

    uint8_t bg_next_tile_id = 0x00, bg_next_tile_attrib = 0x00, bg_next_tile_lsb = 0x00, bg_next_tile_msb = 0x00;
    uint16_t bg_shifter_pattern_lo = 0x0000, bg_shifter_pattern_hi = 0x0000, bg_shifter_attrib_lo = 0x0000, bg_shifter_attrib_hi = 0x0000;

    struct sObjectAttributeEntry {
        uint8_t y;
        uint8_t id;
        uint8_t attribute;
        uint8_t x;
        bool isSpriteZero; 
    };
    
    sObjectAttributeEntry spriteScanline[8];
    uint8_t sprite_count;
    uint8_t sprite_shifter_pattern_lo[8];
    uint8_t sprite_shifter_pattern_hi[8];
    bool spriteZeroBeingRendered = false;

    uint32_t palScreen[0x40];
};

#endif // PPU_H
