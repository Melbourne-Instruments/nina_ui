/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  analog_input_control.h
 * @brief Analog Input Control driver class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _ANALOG_INPUT_CONTROL_H
#define _ANALOG_INPUT_CONTROL_H

#include <mutex>
#include "common.h"

// Analog Input Channel
enum class AnalogInputChannel
{
    LINE_1_IN_LEFT,
    LINE_2_IN_RIGHT,
    LINE_3_IN_LEFT,
    COMBO_IN_RIGHT,
    MIX_OUT_TAP_LEFT,
    MIX_OUT_TAP_RIGHT
};

// Combo Channel Input
enum class ComboChannelInput
{
    MIC_IN,
    LINE_4_IN
};

// Analog Input Control class
class AnalogInputControl
{
public:
    // Constructor
    AnalogInputControl();

    // Destructor
    virtual ~AnalogInputControl();

    // Public functions
    int open();
    int close();
    int set_channel_mute(AnalogInputChannel channel, bool mute);
    int set_combo_channel_input(ComboChannelInput channel);

private:
    // Private data
    int _dev_handle;
    bool _adc1_ok;
    bool _adc2_ok;
    bool _adc3_ok;
    ComboChannelInput _combo_channel_input;
    std::mutex _adc_mutex;

    // Private functuions
    void _config_adcs();
    int _config_adc1();
    int _config_adc2();
    int _config_adc3();
    int _config_adc(uint8_t left_adc_input_sel1, uint8_t left_adc_input_sel2, uint8_t right_adc_input_sel1, uint8_t right_adc_input_sel2);
    int _set_adc_channel_mute(uint8_t addr, bool left_channel, bool mute);
    int _select_adc_slave(uint8_t addr);
    int _write_adc_reg(uint8_t reg, uint8_t val);    
    int _i2c_select_slave(uint8_t addr);
    int _i2c_read(void *buf, size_t buf_len);
    int _i2c_write(const void *buf, size_t buf_len);
};

#endif  // _ANALOG_INPUT_CONTROL_H
