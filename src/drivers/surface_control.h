/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  surface_control.h
 * @brief Surface Control driver class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SURFACE_CONTROL_H
#define _SURFACE_CONTROL_H

#include <mutex>
#include <vector>
#include <fstream>
#include "common.h"

// Surface Control types
enum class SurfaceControlType
{
    KNOB,
    SWITCH,
    UNKNOWN
};

// Switch modes
enum class SwitchMode
{
    TOGGLE,
    TOGGLE_LED_PULSE,
    TOGGLE_RELEASE,
    TOGGLE_HOLD,
    PUSH,
    LATCH_PUSH,
    PUSH_NO_LED
};

// Controller Status
enum class ControllerStatus
{
    STATUS_ERROR,
    BOOTLOADER_ACTIVE,
    FIRMWARE_ACTIVE
};

// Knob state
struct KnobState
{
    // Constants
    static const uint STATE_MOVING_TO_TARGET = 0x02;
    static const uint STATE_TAP_DETECTED = 0x01;
    
    // State and position
    uint16_t state;
    uint16_t position;
};

// Surface Control mode
struct HapticMode
{
    SurfaceControlType type;
    std::string name;
    bool default_mode;

    // Knob related params
    int knob_start_pos;
    uint knob_width;
    int knob_actual_start_pos;
    uint knob_actual_width;    
    uint knob_num_detents;
    uint knob_friction;
    uint knob_detent_strength;
    std::vector<std::pair<bool,uint>> knob_indents;

    // Switch related params
    SwitchMode switch_mode;

    // Constructor
    HapticMode()
    {
        type = SurfaceControlType::UNKNOWN;
        name = "";
        default_mode = false;
        knob_start_pos = -1;
        knob_width = 360;
        knob_actual_start_pos = -1;
        knob_actual_width = 360;        
        knob_num_detents = 0;
        knob_friction = 0;
        knob_detent_strength = 0;
        knob_indents.clear();
        switch_mode = SwitchMode::PUSH;
    }

    // Public functions
    bool knob_haptics_on() const
    {
        // Indicate whether this mode has haptics switched on for a knob
        return ((knob_width < 360) ||
                knob_friction ||
                knob_num_detents || 
                (knob_indents.size() > 0));
    }
};

// Surface Control class
class SurfaceControl
{
public:
    // Helper functions
    static SurfaceControlType ControlTypeFromString(const char *type);

    // Constructor
    SurfaceControl();

    // Destructor
    virtual ~SurfaceControl();

    // Public functions
    int open();
    int close();
    void lock();
    void unlock();
    bool knob_is_active(uint num);
    int request_knob_states();
    int read_knob_states(KnobState *states);
    int read_switch_states(bool *states);
    int set_knob_haptic_mode(unsigned int num, const HapticMode& haptic_mode);
    int set_knob_position(unsigned int num, uint16_t position, bool robust=true);
    int set_switch_led_state(unsigned int num, bool led_on);
    void set_all_switch_led_states(bool leds_on);
    int commit_led_states();
    void reinit();

private:
    // Private data
    int _dev_handle;
    bool _panel_controller_active;
    bool _motor_controller_active[NUM_PHYSICAL_KNOBS];
    bool _motor_controller_knob_state_requested[NUM_PHYSICAL_KNOBS];
    bool _motor_controller_haptic_set[NUM_PHYSICAL_KNOBS];
    std::string _motor_controller_haptic_mode[NUM_PHYSICAL_KNOBS];
    uint8_t *_led_states;
    std::mutex _controller_mutex;
    int _selected_controller_addr;
#ifdef SURFACE_HW_I2C_INTERFACE_STATS
    uint num_i2c_writes;
    uint num_i2c_write_nacks;
    uint num_i2c_write_timeouts;
    uint num_i2c_write_errors;
    uint num_i2c_reads;
    uint num_i2c_read_nacks;
    uint num_i2c_read_timeouts;
    uint num_i2c_read_errors;
#endif
#ifdef SURFACE_HW_LOG_MC_CAL_RESULTS
    std::ofstream _motor_status_file;
#endif

    // Private functuions
    void _init_controllers();
    int _motor_controller_request_status(uint8_t mc_num);
    int _motor_controller_reboot(uint8_t mc_num);    
    int _motor_controller_set_addr(uint8_t mc_num);
    int _motor_controller_get_firmware_ver(uint8_t mc_num, uint8_t *ver);     
    int _start_panel_controller();
    int _start_motor_controller(uint8_t mc_num);
    int _motor_controller_request_cal_enc_params(uint8_t mc_num);
    int _motor_controller_check_cal_enc_params_status(uint8_t mc_num, uint8_t *status);
    int _motor_controller_request_find_datum(uint8_t mc_num);
    int _motor_controller_read_find_datum_status(uint8_t mc_num, uint8_t *status);
    int _motor_controller_set_haptic_mode(uint8_t mc_num, const HapticMode& haptic_mode);
    int _motor_controller_request_knob_state(uint8_t mc_num);
    int _motor_controller_read_knob_state(uint8_t mc_num, KnobState *states);
    int _motor_controller_set_position(uint8_t mc_num, uint16_t position, bool robust);
    int _panel_controller_read_switch_states(uint8_t *switch_states);
    int _panel_controller_set_led_states(uint8_t *led_states);
    int _select_motor_controller_default();
    int _select_motor_controller(uint8_t num);
    int _select_panel_controller();
    ControllerStatus _controller_read_status();
    int _start_controller();
    int _i2c_select_slave(uint8_t addr);
    int _i2c_read(void *buf, size_t buf_len);
    int _i2c_robust_write(const void *buf, size_t buf_len, bool readback, uint8_t readback_value=0);
    int _i2c_write(const void *buf, size_t buf_len);
#ifdef SURFACE_HW_LOG_MC_CAL_RESULTS
    void _log_mc_calibration_results(uint8_t mc_num, bool mc_ok, std::string datetime);
#endif
};

#endif  // _SURFACE_CONTROL_H
