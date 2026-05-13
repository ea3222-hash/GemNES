#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <cstdint>
#include <vector>
#include <string>

enum MIRROR { HORIZONTAL, VERTICAL, ONESCREEN_LO, ONESCREEN_HI, FOUR_SCREEN };

class Cartridge {
public:
    Cartridge(const std::string& fileName);
    ~Cartridge();

    bool isLoaded() const;
    bool cpuRead(uint16_t addr, uint8_t& data);
    bool cpuWrite(uint16_t addr, uint8_t data);
    bool ppuRead(uint16_t addr, uint8_t& data);
    bool ppuWrite(uint16_t addr, uint8_t data);

    MIRROR mirror = HORIZONTAL; 

    void scanline();
    bool irqState() const;
    void reset();

private:
    bool loaded = false;
    uint8_t mapper_id = 0;
    uint8_t prg_banks = 0;
    uint8_t chr_banks = 0;

    std::vector<uint8_t> prg_memory;
    std::vector<uint8_t> chr_memory;
    uint8_t prg_ram[131072]; 

    // MMC1
    uint8_t load_register = 0x00;
    uint8_t load_count = 0x00;
    uint8_t control_register = 0x1C;
    uint8_t chr_bank_0 = 0x00, chr_bank_1 = 0x00;
    uint8_t prg_bank = 0x00;
    uint32_t prg_offsets[2];
    uint32_t chr_offsets[2];
    void MMC1_Write(uint16_t addr, uint8_t data);
    void Update_MMC1_Offsets();

    // MMC3
    uint8_t mmc3_target_reg = 0;
    bool mmc3_prg_mode = false;
    bool mmc3_chr_mode = false;
    uint8_t mmc3_registers[8] = {0};
    uint32_t mmc3_prg_offsets[4] = {0};
    uint32_t mmc3_chr_offsets[8] = {0};
    uint8_t irq_latch = 0;
    uint8_t irq_counter = 0;
    bool irq_enable = false;
    bool irq_reload = false;
    bool irq_active = false;
    void MMC3_Write(uint16_t addr, uint8_t data);
    void Update_MMC3_Offsets();

    // MMC5
    uint8_t mmc5_prg_mode = 3;
    uint8_t mmc5_chr_mode = 0;
    uint8_t mmc5_exram_mode = 0;
    uint8_t mmc5_nt_mapping = 0;
    uint8_t mmc5_fill_tile = 0;
    uint8_t mmc5_fill_color = 0;
    uint8_t mmc5_prg_ram_bank = 0;
    uint8_t mmc5_ram_protect1 = 0;
    uint8_t mmc5_ram_protect2 = 0;
    
    bool mmc5_8x16_mode = false;
    bool mmc5_in_frame = false;
    int mmc5_ppu_reads = 0;
    int mmc5_cpu_reads = 0;
    int mmc5_sprite_fetches = 0;
    uint8_t mmc5_last_exram = 0;
    
    uint8_t mmc5_exram[1024];
    uint8_t mmc5_ciram[2048]; 
    uint8_t mmc5_mult_a = 0;
    uint8_t mmc5_mult_b = 0;
    uint8_t mmc5_prg_banks[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint16_t mmc5_chr_banks_A[8] = {0};
    uint16_t mmc5_chr_banks_B[4] = {0};
    uint8_t mmc5_chr_high_bits = 0;
    
    uint32_t mmc5_prg_offsets[4] = {0};
    bool mmc5_prg_is_ram[4] = {false, false, false, false};
    uint32_t mmc5_chr_offsets_A[8] = {0};
    uint32_t mmc5_chr_offsets_B[4] = {0};
    
    uint8_t mmc5_irq_scanline = 0;
    bool mmc5_irq_enable = false;
    int mmc5_irq_counter = 0;
    void Update_MMC5_PRG();
    void Update_MMC5_CHR();

    // Mapper 69 (Sunsoft FME-7)
    uint8_t fme7_command = 0;
    uint32_t fme7_prg_offsets[4] = {0};
    uint32_t fme7_chr_offsets[8] = {0};
    uint32_t fme7_prg_ram_offset = 0;
    uint16_t fme7_irq_counter = 0;
    bool fme7_irq_enable = false;
    bool fme7_irq_counter_enable = false;
    bool fme7_prg_ram_enable = false;
    bool fme7_prg_ram_rom = false;

    // Mapper 90 (JY Company)
    uint32_t map90_prg_offsets[4] = {0};
    uint32_t map90_chr_offsets[8] = {0};
    uint8_t map90_mul1 = 0;
    uint8_t map90_mul2 = 0;
    uint8_t map90_irq_counter = 0;
    bool map90_irq_enable = false;
};

#endif // CARTRIDGE_H
