#include "APU.h"

const uint8_t duty_table[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0}, 
    {0, 1, 1, 0, 0, 0, 0, 0}, 
    {0, 1, 1, 1, 1, 0, 0, 0}, 
    {1, 0, 0, 1, 1, 1, 1, 1}  
};

const uint8_t length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

const uint8_t triangle_table[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

const uint16_t noise_timer_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

const uint16_t dmc_rate_table[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54
};

APU::APU() {}
APU::~APU() {}

void APU::reset() {
    pulse1 = PulseChannel();
    pulse2 = PulseChannel();
    triangle = TriangleChannel();
    noise = NoiseChannel();
    dmc = DMCChannel(); 
    clock_counter = 0;
    frame_counter = 0; 
    frame_mode = 0;
    irq_inhibit = false;
    irq_active = false;
    dmc_irq = false;
    dmc_irq_enable = false;
    frame_counter_reset_delay = 0;
    delayed_frame_mode = 0;
}

void APU::cpuWrite(uint16_t addr, uint8_t data) {
    switch (addr) {
        case 0x4000: pulse1.duty = (data & 0xC0) >> 6; pulse1.length_halt = (data & 0x20); pulse1.constant_volume = (data & 0x10); pulse1.volume = (data & 0x0F); break;
        case 0x4002: pulse1.timer_reload = (pulse1.timer_reload & 0xFF00) | data; break;
        case 0x4003: pulse1.timer_reload = (pulse1.timer_reload & 0x00FF) | ((data & 0x07) << 8); pulse1.timer = pulse1.timer_reload; if (pulse1.enable) pulse1.length_counter = length_table[(data & 0xF8) >> 3]; pulse1.duty_seq = 0; pulse1.env_start = true; break;

        case 0x4004: pulse2.duty = (data & 0xC0) >> 6; pulse2.length_halt = (data & 0x20); pulse2.constant_volume = (data & 0x10); pulse2.volume = (data & 0x0F); break;
        case 0x4006: pulse2.timer_reload = (pulse2.timer_reload & 0xFF00) | data; break;
        case 0x4007: pulse2.timer_reload = (pulse2.timer_reload & 0x00FF) | ((data & 0x07) << 8); pulse2.timer = pulse2.timer_reload; if (pulse2.enable) pulse2.length_counter = length_table[(data & 0xF8) >> 3]; pulse2.duty_seq = 0; pulse2.env_start = true; break;

        case 0x4008: triangle.length_halt = (data & 0x80); triangle.linear_reload = data & 0x7F; break;
        case 0x400A: triangle.timer_reload = (triangle.timer_reload & 0xFF00) | data; break;
        case 0x400B: triangle.timer_reload = (triangle.timer_reload & 0x00FF) | ((data & 0x07) << 8); triangle.timer = triangle.timer_reload; if (triangle.enable) triangle.length_counter = length_table[(data & 0xF8) >> 3]; triangle.linear_reload_flag = true; break;

        case 0x400C: noise.length_halt = (data & 0x20); noise.constant_volume = (data & 0x10); noise.volume = (data & 0x0F); break;
        case 0x400E: noise.mode = (data & 0x80); noise.timer_reload = noise_timer_table[data & 0x0F]; break;
        case 0x400F: if (noise.enable) noise.length_counter = length_table[(data & 0xF8) >> 3]; noise.env_start = true; break;

        case 0x4010: 
            dmc_irq_enable = (data & 0x80) > 0; 
            dmc.loop = (data & 0x40) > 0;
            dmc.timer_reload = dmc_rate_table[data & 0x0F];
            if (!dmc_irq_enable) dmc_irq = false; 
            break;
            
        case 0x4011:
            dmc.output_level = data & 0x7F;
            break;
            
        case 0x4012:
            dmc.sample_address = 0xC000 | (data << 6);
            dmc.current_address = dmc.sample_address;
            break;

        case 0x4013:
            dmc.reload_length = (data * 16) + 1;
            break;

         case 0x4015:
            pulse1.enable = data & 0x01; if (!pulse1.enable) pulse1.length_counter = 0;
            pulse2.enable = data & 0x02; if (!pulse2.enable) pulse2.length_counter = 0;
            triangle.enable = data & 0x04; if (!triangle.enable) triangle.length_counter = 0;
            noise.enable = data & 0x08; if (!noise.enable) noise.length_counter = 0;
            
            if ((data & 0x10) == 0) {
                dmc.enable = false;
                dmc.length_counter = 0;
            } else {
                dmc.enable = true;
                if (dmc.length_counter == 0) {
                    dmc.length_counter = dmc.reload_length;
                    dmc.current_address = dmc.sample_address;
                    dmc.sample_buffer_empty = true; // Request a new DMA byte!
                }
            }
            dmc_irq = false; 
            break;
            
        case 0x4017:
            delayed_frame_mode = (data & 0x80) >> 7;
            irq_inhibit = (data & 0x40) >> 6;
            irq_active = false; 
            frame_counter_reset_delay = (clock_counter & 1) ? 4 : 3;
            break;
    }
}

uint8_t APU::cpuRead(uint16_t addr, uint8_t open_bus) {
    uint8_t data = 0; 
    if (addr == 0x4015) {
        data = (open_bus & 0x20); 
        if (pulse1.length_counter > 0) data |= 0x01;
        if (pulse2.length_counter > 0) data |= 0x02;
        if (triangle.length_counter > 0) data |= 0x04;
        if (noise.length_counter > 0) data |= 0x08;
        if (dmc.length_counter > 0) data |= 0x10;
        if (irq_active) data |= 0x40; 
        if (dmc_irq) data |= 0x80; 
        irq_active = false; 
    }
    return data;
}

void APU::clock_envelopes() {
    auto clock_env = [](auto& c) {
        if (c.env_start) { c.env_start = false; c.env_vol = 15; c.env_divider = c.volume; } 
        else {
            if (c.env_divider > 0) c.env_divider--;
            else {
                c.env_divider = c.volume;
                if (c.env_vol > 0) c.env_vol--;
                else if (c.length_halt) c.env_vol = 15; 
            }
        }
    };
    clock_env(pulse1); clock_env(pulse2); clock_env(noise);

    if (triangle.linear_reload_flag) triangle.linear_counter = triangle.linear_reload;
    else if (triangle.linear_counter > 0) triangle.linear_counter--;
    if (!triangle.length_halt) triangle.linear_reload_flag = false;
}

void APU::clock_lengths() {
    if (pulse1.length_counter > 0 && !pulse1.length_halt) pulse1.length_counter--;
    if (pulse2.length_counter > 0 && !pulse2.length_halt) pulse2.length_counter--;
    if (triangle.length_counter > 0 && !triangle.length_halt) triangle.length_counter--;
    if (noise.length_counter > 0 && !noise.length_halt) noise.length_counter--;
}

void APU::step() {
    if (frame_counter_reset_delay > 0) {
        frame_counter_reset_delay--;
        if (frame_counter_reset_delay == 0) {
            frame_counter = 0;
            frame_mode = delayed_frame_mode;
            if (frame_mode == 1) { 
                clock_lengths();
                clock_envelopes();
            }
        }
    }

    if (triangle.timer > 0) triangle.timer--;
    else {
        triangle.timer = triangle.timer_reload;
        if (triangle.linear_counter > 0 && triangle.length_counter > 0 && triangle.timer_reload > 2) {
            triangle.duty_seq = (triangle.duty_seq + 1) % 32;
        }
    }

    if (clock_counter % 2 == 0) {
        if (pulse1.timer > 0) pulse1.timer--; else { pulse1.timer = pulse1.timer_reload; pulse1.duty_seq = (pulse1.duty_seq + 1) % 8; }
        if (pulse2.timer > 0) pulse2.timer--; else { pulse2.timer = pulse2.timer_reload; pulse2.duty_seq = (pulse2.duty_seq + 1) % 8; }
        if (noise.timer > 0) noise.timer--; else {
            noise.timer = noise.timer_reload;
            uint16_t shift_amount = noise.mode ? 6 : 1;
            uint16_t bit1 = noise.shift_register & 0x0001;
            uint16_t bit2 = (noise.shift_register >> shift_amount) & 0x0001;
            noise.shift_register = (noise.shift_register >> 1) | ((bit1 ^ bit2) << 14);
        }
    }

    if (dmc.timer > 0) {
        dmc.timer--;
    } else {
        dmc.timer = dmc.timer_reload;
        if (!dmc.silence_flag) {
            if (dmc.shift_register & 0x01) {
                if (dmc.output_level <= 125) dmc.output_level += 2;
            } else {
                if (dmc.output_level >= 2) dmc.output_level -= 2;
            }
        }
        dmc.shift_register >>= 1;
        
        dmc.bit_counter++;
        if (dmc.bit_counter >= 8) {
            dmc.bit_counter = 0;
            if (!dmc.sample_buffer_empty) {
                dmc.silence_flag = false;
                dmc.shift_register = dmc.sample_buffer;
                dmc.sample_buffer_empty = true; 
            } else {
                dmc.silence_flag = true;
            }
        }
    }

    frame_counter++;
    if (frame_mode == 0) { 
        if (frame_counter == 7457)  { clock_envelopes(); }
        if (frame_counter == 14913) { clock_envelopes(); clock_lengths(); }
        if (frame_counter == 22371) { clock_envelopes(); }
        if (frame_counter == 29828) { if (!irq_inhibit) irq_active = true; }
        if (frame_counter == 29829) { if (!irq_inhibit) irq_active = true; clock_envelopes(); clock_lengths(); }
        if (frame_counter == 29830) { if (!irq_inhibit) irq_active = true; frame_counter = 0; }
    } else { 
        if (frame_counter == 7457)  { clock_envelopes(); }
        if (frame_counter == 14913) { clock_envelopes(); clock_lengths(); }
        if (frame_counter == 22371) { clock_envelopes(); }
        if (frame_counter == 37281) { clock_envelopes(); clock_lengths(); }
        if (frame_counter == 37282) { frame_counter = 0; }
    }

    clock_counter++;
}

double APU::getOutputSample() {
    double p1 = 0, p2 = 0, t = 0, n = 0;
    
    if (pulse1.enable && pulse1.length_counter > 0 && pulse1.timer_reload > 8) 
        p1 = duty_table[pulse1.duty][pulse1.duty_seq] ? (pulse1.constant_volume ? pulse1.volume : pulse1.env_vol) : 0;
    
    if (pulse2.enable && pulse2.length_counter > 0 && pulse2.timer_reload > 8) 
        p2 = duty_table[pulse2.duty][pulse2.duty_seq] ? (pulse2.constant_volume ? pulse2.volume : pulse2.env_vol) : 0;
    
    if (triangle.enable && triangle.length_counter > 0 && triangle.linear_counter > 0) 
        t = triangle_table[triangle.duty_seq];
    
    if (noise.enable && noise.length_counter > 0 && (noise.shift_register & 0x0001) == 0) 
        n = noise.constant_volume ? noise.volume : noise.env_vol;

    p1 *= (vol_pulse1 / 50.0);
    p2 *= (vol_pulse2 / 50.0);
    t  *= (vol_triangle / 50.0);
    n  *= (vol_noise / 50.0);

    double pulse_out = 0.0;
    if (p1 + p2 > 0.0) {
        pulse_out = 95.88 / ((8128.0 / (p1 + p2)) + 100.0);
    }
    
    double tnd_out = 0.0;
    double d = dmc.output_level; 
    if (t + n + d > 0.0) {
        tnd_out = 159.79 / ((1.0 / ((t / 8227.0) + (n / 12241.0) + (d / 22638.0))) + 100.0);
    }

    return pulse_out + tnd_out;
}
