#include <iostream>
#include <windows.h>
#include <windowsx.h> 
#include <mmsystem.h>
#include <commdlg.h> 
#include <string>
#include <chrono>
#include <thread>
#include <algorithm> 
#include <fstream> 
#include <vector>  
#include <ctime>
#include "Bus.h"

// --- GUI CONSTANTS ---
#define MENU_FILE_OPEN 1001
#define MENU_FILE_LOAD_TAS 1010
#define MENU_GAME_PAUSE 1002
#define MENU_GAME_RESET 1003
#define MENU_GAME_SAVE 1008
#define MENU_GAME_LOAD 1009
#define MENU_GAME_KEYS 1006 
#define MENU_GAME_FCEUX 1011 
#define MENU_GAME_CONTROL_NONE 1012
#define MENU_GAME_CONTROL_ZAPPER 1013
#define MENU_CHEAT_GENIE 1004
#define MENU_CHEAT_CLEAR 1005
#define MENU_AUDIO_VOLUMES 1007 
#define MENU_FILE_ROM_INFO 1014
#define MENU_FILE_SAVE_SRAM 1015
#define MENU_FILE_LOAD_SRAM 1016
#define MENU_GAME_WORSTNES 1017 

HWND hwnd;
HDC hdc;
BITMAPINFO bmi;
HMENU hMenu, hSubMenuFile, hSubMenuGame, hSubMenuControl, hSubMenuCheats, hSubMenuAudio;

bool is_paused = false;

struct InputBind {
    bool is_joystick;
    int code;
};

InputBind keybinds[8] = {
    {false, 'K'}, {false, 'L'}, {false, 'O'}, {false, 'I'}, 
    {false, 'W'}, {false, 'S'}, {false, 'A'}, {false, 'D'}
};
const char* keyNames[8] = {"A", "B", "Select", "Start", "Up", "Down", "Left", "Right"};

const int SAMPLE_RATE = 44100;
const int BUFFER_SIZE = 1024; 
HWAVEOUT hWaveOut;
WAVEHDR waveBlocks[8];       
int16_t audioBuffer[8][BUFFER_SIZE];
int currentBlock = 0;
int sample_count = 0; 

Bus* global_bus = nullptr;
std::shared_ptr<Cartridge> global_cart = nullptr;

CPU backup_cpu; PPU backup_ppu; APU backup_apu;
uint8_t backup_ram[2048];
std::shared_ptr<Cartridge> backup_cart = nullptr;
bool has_save = false;

std::vector<std::pair<uint8_t, uint8_t>> tas_inputs;
std::vector<uint8_t> tas_commands; 
bool tas_active = false;
size_t tas_frame = 0;

void LoadFM2(const std::string& path) {
    tas_inputs.clear();
    tas_commands.clear();
    std::ifstream file(path);
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] != '|') continue; 
        
        size_t cmd_bar = line.find('|', 1);
        if (cmd_bar == std::string::npos) continue;
        
        size_t p1_bar = line.find('|', cmd_bar + 1);
        if (p1_bar == std::string::npos) continue;
        
        size_t p2_bar = line.find('|', p1_bar + 1);
        
        uint8_t cmd = line[1] - '0';
        tas_commands.push_back(cmd);

        std::string p1_str = line.substr(cmd_bar + 1, p1_bar - cmd_bar - 1);
        std::string p2_str = "........";
        
        if (p2_bar != std::string::npos) {
            p2_str = line.substr(p1_bar + 1, p2_bar - p1_bar - 1);
        }
        
        while (p1_str.length() < 8) p1_str += '.';
        while (p2_str.length() < 8) p2_str += '.';

        auto parseBtn =[](const std::string& str) {
            uint8_t b = 0;
            if (str[7] != '.' && str[7] != ' ') b |= 0x80; 
            if (str[6] != '.' && str[6] != ' ') b |= 0x40; 
            if (str[5] != '.' && str[5] != ' ') b |= 0x20; 
            if (str[4] != '.' && str[4] != ' ') b |= 0x10; 
            if (str[3] != '.' && str[3] != ' ') b |= 0x08; 
            if (str[2] != '.' && str[2] != ' ') b |= 0x04; 
            if (str[1] != '.' && str[1] != ' ') b |= 0x02; 
            if (str[0] != '.' && str[0] != ' ') b |= 0x01; 
            return b;
        };
        tas_inputs.push_back({parseBtn(p1_str), parseBtn(p2_str)});
    }
    
    if (!tas_inputs.empty()) {
        tas_active = true;
        tas_frame = 0;
    }
}

std::string SelectTASDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd; 
    ofn.lpstrFilter = "FCEUX TAS (*.fm2)\0*.fm2\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "fm2";
    ofn.lpstrTitle = "Select a TAS Replay (.fm2)";

    if (GetOpenFileNameA(&ofn)) return std::string(filename);
    return "";
}

void SaveState() {
    if (!global_bus || !global_cart) return;
    backup_cpu = global_bus->cpu; backup_ppu = global_bus->ppu; backup_apu = global_bus->apu;
    memcpy(backup_ram, global_bus->cpuRam, 2048); backup_cart = std::make_shared<Cartridge>(*global_cart); 
    has_save = true;
    MessageBoxA(hwnd, "Game Saved!", "Save State", MB_OK | MB_ICONINFORMATION);
}

void LoadState() {
    if (!has_save || !global_bus) { MessageBoxA(hwnd, "No save state found!", "Error", MB_OK | MB_ICONERROR); return; }
    global_bus->cpu = backup_cpu; global_bus->ppu = backup_ppu; global_bus->apu = backup_apu;
    memcpy(global_bus->cpuRam, backup_ram, 2048); *global_cart = *backup_cart; 
    global_bus->cpu.ConnectBus(global_bus); global_bus->ppu.ConnectCartridge(global_cart);
    waveOutReset(hWaveOut); currentBlock = 0; sample_count = 0; for (int i = 0; i < 8; i++) waveBlocks[i].dwFlags = 0;
    tas_active = false; 
}

std::string SelectROMDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd; 
    ofn.lpstrFilter = "NES ROMs (*.nes)\0*.nes\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "nes";
    ofn.lpstrTitle = "Select a NES ROM to play!";

    if (GetOpenFileNameA(&ofn)) return std::string(filename);
    return "";
}

int current_key_mapping = 0;
HWND hwndDialog = NULL;

std::string GetBindName(InputBind bind) {
    if (bind.is_joystick) {
        for (int i = 0; i < 32; i++) {
            if (bind.code & (1 << i)) return "Joy " + std::to_string(i + 1);
        }
        return "Joy";
    } else {
        if ((bind.code >= 'A' && bind.code <= 'Z') || (bind.code >= '0' && bind.code <= '9')) {
            return std::string(1, (char)bind.code);
        }
        return std::to_string(bind.code);
    }
}

LRESULT CALLBACK KeybindProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG:
            current_key_mapping = 0;
            SetTimer(hDlg, 1, 16, NULL);
            return TRUE;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hDlg, &ps);
            
            RECT rect; GetClientRect(hDlg, &rect);
            HBRUSH bg = CreateSolidBrush(RGB(20, 20, 30)); 
            FillRect(hdc, &rect, bg);
            DeleteObject(bg);
            
            SetBkMode(hdc, TRANSPARENT);
            HFONT hFont = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
            SelectObject(hdc, hFont);
            
            SetTextColor(hdc, RGB(255, 200, 50));
            TextOutA(hdc, 30, 20, "--- GEMNES CONTROLS ---", 23);
            
            for(int i=0; i<8; i++) {
                int y = 70 + (i * 35);
                
                if (i == current_key_mapping) {
                    SetTextColor(hdc, RGB(50, 255, 50)); 
                } else if (i < current_key_mapping) {
                    SetTextColor(hdc, RGB(100, 180, 255)); 
                } else {
                    SetTextColor(hdc, RGB(120, 120, 120)); 
                }
                
                std::string label = keyNames[i];
                while(label.length() < 7) label += " ";
                
                std::string bindText = "";
                if (i < current_key_mapping) bindText = GetBindName(keybinds[i]);
                else if (i == current_key_mapping) bindText = "Press Key/Button...";
                else bindText = "";
                
                std::string line = "[ " + label + " ]  ->  " + bindText;
                TextOutA(hdc, 30, y, line.c_str(), line.length());
            }

            SetTextColor(hdc, RGB(180, 50, 50));
            TextOutA(hdc, 30, 350, "Press ESC to Cancel", 19);
            
            DeleteObject(hFont);
            EndPaint(hDlg, &ps);
            return TRUE;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hDlg);
                return TRUE;
            }
            keybinds[current_key_mapping] = {false, (int)wParam};
            current_key_mapping++;
            if (current_key_mapping >= 8) { 
                DestroyWindow(hDlg); 
            } else { 
                InvalidateRect(hDlg, NULL, TRUE);
            }
            return TRUE;

        case WM_TIMER: {
            JOYINFOEX joy;
            joy.dwSize = sizeof(joy);
            joy.dwFlags = JOY_RETURNBUTTONS;
            if (joyGetPosEx(JOYSTICKID1, &joy) == JOYERR_NOERROR) {
                if (joy.dwButtons > 0) { 
                    keybinds[current_key_mapping] = {true, (int)joy.dwButtons};
                    while (joyGetPosEx(JOYSTICKID1, &joy) == JOYERR_NOERROR && joy.dwButtons > 0) {
                        Sleep(10); 
                    }
                    current_key_mapping++;
                    if (current_key_mapping >= 8) { 
                        DestroyWindow(hDlg); 
                    } else { 
                        InvalidateRect(hDlg, NULL, TRUE);
                    }
                }
            }
            return TRUE;
        }

        case WM_DESTROY: 
            KillTimer(hDlg, 1);
            hwndDialog = NULL; 
            is_paused = false; 
            return TRUE;
    }
    return DefWindowProc(hDlg, message, wParam, lParam);
}

void OpenKeybindWindow() {
    if (hwndDialog) return; 
    is_paused = true; 
    WNDCLASS wc = {0}; wc.lpfnWndProc = KeybindProc; wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = "KEYBIND_WND"; RegisterClass(&wc);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    hwndDialog = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "KEYBIND_WND", "GemNES Setup", WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE, (sw - 400) / 2, (sh - 400) / 2, 400, 420, hwnd, NULL, wc.hInstance, NULL);
    SetFocus(hwndDialog); 
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_ERASEBKGND: return 1; 
        
        case WM_MOUSEMOVE:
            if (global_bus && global_bus->zapper_enabled) {
                RECT clientRect; GetClientRect(hwnd, &clientRect);
                int cw = clientRect.right - clientRect.left;
                int ch = clientRect.bottom - clientRect.top;

                int scale = std::max(1, std::min(cw / 256, ch / 240));
                int target_w = 256 * scale;
                int target_h = 240 * scale;

                int offset_x = (cw - target_w) / 2;
                int offset_y = (ch - target_h) / 2;

                int mx = GET_X_LPARAM(lParam) - offset_x;
                int my = GET_Y_LPARAM(lParam) - offset_y;

                global_bus->zapper_x = mx / scale;
                global_bus->zapper_y = my / scale;
            }
            break;

        case WM_LBUTTONDOWN:
            if (global_bus && global_bus->zapper_enabled) {
                global_bus->zapper_trigger = true;
            }
            break;

        case WM_LBUTTONUP:
            if (global_bus && global_bus->zapper_enabled) {
                global_bus->zapper_trigger = false;
            }
            break;

        case WM_COMMAND: 
            switch (LOWORD(wParam)) {
                case MENU_FILE_OPEN: {
                    bool was_paused = is_paused; is_paused = true; 
                    std::string newRom = SelectROMDialog();
                    if (!newRom.empty() && global_bus) {
                        global_bus->insertCartridge(nullptr); 
                        global_cart.reset(); 
                        global_cart = std::make_shared<Cartridge>(newRom);
                        if (global_cart->isLoaded()) {
                            global_bus->insertCartridge(global_cart); 
                            global_bus->cpu.fceux_mode = false;
                            CheckMenuItem(hMenu, MENU_GAME_FCEUX, MF_BYCOMMAND | MF_UNCHECKED);
                            global_bus->reset(true, false); 
                            
                            currentBlock = 0; sample_count = 0; for(int i=0; i<8; i++) waveBlocks[i].dwFlags = 0; waveOutReset(hWaveOut);
                            has_save = false; tas_active = false; 
                            is_paused = false; CheckMenuItem(hMenu, MENU_GAME_PAUSE, MF_BYCOMMAND | MF_UNCHECKED);
                        } else { MessageBoxA(hwnd, "Failed to load ROM!", "Error", MB_OK | MB_ICONERROR); is_paused = was_paused; }
                    } else { is_paused = was_paused; }
                    break;
                }
                case MENU_FILE_LOAD_TAS: {
                    if (!global_cart || !global_cart->isLoaded()) { MessageBoxA(hwnd, "Please load a ROM first!", "Error", MB_OK | MB_ICONWARNING); break; }
                    bool was_paused = is_paused; is_paused = true;
                    std::string fm2Path = SelectTASDialog();
                    if (!fm2Path.empty()) {
                        LoadFM2(fm2Path);
                        global_bus->cpu.fceux_mode = true;
                        CheckMenuItem(hMenu, MENU_GAME_FCEUX, MF_BYCOMMAND | MF_CHECKED);
                        global_bus->reset(true, true); 
                        
                        currentBlock = 0; sample_count = 0; for(int i=0; i<8; i++) waveBlocks[i].dwFlags = 0; waveOutReset(hWaveOut);
                        is_paused = false; CheckMenuItem(hMenu, MENU_GAME_PAUSE, MF_BYCOMMAND | MF_UNCHECKED);
                    } else { is_paused = was_paused; }
                    break;
                }
                case MENU_FILE_SAVE_SRAM: {
                    if (global_cart && global_cart->isLoaded()) {
                        bool was_paused = is_paused; is_paused = true;
                        char filename[MAX_PATH] = "";
                        OPENFILENAMEA ofn; ZeroMemory(&ofn, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = "Save RAM (*.sav)\0*.sav\0All Files (*.*)\0*.*\0";
                        ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
                        ofn.lpstrDefExt = "sav";
                        if (GetSaveFileNameA(&ofn)) {
                            global_cart->SaveSRAM(std::string(filename));
                            MessageBoxA(hwnd, "SRAM Saved Successfully!", "Success", MB_OK | MB_ICONINFORMATION);
                        }
                        is_paused = was_paused;
                    } else {
                        MessageBoxA(hwnd, "Please load a ROM first!", "Error", MB_OK | MB_ICONWARNING);
                    }
                    break;
                }
                case MENU_FILE_LOAD_SRAM: {
                    if (global_cart && global_cart->isLoaded()) {
                        bool was_paused = is_paused; is_paused = true;
                        char filename[MAX_PATH] = "";
                        OPENFILENAMEA ofn; ZeroMemory(&ofn, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = "Save RAM (*.sav)\0*.sav\0All Files (*.*)\0*.*\0";
                        ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
                        if (GetOpenFileNameA(&ofn)) {
                            global_cart->LoadSRAM(std::string(filename));
                            MessageBoxA(hwnd, "SRAM Loaded Successfully!", "Success", MB_OK | MB_ICONINFORMATION);
                        }
                        is_paused = was_paused;
                    } else {
                        MessageBoxA(hwnd, "Please load a ROM first!", "Error", MB_OK | MB_ICONWARNING);
                    }
                    break;
                }
                case MENU_GAME_PAUSE:
                    if (global_cart && global_cart->isLoaded() && !hwndDialog) {
                        is_paused = !is_paused;
                        CheckMenuItem(hMenu, MENU_GAME_PAUSE, MF_BYCOMMAND | (is_paused ? MF_CHECKED : MF_UNCHECKED));
                    }
                    break;
                case MENU_GAME_RESET: 
                    if (global_bus && global_cart && global_cart->isLoaded()) { 
                        global_bus->reset(true, global_bus->cpu.fceux_mode); 
                        tas_active = false; 
                    } 
                    break;
                case MENU_GAME_FCEUX:
                    if (global_bus) {
                        global_bus->cpu.fceux_mode = !global_bus->cpu.fceux_mode;
                        CheckMenuItem(hMenu, MENU_GAME_FCEUX, MF_BYCOMMAND | (global_bus->cpu.fceux_mode ? MF_CHECKED : MF_UNCHECKED));
                        global_bus->reset(true, global_bus->cpu.fceux_mode); 
                        tas_active = false; 
                    }
                    break;
                case MENU_GAME_WORSTNES:
                    if (global_bus) {
                        global_bus->worst_nes_mode = !global_bus->worst_nes_mode;
                        CheckMenuItem(hMenu, MENU_GAME_WORSTNES, MF_BYCOMMAND | (global_bus->worst_nes_mode ? MF_CHECKED : MF_UNCHECKED));
                        global_bus->reset(true, global_bus->cpu.fceux_mode);
                        
                        if (global_bus->worst_nes_mode) {
                            MessageBoxA(hwnd, "WORSTNES ENABLED!\n\nNot accurate. But your Pentium from 2006 will survive.\n\nAll test ROMs will now fail spectacularly while normal games run blindingly fast and flawlessly.", "WorstNES Mode", MB_OK | MB_ICONEXCLAMATION);
                        }
                    }
                    break;
                case MENU_GAME_KEYS: OpenKeybindWindow(); break;
                
                case MENU_GAME_CONTROL_NONE:
                    if (global_bus) global_bus->zapper_enabled = false;
                    CheckMenuItem(hSubMenuControl, MENU_GAME_CONTROL_NONE, MF_BYCOMMAND | MF_CHECKED);
                    CheckMenuItem(hSubMenuControl, MENU_GAME_CONTROL_ZAPPER, MF_BYCOMMAND | MF_UNCHECKED);
                    break;

                case MENU_GAME_CONTROL_ZAPPER:
                    if (global_bus) global_bus->zapper_enabled = true;
                    CheckMenuItem(hSubMenuControl, MENU_GAME_CONTROL_NONE, MF_BYCOMMAND | MF_UNCHECKED);
                    CheckMenuItem(hSubMenuControl, MENU_GAME_CONTROL_ZAPPER, MF_BYCOMMAND | MF_CHECKED);
                    break;

                case MENU_GAME_SAVE: SaveState(); break;
                case MENU_GAME_LOAD: LoadState(); break;
                case MENU_CHEAT_GENIE:
                    if (global_bus) {
                        std::cout << "\n--- GAME GENIE ---\nEntry (6 or 8 letters, or Cancel): ";
                        std::string c; std::cin >> c;
                        if (c != "Cancel" && c != "cancel") { global_bus->addCheat(c); std::cout << "Cheat added!\n"; }
                    }
                    break;
                case MENU_CHEAT_CLEAR: if (global_bus) { global_bus->clearCheats(); std::cout << "\nCheats cleared!\n"; } break;
                case MENU_AUDIO_VOLUMES:
                    if (global_bus) {
                        bool was_paused = is_paused; is_paused = true; 
                        std::cout << "\n--- AUDIO MIXER (0 to 100, Default is 50) ---\n";
                        std::cout << "Pulse 1 (Melody): "; std::cin >> global_bus->apu.vol_pulse1;
                        std::cout << "Pulse 2 (Harmony): "; std::cin >> global_bus->apu.vol_pulse2;
                        std::cout << "Triangle (Bass): "; std::cin >> global_bus->apu.vol_triangle;
                        std::cout << "Noise (Drums): "; std::cin >> global_bus->apu.vol_noise;
                        std::cout << "DMC (PCM/Voices): "; std::cin >> global_bus->apu.vol_dmc;
                        std::cout << "Volumes updated!\n\n";
                        is_paused = was_paused;
                    }
                    case MENU_FILE_ROM_INFO:
                    if (global_cart && global_cart->isLoaded()) {
                        MessageBoxA(hwnd, global_cart->getROMInfo().c_str(), "ROM Info", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxA(hwnd, "No ROM is currently loaded!", "ROM Info", MB_OK | MB_ICONWARNING);
                    }
                    break;
            }
            return 0;
        case WM_KEYDOWN: if (wParam == VK_F5) SaveState(); if (wParam == VK_F7) LoadState(); break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void SetupWindow() {
    WNDCLASS wc = {0}; 
    wc.lpfnWndProc = WindowProc; 
    wc.hInstance = GetModuleHandle(NULL); 
    wc.lpszClassName = "NES_EMU"; 
    RegisterClass(&wc);
    
    hMenu = CreateMenu();
    
    hSubMenuFile = CreatePopupMenu(); 
    AppendMenu(hSubMenuFile, MF_STRING, MENU_FILE_OPEN, "Open ROM..."); 
    AppendMenu(hSubMenuFile, MF_STRING, MENU_FILE_ROM_INFO, "ROM Info...");
    AppendMenu(hSubMenuFile, MF_SEPARATOR, 0, NULL); 
    AppendMenu(hSubMenuFile, MF_STRING, MENU_FILE_SAVE_SRAM, "Save SRAM (.sav)..."); 
    AppendMenu(hSubMenuFile, MF_STRING, MENU_FILE_LOAD_SRAM, "Load SRAM (.sav)..."); 
    AppendMenu(hSubMenuFile, MF_SEPARATOR, 0, NULL); 
    AppendMenu(hSubMenuFile, MF_STRING, MENU_FILE_LOAD_TAS, "Load TAS (.fm2)..."); 
    AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSubMenuFile, "File");
    
    hSubMenuGame = CreatePopupMenu(); 
    AppendMenu(hSubMenuGame, MF_STRING, MENU_GAME_PAUSE, "Pause"); 
    AppendMenu(hSubMenuGame, MF_STRING, MENU_GAME_RESET, "Reset"); 
    AppendMenu(hSubMenuGame, MF_SEPARATOR, 0, NULL); 
    AppendMenu(hSubMenuGame, MF_STRING, MENU_GAME_SAVE, "Save State (F5)"); 
    AppendMenu(hSubMenuGame, MF_STRING, MENU_GAME_LOAD, "Load State (F7)"); 
    AppendMenu(hSubMenuGame, MF_SEPARATOR, 0, NULL); 
    AppendMenu(hSubMenuGame, MF_STRING, MENU_GAME_KEYS, "Change Keybinds"); 
    AppendMenu(hSubMenuGame, MF_SEPARATOR, 0, NULL); 
    AppendMenu(hSubMenuGame, MF_STRING, MENU_GAME_FCEUX, "FCEUX TAS Mode"); 
    AppendMenu(hSubMenuGame, MF_STRING, MENU_GAME_WORSTNES, "WorstNES Mode (Break Tests)");
    
    hSubMenuControl = CreatePopupMenu();
    AppendMenu(hSubMenuControl, MF_STRING, MENU_GAME_CONTROL_NONE, "None (Gamepad 2)");
    AppendMenu(hSubMenuControl, MF_STRING, MENU_GAME_CONTROL_ZAPPER, "Zapper Gun (Mouse)");
    CheckMenuItem(hSubMenuControl, MENU_GAME_CONTROL_NONE, MF_BYCOMMAND | MF_CHECKED);
    
    AppendMenu(hSubMenuGame, MF_SEPARATOR, 0, NULL);
    AppendMenu(hSubMenuGame, MF_STRING | MF_POPUP, (UINT_PTR)hSubMenuControl, "Control");
    
    AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSubMenuGame, "Game");
    
    hSubMenuCheats = CreatePopupMenu(); 
    AppendMenu(hSubMenuCheats, MF_STRING, MENU_CHEAT_GENIE, "Add Game Genie Code"); 
    AppendMenu(hSubMenuCheats, MF_STRING, MENU_CHEAT_CLEAR, "Clear All Cheats"); 
    AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSubMenuCheats, "Cheats");
    
    hSubMenuAudio = CreatePopupMenu(); 
    AppendMenu(hSubMenuAudio, MF_STRING, MENU_AUDIO_VOLUMES, "Audio Mixer (Console)"); 
    AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSubMenuAudio, "Audio");

    RECT rect = {0, 0, 768, 720}; 
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowEx(0, "NES_EMU", "GemNES Emulator", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, NULL, hMenu, wc.hInstance, NULL);
    
    HICON hIcon = (HICON)LoadImageA(NULL, "icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED);
    if (hIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    hdc = GetDC(hwnd);
    
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); 
    bmi.bmiHeader.biWidth = 256; 
    bmi.bmiHeader.biHeight = -240; 
    bmi.bmiHeader.biPlanes = 1; 
    bmi.bmiHeader.biBitCount = 32; 
    bmi.bmiHeader.biCompression = BI_RGB;
}

void SetupAudio() {
    WAVEFORMATEX wfx = {}; wfx.wFormatTag = WAVE_FORMAT_PCM; wfx.nChannels = 1; wfx.nSamplesPerSec = SAMPLE_RATE; wfx.wBitsPerSample = 16; wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8; wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    for (int i = 0; i < 8; i++) { waveBlocks[i].lpData = (LPSTR)audioBuffer[i]; waveBlocks[i].dwBufferLength = BUFFER_SIZE * sizeof(int16_t); waveBlocks[i].dwFlags = 0; }
}

int main() {
    srand(time(NULL)); 

    SetupWindow(); 
    SetupAudio();

    Bus nes_bus;
    global_bus = &nes_bus; 

    bool running = true;
    MSG msg;

    auto fps_timer = std::chrono::high_resolution_clock::now();
    auto frame_start = std::chrono::high_resolution_clock::now();
    int frames = 0;
    double current_fps = 0.0;

    SetTextColor(hdc, RGB(0, 255, 0));
    SetBkMode(hdc, TRANSPARENT);

    double audio_time = 0.0;
    const double CPU_CYCLES_PER_SAMPLE = 1789773.0 / 44100.0; 

    std::cout << "GemNES Emulator Started!\nClick 'File -> Open ROM' in the window to begin playing.\n";

    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        bool should_emulate = (global_cart && global_cart->isLoaded() && !is_paused);

        if (should_emulate) {
            
            if (tas_active) {
                if (tas_frame < tas_inputs.size()) {
                    if (tas_commands[tas_frame] & 0x02) { nes_bus.reset(true, true); } 
                    else if (tas_commands[tas_frame] & 0x01) { nes_bus.reset(false, true); }
                    
                    nes_bus.controller[0] = tas_inputs[tas_frame].first; 
                    nes_bus.controller[1] = tas_inputs[tas_frame].second; 
                } else {
                    tas_active = false; 
                    nes_bus.controller[0] = 0x00; nes_bus.controller[1] = 0x00;
                }
            } else {
                nes_bus.controller[0] = 0x00; 
                if (!nes_bus.zapper_enabled) {
                    nes_bus.controller[1] = 0x00;
                }
                
                if (GetForegroundWindow() == hwnd) {
                    bool needs_joystick = false;
                    for (int i = 0; i < 8; i++) {
                        if (keybinds[i].is_joystick) needs_joystick = true;
                    }

                    JOYINFOEX joy;
                    bool joy_connected = false;
                    if (needs_joystick) {
                        joy.dwSize = sizeof(joy);
                        joy.dwFlags = JOY_RETURNBUTTONS;
                        joy_connected = (joyGetPosEx(JOYSTICKID1, &joy) == JOYERR_NOERROR);
                    }

                    uint8_t bitmasks[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

                    for (int i = 0; i < 8; i++) {
                        if (keybinds[i].is_joystick) {
                            if (joy_connected && (joy.dwButtons & keybinds[i].code)) {
                                nes_bus.controller[0] |= bitmasks[i];
                            }
                        } else {
                            if (GetAsyncKeyState(keybinds[i].code) & 0x8000) {
                                nes_bus.controller[0] |= bitmasks[i];
                            }
                        }
                    }
                }
            }

            if (waveBlocks[currentBlock].dwFlags & WHDR_PREPARED) {
                while ((waveBlocks[currentBlock].dwFlags & WHDR_DONE) == 0) { std::this_thread::yield(); }
                waveOutUnprepareHeader(hWaveOut, &waveBlocks[currentBlock], sizeof(WAVEHDR));
            }

            while (!nes_bus.ppu.frame_complete) {
                if (nes_bus.cpu.nmi_pending) { nes_bus.cpu.nmi_pending = false; nes_bus.cpu.nmi(); }

                int cpu_cycles = nes_bus.cpu.step();

                if (global_cart) global_cart->stepAudio(cpu_cycles);
                
                audio_time += cpu_cycles;
                while (audio_time >= CPU_CYCLES_PER_SAMPLE) {
                    audio_time -= CPU_CYCLES_PER_SAMPLE;
                    
                    double sample = nes_bus.apu.getOutputSample();

                    if (global_cart && global_cart->isLoaded()) {
                        sample += global_cart->getAudioSample();
                    }
                    audioBuffer[currentBlock][sample_count] = (int16_t)((sample - 0.5) * 10000.0);
                    sample_count++;

                    if (sample_count >= BUFFER_SIZE) {
                        waveOutPrepareHeader(hWaveOut, &waveBlocks[currentBlock], sizeof(WAVEHDR));
                        waveOutWrite(hWaveOut, &waveBlocks[currentBlock], sizeof(WAVEHDR));
                        currentBlock = (currentBlock + 1) % 8; 
                        sample_count = 0;
                    }
                }
            }
            nes_bus.ppu.frame_complete = false;
            if (tas_active) tas_frame++; 
            
        } else {
            Sleep(16);
            fps_timer = std::chrono::high_resolution_clock::now(); 
            frames = 0;
        }

        RECT clientRect; GetClientRect(hwnd, &clientRect);
        int cw = clientRect.right - clientRect.left;
        int ch = clientRect.bottom - clientRect.top;

        int scale = std::max(1, std::min(cw / 256, ch / 240));
        int target_w = 256 * scale;
        int target_h = 240 * scale;

        int offset_x = (cw - target_w) / 2;
        int offset_y = (ch - target_h) / 2;

        HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
        if (offset_x > 0) {
            RECT leftBar = {0, 0, offset_x, ch}; RECT rightBar = {offset_x + target_w, 0, cw, ch};
            FillRect(hdc, &leftBar, blackBrush); FillRect(hdc, &rightBar, blackBrush);
        }
        if (offset_y > 0) {
            RECT topBar = {0, 0, cw, offset_y}; RECT bottomBar = {0, offset_y + target_h, cw, ch};
            FillRect(hdc, &topBar, blackBrush); FillRect(hdc, &bottomBar, blackBrush);
        }

        StretchDIBits(hdc, offset_x, offset_y, target_w, target_h, 0, 0, 256, 240, nes_bus.ppu.screen, &bmi, DIB_RGB_COLORS, SRCCOPY);

        if (is_paused && global_cart && global_cart->isLoaded()) {
            HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
            int px = offset_x + target_w - 60; int py = offset_y + 20;
            RECT bar1 = {px, py, px + 15, py + 50}; RECT bar2 = {px + 25, py, px + 40, py + 50}; 
            FillRect(hdc, &bar1, whiteBrush); FillRect(hdc, &bar2, whiteBrush); DeleteObject(whiteBrush);
        }

        frames++;
        auto time_now = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_now - fps_timer).count();
        if (elapsed_ms >= 500.0) {
            current_fps = (frames / elapsed_ms) * 1000.0; frames = 0; fps_timer = time_now;
        }
        
        char fps_buffer[64];
        if (global_bus && global_bus->worst_nes_mode) {
            snprintf(fps_buffer, sizeof(fps_buffer), "WORSTNES | FPS: %.2f", current_fps);
            SetTextColor(hdc, RGB(255, 0, 0)); 
        } else {
            SetTextColor(hdc, RGB(0, 255, 0));
            if (tas_active) snprintf(fps_buffer, sizeof(fps_buffer), "TAS: %zu/%zu | Target: 60.10 Hz | FPS: %.2f", tas_frame, tas_inputs.size(), current_fps);
            else if (should_emulate) snprintf(fps_buffer, sizeof(fps_buffer), "Target: 60.10 Hz | FPS: %.2f", current_fps);
            else snprintf(fps_buffer, sizeof(fps_buffer), "FPS: 0.00");
        }
        TextOut(hdc, 10, 10, fps_buffer, strlen(fps_buffer));

        auto frame_end = std::chrono::high_resolution_clock::now();
    }

    waveOutReset(hWaveOut);
    waveOutClose(hWaveOut);
    ReleaseDC(hwnd, hdc);
    return 0;
}
