#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <cstdint>
#include <vector>
#include <string>

enum MIRROR { HORIZONTAL, VERTICAL, ONESCREEN_LO, ONESCREEN_HI, FOUR_SCREEN };

struct MMC5Pulse {
    int timer = 0;
    int timer_reload = 0;
    int duty = 0;
    int duty_pos = 0;
    int volume = 0;
    int length = 0;
    bool enable = false;
    bool halt = false;
};

class Cartridge {
public:
    Cartridge(const std::string& fileName);
    ~Cartridge();

    bool isLoaded() const;
    bool cpuRead(uint16_t addr, uint8_t& data);
    bool cpuWrite(uint16_t addr, uint8_t data);
    bool ppuRead(uint16_t addr, uint8_t& data, bool is_sprite = false);
    bool ppuWrite(uint16_t addr, uint8_t data);

    void ppuCtrlWrite(uint8_t data);
    void SaveSRAM(const std::string& path);
    void LoadSRAM(const std::string& path);

    MIRROR mirror = HORIZONTAL; 

    void scanline();
    bool irqState() const;
    void reset();

    void stepAudio(int cycles);
    double getAudioSample();

    std::string getROMInfo() const;
    bool is_nes20 = false;
    uint16_t submapper_id = 0;
    
    // --- DIABOLICAL MODE ---
    bool worst_nes = false;

private:
    bool loaded = false;
    uint8_t mapper_id = 0;
    uint8_t prg_banks = 0;
    uint8_t chr_banks = 0;

    std::vector<uint8_t> prg_memory;
    std::vector<uint8_t> chr_memory;
    uint8_t prg_ram[131072]; 

    uint8_t load_register = 0x00, load_count = 0x00, control_register = 0x1C;
    uint8_t chr_bank_0 = 0x00, chr_bank_1 = 0x00, prg_bank = 0x00;
    uint32_t prg_offsets[2], chr_offsets[2];
    void MMC1_Write(uint16_t addr, uint8_t data);
    void Update_MMC1_Offsets();

    uint8_t mmc3_target_reg = 0;
    bool mmc3_prg_mode = false, mmc3_chr_mode = false;
    uint8_t mmc3_registers[8] = {0};
    uint32_t mmc3_prg_offsets[4] = {0}, mmc3_chr_offsets[8] = {0};
    uint8_t irq_latch = 0, irq_counter = 0;
    bool irq_enable = false, irq_reload = false, irq_active = false;
    void MMC3_Write(uint16_t addr, uint8_t data);
    void Update_MMC3_Offsets();

    uint8_t mmc5_prg_mode = 3, mmc5_chr_mode = 0, mmc5_prg_protect1 = 0, mmc5_prg_protect2 = 0;
    uint8_t mmc5_exram_mode = 0, mmc5_nt_mapping = 0, mmc5_fill_tile = 0, mmc5_fill_color = 0;
    uint8_t mmc5_prg_banks[5] = {0,0,0,0,0xFF};
    uint16_t mmc5_chr_banks[12] = {0};
    uint8_t mmc5_chr_upper = 0, mmc5_last_exram = 0;
    uint16_t mmc5_last_chr_reg = 0;

    bool mmc5_8x16_mode = false, mmc5_ppu_in_frame = false, mmc5_irq_enable = false, mmc5_irq_pending = false;
    int mmc5_ppu_idle_counter = 0, mmc5_scanline_counter = 0;
    uint8_t mmc5_irq_scanline = 0, mmc5_mult_a = 0, mmc5_mult_b = 0;

    uint8_t mmc5_exram[1024];
    uint8_t mmc5_ciram[2048]; 

    MMC5Pulse mmc5_p1, mmc5_p2;
    int mmc5_pcm = 128;
    int mmc5_audio_divider = 0;

    uint32_t GetPrgRomAddr(int bank8k, int offset);
    uint32_t GetPrgRamAddr(int bank, int offset);
    bool IsPrgRamWritable();

    uint8_t fme7_command = 0; uint32_t fme7_prg_offsets[4] = {0}, fme7_chr_offsets[8] = {0}, fme7_prg_ram_offset = 0;
    uint16_t fme7_irq_counter = 0; bool fme7_irq_enable = false, fme7_irq_counter_enable = false, fme7_prg_ram_enable = false, fme7_prg_ram_rom = false;
    uint32_t map90_prg_offsets[4] = {0}, map90_chr_offsets[8] = {0};
    uint8_t map90_mul1 = 0, map90_mul2 = 0, map90_irq_counter = 0; bool map90_irq_enable = false;
};

#endif // CARTRIDGE_H
