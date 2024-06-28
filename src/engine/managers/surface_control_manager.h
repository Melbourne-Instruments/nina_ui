/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  surface_control_manager.h
 * @brief Surface Control Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SURFACE_CONTROL_MANAGER
#define _SURFACE_CONTROL_MANAGER

#include <iostream>
#include "base_manager.h"
#include "event.h"
#include "surface_control.h"
#include "daw_manager.h"
#include <iostream>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <vector>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include "param.h"

// Knob control
struct KnobControl
{
    // Public variables
    uint num;
    uint16_t position;
    int16_t position_delta;
    bool use_large_movement_threshold;
    uint poll_skip_count;
    uint small_movement_threshold_count;
    uint polls_since_last_threshold_hit;
    bool moving_to_target;
    std::chrono::_V2::steady_clock::time_point large_movement_time_start;
    bool moving_to_large_threshold;
    bool morphable;
};

// Switch control
struct SwitchControl
{
    // Public variables
    uint num;
    uint logical_state;
    bool physical_state;
    std::chrono::_V2::steady_clock::time_point push_time_start;
    Timer *led_pulse_timer;
    bool led_state;
    bool latched;
    bool morphable;
    bool push_time_processed;
};

// Surfance Control Manager class
class SurfaceControlManager: public BaseManager
{
public:
    // Constructor
    SurfaceControlManager(EventRouter *event_router);

    // Destructor
    ~SurfaceControlManager();

    // Public functions
    bool start();
    void stop();
    void process();
    void process_event(const BaseEvent *event);
    void process_surface_control();

private:
    // Private variables
    mutable std::mutex _mutex;
    SurfaceControl *_surface_control;
    SwitchControl _switch_controls[NUM_PHYSICAL_SWITCHES];
    EventListener *_osc_listener;
    EventListener *_midi_listener;
    EventListener *_fm_reload_presets_listener;
    EventListener *_fm_param_changed_listener;
    EventListener *_gui_param_changed_listener;
    EventListener *_gui_sfc_listener;
    EventListener *_seq_sfc_listener;
    EventListener *_kbd_sfc_listener;
#ifndef NO_XENOMAI
    pthread_t _surface_control_thread;
#else
    std::thread* _surface_control_thread;
#endif
    KnobControl _knob_controls[NUM_PHYSICAL_KNOBS];
    std::atomic<bool> _exit_surface_control_thread;
    bool _surface_control_init;
    int _morph_knob_num;
    KnobParam *_morph_knob_param;
    bool _presets_reloaded;

    // Private functions
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_reload_presets();
    void _process_sfc_func(const SurfaceControlFunc &sfc_func);
    void _set_knob_control_position_from_preset(uint num);
    void _set_switch_control_value_from_preset(uint num);
    void _process_sfc_param_changed(Param *param);
    void _process_param_changed_mapped_params(const Param *param, const Param *skip_param, bool displayed);
    void _send_control_param_change_events(const Param *param);
    void _set_knob_control_position(const KnobParam *param, bool robust=true);
    void _set_switch_control_value(const SwitchParam *param);
    void _set_knob_control_haptic_mode(const KnobParam *param);
    void _process_physical_knob(KnobControl &knob_control, const KnobState &knob_state);
    void _process_physical_switch(SwitchControl &switch_control, bool physical_state);
    void _morph_control(SurfaceControlType type, uint num);
    void _commit_led_control_states();
    void _switch_led_pulse_timer_callback(SwitchControl *switch_control);
    void _register_params();
};

#endif  //_SURFACE_CONTROL_MANAGER
