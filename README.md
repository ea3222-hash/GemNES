# GemNES
is a NES emulator but developed by AI (Gemini Pro 3.1)
  the emulator is partially cycle-accurate and buggy
# Compile
to compile you need G++ and run this command "g++ APU.cpp Bus.cpp Cartridge.cpp CPU.cpp PPU.cpp main.cpp -std=c++17 -O2 -o GemNES.exe -lcomdlg32 -lwinmm -lgdi32 -luser32"
# AccuracyCoin TEST
<img width="768" height="768" alt="image" src="https://github.com/user-attachments/assets/33da8508-6041-4dd4-8d35-d7dca74ac6a5" />

# Mappers Supported
Mapper 1\
Mapper 2\
Mapper 3\
Mapper 4\
Mapper 7\
Mapper 9\
Mapper 66\
Mapper 69\
Mapper 90

### CPU BEHAVIOR
PASS : Rom is not writable\
PASS : RAM Mirroring\
PASS : PC Warparound\
PASS : The decimal flag\
PASS : The B flag\
PASS : Dummy read cycles\
PASS : Dummy write cycles\
PASS : Open bus\
PASS : All NOP instructions
### ADDRESSING MODE WARPAROUND
PASS : Absolute indexed\
PASS : Zero page indexed\
PASS : Indirect\
PASS : Indirect, X\
PASS : Indirect, Y\
PASS : Relative
### UNOFFICIAL INSTRUCTIONS: SLO
PASS : SLO Indirect, X\
PASS : SLO Zeropage\
PASS : SLO Absolute\
PASS : SLO Indirect, Y\
PASS : SLO Absolute, X\
PASS : SLO Absolute, Y\
PASS : SLO Absolute, X
### UNOFFICIAL INSTRUCTIONS: RLA
PASS : RLA Indirect, X\
PASS : RLA Zeropage\
PASS : RLA Absolute\
PASS : RLA Indirect, Y\
PASS : RLA Absolute, X\
PASS : RLA Absolute, Y\
PASS : RLA Absolute, X
### UNOFFICIAL INSTRUCTIONS: SRE
PASS : SRE Indirect, X\
PASS : SRE Zeropage\
PASS : SRE Absolute\
PASS : SRE Indirect, Y\
PASS : SRE Absolute, X\
PASS : SRE Absolute, Y\
PASS : SRE Absolute, X
### UNOFFICIAL INSTRUCTIONS: RRA
PASS : RRA Indirect, X\
PASS : RRA Zeropage\
PASS : RRA Absolute\
PASS : RRA Indirect, Y\
PASS : RRA Absolute, X\
PASS : RRA Absolute, Y\
PASS : RRA Absolute, X
### UNOFFICIAL INSTRUCTIONS: *AX
PASS : SAX Indirect, X\
PASS : SAX Zeropage\
PASS : SAX Absolute\
PASS : SAX Zeropage, Y\
PASS : LAX Indirect, X\
PASS : LAX Zeropage\
PASS : LAX Absolute\
PASS : LAX Indirect, Y\
PASS : LAX Zeropage, Y\
PASS : LAX Absolute, X
### UNOFFICIAL INSTRUCTIONS: DCP
PASS : DCP Indirect, X\
PASS : DCP Zeropage\
PASS : DCP Absolute\
PASS : DCP Indirect, Y\
PASS : DCP Absolute, X\
PASS : DCP Absolute, Y\
PASS : DCP Absolute, X
### UNOFFICIAL INSTRUCTIONS: ISC
PASS : ISC Indirect, X\
PASS : ISC Zeropage\
PASS : ISC Absolute\
PASS : ISC Indirect, Y\
PASS : ISC Absolute, X\
PASS : ISC Absolute, Y\
PASS : ISC Absolute, X
### UNOFFICIAL INSTRUCTIONS: SH*
FAIL 7 : SHA Indirect, Y\
FAIL 7 : SHA Absolute, Y\
FAIL 7 : SHS Absolute, Y\
FAIL 7 : SHY Absolute, X\
FAIL 7 : SHX Absolute, Y\
PASS : LAE Absolute, Y
### UNOFFICIAL IMMEDIATES
PASS : ANC Immediate\
PASS : ANC Immediate\
PASS : ASR Immediate\
PASS : ARR Immediate\
PASS : ANE Immediate (magic = $EE)\
PASS : LXA Immediate (magic = $EE)\
PASS : AXS Immediate\
PASS : SBC Immediate
### CPU INTERRPUTS
FAIL 2 : Interrupt flag latency\
FAIL 2 : NMI Overlap BRK\
FAIL 1 : NMI Overlap IRQ
### APU Registers and DMA tests
FAIL 2 : DMA + Open bus\
PASS : DMA + $2002 Read\
FAIL 2 : DMA + $2007 Read\
FAIL 1 : DMA + $2007 Write\
FAIL 2 : DMA + $4015 Read\
FAIL 1 : DMA + $4016 Read\
FAIL 1 : DMC DMA Bus conflicts\
FAIL 1 : DMC DMA + OAM DMA\
FAIL 1 : Explicit DMA abort\
FAIL 1 : Implicit DMA abort
### APU Tests
PASS : Length counter\
PASS : Length table\
FAIL 7 : Frame counter IRQ\
PASS : Frame counter 4-step\
PASS : Frame counter 5-step\
FAIL I : Delta modulation channel\
FAIL 1 : APU Register Activation\
FAIL 4 : Controller strobing\
PASS : Controller cloking ($4016 Double-Read like Famciom)
### POWER ON STATE
PPU Reset flag : "NO RESET FLAG DETECTED!"\
CPU RAM\
BF CE 90 67 BA AC C1 B0\
8B 73 FE 87 A0 A9 44 43\
02 FE 0F C1 2D D4 CD 06\
1E FE 93 25 AD C8 OF 63\
PPU RAM\
00 00 00 00 FF FF FF FF\
00 00 00 00 FF FF FF FF\
00 00 00 00 FF FF FF FF\
00 00 00 00 FF FF FF FF\
Palette RAM\
00 00 28 00 00 08 00 00\
00 01 01 20 00 08 00 02\
00 00 00 00 00 08 00 02\
00 00 00 00 00 10 00 00
### PPU BEHAVIOR
PASS : CHR ROM Is not writable\
PASS : PPU Register mirroring\
PASS : PPU Register open bus\
PASS : PPU Read buffer\
PASS : Palette RAM Quirks\
PASS : Rendering Flag behavior\
PASS : $2007 Red W/ Rendering
### PPU VBLANK TIMING
PASS : VBlank beginning\
PASS : VBlank end\
PASS : NMI control\
PASS : NMI timing\
PASS : NMI Suppression\
FAIL 1 : NMI At VBlank end\
PASS : NMI Disabled at VBlank
### SPRITE EVALUATION
PASS : Sprite overflow behavior\
PASS : Sprite 0 hit behavior\
FAIL 1 : $2002 Flag timing\
PASS : Suddenly resize sprite\
FAIL 2 : Arbitrary sprite zero\
FAIL 1 : Misaligned OAM behavior\
FAIL 4 : Address $2004 behavior\
FAIL 2 : OAM Corruption\
FAIL 1 : INC $4014
### PPU MISC.
PASS : Attributes as tiles\
PASS : T Register quirks\
FAIL 4 : Stale BG shift registers\
FAIL 3 : Stale sprite shift REGS\
FAIL 2 : BG Serial in\
FAIL 2 : Sprites on scanline 0\
FAIL 1 : $2004 Stress test\
FAIL 1 : $2007 Stress test
### CPU BEHAVIOR 2
FAIL 1 : Instruction timing\
FAIL 3 : Implied dummy reads\
PASS : Branch dummy reads\
PASS : JSR Edge cases\
FAIL 1 : Internal data bus
### Overall 102/139

