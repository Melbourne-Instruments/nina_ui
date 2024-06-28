/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  analog_input_control.cpp
 * @brief Analog Input Control driver implementation.
 *-----------------------------------------------------------------------------
 */

#include <iostream>
#include <stdint.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include "analog_input_control.h"
#include "utils.h"
#include "logger.h"

// Analog Input Control device name
#define ANALOG_INPUT_CONTROL_I2C_BUS_NUM    "0"
#define ANALOG_INPUT_CONTROL_DEV_NAME       "/dev/i2c-" ANALOG_INPUT_CONTROL_I2C_BUS_NUM

// Analog Input ADC constants
constexpr uint8_t ANALOG_INPUT_ADC1_I2C_SLAVE_ADDR              = 24;
constexpr uint8_t ANALOG_INPUT_ADC2_I2C_SLAVE_ADDR              = 25;
constexpr uint8_t ANALOG_INPUT_ADC3_I2C_SLAVE_ADDR              = 27;
constexpr uint I2C_READ_RETRY_COUNT                             = 5;
constexpr uint I2C_WRITE_RETRY_COUNT                            = 5;
constexpr uint8_t ADC_PAGE_CONTROL_REG                          = 0;
constexpr uint8_t SW_RESET_REG                                  = 1;
constexpr uint8_t CLOCK_GEN_MULTIPLEXING_REG                    = 4;
constexpr uint8_t ADC_NADC_CLOCK_DIVIDER_REG                    = 18;
constexpr uint8_t ADC_MADC_CLOCK_DIVIDER_REG                    = 19;
constexpr uint8_t ADC_AOSR_REG                                  = 20;
constexpr uint8_t ADC_AUDIO_INTERFACE_CONTROL1_REG              = 27;
constexpr uint8_t ADC_DIGITAL_REG                               = 81;
constexpr uint8_t ADC_FINE_VOLUME_CONTROL_REG                   = 82;
constexpr uint8_t LEFT_ADC_VOLUME_CONTROL_REG                   = 83;
constexpr uint8_t RIGHT_ADC_VOLUME_CONTROL_REG                  = 84;
constexpr uint8_t LEFT_ADC_INPUT_SELECTION_FOR_LEFT_PGA_1_REG   = 52;
constexpr uint8_t LEFT_ADC_INPUT_SELECTION_FOR_LEFT_PGA_2_REG   = 54;
constexpr uint8_t RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_1_REG = 55;
constexpr uint8_t RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_2_REG = 57;
constexpr uint8_t LEFT_ANALOG_PGA_SETTINGS_REG                  = 59;
constexpr uint8_t RIGHT_ANALOG_PGA_SETTINGS_REG                 = 60;

//----------------------------------------------------------------------------
// AnalogInputControl
//----------------------------------------------------------------------------
AnalogInputControl::AnalogInputControl()
{
    // Initialise private data
    _dev_handle = -1;
    _adc1_ok = false;
    _adc2_ok = false;
    _adc3_ok = false;
    _combo_channel_input = ComboChannelInput::MIC_IN;
}

//----------------------------------------------------------------------------
// ~SurfaceControl
//----------------------------------------------------------------------------
AnalogInputControl::~AnalogInputControl()
{    
    // Is the Analog Input Control device still open?
    if (_dev_handle > 0)
    {
        // Close it
        close();
    }      
}

//----------------------------------------------------------------------------
// open
//----------------------------------------------------------------------------
int AnalogInputControl::open()
{
    int handle;

    // Return an error if already open
    if (_dev_handle > 0)
    {
        // The driver is already open
        return -EBUSY;
    }

    // Open the I2C bus
    handle = ::open(ANALOG_INPUT_CONTROL_DEV_NAME, O_RDWR);
    if (handle < 0)
    {
        // An error occurred opening the I2C device
        return handle;
    }

    // Save the device handle
    _dev_handle = handle;

    // Configure Analog Input ADCs
    _config_adcs();

    // All Analog Input ADCs configured OK?
    if (_adc1_ok && _adc2_ok && _adc3_ok)
    {
        // Log success
        DEBUG_MSG("Config Analog Input ADCs 1-3: OK");
        NINA_LOG_INFO(NinaModule::ANALOG_INPUT_CONTROL, "Config Analog Input ADCs 1-3: OK");
    }
    else
    {
        // Log one or more failures
        if (_adc1_ok)
            NINA_LOG_INFO(NinaModule::ANALOG_INPUT_CONTROL, "Config Analog Input ADC1: OK");
        else
        {
            MSG("ERROR: Config Analog Input ADC1: FAILED");
            NINA_LOG_ERROR(NinaModule::ANALOG_INPUT_CONTROL, "Config Analog Input ADC1: FAILED");
        }
        if (_adc2_ok)
            NINA_LOG_INFO(NinaModule::ANALOG_INPUT_CONTROL, "Config Analog Input ADC2: OK");
        else
        {
            MSG("ERROR: Config Analog Input ADC2: FAILED");
            NINA_LOG_ERROR(NinaModule::ANALOG_INPUT_CONTROL, "Config Analog Input ADC2: FAILED");
        }
        if (_adc3_ok)
            NINA_LOG_INFO(NinaModule::ANALOG_INPUT_CONTROL, "Config Analog Input ADC3: OK");
        else
        {
            MSG("ERROR: Config Analog Input ADC3: FAILED");
            NINA_LOG_ERROR(NinaModule::ANALOG_INPUT_CONTROL, "Config Analog Input ADC3: FAILED");
        }
    }
    NINA_LOG_FLUSH();
    return 0;
}

//----------------------------------------------------------------------------
// close
//----------------------------------------------------------------------------
int AnalogInputControl::close()
{
    // If the device is open
    if (_dev_handle > 0)
    {
        // Close it
        int res = ::close(_dev_handle);
        if (res < 0)
        {
            return res;
        }
        _dev_handle = -1;       
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_channel_mute
//----------------------------------------------------------------------------
int AnalogInputControl::set_channel_mute(AnalogInputChannel channel, bool mute)
{
    int ret;

    // Get the ADC mutex
    std::lock_guard<std::mutex> lock(_adc_mutex);

    // Parse the channel to mute/unmute
    switch (channel)
    {
        case AnalogInputChannel::LINE_1_IN_LEFT:
            // Mute/unmute LINE 1 IN LEFT
            ret = _set_adc_channel_mute(ANALOG_INPUT_ADC1_I2C_SLAVE_ADDR, true, mute);
            break;

        case AnalogInputChannel::LINE_2_IN_RIGHT:
            // Mute/unmute LINE 2 IN RIGHT
            ret = _set_adc_channel_mute(ANALOG_INPUT_ADC1_I2C_SLAVE_ADDR, false, mute);
            break;

        case AnalogInputChannel::LINE_3_IN_LEFT:
            // Mute/unmute LINE 3 IN LEFT
            ret = _set_adc_channel_mute(ANALOG_INPUT_ADC2_I2C_SLAVE_ADDR, true, mute);
            break;

        case AnalogInputChannel::COMBO_IN_RIGHT:
            // Mute/unmute COMBO IN RIGHT
            ret = _set_adc_channel_mute(ANALOG_INPUT_ADC2_I2C_SLAVE_ADDR, false, mute);
            break;

        case AnalogInputChannel::MIX_OUT_TAP_LEFT:
            // Mute/unmute MIX OUT TAP LEFT
            ret = _set_adc_channel_mute(ANALOG_INPUT_ADC3_I2C_SLAVE_ADDR, true, mute);
            break;

        case AnalogInputChannel::MIX_OUT_TAP_RIGHT:
            // Mute/unmute MIX OUT TAP RIGHT
            ret = _set_adc_channel_mute(ANALOG_INPUT_ADC3_I2C_SLAVE_ADDR, false, mute);
            break;

        default:
            // Unknown channel
            ret = -EINVAL;
    }
    return ret;
}

//----------------------------------------------------------------------------
// set_combo_channel
//----------------------------------------------------------------------------
int AnalogInputControl::set_combo_channel_input(ComboChannelInput channel)
{
    int ret = 0;

    // Has the combo channel input changed?
    if (channel != _combo_channel_input)
    {
        // Get the ADC mutex
        std::lock_guard<std::mutex> lock(_adc_mutex);

        // Select the ADC2 slave
        ret = _select_adc_slave(ANALOG_INPUT_ADC2_I2C_SLAVE_ADDR);
        if (ret == 0)
        {
            // Select page 1
            ret = _write_adc_reg(ADC_PAGE_CONTROL_REG, 0x01);
            if (ret == 0)
            {              
                // Set the new combo channel input
                if (channel == ComboChannelInput::LINE_4_IN)
                {
                    // Set the combo channel input to LINE 4 - gain 0dB
                    ret = _write_adc_reg(RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_1_REG, 0x3F);
                    if (ret == 0)
                    {
                        ret = _write_adc_reg(RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_2_REG, 0x3F);
                        if (ret == 0)
                        {
                            ret = _write_adc_reg(RIGHT_ANALOG_PGA_SETTINGS_REG, 0x00);
                            if (ret == 0)
                                _combo_channel_input = channel;
                        }
                    }
                }
                else
                {
                    // Set the combo channel input to MIC - gain 20dB
                    ret = _write_adc_reg(RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_1_REG, 0xFF);
                    if (ret == 0)
                    {
                        ret = _write_adc_reg(RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_2_REG, 0x0F);
                        if (ret == 0)
                        {
                            ret = _write_adc_reg(RIGHT_ANALOG_PGA_SETTINGS_REG, 0x28);
                            if (ret == 0)
                                _combo_channel_input = channel;
                        }
                    }
                }
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _config_adcs
//----------------------------------------------------------------------------
void AnalogInputControl::_config_adcs()
{
    // Configure the ADCs
    _adc1_ok = _config_adc1() == 0;
    _adc2_ok = _config_adc2() == 0;
    _adc3_ok = _config_adc3() == 0;
}

//----------------------------------------------------------------------------
// _config_adc1
//----------------------------------------------------------------------------
int AnalogInputControl::_config_adc1()
{
    int ret;

    // Select the ADC1 slave
    ret = _select_adc_slave(ANALOG_INPUT_ADC1_I2C_SLAVE_ADDR);
    if (ret == 0)
    {
        // Configure the ADC1 - LINE 1 IN LEFT, LINE 2 IN RIGHT
        ret = _config_adc(0xFF, 0x0F, 0x3F, 0x3F);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _config_adc2
//----------------------------------------------------------------------------
int AnalogInputControl::_config_adc2()
{
    int ret;

    // Select the ADC2 slave
    ret = _select_adc_slave(ANALOG_INPUT_ADC2_I2C_SLAVE_ADDR);
    if (ret == 0)
    {
        // Configure ADC2 - LINE 3 IN LEFT, COMBO IN RIGHT
        // The COMBO IN RIGHT defaults to MIC IN - gain 20dB
        ret = _config_adc(0x3F, 0x3F, 0xFF, 0x0F);
        if (ret == 0)
            ret = _write_adc_reg(RIGHT_ANALOG_PGA_SETTINGS_REG, 0x28);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _config_adc3
//----------------------------------------------------------------------------
int AnalogInputControl::_config_adc3()
{
    int ret;

    // Select the ADC3 slave
    ret =_select_adc_slave(ANALOG_INPUT_ADC3_I2C_SLAVE_ADDR);
    if (ret == 0)
    {
        // Configure ADC3 - MIX OUT TAP IN LEFT/RIGHT
        ret = _config_adc(0xFC, 0x3F, 0xF3, 0x3F);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _config_adc
//----------------------------------------------------------------------------
int AnalogInputControl::_config_adc(uint8_t left_adc_input_sel1, uint8_t left_adc_input_sel2, uint8_t right_adc_input_sel1, uint8_t right_adc_input_sel2)
{
    // Set page 0 registers
    if (int ret = _write_adc_reg(ADC_PAGE_CONTROL_REG, 0x00); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(SW_RESET_REG, 0x01); ret != 0)
        return ret;    
    if (int ret = _write_adc_reg(CLOCK_GEN_MULTIPLEXING_REG, 0x0C); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_NADC_CLOCK_DIVIDER_REG, 0x81); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_MADC_CLOCK_DIVIDER_REG, 0x84); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_AOSR_REG, 0x40); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_AUDIO_INTERFACE_CONTROL1_REG, 0xE0); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_DIGITAL_REG, 0xC0); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(ADC_FINE_VOLUME_CONTROL_REG, 0x00); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(LEFT_ADC_VOLUME_CONTROL_REG, 0x00); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(RIGHT_ADC_VOLUME_CONTROL_REG, 0x00); ret != 0)
        return ret;

    // Set page 1 registers
    if (int ret = _write_adc_reg(ADC_PAGE_CONTROL_REG, 0x01); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(LEFT_ADC_INPUT_SELECTION_FOR_LEFT_PGA_1_REG, left_adc_input_sel1); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(LEFT_ADC_INPUT_SELECTION_FOR_LEFT_PGA_2_REG, left_adc_input_sel2); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_1_REG, right_adc_input_sel1); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(RIGHT_ADC_INPUT_SELECTION_FOR_RIGHT_PGA_2_REG, right_adc_input_sel2); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(LEFT_ANALOG_PGA_SETTINGS_REG, 0x00); ret != 0)
        return ret;
    if (int ret = _write_adc_reg(RIGHT_ANALOG_PGA_SETTINGS_REG, 0x00); ret != 0)
        return ret;
    return 0;
}

//----------------------------------------------------------------------------
// _set_adc_channel_mute
//----------------------------------------------------------------------------
int AnalogInputControl::_set_adc_channel_mute(uint8_t addr, bool left, bool mute)
{
    // Select the ADC
    int ret = _select_adc_slave(addr);
    if (ret == 0)
    {
        // Select page 1
        ret = _write_adc_reg(ADC_PAGE_CONTROL_REG, 0x01);
        if (ret == 0)
        {       
            // Mute/unmute the ADC channel
            ret = _write_adc_reg((left ? LEFT_ANALOG_PGA_SETTINGS_REG : RIGHT_ANALOG_PGA_SETTINGS_REG),
                                (mute ? 0x80 : 0x00));
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _select_adc_slave
//----------------------------------------------------------------------------
int AnalogInputControl::_select_adc_slave(uint8_t addr)
{
    // Select the ADC slave
    int ret = _i2c_select_slave(addr);
    if (ret)
        DEBUG_MSG("Could not select the Analog Control ADC I2C slave: " << ret);
    return ret;
}

//----------------------------------------------------------------------------
// _write_adc_reg
//----------------------------------------------------------------------------
int AnalogInputControl::_write_adc_reg(uint8_t reg, uint8_t val)
{
    uint8_t cmd[sizeof(uint8_t)*2];

    // Write the ADC register
    cmd[0] = reg;
    cmd[1] = val;
    int ret = _i2c_write(cmd, sizeof(cmd));
    if (ret) 
        DEBUG_MSG("Error writing to the Analog Control ADC I2C slave: " << ret);
    return ret;
}

//------------------------
// Low-level I2C functions
//------------------------

//----------------------------------------------------------------------------
// _i2c_select_slave
//----------------------------------------------------------------------------
int AnalogInputControl::_i2c_select_slave(uint8_t addr)
{
    // Select the requested I2C slave
    int ret = ioctl(_dev_handle, I2C_SLAVE, addr);
    if (ret < 0)
    {
        // Select save failed
        return -errno;
    }
    return 0;
}

//----------------------------------------------------------------------------
// _i2c_read
//----------------------------------------------------------------------------
int AnalogInputControl::_i2c_read(void *buf, size_t buf_len) 
{
    uint retry_count = I2C_READ_RETRY_COUNT;
    int ret;

    // Perform the I2C reads with retries
    while (retry_count--)
    {
        // Read the required bytes from the I2C slave
        ret = read(_dev_handle, buf, buf_len);
        if (ret >= 0)
        {
            // Were the required number of bytes read?
            if ((size_t)ret == buf_len)
            {
                // Read was successful
                ret = 0;               
            }
            else
            {
                // The number of required bytes were not returned, this
                // is treated as a read error
                ret = -EIO;                
            }
            break;
        }
        else
        {
            // Set the return value to -errno in case we need to return
            ret = -errno;

            // Did a timeout occur?
            if (errno == ETIMEDOUT)
            {
                // If a timeout occurred then there is no I2C contact
                // with the device, and we can stop trying the read             
                break;
            }          
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _i2c_write
//----------------------------------------------------------------------------
int AnalogInputControl::_i2c_write(const void *buf, size_t buf_len) 
{
    uint retry_count = I2C_WRITE_RETRY_COUNT;
    int ret;

    // Perform the I2C writes with retries
    while (retry_count--)
    {
        // Write the bytes to the I2C device
        ret = write(_dev_handle, buf, buf_len);
        if (ret >= 0)
        {
            // Were the required number of bytes written?
            if ((size_t)ret == buf_len)
            {
                // Write was successful
                ret = 0;              
            }
            else
            {
                // The number of required bytes were not written, this
                // is treated as a write error
                ret = -EIO;             
            }
            break;
        }
        else
        {
            // Set the return value to -errno in case we need to return
            ret = -errno;

            // Did a timeout occur?
            if (errno == ETIMEDOUT)
            {
                // If a timeout occurred then there is no I2C contact
                // with the device, and we can stop trying the write              
                break;
            }
        }
    }
    return ret; 
}
