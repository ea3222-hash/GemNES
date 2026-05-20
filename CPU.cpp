#include "CPU.h"
#include "Bus.h"

static const bool is_official[256] = {
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, // 00-0F
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, // 10-1F
    1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, // 20-2F
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, // 30-3F
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, // 40-4F
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, // 50-5F
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, // 60-6F
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, // 70-7F
    0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, // 80-8F
    1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, // 90-9F
    1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, // A0-AF
    1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, // B0-BF
    1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, // C0-CF
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, // D0-DF
    1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, // E0-EF
    1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0  // F0-FF
};

CPU::CPU() {
    for (int i = 0; i < 256; i++) is_write_instr[i] = false;
    uint8_t write_opcodes[] = {
        0x85, 0x95, 0x8D, 0x9D, 0x99, 0x81, 0x91, 0x86, 0x96, 0x8E, 0x84, 0x94, 0x8C, 
        0xE6, 0xF6, 0xEE, 0xFE, 0xC6, 0xD6, 0xCE, 0xDE, 0x06, 0x16, 0x0E, 0x1E, 
        0x46, 0x56, 0x4E, 0x5E, 0x26, 0x36, 0x2E, 0x3E, 0x66, 0x76, 0x6E, 0x7E, 
        0x87, 0x97, 0x8F, 0x83, 0xC7, 0xD7, 0xCF, 0xDF, 0xDB, 0xC3, 0xD3, 0xE7, 
        0xF7, 0xEF, 0xFF, 0xFB, 0xE3, 0xF3, 0x07, 0x17, 0x0F, 0x1F, 0x1B, 0x03, 
        0x13, 0x27, 0x37, 0x2F, 0x3F, 0x3B, 0x23, 0x33, 0x47, 0x57, 0x4F, 0x5F, 
        0x5B, 0x43, 0x53, 0x67, 0x77, 0x6F, 0x7F, 0x7B, 0x63, 0x73, 
        0x93, 0x9F, 0x9E, 0x9C, 0x9B
    };
    for (uint8_t op : write_opcodes) is_write_instr[op] = true;
}

CPU::~CPU() {}
void CPU::ConnectBus(Bus* n) { bus = n; }

void CPU::poll_nmi() {
    if (bus->ppu.nmi_suppressed) {
        nmi_pending = false;
        nmi_delay = false;
        nmi_edge_cycle = -1;
        bus->ppu.nmi_suppressed = false;
    }

    bool current_nmi = bus->ppu.nmi_output;
    if (!prev_nmi_line && current_nmi) {
        if (nmi_edge_cycle == -1) {
            nmi_edge_cycle = cycle_count_this_inst;
        }
    }
    prev_nmi_line = current_nmi;
}

uint8_t CPU::read(uint16_t addr) { 
    poll_dma(addr);
    cycle_count_this_inst++;
    uint8_t data = bus->cpuRead(addr, open_bus);
    if (addr != 0x4015) open_bus = data; 
    
    bus->ppu.step(); bus->ppu.step(); bus->ppu.step(); bus->apu.step();
    poll_nmi();
    irq_pending = (bus->cart && bus->cart->irqState());
    
    cycles++; total_cycles++; 
    
    poll_dma(addr); 
    
    return data; 
}

void CPU::write(uint16_t addr, uint8_t data) { 
    cycle_count_this_inst++;
    open_bus = data;
    bus->cpuWrite(addr, data);
    
    bus->ppu.step(); bus->ppu.step(); bus->ppu.step(); bus->apu.step();
    poll_nmi();
    irq_pending = (bus->cart && bus->cart->irqState());
    
    cycles++; total_cycles++; 
}

void CPU::dummy_write(uint16_t addr, uint8_t data) { 
    cycle_count_this_inst++;
    open_bus = data;
    bus->cpuWrite(addr, data);
    
    bus->ppu.step(); bus->ppu.step(); bus->ppu.step(); bus->apu.step();
    poll_nmi();
    irq_pending = (bus->cart && bus->cart->irqState());
    
    cycles++; total_cycles++; 
}

void CPU::setFlag(Flags flag, bool value) { if (value) P |= flag; else P &= ~flag; }
bool CPU::getFlag(Flags flag) { return (P & flag) > 0; }
void CPU::updateZeroAndNegativeFlags(uint8_t value) { setFlag(Z, value == 0x00); setFlag(N, value & 0x80); }

void CPU::push(uint8_t data) { write(0x0100 + SP, data); SP--; }
uint8_t CPU::pop() { SP++; return read(0x0100 + SP); }

void CPU::reset() {
    addr_abs = 0xFFFC;
    uint16_t lo = read(addr_abs + 0); uint16_t hi = read(addr_abs + 1);
    PC = (hi << 8) | lo;
    A = 0; X = 0; Y = 0; SP = 0xFD;
    P = 0x24; 
    addr_dummy = 0; fetched = 0; cycles = 0; total_cycles = 0;
    open_bus = 0; base_hi = 0;
    nmi_pending = false; nmi_delay = false; irq_pending = false; prev_nmi_line = false;
}

void CPU::nmi() {
    read(PC); 
    read(PC); 
    push((PC >> 8) & 0x00FF); 
    push(PC & 0x00FF);
    
    // BREAK THE B FLAG IN WORSTNES
    if (worst_nes) push(P | 0x30); 
    else push((P | 0x20) & ~0x10); 
    
    setFlag(I, true);
    
    uint16_t lo = read(0xFFFA);
    uint16_t hi = read(0xFFFB);
    PC = (hi << 8) | lo;
    
    nmi_pending = false;
    nmi_delay = false;
    nmi_edge_cycle = -1;
}

void CPU::irq() {
    read(PC); 
    read(PC); 
    push((PC >> 8) & 0x00FF); 
    push(PC & 0x00FF);
    
    if (worst_nes) push(P | 0x30); 
    else push((P | 0x20) & ~0x10); 
    
    bool hijack = (nmi_edge_cycle != -1) || nmi_pending || nmi_delay;
    setFlag(I, true);
    
    uint16_t vec = hijack ? 0xFFFA : 0xFFFE;
    if (hijack) {
        nmi_pending = false;
        nmi_delay = false;
        nmi_edge_cycle = -1; 
    }

    uint16_t lo = read(vec);
    uint16_t hi = read(vec + 1);
    PC = (hi << 8) | lo;
}

void CPU::BRK() { 
    read(PC); 
    PC++;
    push((PC >> 8) & 0x00FF); 
    push(PC & 0x00FF); 
    
    // BREAK DECIMAL FLAG IN WORSTNES
    if (worst_nes) push((P | 0x30) & ~0x08);
    else push(P | 0x30);
    
    bool hijack = (nmi_edge_cycle != -1) || nmi_pending || nmi_delay;
    setFlag(I, true); 
    
    uint16_t vec = hijack ? 0xFFFA : 0xFFFE;
    if (hijack) {
        nmi_pending = false;
        nmi_delay = false;
        nmi_edge_cycle = -1; 
    }

    uint16_t lo = read(vec); 
    uint16_t hi = read(vec + 1); 
    PC = (hi << 8) | lo; 
}

void CPU::IMP() { fetched = A; read(PC); } 
void CPU::IMM() { addr_abs = PC++; }
void CPU::ZP0() { addr_abs = read(PC++); }
// BREAK ZERO PAGE INDEX WRAPAROUND
void CPU::ZPX() { uint16_t ptr = read(PC++); read(ptr); addr_abs = worst_nes ? (ptr + X) : ((ptr + X) & 0x00FF); } 
void CPU::ZPY() { uint16_t ptr = read(PC++); read(ptr); addr_abs = worst_nes ? (ptr + Y) : ((ptr + Y) & 0x00FF); } 
void CPU::REL() { addr_rel = read(PC++); if (addr_rel & 0x80) addr_rel |= 0xFF00; }
void CPU::ABS() { uint16_t lo = read(PC++); uint16_t hi = read(PC++); addr_abs = (hi << 8) | lo; }

void CPU::ABX() { 
    uint16_t lo = read(PC++); uint16_t hi = read(PC++); 
    base_hi = hi; 
    addr_abs = (hi << 8) | lo; addr_abs += X; 
    addr_dummy = (hi << 8) | ((lo + X) & 0x00FF);
    page_crossed = (addr_abs & 0xFF00) != (hi << 8);
    // BREAK ABSOLUTE DUMMY READS
    if (!worst_nes && (page_crossed || is_write_instr[current_opcode])) read(addr_dummy); 
}
void CPU::ABY() { 
    uint16_t lo = read(PC++); uint16_t hi = read(PC++); 
    base_hi = hi; 
    addr_abs = (hi << 8) | lo; addr_abs += Y; 
    addr_dummy = (hi << 8) | ((lo + Y) & 0x00FF);
    page_crossed = (addr_abs & 0xFF00) != (hi << 8);
    // BREAK ABSOLUTE DUMMY READS
    if (!worst_nes && (page_crossed || is_write_instr[current_opcode])) read(addr_dummy); 
}
void CPU::IND() { 
    uint16_t ptr_lo = read(PC++); uint16_t ptr_hi = read(PC++); 
    uint16_t ptr = (ptr_hi << 8) | ptr_lo; 
    uint16_t lo = read(ptr);
    // FIX THE 6502 HARDWARE BOUNDARY BUG!
    uint16_t hi = (ptr_lo == 0x00FF && !worst_nes) ? read(ptr & 0xFF00) : read(ptr + 1);
    addr_abs = (hi << 8) | lo;
}
void CPU::IZX() { 
    uint16_t t = read(PC++); read(t); 
    // BREAK IZX ZERO PAGE WRAPAROUND!
    uint16_t lo = read(worst_nes ? (t + X) : ((t + X) & 0x00FF)); 
    uint16_t hi = read(worst_nes ? (t + X + 1) : ((t + X + 1) & 0x00FF)); 
    addr_abs = (hi << 8) | lo; 
}
void CPU::IZY() { 
    uint16_t t = read(PC++); 
    // BREAK IZY ZERO PAGE WRAPAROUND!
    uint16_t lo = read(worst_nes ? t : (t & 0x00FF)); 
    uint16_t hi = read(worst_nes ? (t + 1) : ((t + 1) & 0x00FF)); 
    base_hi = hi; 
    addr_abs = (hi << 8) | lo; addr_abs += Y; 
    addr_dummy = (hi << 8) | ((lo + Y) & 0x00FF);
    page_crossed = (addr_abs & 0xFF00) != (hi << 8);
    // BREAK IZY DUMMY READ
    if (!worst_nes && (page_crossed || is_write_instr[current_opcode])) read(addr_dummy); 
}
void CPU::fetch() { fetched = read(addr_abs); }

void CPU::PHP() { 
    read(PC); 
    // BREAK DECIMAL FLAG IN WORSTNES
    if (worst_nes) push((P | 0x30) & ~0x08);
    else push(P | 0x30); 
} 
void CPU::PLP() { read(PC); read(0x0100 + SP); P = (pop() & 0xEF) | 0x20; } 
void CPU::RTI() { read(PC); read(0x0100 + SP); P = (pop() & 0xEF) | 0x20; uint16_t lo = pop(); uint16_t hi = pop(); PC = (hi << 8) | lo; }
void CPU::RTS() { read(PC); read(0x0100 + SP); uint16_t lo = pop(); uint16_t hi = pop(); PC = (hi << 8) | lo; read(PC); PC++; }
void CPU::PHA() { read(PC); push(A); }
void CPU::PLA() { read(PC); read(0x0100 + SP); A = pop(); updateZeroAndNegativeFlags(A); }
void CPU::JSR() { uint16_t lo = read(PC++); read(0x0100 + SP); push((PC >> 8) & 0x00FF); push(PC & 0x00FF); uint16_t hi = read(PC++); PC = (hi << 8) | lo; }

void CPU::ASL() { fetch(); dummy_write(addr_abs, fetched); uint16_t temp = (uint16_t)fetched << 1; setFlag(C, (temp & 0xFF00) > 0); updateZeroAndNegativeFlags(temp & 0x00FF); write(addr_abs, temp & 0x00FF); }
void CPU::LSR() { fetch(); dummy_write(addr_abs, fetched); setFlag(C, fetched & 0x0001); uint16_t temp = fetched >> 1; updateZeroAndNegativeFlags(temp & 0x00FF); write(addr_abs, temp & 0x00FF); }
void CPU::ROL() { fetch(); dummy_write(addr_abs, fetched); uint16_t temp = (uint16_t)(fetched << 1) | getFlag(C); setFlag(C, temp & 0xFF00); updateZeroAndNegativeFlags(temp & 0x00FF); write(addr_abs, temp & 0x00FF); }
void CPU::ROR() { fetch(); dummy_write(addr_abs, fetched); uint16_t temp = (uint16_t)(getFlag(C) << 7) | (fetched >> 1); setFlag(C, fetched & 0x01); updateZeroAndNegativeFlags(temp & 0x00FF); write(addr_abs, temp & 0x00FF); }
void CPU::DEC() { fetch(); dummy_write(addr_abs, fetched); uint16_t temp = fetched - 1; write(addr_abs, temp & 0x00FF); updateZeroAndNegativeFlags(temp & 0x00FF); }
void CPU::INC() { fetch(); dummy_write(addr_abs, fetched); uint16_t temp = fetched + 1; write(addr_abs, temp & 0x00FF); updateZeroAndNegativeFlags(temp & 0x00FF); }

void CPU::BCC() { if (!getFlag(C)) { read(PC); addr_abs = PC + addr_rel; if (!worst_nes && (addr_abs & 0xFF00) != (PC & 0xFF00)) read((PC & 0xFF00) | (addr_abs & 0x00FF)); PC = addr_abs; } }
void CPU::BCS() { if (getFlag(C))  { read(PC); addr_abs = PC + addr_rel; if (!worst_nes && (addr_abs & 0xFF00) != (PC & 0xFF00)) read((PC & 0xFF00) | (addr_abs & 0x00FF)); PC = addr_abs; } }
void CPU::BEQ() { if (getFlag(Z))  { read(PC); addr_abs = PC + addr_rel; if (!worst_nes && (addr_abs & 0xFF00) != (PC & 0xFF00)) read((PC & 0xFF00) | (addr_abs & 0x00FF)); PC = addr_abs; } }
void CPU::BNE() { if (!getFlag(Z)) { read(PC); addr_abs = PC + addr_rel; if (!worst_nes && (addr_abs & 0xFF00) != (PC & 0xFF00)) read((PC & 0xFF00) | (addr_abs & 0x00FF)); PC = addr_abs; } }
void CPU::BMI() { if (getFlag(N))  { read(PC); addr_abs = PC + addr_rel; if (!worst_nes && (addr_abs & 0xFF00) != (PC & 0xFF00)) read((PC & 0xFF00) | (addr_abs & 0x00FF)); PC = addr_abs; } }
void CPU::BPL() { if (!getFlag(N)) { read(PC); addr_abs = PC + addr_rel; if (!worst_nes && (addr_abs & 0xFF00) != (PC & 0xFF00)) read((PC & 0xFF00) | (addr_abs & 0x00FF)); PC = addr_abs; } }
void CPU::BVC() { if (!getFlag(V)) { read(PC); addr_abs = PC + addr_rel; if (!worst_nes && (addr_abs & 0xFF00) != (PC & 0xFF00)) read((PC & 0xFF00) | (addr_abs & 0x00FF)); PC = addr_abs; } }
void CPU::BVS() { if (getFlag(V))  { read(PC); addr_abs = PC + addr_rel; if (!worst_nes && (addr_abs & 0xFF00) != (PC & 0xFF00)) read((PC & 0xFF00) | (addr_abs & 0x00FF)); PC = addr_abs; } }

void CPU::ADC() { fetch(); uint16_t temp = (uint16_t)A + (uint16_t)fetched + (uint16_t)getFlag(C); setFlag(C, temp > 255); setFlag(V, (~((uint16_t)A ^ (uint16_t)fetched) & ((uint16_t)A ^ temp)) & 0x0080); A = temp & 0x00FF; updateZeroAndNegativeFlags(A); }
void CPU::SBC() { fetch(); uint16_t val = ((uint16_t)fetched) ^ 0x00FF; uint16_t temp = (uint16_t)A + val + (uint16_t)getFlag(C); setFlag(C, temp > 255); setFlag(V, (temp ^ (uint16_t)A) & (temp ^ val) & 0x0080); A = temp & 0x00FF; updateZeroAndNegativeFlags(A); }
void CPU::AND() { fetch(); A &= fetched; updateZeroAndNegativeFlags(A); }
void CPU::ORA() { fetch(); A |= fetched; updateZeroAndNegativeFlags(A); }
void CPU::EOR() { fetch(); A ^= fetched; updateZeroAndNegativeFlags(A); }
void CPU::CMP() { fetch(); uint16_t temp = (uint16_t)A - (uint16_t)fetched; setFlag(C, A >= fetched); updateZeroAndNegativeFlags(temp & 0x00FF); }
void CPU::CPX() { fetch(); uint16_t temp = (uint16_t)X - (uint16_t)fetched; setFlag(C, X >= fetched); updateZeroAndNegativeFlags(temp & 0x00FF); }
void CPU::CPY() { fetch(); uint16_t temp = (uint16_t)Y - (uint16_t)fetched; setFlag(C, Y >= fetched); updateZeroAndNegativeFlags(temp & 0x00FF); }
void CPU::BIT() { fetch(); uint16_t temp = A & fetched; setFlag(Z, (temp & 0x00FF) == 0x00); setFlag(N, fetched & (1 << 7)); setFlag(V, fetched & (1 << 6)); }
void CPU::LDA() { fetch(); A = fetched; updateZeroAndNegativeFlags(A); }
void CPU::LDX() { fetch(); X = fetched; updateZeroAndNegativeFlags(X); }
void CPU::LDY() { fetch(); Y = fetched; updateZeroAndNegativeFlags(Y); }
void CPU::STA() { write(addr_abs, A); } void CPU::STX() { write(addr_abs, X); } void CPU::STY() { write(addr_abs, Y); }

void CPU::CLC() { read(PC); setFlag(C, false); } void CPU::CLD() { read(PC); setFlag(D, false); } void CPU::CLI() { read(PC); setFlag(I, false); } void CPU::CLV() { read(PC); setFlag(V, false); }
void CPU::SEC() { read(PC); setFlag(C, true); }  void CPU::SED() { read(PC); setFlag(D, true); }  void CPU::SEI() { read(PC); setFlag(I, true); }
void CPU::DEX() { read(PC); X--; updateZeroAndNegativeFlags(X); } void CPU::DEY() { read(PC); Y--; updateZeroAndNegativeFlags(Y); }
void CPU::INX() { read(PC); X++; updateZeroAndNegativeFlags(X); } void CPU::INY() { read(PC); Y++; updateZeroAndNegativeFlags(Y); }
void CPU::JMP() { PC = addr_abs; }
void CPU::TAX() { read(PC); X = A; updateZeroAndNegativeFlags(X); } void CPU::TAY() { read(PC); Y = A; updateZeroAndNegativeFlags(Y); }
void CPU::TSX() { read(PC); X = SP; updateZeroAndNegativeFlags(X); } void CPU::TXA() { read(PC); A = X; updateZeroAndNegativeFlags(A); }
void CPU::TXS() { read(PC); SP = X; } void CPU::TYA() { read(PC); A = Y; updateZeroAndNegativeFlags(A); }
void CPU::NOP() { read(PC); }

void CPU::ASL_A() { read(PC); uint16_t temp = (uint16_t)A << 1; setFlag(C, (temp & 0xFF00) > 0); updateZeroAndNegativeFlags(temp & 0x00FF); A = temp & 0x00FF; } 
void CPU::LSR_A() { read(PC); setFlag(C, A & 0x0001); uint16_t temp = A >> 1; updateZeroAndNegativeFlags(temp & 0x00FF); A = temp & 0x00FF; } 
void CPU::ROL_A() { read(PC); uint16_t temp = (uint16_t)(A << 1) | getFlag(C); setFlag(C, temp & 0xFF00); updateZeroAndNegativeFlags(temp & 0x00FF); A = temp & 0x00FF; } 
void CPU::ROR_A() { read(PC); uint16_t temp = (uint16_t)(getFlag(C) << 7) | (A >> 1); setFlag(C, A & 0x01); updateZeroAndNegativeFlags(temp & 0x00FF); A = temp & 0x00FF; } 

void CPU::LAX() { fetch(); A = fetched; X = fetched; updateZeroAndNegativeFlags(A); }
void CPU::SAX() { write(addr_abs, A & X); }
void CPU::DCP() { fetch(); dummy_write(addr_abs, fetched); uint16_t t = (fetched - 1) & 0x00FF; write(addr_abs, t); setFlag(C, A >= t); updateZeroAndNegativeFlags((A - t) & 0x00FF); }
void CPU::ISC() { fetch(); dummy_write(addr_abs, fetched); uint16_t t = (fetched + 1) & 0x00FF; write(addr_abs, t); uint16_t val = t ^ 0x00FF; uint16_t temp = (uint16_t)A + val + (uint16_t)getFlag(C); setFlag(C, temp > 255); setFlag(V, (temp ^ (uint16_t)A) & (temp ^ val) & 0x0080); A = temp & 0x00FF; updateZeroAndNegativeFlags(A); }
void CPU::SLO() { fetch(); dummy_write(addr_abs, fetched); uint16_t t = (uint16_t)fetched << 1; setFlag(C, (t & 0xFF00) > 0); write(addr_abs, t & 0x00FF); A |= (t & 0x00FF); updateZeroAndNegativeFlags(A); }
void CPU::RLA() { fetch(); dummy_write(addr_abs, fetched); uint16_t t = (uint16_t)(fetched << 1) | getFlag(C); setFlag(C, t & 0xFF00); write(addr_abs, t & 0x00FF); A &= (t & 0x00FF); updateZeroAndNegativeFlags(A); }
void CPU::SRE() { fetch(); dummy_write(addr_abs, fetched); setFlag(C, fetched & 0x0001); uint16_t t = fetched >> 1; write(addr_abs, t & 0x00FF); A ^= (t & 0x00FF); updateZeroAndNegativeFlags(A); }
void CPU::RRA() { fetch(); dummy_write(addr_abs, fetched); uint16_t t = (uint16_t)(getFlag(C) << 7) | (fetched >> 1); setFlag(C, fetched & 0x0001); write(addr_abs, t & 0x00FF); uint16_t temp = (uint16_t)A + t + (uint16_t)getFlag(C); setFlag(C, temp > 255); setFlag(V, (~((uint16_t)A ^ t) & ((uint16_t)A ^ temp)) & 0x0080); A = temp & 0x00FF; updateZeroAndNegativeFlags(A); }
void CPU::ANC() { fetch(); A &= fetched; updateZeroAndNegativeFlags(A); setFlag(C, getFlag(N)); }
void CPU::ALR() { fetch(); A &= fetched; setFlag(C, A & 0x01); A >>= 1; updateZeroAndNegativeFlags(A); }
void CPU::ARR() { fetch(); A &= fetched; uint16_t t = (A >> 1) | (getFlag(C) << 7); setFlag(C, t & 0x40); setFlag(V, ((t >> 6) ^ (t >> 5)) & 0x01); A = t & 0xFF; updateZeroAndNegativeFlags(A); }
void CPU::AXS() { fetch(); uint16_t t = (A & X) - fetched; setFlag(C, (A & X) >= fetched); X = t & 0x00FF; updateZeroAndNegativeFlags(X); }
void CPU::SBC_U() { SBC(); } 

void CPU::SHA() {
    uint8_t val = A & X & ((addr_dummy >> 8) + 1); 
    uint16_t target = page_crossed ? ((val << 8) | (addr_abs & 0x00FF)) : addr_abs; 
    write(target, val); 
}
void CPU::SHX() {
    uint8_t val = X & ((addr_dummy >> 8) + 1); 
    uint16_t target = page_crossed ? ((val << 8) | (addr_abs & 0x00FF)) : addr_abs; 
    write(target, val); 
}
void CPU::SHY() {
    uint8_t val = Y & ((addr_dummy >> 8) + 1); 
    uint16_t target = page_crossed ? ((val << 8) | (addr_abs & 0x00FF)) : addr_abs; 
    write(target, val); 
}
void CPU::SHS() { 
    SP = A & X;
    uint8_t val = SP & ((addr_dummy >> 8) + 1); 
    uint16_t target = page_crossed ? ((val << 8) | (addr_abs & 0x00FF)) : addr_abs; 
    write(target, val); 
}

void CPU::LAE() { fetch(); uint8_t val = fetched & SP; A = val; X = val; SP = val; updateZeroAndNegativeFlags(A); }
void CPU::ANE() { fetch(); A = (A | 0xFF) & X & fetched; updateZeroAndNegativeFlags(A); }
void CPU::LXA() { fetch(); A = (A | 0xFF) & fetched; X = A; updateZeroAndNegativeFlags(A); } 

void CPU::poll_dma(uint16_t addr) {
    if (bus->apu.dmc.dma_pending) {
        bus->apu.dmc.dma_pending = false;
        
        // WORSTNES: Holy Inaccurate DMC DMA. Instant fetch without pausing CPU!
        if (worst_nes) {
            uint8_t dmc_data = bus->cpuRead(bus->apu.dmc.current_address, open_bus);
            bus->apu.dmc.sample_buffer = dmc_data;
            bus->apu.dmc.buffer_empty = false;
            
            if (bus->apu.dmc.current_address == 0xFFFF) bus->apu.dmc.current_address = 0x8000;
            else bus->apu.dmc.current_address++;
            
            if (bus->apu.dmc.length_counter > 0) {
                bus->apu.dmc.length_counter--;
                if (bus->apu.dmc.length_counter == 0) {
                    if (bus->apu.dmc.loop) {
                        bus->apu.dmc.length_counter = bus->apu.dmc.reload_length;
                        bus->apu.dmc.current_address = bus->apu.dmc.sample_address;
                    } else if (bus->apu.dmc_irq_enable) {
                        bus->apu.dmc_irq = true; 
                    }
                }
            }
            return; 
        }

        int dma_cycles = (total_cycles % 2 == 1) ? 3 : 4;
        
        for (int i = 0; i < dma_cycles; i++) {
            if (i == dma_cycles - 2) {
                open_bus = bus->cpuRead(addr, open_bus); 
            } else if (i == dma_cycles - 1) {
                uint8_t dmc_data = bus->cpuRead(bus->apu.dmc.current_address, open_bus);
                open_bus = dmc_data;
                bus->apu.dmc.sample_buffer = dmc_data;
                bus->apu.dmc.buffer_empty = false;
                
                if (bus->apu.dmc.current_address == 0xFFFF) bus->apu.dmc.current_address = 0x8000;
                else bus->apu.dmc.current_address++;
                
                if (bus->apu.dmc.length_counter > 0) {
                    bus->apu.dmc.length_counter--;
                    if (bus->apu.dmc.length_counter == 0) {
                        if (bus->apu.dmc.loop) {
                            bus->apu.dmc.length_counter = bus->apu.dmc.reload_length;
                            bus->apu.dmc.current_address = bus->apu.dmc.sample_address;
                        } else if (bus->apu.dmc_irq_enable) {
                            bus->apu.dmc_irq = true; 
                        }
                    }
                }
            }
            
            bus->ppu.step(); bus->ppu.step(); bus->ppu.step(); bus->apu.step();
            poll_nmi();
            cycles++; total_cycles++;
        }
    }
}

int CPU::step() {
    cycles = 0;
    cycle_count_this_inst = 0;
    nmi_edge_cycle = -1;

    if (nmi_pending) { 
        nmi_pending = false; 
        nmi(); 
        return cycles; 
    }
    
    if (nmi_delay) { 
        nmi_delay = false; 
        nmi_pending = true; 
    } 
    
    if ((irq_pending || bus->apu.irq_active || bus->apu.dmc_irq) && !getFlag(I)) { 
        irq(); 
        return cycles; 
    }
    
    current_opcode = read(PC++); 
    uint8_t opcode = current_opcode; 
    addr_dummy = 0; 
    page_crossed = false; 

    if (worst_nes && !is_official[opcode]) {
        NOP(); 
    } 
    else {
        switch (opcode) {
            case 0x00: BRK(); break; case 0x08: PHP(); break; case 0x18: CLC(); break; case 0x28: PLP(); break;
            case 0x38: SEC(); break; case 0x40: RTI(); break; case 0x48: PHA(); break; case 0x58: CLI(); break;
            case 0x60: RTS(); break; case 0x68: PLA(); break; case 0x78: SEI(); break; case 0x88: DEY(); break;
            case 0x8A: TXA(); break; case 0x98: TYA(); break; case 0x9A: TXS(); break; case 0xA8: TAY(); break;
            case 0xAA: TAX(); break; case 0xB8: CLV(); break; case 0xBA: TSX(); break; case 0xC8: INY(); break;
            case 0xCA: DEX(); break; case 0xD8: CLD(); break; case 0xE8: INX(); break; case 0xF8: SED(); break;

            case 0x0A: ASL_A(); break; case 0x2A: ROL_A(); break; case 0x4A: LSR_A(); break; case 0x6A: ROR_A(); break; 

            case 0x69: IMM(); ADC(); break; case 0x65: ZP0(); ADC(); break; case 0x75: ZPX(); ADC(); break; case 0x6D: ABS(); ADC(); break; case 0x7D: ABX(); ADC(); break; case 0x79: ABY(); ADC(); break; case 0x61: IZX(); ADC(); break; case 0x71: IZY(); ADC(); break;
            case 0xE9: IMM(); SBC(); break; case 0xE5: ZP0(); SBC(); break; case 0xF5: ZPX(); SBC(); break; case 0xED: ABS(); SBC(); break; case 0xFD: ABX(); SBC(); break; case 0xF9: ABY(); SBC(); break; case 0xE1: IZX(); SBC(); break; case 0xF1: IZY(); SBC(); break;
            case 0x29: IMM(); AND(); break; case 0x25: ZP0(); AND(); break; case 0x35: ZPX(); AND(); break; case 0x2D: ABS(); AND(); break; case 0x3D: ABX(); AND(); break; case 0x39: ABY(); AND(); break; case 0x21: IZX(); AND(); break; case 0x31: IZY(); AND(); break;
            case 0x09: IMM(); ORA(); break; case 0x05: ZP0(); ORA(); break; case 0x15: ZPX(); ORA(); break; case 0x0D: ABS(); ORA(); break; case 0x1D: ABX(); ORA(); break; case 0x19: ABY(); ORA(); break; case 0x01: IZX(); ORA(); break; case 0x11: IZY(); ORA(); break;
            case 0x49: IMM(); EOR(); break; case 0x45: ZP0(); EOR(); break; case 0x55: ZPX(); EOR(); break; case 0x4D: ABS(); EOR(); break; case 0x5D: ABX(); EOR(); break; case 0x59: ABY(); EOR(); break; case 0x41: IZX(); EOR(); break; case 0x51: IZY(); EOR(); break;
            case 0xC9: IMM(); CMP(); break; case 0xC5: ZP0(); CMP(); break; case 0xD5: ZPX(); CMP(); break; case 0xCD: ABS(); CMP(); break; case 0xDD: ABX(); CMP(); break; case 0xD9: ABY(); CMP(); break; case 0xC1: IZX(); CMP(); break; case 0xD1: IZY(); CMP(); break;
            
            case 0xA9: IMM(); LDA(); break; case 0xA5: ZP0(); LDA(); break; case 0xB5: ZPX(); LDA(); break; case 0xAD: ABS(); LDA(); break; case 0xBD: ABX(); LDA(); break; case 0xB9: ABY(); LDA(); break; case 0xA1: IZX(); LDA(); break; case 0xB1: IZY(); LDA(); break;
            case 0xA2: IMM(); LDX(); break; case 0xA6: ZP0(); LDX(); break; case 0xB6: ZPY(); LDX(); break; case 0xAE: ABS(); LDX(); break; case 0xBE: ABY(); LDX(); break;
            case 0xA0: IMM(); LDY(); break; case 0xA4: ZP0(); LDY(); break; case 0xB4: ZPX(); LDY(); break; case 0xAC: ABS(); LDY(); break; case 0xBC: ABX(); LDY(); break;
            
            case 0x85: ZP0(); STA(); break; case 0x95: ZPX(); STA(); break; case 0x8D: ABS(); STA(); break; case 0x9D: ABX(); STA(); break; case 0x99: ABY(); STA(); break; case 0x81: IZX(); STA(); break; case 0x91: IZY(); STA(); break;
            case 0x86: ZP0(); STX(); break; case 0x96: ZPY(); STX(); break; case 0x8E: ABS(); STX(); break;
            case 0x84: ZP0(); STY(); break; case 0x94: ZPX(); STY(); break; case 0x8C: ABS(); STY(); break;

            case 0xE6: ZP0(); INC(); break; case 0xF6: ZPX(); INC(); break; case 0xEE: ABS(); INC(); break; case 0xFE: ABX(); INC(); break;
            case 0xC6: ZP0(); DEC(); break; case 0xD6: ZPX(); DEC(); break; case 0xCE: ABS(); DEC(); break; case 0xDE: ABX(); DEC(); break;
            case 0x06: ZP0(); ASL(); break; case 0x16: ZPX(); ASL(); break; case 0x0E: ABS(); ASL(); break; case 0x1E: ABX(); ASL(); break;
            case 0x46: ZP0(); LSR(); break; case 0x56: ZPX(); LSR(); break; case 0x4E: ABS(); LSR(); break; case 0x5E: ABX(); LSR(); break;
            case 0x26: ZP0(); ROL(); break; case 0x36: ZPX(); ROL(); break; case 0x2E: ABS(); ROL(); break; case 0x3E: ABX(); ROL(); break;
            case 0x66: ZP0(); ROR(); break; case 0x76: ZPX(); ROR(); break; case 0x6E: ABS(); ROR(); break; case 0x7E: ABX(); ROR(); break;

            case 0xE0: IMM(); CPX(); break; case 0xE4: ZP0(); CPX(); break; case 0xEC: ABS(); CPX(); break;
            case 0xC0: IMM(); CPY(); break; case 0xC4: ZP0(); CPY(); break; case 0xCC: ABS(); CPY(); break;
            case 0x24: ZP0(); BIT(); break; case 0x2C: ABS(); BIT(); break;
            
            case 0x90: REL(); BCC(); break; case 0xB0: REL(); BCS(); break; case 0xF0: REL(); BEQ(); break; case 0x30: REL(); BMI(); break; 
            case 0xD0: REL(); BNE(); break; case 0x10: REL(); BPL(); break; case 0x50: REL(); BVC(); break; case 0x70: REL(); BVS(); break;
            
            case 0x4C: ABS(); JMP(); break; case 0x6C: IND(); JMP(); break;
            case 0x20: JSR(); break;

            case 0xEA: read(PC); break; 
            case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: read(PC); break; 
            case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: IMM(); fetch(); break; 
            case 0x04: case 0x44: case 0x64: ZP0(); fetch(); break;
            case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: ZPX(); fetch(); break;
            case 0x0C: ABS(); fetch(); break;
            case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: ABX(); fetch(); break;

            case 0xA7: ZP0(); LAX(); break; case 0xB7: ZPY(); LAX(); break; case 0xAF: ABS(); LAX(); break; case 0xBF: ABY(); LAX(); break; case 0xA3: IZX(); LAX(); break; case 0xB3: IZY(); LAX(); break;
            case 0x87: ZP0(); SAX(); break; case 0x97: ZPY(); SAX(); break; case 0x8F: ABS(); SAX(); break; case 0x83: IZX(); SAX(); break;
            case 0xC7: ZP0(); DCP(); break; case 0xD7: ZPX(); DCP(); break; case 0xCF: ABS(); DCP(); break; case 0xDF: ABX(); DCP(); break; case 0xDB: ABY(); DCP(); break; case 0xC3: IZX(); DCP(); break; case 0xD3: IZY(); DCP(); break;
            case 0xE7: ZP0(); ISC(); break; case 0xF7: ZPX(); ISC(); break; case 0xEF: ABS(); ISC(); break; case 0xFF: ABX(); ISC(); break; case 0xFB: ABY(); ISC(); break; case 0xE3: IZX(); ISC(); break; case 0xF3: IZY(); ISC(); break;
            case 0x07: ZP0(); SLO(); break; case 0x17: ZPX(); SLO(); break; case 0x0F: ABS(); SLO(); break; case 0x1F: ABX(); SLO(); break; case 0x1B: ABY(); SLO(); break; case 0x03: IZX(); SLO(); break; case 0x13: IZY(); SLO(); break;
            case 0x27: ZP0(); RLA(); break; case 0x37: ZPX(); RLA(); break; case 0x2F: ABS(); RLA(); break; case 0x3F: ABX(); RLA(); break; case 0x3B: ABY(); RLA(); break; case 0x23: IZX(); RLA(); break; case 0x33: IZY(); RLA(); break;
            case 0x47: ZP0(); SRE(); break; case 0x57: ZPX(); SRE(); break; case 0x4F: ABS(); SRE(); break; case 0x5F: ABX(); SRE(); break; case 0x5B: ABY(); SRE(); break; case 0x43: IZX(); SRE(); break; case 0x53: IZY(); SRE(); break;
            case 0x67: ZP0(); RRA(); break; case 0x77: ZPX(); RRA(); break; case 0x6F: ABS(); RRA(); break; case 0x7F: ABX(); RRA(); break; case 0x7B: ABY(); RRA(); break; case 0x63: IZX(); RRA(); break; case 0x73: IZY(); RRA(); break;

            case 0xEB: IMM(); SBC_U(); break; 
            case 0x0B: case 0x2B: IMM(); ANC(); break;
            case 0x4B: IMM(); ALR(); break;
            case 0x6B: IMM(); ARR(); break;
            case 0x8B: IMM(); ANE(); break; 
            case 0xAB: IMM(); LXA(); break; 
            case 0xCB: IMM(); AXS(); break;
            
            case 0x93: IZY(); SHA(); break;
            case 0x9F: ABY(); SHA(); break;
            case 0x9E: ABY(); SHX(); break;
            case 0x9C: ABX(); SHY(); break;
            case 0x9B: ABY(); SHS(); break;
            case 0xBB: ABY(); LAE(); break;

            default: read(PC); break;
        }
    }

    if (nmi_edge_cycle != -1) {
        if (nmi_edge_cycle < cycle_count_this_inst) {
            nmi_pending = true;
        } else {
            nmi_delay = true;
        }
    }
    
    return cycles;
}
