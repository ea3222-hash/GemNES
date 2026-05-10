#ifndef CPU_H
#define CPU_H

#include <cstdint>

class Bus; 

class CPU {
public:
    CPU();
    ~CPU();

    void ConnectBus(Bus* n);

    uint16_t PC; 
    uint8_t SP;  
    uint8_t A;   
    uint8_t X;   
    uint8_t Y;   
    uint8_t P;  
    
    bool nmi_pending = false;
    bool nmi_delay = false; 
    bool fceux_mode = false;
    bool irq_pending = false;
    uint8_t open_bus = 0x00;
    
    int cycles;
    uint32_t total_cycles = 0; 

    // --- DMA TIMING VARIABLES ---
    bool dma_stole_cycle = false; 
    int dma_cycle_stolen = -1;

enum Flags {
        C = (1 << 0), Z = (1 << 1), I = (1 << 2), D = (1 << 3),
        V = (1 << 6), N = (1 << 7) 
    };

    void reset();
    void nmi();  
    void irq();
    int step(); 

private:
    Bus* bus = nullptr;

    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t data);
    void dummy_write(uint16_t addr, uint8_t data); 

    void push(uint8_t data);
    uint8_t pop();

    void setFlag(Flags flag, bool value);
    bool getFlag(Flags flag);
    void updateZeroAndNegativeFlags(uint8_t value);
    uint8_t current_opcode;
    uint8_t fetched;
    uint16_t addr_abs;
    uint16_t addr_rel;
    uint16_t addr_dummy;
    uint8_t base_hi; 
    uint8_t cpuRead(uint16_t addr, uint8_t current_open_bus);
    void cpuWrite(uint16_t addr, uint8_t data);
    bool page_crossed;
    bool is_write_instr[256];
    
    int cycle_count_this_inst = 0;
    int nmi_edge_cycle = -1;
    bool prev_nmi_line = false;
    void poll_nmi();
    void fetch();

    void IMP(); void IMM(); void ZP0(); void ZPX(); 
    void ZPY(); void REL(); void ABS(); void ABX(); 
    void ABY(); void IND(); void IZX(); void IZY();

    void Branch(bool condition);

    void ADC(); void AND(); void ASL(); void BCC(); void BCS(); void BEQ(); 
    void BIT(); void BMI(); void BNE(); void BPL(); void BRK(); void BVC(); 
    void BVS(); void CLC(); void CLD(); void CLI(); void CLV(); void CMP(); 
    void CPX(); void CPY(); void DEC(); void DEX(); void DEY(); void EOR(); 
    void INC(); void INX(); void INY(); void JMP(); void JSR(); void LDA(); 
    void LDX(); void LDY(); void LSR(); void NOP(); void ORA(); void PHA(); 
    void PHP(); void PLA(); void PLP(); void ROL(); void ROR(); void RTI(); 
    void RTS(); void SBC(); void SEC(); void SED(); void SEI(); void STA(); 
    void STX(); void STY(); void TAX(); void TAY(); void TSX(); void TXA(); 
    void TXS(); void TYA();
    void ASL_A(); void LSR_A(); void ROL_A(); void ROR_A();

    void LAX(); void SAX(); void DCP(); void ISC(); 
    void SLO(); void RLA(); void SRE(); void RRA();
    void ANC(); void ALR(); void ARR(); void AXS();
    void SBC_U();
    
    void ANE(); void LXA();
    void SHA(); void SHX(); void SHY(); void SHS(); void LAE();
};

#endif // CPU_H
