#include "PPU.h"
#include <cstring>
#include <cstdlib>
#include <algorithm> 

PPU::PPU() {
    uint32_t hex_colors[64] = {
        0x545454, 0x001E74, 0x081090, 0x300088, 0x440064, 0x5C0030, 0x540400, 0x3C1800, 0x202A00, 0x083A00, 0x004000, 0x003C00, 0x00323C, 0x000000, 0x000000, 0x000000,
        0x989698, 0x084CC4, 0x3032EC, 0x5C1EE4, 0x8814B0, 0xA01464, 0x982220, 0x783C00, 0x545A00, 0x287200, 0x087C00, 0x007628, 0x006678, 0x000000, 0x000000, 0x000000,
        0xECEEEC, 0x4C9AEC, 0x787CEC, 0xB062EC, 0xE454EC, 0xEC58B4, 0xEC6A64, 0xD48820, 0xA0AA00, 0x74C400, 0x4CD020, 0x38CC6C, 0x38B4CC, 0x3C3C3C, 0x000000, 0x000000,
        0xECEEEC, 0xA8CCEC, 0xBCBCEC, 0xD4B2EC, 0xECAEEC, 0xECAED4, 0xECB4B0, 0xE4C490, 0xCCD278, 0xB4DE78, 0xA8E290, 0x98E2B4, 0xA0D6E4, 0xA0A2A0, 0x000000, 0x000000
    };
    for (int i = 0; i < 64; i++) palScreen[i] = hex_colors[i];
    memset(screen, 0, sizeof(screen));
    memset(OAM, 0xFF, sizeof(OAM));
}
PPU::~PPU() {}

void PPU::ConnectCartridge(const std::shared_ptr<Cartridge>& cartridge) { cart = cartridge; }

void PPU::reset(bool fceux_mode) {
    memset(screen, 0, sizeof(screen));
    
    if (fceux_mode) {
        memset(OAM, 0x00, sizeof(OAM));
        memset(nameTable, 0x00, sizeof(nameTable)); 
        memset(paletteTable, 0x00, sizeof(paletteTable));
    } else {
        memset(OAM, 0xFF, sizeof(OAM)); 
        for (int i = 0; i < 1024; i++) {
            uint8_t pattern = (i & 0x04) ? 0xFF : 0x00;
            nameTable[0][i] = pattern;
            nameTable[1][i] = pattern;
        }
        uint8_t pal_power_on[32] = {
            0x00, 0x00, 0x28, 0x00, 0x00, 0x08, 0x00, 0x00,
            0x00, 0x01, 0x01, 0x20, 0x00, 0x08, 0x00, 0x02,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x21, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00
        };
        for (int i = 0; i < 32; i++) paletteTable[i] = pal_power_on[i];
    }
    
    scanline = 0; cycle = 0;
    status = 0x00; control = 0x00; mask = 0x00; 
    ppu_data_buffer = 0x00; ppu_data_latch = 0x00;
    v = 0; t = 0; x = 0; w = 0;
    vblank_suppress = false; is_odd_frame = false; oam_eval_done = false;
    frame_complete = false; nmi_output = false; nmi = false;
    nmi_edge_latched = false; nmi_suppressed = false;
}

void PPU::update_nmi() { nmi_output = (status & 0x80) && (control & 0x80); }

uint8_t PPU::cpuRead(uint16_t addr, uint8_t open_bus) {
    uint8_t data = ppu_data_latch; 
    
    if (worst_nes) {
        ppu_data_latch = 0x00; // Open bus fails immediately!
    }
    
    switch (addr & 0x0007) {
        case 0x0002: {
            data = (status & 0xE0) | (worst_nes ? 0x00 : (ppu_data_latch & 0x1F));
            
            // WORSTNES: Breaks VBLANK Suppression logic entirely!
            if (scanline == 241) {
                if (!worst_nes && cycle == 0) { vblank_suppress = true; data &= ~0x80; nmi_suppressed = true; } 
                else if (!worst_nes && (cycle == 1 || cycle == 2)) { vblank_suppress = true; data |= 0x80; nmi_suppressed = true; }
            }
            if (scanline == -1 && cycle == 1 && !worst_nes) data &= ~0x80; 
            
            status &= ~0x80; w = 0; update_nmi();
            break;
        }
        case 0x0004:
            data = OAM[oam_addr];
            if ((oam_addr & 0x03) == 0x02) data &= 0xE3; 
            if ((mask & 0x18) && (scanline >= -1 && scanline < 240)) oam_addr += 4; 
            else OAM[oam_addr++] = data; 
            break;
        case 0x0007:
            data = ppu_data_buffer;
            ppu_data_buffer = ppuRead(v);
            if (v >= 0x3F00) {
                data = (ppu_data_buffer & 0x3F) | (worst_nes ? 0x00 : (ppu_data_latch & 0xC0));
                ppu_data_buffer = ppuRead(v & 0x2FFF); 
            }
            bool is_rendering = (mask & 0x18) && (scanline >= 0 && scanline < 240);
            bool is_active_cycle = (cycle >= 1 && cycle <= 256) || (cycle >= 321 && cycle <= 336);
            if (is_rendering && is_active_cycle) {
                if ((v & 0x001F) == 31) { v &= ~0x001F; v ^= 0x0400; } else { v++; }
                if ((v & 0x7000) != 0x7000) { v += 0x1000; }
                else {
                    v &= ~0x7000; int y = (v & 0x03E0) >> 5;
                    if (y == 29) { y = 0; v ^= 0x0800; } else if (y == 31) { y = 0; } else { y++; }
                    v = (v & ~0x03E0) | (y << 5);
                }
            } else { v += (control & 0x04) ? 32 : 1; }
            break;
    }
    
    if (worst_nes) ppu_data_latch = 0x00;
    else ppu_data_latch = data;
    
    latch_decay_timer = 3000000; 
    return data;
}

void PPU::cpuWrite(uint16_t addr, uint8_t data) {
    if (worst_nes) ppu_data_latch = 0x00;
    else ppu_data_latch = data; 
    
    switch (addr & 0x0007) {
        // WORSTNES: Breaks T Register Quirk by not updating nametable bits on $2000 write!
        case 0x0000: control = data; if (!worst_nes) t = (t & 0xF3FF) | ((data & 0x03) << 10); update_nmi(); break;
        case 0x0001: mask = data; break;
        case 0x0003: oam_addr = data; break;
        case 0x0004: OAM[oam_addr++] = data; break;
        case 0x0005:
            if (w == 0) { x = data & 0x07; t = (t & 0xFFE0) | (data >> 3); w = 1; }
            else        { t = (t & 0x8FFF) | ((data & 0x07) << 12); t = (t & 0xFC1F) | ((data & 0xF8) << 2); w = 0; }
            break;
        case 0x0006:
            if (w == 0) { t = (t & 0x00FF) | ((worst_nes ? data : (data & 0x3F)) << 8); w = 1; }
            else        { t = (t & 0xFF00) | data; v = t; w = 0; }
            break;
        case 0x0007:
            ppuWrite(v, data);
            bool is_rendering = (mask & 0x18) && (scanline >= 0 && scanline < 240);
            bool is_active_cycle = (cycle >= 1 && cycle <= 256) || (cycle >= 321 && cycle <= 336);
            if (is_rendering && is_active_cycle) {
                if ((v & 0x001F) == 31) { v &= ~0x001F; v ^= 0x0400; } else { v++; }
                if ((v & 0x7000) != 0x7000) { v += 0x1000; }
                else {
                    v &= ~0x7000; int y = (v & 0x03E0) >> 5;
                    if (y == 29) { y = 0; v ^= 0x0800; } else if (y == 31) { y = 0; } else { y++; }
                    v = (v & ~0x03E0) | (y << 5);
                }
            } else { v += (control & 0x04) ? 32 : 1; }
            latch_decay_timer = 3000000;
            break;
    }
}

uint8_t PPU::ppuRead(uint16_t addr, bool is_sprite) {
    addr &= 0x3FFF;
    uint8_t data = 0x00;

    if (cart->ppuRead(addr, data, is_sprite)) {}
    else if (addr >= 0x2000 && addr <= 0x3EFF) {
        addr &= 0x0FFF;
        if (cart->mirror == VERTICAL) {
            if (addr < 0x0400)      data = nameTable[0][addr & 0x03FF];
            else if (addr < 0x0800) data = nameTable[1][addr & 0x03FF];
            else if (addr < 0x0C00) data = nameTable[0][addr & 0x03FF];
            else                    data = nameTable[1][addr & 0x03FF];
        } else if (cart->mirror == HORIZONTAL) {
            if (addr < 0x0400)      data = nameTable[0][addr & 0x03FF];
            else if (addr < 0x0800) data = nameTable[0][addr & 0x03FF];
            else if (addr < 0x0C00) data = nameTable[1][addr & 0x03FF];
            else                    data = nameTable[1][addr & 0x03FF];
        } else if (cart->mirror == ONESCREEN_LO) { data = nameTable[0][addr & 0x03FF]; } 
        else if (cart->mirror == ONESCREEN_HI) { data = nameTable[1][addr & 0x03FF]; }
    } 
    else if (addr >= 0x3F00 && addr <= 0x3FFF) {
        addr &= 0x001F;
        if (addr == 0x10) addr = 0x00; else if (addr == 0x14) addr = 0x04;
        else if (addr == 0x18) addr = 0x08; else if (addr == 0x1C) addr = 0x0C;
        data = paletteTable[addr] & 0x3F;
        if (mask & 0x01) data &= 0x30; 
    }
    return data;
}

void PPU::ppuWrite(uint16_t addr, uint8_t data) {
    addr &= 0x3FFF;
    if (cart->ppuWrite(addr, data)) {} 
    else if (addr >= 0x2000 && addr <= 0x3EFF) {
        addr &= 0x0FFF;
        if (cart->mirror == VERTICAL) {
            if (addr < 0x0400)      nameTable[0][addr & 0x03FF] = data;
            else if (addr < 0x0800) nameTable[1][addr & 0x03FF] = data;
            else if (addr < 0x0C00) nameTable[0][addr & 0x03FF] = data;
            else                    nameTable[1][addr & 0x03FF] = data;
        } else if (cart->mirror == HORIZONTAL) {
            if (addr < 0x0400)      nameTable[0][addr & 0x03FF] = data;
            else if (addr < 0x0800) nameTable[0][addr & 0x03FF] = data;
            else if (addr < 0x0C00) nameTable[1][addr & 0x03FF] = data;
            else                    nameTable[1][addr & 0x03FF] = data;
        } else if (cart->mirror == ONESCREEN_LO) { nameTable[0][addr & 0x03FF] = data; } 
        else if (cart->mirror == ONESCREEN_HI) { nameTable[1][addr & 0x03FF] = data; }
    } 
    else if (addr >= 0x3F00 && addr <= 0x3FFF) {
        addr &= 0x001F;
        if (addr == 0x10) addr = 0x00; else if (addr == 0x14) addr = 0x04;
        else if (addr == 0x18) addr = 0x08; else if (addr == 0x1C) addr = 0x0C;
        paletteTable[addr] = data & 0x3F; 
    }
}

void PPU::step() {
    bool rendering_enabled = (mask & 0x18);

    if (scanline >= -1 && scanline < 240) {
        if (scanline == -1 && cycle == 1) { status &= ~0xE0; update_nmi(); }
        if (scanline == -1 && cycle == 339 && is_odd_frame && rendering_enabled && !worst_nes) cycle++; 

        if ((cycle >= 1 && cycle <= 256) || (cycle >= 321 && cycle <= 336)) {
            if (rendering_enabled) {
                bg_shifter_pattern_lo = (bg_shifter_pattern_lo << 1) | 1;
                bg_shifter_pattern_hi = (bg_shifter_pattern_hi << 1) | 1;
                bg_shifter_attrib_lo  = (bg_shifter_attrib_lo << 1); 
                bg_shifter_attrib_hi  = (bg_shifter_attrib_hi << 1); 
                
                switch ((cycle - 1) % 8) {
                    case 0: 
                        bg_shifter_pattern_lo = (bg_shifter_pattern_lo & 0xFF00) | bg_next_tile_lsb;
                        bg_shifter_pattern_hi = (bg_shifter_pattern_hi & 0xFF00) | bg_next_tile_msb;
                        bg_shifter_attrib_lo  = (bg_shifter_attrib_lo & 0xFF00) | ((bg_next_tile_attrib & 0b01) ? 0xFF : 0x00);
                        bg_shifter_attrib_hi  = (bg_shifter_attrib_hi & 0xFF00) | ((bg_next_tile_attrib & 0b10) ? 0xFF : 0x00);
                        break;
                    case 1: {
                        uint16_t fetch_addr = 0x2000 | (v & 0x0FFF);
                        // WORSTNES: Break "Attributes as Tiles" by forcefully clearing attribute-space fetches!
                        if (worst_nes && (fetch_addr & 0x03C0) == 0x03C0) fetch_addr &= ~0x03C0;
                        bg_next_tile_id = ppuRead(fetch_addr); 
                        break;
                    }
                    case 3: 
                        bg_next_tile_attrib = ppuRead(0x23C0 | (v & 0x0C00) | ((v >> 4) & 0x38) | ((v >> 2) & 0x07));
                        if (v & 0x0040) bg_next_tile_attrib >>= 4;
                        if (v & 0x0002) bg_next_tile_attrib >>= 2;
                        bg_next_tile_attrib &= 0x03;
                        break;
                    case 5: bg_next_tile_lsb = ppuRead(((control & 0x10) << 8) + ((uint16_t)bg_next_tile_id << 4) + ((v >> 12) & 0x07)); break;
                    case 7: bg_next_tile_msb = ppuRead(((control & 0x10) << 8) + ((uint16_t)bg_next_tile_id << 4) + ((v >> 12) & 0x07) + 8); break;
                }
            }
        }

        if (rendering_enabled && cycle == 260 && scanline >= 0 && scanline < 240) cart->scanline();

        if (rendering_enabled) {
            if ((cycle >= 1 && cycle <= 256) || (cycle >= 321 && cycle <= 336)) {
                if (cycle % 8 == 0) { if ((v & 0x001F) == 31) { v &= ~0x001F; v ^= 0x0400; } else { v++; } }
            }
            if (cycle == 256) {
                if ((v & 0x7000) != 0x7000) { v += 0x1000; }
                else {
                    v &= ~0x7000; int y = (v & 0x03E0) >> 5;
                    if (y == 29) { y = 0; v ^= 0x0800; } else if (y == 31) { y = 0; } else { y++; }
                    v = (v & ~0x03E0) | (y << 5);
                }
            }
            if (cycle == 257) v = (v & ~0x041F) | (t & 0x041F); 
            if (scanline == -1 && cycle >= 280 && cycle < 305) v = (v & ~0x7BE0) | (t & 0x7BE0); 
        }

        if (cycle == 65 && scanline >= -1 && scanline < 240) {
            if (rendering_enabled) {
                eval_oam_addr = oam_addr; 
                if (eval_oam_addr >= 8) {
                    uint8_t row = eval_oam_addr & 0xF8;
                    for (int i = 0; i < 8; i++) OAM[row + i] = OAM[i];
                }
            } else {
                eval_oam_addr = 0;
            }
        }

        if (cycle == 256 && scanline >= -1 && scanline < 240) {
            oam_eval_done = rendering_enabled; 
        }

        if (cycle == 257 && scanline >= -1 && scanline < 240) {
            if (rendering_enabled) oam_addr = 0; 
            
            if (oam_eval_done) {
                memset(spriteScanline, 0xFF, sizeof(spriteScanline));
                sprite_count = 0;
                int spriteSize = (control & 0x20) ? 16 : 8; 
                int eval_y = (scanline == -1) ? 255 : scanline;
                
                int max_sprites_allowed = worst_nes ? 64 : 8;
                int loops = 0;
                
                while (loops < 64 && sprite_count < (max_sprites_allowed + 1)) {
                    int addr = (eval_oam_addr + loops * 4) % 256;
                    uint8_t y    = OAM[addr];
                    uint8_t id   = OAM[(addr + 1) % 256];
                    uint8_t attr = OAM[(addr + 2) % 256];
                    uint8_t x    = OAM[(addr + 3) % 256];
                    
                    int diff = eval_y - y;
                    if (diff >= 0 && diff < spriteSize) {
                        if (sprite_count < max_sprites_allowed) {
                            spriteScanline[sprite_count].y = y;
                            spriteScanline[sprite_count].id = id;
                            spriteScanline[sprite_count].attribute = attr;
                            spriteScanline[sprite_count].x = x;
                            spriteScanline[sprite_count].isSpriteZero = (addr < 4); 
                        }
                        sprite_count++;
                    }
                    loops++;
                }
                if (sprite_count > 8 && scanline >= 0 && !worst_nes) status |= 0x20; 
            }
        }

        if (cycle == 340 && scanline >= -1 && scanline < 240) {
            int safe_sprite_count = worst_nes ? sprite_count : std::min((int)sprite_count, 8);
            for (int i = 0; i < safe_sprite_count; i++) {
                if (!rendering_enabled) {
                    spriteScanline[i].x = 0; 
                } else {
                    uint8_t sprite_pattern_bits_lo, sprite_pattern_bits_hi;
                    uint16_t sprite_pattern_addr_lo, sprite_pattern_addr_hi;
                    bool flip_v = spriteScanline[i].attribute & 0x80;
                    bool flip_h = spriteScanline[i].attribute & 0x40;

                    int diff = (scanline == -1 ? 255 : scanline) - spriteScanline[i].y;
                    if (diff < 0 || diff > 15) diff = 0; 

                    if (!(control & 0x20)) { 
                        if (!flip_v) sprite_pattern_addr_lo = ((control & 0x08) << 9) | (spriteScanline[i].id << 4) | diff;
                        else         sprite_pattern_addr_lo = ((control & 0x08) << 9) | (spriteScanline[i].id << 4) | (7 - diff);
                    } else { 
                        if (!flip_v) {
                            if (diff < 8)
                                sprite_pattern_addr_lo = ((spriteScanline[i].id & 0x01) << 12) | ((spriteScanline[i].id & 0xFE) << 4) | diff;
                            else
                                sprite_pattern_addr_lo = ((spriteScanline[i].id & 0x01) << 12) | (((spriteScanline[i].id & 0xFE) + 1) << 4) | (diff & 0x07);
                        } else {
                            if (diff < 8)
                                sprite_pattern_addr_lo = ((spriteScanline[i].id & 0x01) << 12) | (((spriteScanline[i].id & 0xFE) + 1) << 4) | (7 - diff);
                            else
                                sprite_pattern_addr_lo = ((spriteScanline[i].id & 0x01) << 12) | ((spriteScanline[i].id & 0xFE) << 4) | (7 - (diff & 0x07));
                        }
                    }

                    sprite_pattern_addr_hi = sprite_pattern_addr_lo + 8;
                    
                    sprite_pattern_bits_lo = ppuRead(sprite_pattern_addr_lo, true);
                    sprite_pattern_bits_hi = ppuRead(sprite_pattern_addr_hi, true);

                    if (flip_h) {
                        auto flipbyte = [](uint8_t b) {
                            b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
                            b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
                            b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
                            return b;
                        };
                        sprite_pattern_bits_lo = flipbyte(sprite_pattern_bits_lo);
                        sprite_pattern_bits_hi = flipbyte(sprite_pattern_bits_hi);
                    }
                    sprite_shifter_pattern_lo[i] = sprite_pattern_bits_lo;
                    sprite_shifter_pattern_hi[i] = sprite_pattern_bits_hi;
                }
            }
        }
    }

    if (scanline == 241 && cycle == 1) {
        status |= 0x80; 
        if (vblank_suppress && !worst_nes) status &= ~0x80; 
        update_nmi();
        vblank_suppress = false; 
    }

    if (scanline >= 0 && scanline < 240 && cycle >= 1 && cycle <= 256) {
        uint8_t bg_pixel = 0x00, bg_palette = 0x00;
        uint8_t fg_pixel = 0x00, fg_palette = 0x00, fg_priority = 0x00;

        bool show_bg = mask & 0x08;
        bool show_sp = mask & 0x10;

        if (cycle <= 8 && !(mask & 0x02)) show_bg = false; 
        if (cycle <= 8 && !(mask & 0x04)) show_sp = false; 

        if (show_bg) {
            uint16_t bit_mux = 0x8000 >> x;
            uint8_t p0 = (bg_shifter_pattern_lo & bit_mux) > 0;
            uint8_t p1 = (bg_shifter_pattern_hi & bit_mux) > 0;
            bg_pixel = (p1 << 1) | p0;

            uint8_t pal0 = (bg_shifter_attrib_lo & bit_mux) > 0;
            uint8_t pal1 = (bg_shifter_attrib_hi & bit_mux) > 0;
            bg_palette = (pal1 << 1) | pal0;
        }

        if (show_sp) {
            spriteZeroBeingRendered = false;
            int safe_sprite_count = worst_nes ? sprite_count : std::min((int)sprite_count, 8); 
            for (int i = 0; i < safe_sprite_count; i++) {
                if (spriteScanline[i].x == 0) {
                    uint8_t p0 = (sprite_shifter_pattern_lo[i] & 0x80) > 0;
                    uint8_t p1 = (sprite_shifter_pattern_hi[i] & 0x80) > 0;
                    fg_pixel = (p1 << 1) | p0;

                    fg_palette = (spriteScanline[i].attribute & 0x03) + 0x04;
                    fg_priority = (spriteScanline[i].attribute & 0x20) == 0;

                    if (fg_pixel != 0) {
                        if (spriteScanline[i].isSpriteZero) spriteZeroBeingRendered = true;
                        break;
                    }
                }
            }
        }

        uint8_t pixel = 0x00, palette = 0x00;

        if (bg_pixel == 0 && fg_pixel > 0) { pixel = fg_pixel; palette = fg_palette; }
        else if (bg_pixel > 0 && fg_pixel == 0) { pixel = bg_pixel; palette = bg_palette; }
        else if (bg_pixel > 0 && fg_pixel > 0) {
            if (fg_priority) { pixel = fg_pixel; palette = fg_palette; }
            else             { pixel = bg_pixel; palette = bg_palette; }

            // WORSTNES BREAKS SPRITE 0 HIT TIMING!
            if (show_bg && show_sp && spriteZeroBeingRendered) {
                if (worst_nes || cycle != 256) {
                    status |= 0x40;
                }
            }
        }

        uint8_t color_index = 0x00; 
        if (rendering_enabled) { 
            if (pixel != 0) color_index = ppuRead(0x3F00 + (palette << 2) + pixel);
            else color_index = ppuRead(0x3F00); 
        } else {
            if (v >= 0x3F00 && v <= 0x3FFF) color_index = ppuRead(v);
            else color_index = ppuRead(0x3F00);
        }
        
        screen[scanline * 256 + (cycle - 1)] = palScreen[color_index];

        if (rendering_enabled && cycle >= 1 && cycle <= 256) {
            int safe_sprite_count = worst_nes ? sprite_count : std::min((int)sprite_count, 8); 
            for (int i = 0; i < safe_sprite_count; i++) {
                if (spriteScanline[i].x > 0) spriteScanline[i].x--;
                else {
                    sprite_shifter_pattern_lo[i] <<= 1;
                    sprite_shifter_pattern_hi[i] <<= 1;
                }
            }
        }
    }
    
    // WORSTNES: Latch Never Decays
    if (!worst_nes && latch_decay_timer > 0) {
        latch_decay_timer--;
        if (latch_decay_timer == 0) ppu_data_latch = 0x00;
    }

    cycle++;
    if (cycle >= 341) { 
        cycle = 0; scanline++; 
        if (scanline >= 261) { scanline = -1; frame_complete = true; is_odd_frame = !is_odd_frame; }
    }
}
