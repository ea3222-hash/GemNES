# GemNES
is a NES emulator but developed by AI (Gemini Pro 3.1)
  the emulator is partially cycle-accurate and buggy
# Compile
to compile you need G++ and run this command "g++ APU.cpp Bus.cpp Cartridge.cpp CPU.cpp PPU.cpp main.cpp -std=c++17 -O2 -o GemNES.exe -lcomdlg32 -lwinmm -lgdi32 -luser32"
