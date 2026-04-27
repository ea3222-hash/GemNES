#ifndef APU_H
#define APU_H

#include <cstdint>

struct PulseChannel {
    bool enable = false; uint8_t duty = 0; uint8_t duty_seq = 0;
    uint16_t timer = 0; uint16_t timer_reload = 0; uint8_t length_counter = 0;
    bool length_halt = false; bool constant_volume = false; uint8_t volume = 0;
    bool env_start = false; uint8_t env_vol = 0; uint8_t env_divider = 0;
};

struct TriangleChannel {
    bool enable = false; bool length_halt = false; uint8_t linear_counter = 0;
    uint8_t linear_reload = 0; bool linear_reload_flag = false; uint16_t timer = 0;
    uint16_t timer_reload = 0; uint8_t length_counter = 0; uint8_t duty_seq = 0;
};

struct NoiseChannel {
    bool enable = false; bool length_halt = false; bool constant_volume = false;
    uint8_t volume = 0; uint16_t timer = 0; uint16_t timer_reload = 0;
    uint8_t length_counter = 0; uint16_t shift_register = 1; bool mode = false;
    bool env_start = false; uint8_t env_vol = 0; uint8_t env_divider = 0;
};

struct DMCChannel {
    bool enable = false;
    uint16_t length_counter = 0;
    uint16_t reload_length = 1;
    uint16_t timer = 0;
    uint16_t timer_reload = 428;
    uint8_t bit_counter = 0;
    bool loop = false;
};

class APU {
public:
    APU();
    ~APU();

    void cpuWrite(uint16_t addr, uint8_t data);
    uint8_t cpuRead(uint16_t addr, uint8_t open_bus);
    
    void step();
    double getOutputSample();
    void reset();

    bool irq_active = false;
    bool dmc_irq = false; 
    bool dmc_irq_enable = false;

    float vol_pulse1 = 50.0f;
    float vol_pulse2 = 50.0f;
    float vol_triangle = 50.0f;
    float vol_noise = 50.0f;

private:
    PulseChannel pulse1;
    PulseChannel pulse2;
    TriangleChannel triangle;
    NoiseChannel noise;       
    DMCChannel dmc; 

    uint32_t clock_counter = 0;
    uint32_t frame_counter = 0;
    uint8_t frame_mode = 0;
    bool irq_inhibit = false;
    
    void clock_envelopes();
    void clock_lengths();
};

#endif // APU_H
