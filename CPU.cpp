#include "CPU.h"
#include <iostream>

#define OP(op, in, md, cyc) opcodes[0x##op] = {in, md, cyc}

CPU::CPU(Bus* bus) : bus(bus) {
    initOpcodes();
}

void CPU::initOpcodes() {
    for (int i = 0; i < 256; i++) opcodes[i] = {XXX, IMP, 2}; // Default Unofficial

    OP(69,ADC,IMM,2); OP(65,ADC,ZP,3); OP(75,ADC,ZPX,4); OP(6D,ADC,ABS,4); OP(7D,ADC,ABSX,4); OP(79,ADC,ABSY,4); OP(61,ADC,INDX,6); OP(71,ADC,INDY,5);
    OP(29,AND,IMM,2); OP(25,AND,ZP,3); OP(35,AND,ZPX,4); OP(2D,AND,ABS,4); OP(3D,AND,ABSX,4); OP(39,AND,ABSY,4); OP(21,AND,INDX,6); OP(31,AND,INDY,5);
    OP(0A,ASL,ACC,2); OP(06,ASL,ZP,5); OP(16,ASL,ZPX,6); OP(0E,ASL,ABS,6); OP(1E,ASL,ABSX,7);
    OP(90,BCC,REL,2); OP(B0,BCS,REL,2); OP(F0,BEQ,REL,2); OP(30,BMI,REL,2); OP(D0,BNE,REL,2); OP(10,BPL,REL,2); OP(50,BVC,REL,2); OP(70,BVS,REL,2);
    OP(24,BIT,ZP,3);  OP(2C,BIT,ABS,4); OP(00,BRK,IMP,7); OP(18,CLC,IMP,2); OP(D8,CLD,IMP,2); OP(58,CLI,IMP,2); OP(B8,CLV,IMP,2);
    OP(C9,CMP,IMM,2); OP(C5,CMP,ZP,3); OP(D5,CMP,ZPX,4); OP(CD,CMP,ABS,4); OP(DD,CMP,ABSX,4); OP(D9,CMP,ABSY,4); OP(C1,CMP,INDX,6); OP(D1,CMP,INDY,5);
    OP(E0,CPX,IMM,2); OP(E4,CPX,ZP,3); OP(EC,CPX,ABS,4); OP(C0,CPY,IMM,2); OP(C4,CPY,ZP,3); OP(CC,CPY,ABS,4);
    OP(C6,DEC,ZP,5);  OP(D6,DEC,ZPX,6); OP(CE,DEC,ABS,6); OP(DE,DEC,ABSX,7); OP(CA,DEX,IMP,2); OP(88,DEY,IMP,2);
    OP(49,EOR,IMM,2); OP(45,EOR,ZP,3); OP(55,EOR,ZPX,4); OP(4D,EOR,ABS,4); OP(5D,EOR,ABSX,4); OP(59,EOR,ABSY,4); OP(41,EOR,INDX,6); OP(51,EOR,INDY,5);
    OP(E6,INC,ZP,5);  OP(F6,INC,ZPX,6); OP(EE,INC,ABS,6); OP(FE,INC,ABSX,7); OP(E8,INX,IMP,2); OP(C8,INY,IMP,2);
    OP(4C,JMP,ABS,3); OP(6C,JMP,IND,5); OP(20,JSR,ABS,6);
    OP(A9,LDA,IMM,2); OP(A5,LDA,ZP,3); OP(B5,LDA,ZPX,4); OP(AD,LDA,ABS,4); OP(BD,LDA,ABSX,4); OP(B9,LDA,ABSY,4); OP(A1,LDA,INDX,6); OP(B1,LDA,INDY,5);
    OP(A2,LDX,IMM,2); OP(A6,LDX,ZP,3); OP(B6,LDX,ZPY,4); OP(AE,LDX,ABS,4); OP(BE,LDX,ABSY,4);
    OP(A0,LDY,IMM,2); OP(A4,LDY,ZP,3); OP(B4,LDY,ZPX,4); OP(AC,LDY,ABS,4); OP(BC,LDY,ABSX,4);
    OP(4A,LSR,ACC,2); OP(46,LSR,ZP,5); OP(56,LSR,ZPX,6); OP(4E,LSR,ABS,6); OP(5E,LSR,ABSX,7); OP(EA,NOP,IMP,2);
    OP(09,ORA,IMM,2); OP(05,ORA,ZP,3); OP(15,ORA,ZPX,4); OP(0D,ORA,ABS,4); OP(1D,ORA,ABSX,4); OP(19,ORA,ABSY,4); OP(01,ORA,INDX,6); OP(11,ORA,INDY,5);
    OP(48,PHA,IMP,3); OP(08,PHP,IMP,3); OP(68,PLA,IMP,4); OP(28,PLP,IMP,4);
    OP(2A,ROL,ACC,2); OP(26,ROL,ZP,5); OP(36,ROL,ZPX,6); OP(2E,ROL,ABS,6); OP(3E,ROL,ABSX,7);
    OP(6A,ROR,ACC,2); OP(66,ROR,ZP,5); OP(76,ROR,ZPX,6); OP(6E,ROR,ABS,6); OP(7E,ROR,ABSX,7);
    OP(40,RTI,IMP,6); OP(60,RTS,IMP,6);
    OP(E9,SBC,IMM,2); OP(E5,SBC,ZP,3); OP(F5,SBC,ZPX,4); OP(ED,SBC,ABS,4); OP(FD,SBC,ABSX,4); OP(F9,SBC,ABSY,4); OP(E1,SBC,INDX,6); OP(F1,SBC,INDY,5);
    OP(38,SEC,IMP,2); OP(F8,SED,IMP,2); OP(78,SEI,IMP,2);
    OP(85,STA,ZP,3);  OP(95,STA,ZPX,4); OP(8D,STA,ABS,4); OP(9D,STA,ABSX,5); OP(99,STA,ABSY,5); OP(81,STA,INDX,6); OP(91,STA,INDY,6);
    OP(86,STX,ZP,3);  OP(96,STX,ZPY,4); OP(8E,STX,ABS,4); OP(84,STY,ZP,3); OP(94,STY,ZPX,4); OP(8C,STY,ABS,4);
    OP(AA,TAX,IMP,2); OP(A8,TAY,IMP,2); OP(BA,TSX,IMP,2); OP(8A,TXA,IMP,2); OP(9A,TXS,IMP,2); OP(98,TYA,IMP,2);
}

void CPU::reset() {
    sp = 0xFD; status = 0x24; 
    pc = bus->read16(0xFFFC);
}

void CPU::nmi() {
    push(pc >> 8); push(pc & 0xFF);
    push(status | U); setFlag(I, true);
    pc = bus->read16(0xFFFA);
}

uint8_t CPU::fetch() { return bus->read(pc++); }
void CPU::push(uint8_t val) { bus->write(0x0100 + sp--, val); }
uint8_t CPU::pop() { return bus->read(0x0100 + ++sp); }

void CPU::setFlag(uint8_t flag, bool v) { if (v) status |= flag; else status &= ~flag; }
bool CPU::getFlag(uint8_t flag) { return (status & flag) != 0; }
void CPU::setZN(uint8_t val) { setFlag(Z, val == 0); setFlag(N, val & 0x80); }

void CPU::printState() { printf("PC:%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X\n", pc, a, x, y, status, sp); }

uint16_t CPU::getOperandAddress(AddrMode mode) {
    switch(mode) {
        case IMP: case ACC: return 0;
        case IMM: return pc++;
        case ZP: return fetch();
        case ZPX: return (fetch() + x) & 0xFF;
        case ZPY: return (fetch() + y) & 0xFF;
        case REL: return pc + (int8_t)fetch();
        case ABS: { uint16_t addr = bus->read16(pc); pc += 2; return addr; }
        case ABSX: { uint16_t addr = bus->read16(pc) + x; pc += 2; return addr; }
        case ABSY: { uint16_t addr = bus->read16(pc) + y; pc += 2; return addr; }
        case IND: {
            uint16_t ptr = bus->read16(pc); pc += 2;
            if ((ptr & 0x00FF) == 0x00FF) return (bus->read(ptr & 0xFF00) << 8) | bus->read(ptr);
            return bus->read16(ptr);
        }
        case INDX: {
            uint8_t ptr = (fetch() + x) & 0xFF;
            return (bus->read((ptr + 1) & 0xFF) << 8) | bus->read(ptr);
        }
        case INDY: {
            uint8_t ptr = fetch();
            return ((bus->read((ptr + 1) & 0xFF) << 8) | bus->read(ptr)) + y;
        }
    }
    return 0;
}

uint8_t CPU::step() {
    uint8_t opcode = fetch();
    OpcodeDecode op = opcodes[opcode];
    
    if (op.inst == XXX) {
        // CPU crashed! It tried to execute an invalid instruction.
        printf("CRASH: Unofficial/Unknown Opcode %02X at PC:%04X\n", opcode, pc - 1);
        return op.cycles;
    }

    uint16_t addr = getOperandAddress(op.mode);
    
    switch(op.inst) {
        case LDA: a = bus->read(addr); setZN(a); break;
        case LDX: x = bus->read(addr); setZN(x); break;
        case LDY: y = bus->read(addr); setZN(y); break;
        case STA: bus->write(addr, a); break;
        case STX: bus->write(addr, x); break;
        case STY: bus->write(addr, y); break;
        case TAX: x = a; setZN(x); break;
        case TAY: y = a; setZN(y); break;
        case TXA: a = x; setZN(a); break;
        case TYA: a = y; setZN(a); break;
        case TSX: x = sp; setZN(x); break;
        case TXS: sp = x; break;
        case PHA: push(a); break;
        case PHP: push(status | B | U); break; // FIXED: Added U flag
        case PLA: a = pop(); setZN(a); break;
        case PLP: status = (pop() & ~B) | U; break;
        case AND: a &= bus->read(addr); setZN(a); break;
        case EOR: a ^= bus->read(addr); setZN(a); break;
        case ORA: a |= bus->read(addr); setZN(a); break;
        case BIT: {
            uint8_t m = bus->read(addr);
            setFlag(Z, (a & m) == 0); setFlag(V, m & V); setFlag(N, m & N);
            break;
        }
        case ADC: {
            uint8_t m = bus->read(addr);
            uint16_t sum = a + m + getFlag(C);
            setFlag(C, sum > 0xFF);
            setFlag(V, ~(a ^ m) & (a ^ sum) & 0x80);
            a = sum & 0xFF; setZN(a); break;
        }
        case SBC: {
            uint8_t m = bus->read(addr) ^ 0xFF;
            uint16_t sum = a + m + getFlag(C);
            setFlag(C, sum > 0xFF);
            setFlag(V, ~(a ^ m) & (a ^ sum) & 0x80);
            a = sum & 0xFF; setZN(a); break;
        }
        case CMP: { uint8_t m = bus->read(addr); setFlag(C, a >= m); setZN(a - m); break; }
        case CPX: { uint8_t m = bus->read(addr); setFlag(C, x >= m); setZN(x - m); break; }
        case CPY: { uint8_t m = bus->read(addr); setFlag(C, y >= m); setZN(y - m); break; }
        case INC: { uint8_t m = bus->read(addr) + 1; bus->write(addr, m); setZN(m); break; }
        case INX: x++; setZN(x); break;
        case INY: y++; setZN(y); break;
        case DEC: { uint8_t m = bus->read(addr) - 1; bus->write(addr, m); setZN(m); break; }
        case DEX: x--; setZN(x); break;
        case DEY: y--; setZN(y); break;
        case ASL: {
            if (op.mode == ACC) { setFlag(C, a & 0x80); a <<= 1; setZN(a); } 
            else { uint8_t m = bus->read(addr); setFlag(C, m & 0x80); m <<= 1; bus->write(addr, m); setZN(m); }
            break;
        }
        case LSR: {
            if (op.mode == ACC) { setFlag(C, a & 0x01); a >>= 1; setZN(a); } 
            else { uint8_t m = bus->read(addr); setFlag(C, m & 0x01); m >>= 1; bus->write(addr, m); setZN(m); }
            break;
        }
        case ROL: {
            uint8_t oldC = getFlag(C);
            if (op.mode == ACC) { setFlag(C, a & 0x80); a = (a << 1) | oldC; setZN(a); } 
            else { uint8_t m = bus->read(addr); setFlag(C, m & 0x80); m = (m << 1) | oldC; bus->write(addr, m); setZN(m); }
            break;
        }
        case ROR: {
            uint8_t oldC = getFlag(C);
            if (op.mode == ACC) { setFlag(C, a & 0x01); a = (a >> 1) | (oldC << 7); setZN(a); } 
            else { uint8_t m = bus->read(addr); setFlag(C, m & 0x01); m = (m >> 1) | (oldC << 7); bus->write(addr, m); setZN(m); }
            break;
        }
        case JMP: pc = addr; break;
        // FIXED: Safe evaluation order for subroutine jumps!
        case JSR: { 
            uint16_t ret = pc - 1; 
            push(ret >> 8); 
            push(ret & 0xFF); 
            pc = addr; 
            break; 
        }
        case RTS: { 
            uint16_t lo = pop(); 
            uint16_t hi = pop(); 
            pc = (hi << 8) | lo; 
            pc++; 
            break; 
        }
        case RTI: { 
            status = (pop() & ~B) | U; 
            uint16_t lo = pop(); 
            uint16_t hi = pop(); 
            pc = (hi << 8) | lo; 
            break; 
        }
        case BCC: if (!getFlag(C)) { pc = addr; op.cycles++; } break;
        case BCS: if (getFlag(C)) { pc = addr; op.cycles++; } break;
        case BEQ: if (getFlag(Z)) { pc = addr; op.cycles++; } break;
        case BNE: if (!getFlag(Z)) { pc = addr; op.cycles++; } break;
        case BMI: if (getFlag(N)) { pc = addr; op.cycles++; } break;
        case BPL: if (!getFlag(N)) { pc = addr; op.cycles++; } break;
        case BVC: if (!getFlag(V)) { pc = addr; op.cycles++; } break;
        case BVS: if (getFlag(V)) { pc = addr; op.cycles++; } break;
        case CLC: setFlag(C, false); break;
        case SEC: setFlag(C, true); break;
        case CLD: setFlag(D, false); break;
        case SED: setFlag(D, true); break;
        case CLI: setFlag(I, false); break;
        case SEI: setFlag(I, true); break;
        case CLV: setFlag(V, false); break;
        case NOP: break;
        case BRK: 
            push((pc + 1) >> 8); push((pc + 1) & 0xFF); 
            push(status | B | U); setFlag(I, true); 
            pc = bus->read16(0xFFFE); break;
        case XXX: break;
    }
    
    return op.cycles;
}
