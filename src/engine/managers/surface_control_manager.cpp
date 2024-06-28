/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  surface_control_manager.cpp
 * @brief Surface Control Manager implementation
 *-----------------------------------------------------------------------------
 */
#include <cstring>
#include <atomic>
#include <type_traits>
#include <sys/reboot.h>
#include "event.h"
#include "event_router.h"
#include "surface_control_manager.h"
#include "utils.h"
#include "logger.h"

// Constants
constexpr uint POLL_THRESH_COUNT = 300;
constexpr uint SMALL_MOVEMENT_THRESHOLD_MAX_COUNT  = 20;
constexpr int KNOB_MOVED_TO_TARGET_POLL_SKIP_COUNT = 6;
constexpr int TAP_DETECTED_POLL_SKIP_COUNT         = 10;
#ifndef TESTING_SURFACE_HARDWARE
constexpr int SURFACE_HW_POLL_SECONDS     = 0;
constexpr int SURFACE_HW_POLL_NANOSECONDS = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(30)).count();
#else
constexpr int SURFACE_HW_POLL_SECONDS    = std::chrono::seconds(1).count();
constexpr int SURFACE_HW_POLL_NANOSECONDS = 0;
#endif
constexpr uint KNOB_LARGE_MOVEMENT_TIME_THRESHOLD = std::chrono::milliseconds(2000).count();
constexpr uint SWITCH_PUSH_LATCH_THRESHOLD        = std::chrono::milliseconds(500).count();
constexpr uint SWITCH_TOGGLE_LED_DUTY             = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(500)).count();
constexpr uint SWITCH_TOGGLE_RELEASE_THRESHOLD    = std::chrono::milliseconds(500).count();
constexpr uint SWITCH_TOGGLE_HOLD_THRESHOLD       = std::chrono::milliseconds(1000).count();

// Static functions
static void *_process_surface_control(void* data);


//----------------------------------------------------------------------------
// SurfaceControlManager
//----------------------------------------------------------------------------
SurfaceControlManager::SurfaceControlManager(EventRouter *event_router) :
#ifndef NO_XENOMAI
    BaseManager(NinaModule::SURFACE_CONTROL, "SurfaceControlManager", event_router, true)
#else
    BaseManager(NinaModule::SURFACE_CONTROL, "SurfaceControlManager", event_router, false)
#endif
{
    // Initialise class data
    _surface_control = new SurfaceControl();
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++) {
        _knob_controls[i].num = i;
        _knob_controls[i].position = 0;
        _knob_controls[i].position_delta = 0;
        _knob_controls[i].use_large_movement_threshold = true;
        _knob_controls[i].poll_skip_count = 0;
        _knob_controls[i].small_movement_threshold_count = 0;
        _knob_controls[i].polls_since_last_threshold_hit = 0;
        _knob_controls[i].moving_to_target = false;
        _knob_controls[i].moving_to_large_threshold = false;
        if ((i == 0) || (i == 8) || (i == 31) || (i == 29)) {
            // The data, tempo, morph, and effects knobs are NOT morphable
            // Note: Shouldn't use magic numbers here
            _knob_controls[i].morphable = false;
        }
        else {
            _knob_controls[i].morphable = true;
        }
    }
    for (uint i=0; i<NUM_PHYSICAL_SWITCHES; i++) {
        _switch_controls[i].num = i;
        _switch_controls[i].logical_state = 0.0;
        _switch_controls[i].physical_state = 0.0;
        _switch_controls[i].led_pulse_timer = nullptr;
        _switch_controls[i].latched = false;
        _switch_controls[i].push_time_processed = false;
        if ((i == 14) || (i == 17)) {
            // Only SYNC and SUB switches are morphable
            // Note: Shouldn't use magic numbers here
            _switch_controls[i].morphable = true;
        }
        else {
            _switch_controls[i].morphable = false;
        }
    }
    _osc_listener = 0;
    _midi_listener = 0;
    _fm_reload_presets_listener = 0;
    _fm_param_changed_listener = 0;
    _gui_param_changed_listener = 0;
    _gui_sfc_listener = 0;
    _seq_sfc_listener = 0;
    _kbd_sfc_listener = 0;
    _surface_control_thread = 0;
    _exit_surface_control_thread = false;
    _surface_control_init = false;
    _morph_knob_param = 0;
    _presets_reloaded = false;

    // Register the Surface Control params
    _register_params();	    

    // Initialise Xenomai
    utils::init_xenomai();
}

//----------------------------------------------------------------------------
// ~SurfaceControlManager
//----------------------------------------------------------------------------
SurfaceControlManager::~SurfaceControlManager()
{
    // Stop the switch LED pulse timer tasks (if any)
    for (auto sc : _switch_controls) {
        if (sc.led_pulse_timer) {
            sc.led_pulse_timer->stop();
            delete sc.led_pulse_timer;
            sc.led_pulse_timer = nullptr;
        }
    }
    
    // Clean up allocated data
    if (_surface_control)
        delete _surface_control;
    if (_osc_listener)
        delete _osc_listener;
    if (_midi_listener)
        delete _midi_listener;
    if (_fm_reload_presets_listener)
        delete _fm_reload_presets_listener;
    if (_fm_param_changed_listener)
        delete _fm_param_changed_listener;
    if (_gui_param_changed_listener)
        delete _gui_param_changed_listener;
    if (_gui_sfc_listener)
        delete _gui_sfc_listener;
    if (_seq_sfc_listener)
        delete _seq_sfc_listener;        
    if (_kbd_sfc_listener)
        delete _kbd_sfc_listener;        
};

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool SurfaceControlManager::start()
{
    // Open the surface control
    int res = _surface_control->open();
    if (res < 0)
    {
        // Could not open the surface control, show an error
        MSG("ERROR: Could not open the surface control");
        NINA_LOG_CRITICAL(module(), "Could not open the Surface Control: {}", res);
    }
    else
    {
        // If we are not in maintenance mode
        if (!utils::maintenance_mode()) {
            // Set the initial knob haptic modes (physical knobs only)
            for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
            {
                // Get the knob param
                auto param = utils::get_param(KnobParam::ParamPath(i));
                if (param)
                {
                    // Set the knob control haptic mode
                    _set_knob_control_haptic_mode(static_cast<KnobParam *>(param));
                }
            }
        }

        // The Surface Control was successfully initialised
        _surface_control_init = true;        

        // Switch all LEDs on and then off
        _surface_control->set_all_switch_led_states(true);
        _commit_led_control_states();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        _surface_control->set_all_switch_led_states(false);
        _commit_led_control_states();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));

        // If we are not in maintenance mode
        if (!utils::maintenance_mode()) {
            // Before starting the Surface Control Manager polling loop, process all the preset values
            _process_reload_presets();

            // Get the morph knob number, if any
            _morph_knob_num = utils::get_morph_knob_num();
        }

#ifndef NO_XENOMAI
        // Create the real-time task to start process the surface control
        // Note: run in secondary mode      
        res = utils::create_rt_task(&_surface_control_thread, _process_surface_control, this, SCHED_OTHER);
        if (res < 0)
        {
            // Error creating the RT thread, show the error
            MSG("ERROR: Could not start the surface control processing thread: " << errno);
            return false;        
        }
#else
        // Create a normal thread to process the surface control
        _surface_control_thread = new std::thread(_process_surface_control, this);
#endif
    }

    // Send a system function message to clear the boot warning screen now the Surface Manager has started and
    // performed any initialisation of the surface controls
    _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SFC_INIT, 0, module())));

    // Call the base manager method
    return BaseManager::start();
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void SurfaceControlManager::stop()
{
    // Call the base manager
    BaseManager::stop();

    // Surface control task running?
    if (_surface_control_thread != 0)
    {
        // Stop the surface control real-time task
        _exit_surface_control_thread = true;
#ifndef NO_XENOMAI
        utils::stop_rt_task(&_surface_control_thread);
#else
		if (_surface_control_thread->joinable())
			_surface_control_thread->join(); 
#endif
        _surface_control_thread = 0;       
    }

    // Close the surface control
    int res = _surface_control->close();
    if (res < 0)
    {
        // Error closing the surface control, show the error
        DEBUG_BASEMGR_MSG("Could not close the surface control, close has failed: " << res);
    }
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void SurfaceControlManager::process()
{
    // Create and add the listeners
    _osc_listener = new EventListener(NinaModule::OSC, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_osc_listener);
    _midi_listener = new EventListener(NinaModule::MIDI_DEVICE, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_midi_listener);
    _fm_reload_presets_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_fm_reload_presets_listener);
    _fm_param_changed_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_fm_param_changed_listener);
    _gui_param_changed_listener = new EventListener(NinaModule::GUI, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_gui_param_changed_listener);
    _gui_sfc_listener = new EventListener(NinaModule::GUI, EventType::SURFACE_CONTROL_FUNC, this);
    _event_router->register_event_listener(_gui_sfc_listener);
    _seq_sfc_listener = new EventListener(NinaModule::SEQUENCER, EventType::SURFACE_CONTROL_FUNC, this);
    _event_router->register_event_listener(_seq_sfc_listener);
    _kbd_sfc_listener = new EventListener(NinaModule::KEYBOARD, EventType::SURFACE_CONTROL_FUNC, this);
    _event_router->register_event_listener(_kbd_sfc_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void SurfaceControlManager::process_event(const BaseEvent * event)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
            // Process the param changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;

        case EventType::RELOAD_PRESETS:
            // Process reloading of the presets
            _process_reload_presets();
            break;

        case EventType::SURFACE_CONTROL_FUNC:
            // Process the surface control function
            _process_sfc_func(static_cast<const SurfaceControlFuncEvent *>(event)->sfc_func());
            break;

		default:
            // Event unknown, we can ignore it
            break;
	}
}

//----------------------------------------------------------------------------
// process_surface_control
// Note: RT thread
//----------------------------------------------------------------------------
void SurfaceControlManager::process_surface_control()
{
    struct timespec poll_time;
    KnobState knob_states[NUM_PHYSICAL_KNOBS];    
    bool switch_physical_states[NUM_PHYSICAL_SWITCHES];
    int knob_states_res;
    int switch_states_res;
    std::chrono::system_clock::time_point start;
    bool poweroff_initiated = false;
    bool poweroff = false;
    bool reinit_sfc_control_initiated = false;
    bool reinit_sfc_control = false;
    Param *morph_mode_param = 0;

    // Initialise the knob and switch state arrays to zeros
    std::memset(knob_states, 0, sizeof(knob_states));
    std::memset(switch_physical_states, 0, sizeof(switch_physical_states));

    // Is there a physical morph knob param?
    if ((_morph_knob_num != -1) && (_morph_knob_num < NUM_PHYSICAL_KNOBS))
    {
        // Get the morph knob param
        _morph_knob_param = utils::get_morph_knob_param();

        // Get the morph mode param
        morph_mode_param = utils::get_param_from_ref(utils::ParamRef::MORPH_MODE);
    }

    // Set the thread poll time (in nano-seconds)
    std::memset(&poll_time, 0, sizeof(poll_time));
    poll_time.tv_sec = SURFACE_HW_POLL_SECONDS;
    poll_time.tv_nsec = SURFACE_HW_POLL_NANOSECONDS;   

    // Block the thread for the poll time
#ifndef NO_XENOMAI
    utils::rt_task_nanosleep(&poll_time); 
#else
    ::nanosleep(&poll_time, NULL);
#endif

    // Loop forever until exited
    while (!_exit_surface_control_thread && !poweroff)
    {
        {
            // Get the mutex
            std::lock_guard<std::mutex> lock(_mutex);

            bool morph_knob_in_default_state = true;
            bool morph_params_changed = false;

            // If not in maintenance mode
            if (!utils::maintenance_mode()) {
                // Re-init the panel?
                if (reinit_sfc_control)
                {
                    // Re-initialise the surface control
                    MSG("Re-initialising the Surface Control....");
                    _surface_control->reinit();

                    // Set the knob haptic modes
                    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
                    {
                        // Get the knob param
                        auto param = utils::get_param(KnobParam::ParamPath(i));
                        if (param)
                        {
                            // Set the knob control haptic mode
                            _set_knob_control_haptic_mode(static_cast<KnobParam *>(param));
                        }
                    }  

                    // Switch all LEDs on and then off
                    _surface_control->set_all_switch_led_states(true);
                    _commit_led_control_states();
                    std::this_thread::sleep_for(std::chrono::milliseconds(600));
                    _surface_control->set_all_switch_led_states(false);
                    _commit_led_control_states();
                    std::this_thread::sleep_for(std::chrono::milliseconds(600));

                    // process all the preset values
                    _process_reload_presets();
                    reinit_sfc_control = false;
                    reinit_sfc_control_initiated = false;
                }

                // Get the current state of the morph knob, if it is not in the default state,
                // then we don't process the knob as morph
                morph_knob_in_default_state = utils::get_param_state(_morph_knob_param->get_path()) == "default";

                // Are we currently morphing?
                // Lock before performing this action so that the File Manager controller doesn't clash with this
                // processing      
                utils::morph_lock();
                bool morph_state = utils::is_morph_enabled();
                bool prev_morph_state = utils::get_prev_morph_state();
                if (utils::is_morph_on() && (morph_state || prev_morph_state) && morph_knob_in_default_state) {
                    // Yes - if we are in morph dance mode, retrieve the state params
                    if (utils::get_morph_mode(morph_mode_param->get_value()) == MorphMode::DANCE) {
                        // Dance mode, retrieve the params
                        auto s = std::chrono::steady_clock::now();
                        morph_params_changed = static_cast<DawManager *>(utils::get_manager(NinaModule::DAW))->get_patch_state_params();
                        auto f = std::chrono::steady_clock::now();
                        float tt = std::chrono::duration_cast<std::chrono::microseconds>(f - s).count();
                        if(tt > 10000) {
                            DEBUG_MSG("get_patch_state_params time (us): " << tt);
                        }
                    }
                }
                utils::set_prev_morph_state();
                utils::morph_unlock();
            }

            // Lock the Surface Control
            _surface_control->lock();

            // Read the switch states
            switch_states_res = _surface_control->read_switch_states(switch_physical_states);

            // Request the knob states
            knob_states_res = _surface_control->request_knob_states(); 
            if (knob_states_res == 0)
            {
                // Read the knob states
                knob_states_res = _surface_control->read_knob_states(knob_states);            
            }

            // Unlock the Surface Control
            _surface_control->unlock();

            // If not in maintenance mode
            if (!utils::maintenance_mode()) {
                // Always process the moph knob
                _process_physical_knob(_knob_controls[_morph_knob_num], knob_states[_morph_knob_num]);
            }

            // If either we are not morphing, or we are morphing but no paramters
            // have changed (i.e. DJ mode or the morph knob is stationary)
            if (!morph_params_changed)
            {
                // Switches read OK?
                if (switch_states_res == 0) {
                    // Process each switch physical
                    for (int i=0; i<NUM_PHYSICAL_SWITCHES; i++) {
                        // Process the switch
                        _process_physical_switch(_switch_controls[i], switch_physical_states[i]);

                        // Update the switch physical state for the next poll processing
                        _switch_controls[i].physical_state = switch_physical_states[i];
                    }           
                }

                // Are all three soft buttons pressed?
                if (_switch_controls[0].logical_state == 1 && _switch_controls[1].logical_state == 1 &&
                    _switch_controls[2].logical_state == 1)
                {
                    if (!poweroff)
                    {
                        if (!poweroff_initiated)
                        {
                            start = std::chrono::high_resolution_clock::now();
                            poweroff_initiated = true;
                        }
                        else
                        {
                            auto end = std::chrono::high_resolution_clock::now();
                            if (std::chrono::duration_cast<std::chrono::seconds>(end -  start).count() > 3)
                            {
                                // Poweroff the beast!
                                poweroff = true;
                                MSG("Power off....");
                                sync();
                                reboot(RB_POWER_OFF);
                            }
                        }
                    }
                }
                // Are ALT & ENTER pressed?
                else if (_switch_controls[0].logical_state == 0 && _switch_controls[1].logical_state == 1 && 
                        _switch_controls[2].logical_state == 1)
                {
                    if (!reinit_sfc_control_initiated)
                    {
                        start = std::chrono::high_resolution_clock::now();
                        reinit_sfc_control_initiated = true;
                    }
                    else
                    {
                        auto end = std::chrono::high_resolution_clock::now();
                        if (std::chrono::duration_cast<std::chrono::seconds>(end -  start).count() > 3)
                        {
                            // Re-initialise the surface control on the next poll loop
                            reinit_sfc_control = true;
                        }
                    }
                }
                else
                {
                    poweroff_initiated = false;
                    reinit_sfc_control_initiated = false;
                }

                // Process each knob position (except for the morph knob if morphing)
                for (int i=0; i<NUM_PHYSICAL_KNOBS; i++) {
                    if (_surface_control->knob_is_active(i) && 
                        ((i != _morph_knob_num) || !morph_knob_in_default_state)) {
                        _process_physical_knob(_knob_controls[i], knob_states[i]);
                    }
                }
            }
            else
            {
                // Morphing
                // Process each knob position
                for (int i=0; i<NUM_PHYSICAL_KNOBS; i++) {
                    if (_surface_control->knob_is_active(i)) {
                        // Morph the knob or process normally
                        _knob_controls[i].morphable ?
                            _morph_control(SurfaceControlType::KNOB, i) :
                            _process_physical_knob(_knob_controls[i], knob_states[i]);
                    }
                }

                // Process each switch
                for (int i=0; i<NUM_PHYSICAL_SWITCHES; i++) {
                    // If morphable
                    if (_switch_controls[i].morphable) {
                        // Norph the switch
                        _morph_control(SurfaceControlType::SWITCH, i);
                    }
                    else {
                        // Process the switch normally
                        _process_physical_switch(_switch_controls[i], switch_physical_states[i]);

                        // Update the switch physical state for the next poll processing
                        _switch_controls[i].physical_state = switch_physical_states[i];                    
                    }
                }                        
            }
            
            // Update the read knob positions for the next poll processing
            for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
                _knob_controls[i].position = knob_states[i].position;

            // Reset the presets loaded flag, if set
            if (_presets_reloaded) {
                _presets_reloaded = false;
            }

            // Always commit the LED states so that they are guaranteed to be in the correct state
            _commit_led_control_states();
        }

        // Sleep to give other tasks time to run
        ::nanosleep(&poll_time, NULL);  
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void SurfaceControlManager::_process_param_changed_event(const ParamChange &param_change)
{
    // If this is a Surface Control param change
    Param *param = utils::get_param(param_change.path.c_str());
    if (param && (param->module == NinaModule::SURFACE_CONTROL))
    {
        // Process the Surface Control param change
        _process_sfc_param_changed(param);
    }
}

//----------------------------------------------------------------------------
// _process_reload_presets
//----------------------------------------------------------------------------
void SurfaceControlManager::_process_reload_presets()
{
    // Get the Morph Value and Mode params so we can check them
    auto morph_value_param = utils::get_param_from_ref(utils::ParamRef::MORPH_VALUE);
    auto morph_mode_param = utils::get_param_from_ref(utils::ParamRef::MORPH_MODE);

    // Process the initial state and preset value for each knob
    for (int i=1; i<NUM_PHYSICAL_KNOBS; i++) {
        // Process this knob if:
        // - It is not morphable OR
        // - We are not currently morphing OR in DJ mode
        if (!_knob_controls[i].morphable ||
           (morph_mode_param && (morph_mode_param->get_position_value() == MorphMode::DJ)) ||
           (morph_value_param && (morph_value_param->get_value() == 0.0f || morph_value_param->get_value() == 1.0f))) {
            // Set the knob control position from the knob param preset
            _set_knob_control_position_from_preset(i);
        }
    }

    // Process the initial state and preset value for each switch
    for (int i=0; i<NUM_PHYSICAL_SWITCHES; i++) {
        // Process this switch if:
        // - It is not morphable OR
        // - We are not currently morphing OR in DJ mode
        if (!_switch_controls[i].morphable ||
           (morph_mode_param && (morph_mode_param->get_position_value() == MorphMode::DJ)) ||
           (morph_value_param && (morph_value_param->get_value() == 0.0f || morph_value_param->get_value() == 1.0f))) {        
            _set_switch_control_value_from_preset(i);
        }
    }

    // Indicate presets have been re-loaded
    _presets_reloaded = true;
}

//----------------------------------------------------------------------------
// _process_sfc_func
//----------------------------------------------------------------------------
void SurfaceControlManager::_process_sfc_func(const SurfaceControlFunc &sfc_func)
{
    // Parse the function
    switch (sfc_func.type)
    {
        case SurfaceControlFuncType::RESET_MULTIFN_SWITCHES:
        {
            // Reset the multi-function switches to the default state
            auto params = utils::get_multifn_switch_params();
            for (SwitchParam *sp : params)
            {
                // Always reset the latched state
                _switch_controls[sp->param_id].latched = false;

                // Force the switch into the OFF state
                _switch_controls[sp->param_id].logical_state = 0;

                // Set the switch LED state
                _surface_control->set_switch_led_state(_switch_controls[sp->param_id].num, false);

                // Update the switch parameter value
                sp->set_value(0);
            }
            break;            
        }

        case SurfaceControlFuncType::SET_MULTIFN_SWITCH:
        {
            auto param = utils::get_param(sfc_func.control_path.c_str());
            if (param)
            {
                // Set the specified switch to ON
                _switch_controls[param->param_id].logical_state = 1;

                // Set the switch LED state
                _surface_control->set_switch_led_state(_switch_controls[param->param_id].num, true);

                // Update the switch parameter value
                param->set_value(1.0);

                // Reset any other associated switches
                auto switch_num = param->param_id - utils::system_config()->get_first_multifn_switch_num(); 
                auto params = utils::get_multifn_switch_params();
                if (utils::is_active_multifn_switch(switch_num) && 
                    (utils::get_multifn_switches_mode() == MultifnSwitchesMode::SINGLE_SELECT))
                {
                    for (SwitchParam *sp : params)
                    {
                        if (utils::is_active_multifn_switch(sp->param_id - utils::system_config()->get_first_multifn_switch_num()) &&
                            (sp->param_id != param->param_id) && (_switch_controls[sp->param_id].logical_state)) {
                            _switch_controls[sp->param_id].logical_state = 0;

                            // Set the switch LED state
                            _surface_control->set_switch_led_state(_switch_controls[sp->param_id].num, false);

                            // Update the switch parameter value
                            sp->set_value(0);
                        }
                        else if (sp->param_id == param->param_id) {
                            // This is the selected position
                            sp->is_selected_position = true;
                        }
                    }
                }
            } 
            break;
        }

        case SurfaceControlFuncType::SET_SWITCH_VALUE:
        {
            auto param = static_cast<SwitchParam *>(utils::get_param(sfc_func.control_path.c_str()));
            if (param)
            {
                uint new_state = sfc_func.set_switch ? 1 : 0;

                // If the current switch state is not the state to set
                //if (_switch_controls[param->param_id].logical_state != new_state) {
                    // Set the switch ON/OFF
                    _switch_controls[param->param_id].logical_state = new_state;

                    // Set the switch LED state
                    _surface_control->set_switch_led_state(_switch_controls[param->param_id].num, sfc_func.set_switch);

                    // Update the switch parameter value
                    param->set_value(new_state);

                    // If this is a multi-function switch
                    if (param->multifn_switch) {
                        // Indicate it has been latched
                        _switch_controls[param->param_id].latched = sfc_func.set_switch ? true : false;
                    }
                //}
            }
            break;            
        }

        case SurfaceControlFuncType::SET_CONTROL_HAPTIC_MODE:
        {
            // Haptic mode specified?
            if (sfc_func.control_haptic_mode.size() > 0)
            {
                // Set the control haptic mode
                auto param = utils::get_param(sfc_func.control_path);
                if (param)
                {
                    auto sfc_param = static_cast<SurfaceControlParam *>(param);
                    if (sfc_param->module == module())
                    {
                         sfc_param->set_haptic_mode(sfc_func.control_haptic_mode);
                         
                        // Are we changing a knob control state?
                        if (sfc_param->type() == SurfaceControlType::KNOB)
                        {
                            // Set the knob control haptic mode
                            _set_knob_control_haptic_mode(static_cast<KnobParam *>(sfc_param));
                                                            
                            // Now set the knob position from the param
                            //_set_knob_control_position(static_cast<KnobParam *>(sfc_param));
                        }
                        // Are we changing a switch control value?
                        else if (sfc_param->type() == SurfaceControlType::SWITCH)
                        {
                            // Save the switch value from the param
                            //_set_switch_control_value(static_cast<SwitchParam *>(sfc_param));                        
                        }
                    }
                }
            }
            break;
        }

        case SurfaceControlFuncType::PUSH_POP_CONTROLS_STATE:
        {
            // Get all the params associated with the push and pop (if any)
            auto push_params = utils::get_params_with_state(sfc_func.push_controls_state);
            auto pop_params = utils::get_params_with_state(sfc_func.pop_controls_state);

            // Create a list of all params to process
            auto params = push_params;
            for (Param *pp : pop_params) {
                bool found = false;
                for (Param *p : params) {
                    if (p->get_path() == pp->get_path()) {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    params.push_back(pp);
            }

            // Process the state params
            for (Param *p : params)
            {
                // Are we only pushing a controls state?
                if (!sfc_func.popping_controls_state())
                {
                    // Push the new param state
                    p = utils::push_param_state(p->get_path(), sfc_func.push_controls_state);
                }
                // Are we only popping a controls state?
                else if (!sfc_func.pushing_controls_state())
                {
                    // Pop the most recent controls state
                    p = utils::pop_param_state(p->get_path(), sfc_func.pop_controls_state);
                }
                else
                {
                    // Popping and pushing a new controls state
                    p = utils::pop_and_push_param_state(p->get_path(), sfc_func.push_controls_state, sfc_func.pop_controls_state);
                }
                if (p && sfc_func.process_physical_control)
                {
                    // If the param is a Surface Control param, proces it
                    auto sfc_param = static_cast<SurfaceControlParam *>(p);
                    if (sfc_param->module == module())
                    {
                        // Are we changing a knob control value?
                        if (sfc_param->type() == SurfaceControlType::KNOB)
                        {
                            // Set the knob control haptic mode
                            _set_knob_control_haptic_mode(static_cast<KnobParam *>(sfc_param));
                                                            
                            // Now set the knob position from the param
                            _set_knob_control_position(static_cast<KnobParam *>(sfc_param));

                            // Create the control param change event
                            auto mapped_param_change = ParamChange(p->get_path(), p->get_value(), module());
                            _event_router->post_param_changed_event(new ParamChangedEvent(mapped_param_change));                            
                        }
                        // Are we changing a switch control value?
                        else if (sfc_param->type() == SurfaceControlType::SWITCH)
                        {
                            // Save the switch value from the param
                            _set_switch_control_value(static_cast<SwitchParam *>(sfc_param));                        
                        }
                    }
                }               
            }
            break;
        }
    }
}

//----------------------------------------------------------------------------
// _set_knob_control_position_from_preset
//----------------------------------------------------------------------------
void SurfaceControlManager::_set_knob_control_position_from_preset(uint num)
{
    // Get the knob param
    const KnobParam *param = static_cast<const KnobParam *>(utils::get_param(KnobParam::ParamPath(num).c_str()));
    if (param)
    {
        // Is this knob active?
        if (_surface_control->knob_is_active(num))
        {        
            // Set the knob control haptic mode
            _set_knob_control_haptic_mode(param);
        }

        // Now set the knob position from the param
        _set_knob_control_position(param);
    }
}

//----------------------------------------------------------------------------
// _set_switch_control_value_from_preset
//----------------------------------------------------------------------------
void SurfaceControlManager::_set_switch_control_value_from_preset(uint num)
{
    // Get the switch param - don't process multi-function switches
    const SwitchParam *param = static_cast<const SwitchParam *>(utils::get_param(SwitchParam::ParamPath(num).c_str()));
    if (param && !param->multifn_switch)
    {
        // Save the switch logical state so that it can be toggled at the next 
        // switch press
        _switch_controls[num].logical_state = (int)param->get_value();

        // Set the switch LED state
        _surface_control->set_switch_led_state(num, param->get_value() == 0 ? false : true);
    }
}

//----------------------------------------------------------------------------
// _process_sfc_param_changed
//----------------------------------------------------------------------------
void SurfaceControlManager::_process_sfc_param_changed(Param *param)
{
    // Get the param as a Surface Control param
    SurfaceControlParam *sfc_param = static_cast<SurfaceControlParam *>(param);
    
    // Parse the control type
    switch (sfc_param->type())
    {
        case SurfaceControlType::KNOB:
            // Process the knob control
            _set_knob_control_position(static_cast<KnobParam *>(sfc_param));
            break;

        case SurfaceControlType::SWITCH:
            // Process the switch control
            _set_switch_control_value(static_cast<SwitchParam *>(sfc_param));
            break;

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_mapped_params
//----------------------------------------------------------------------------
void SurfaceControlManager::_process_param_changed_mapped_params(const Param *param, const Param *skip_param, bool displayed)
{
    // Get the mapped params and linked param, if any, and process them
    auto mapped_params = param->get_mapped_params();
    auto linked_param = param->get_linked_param();
    for (Param *mp : mapped_params)
    {
        // Because this function is recursive, we need to skip the param that
        // caused any recursion, so it is not processed twice
        if (skip_param && (mp == skip_param))
            continue;

        // Is this not a state change param?
        if (mp->type != ParamType::UI_STATE_CHANGE)
        {
            // If a normal param (not a system function)
            if (mp->type != ParamType::SYSTEM_FUNC)
            {
                // Get the current param value
                auto current_value = mp->get_value();

                // Update the mapped parameter value
                mp->set_value_from_param(*param);

                // Is this a Surface Control param
                if ((mp->module == NinaModule::SURFACE_CONTROL)) {
                    // If in the current state, process it
                    if (mp->state == utils::get_param_state(mp->get_path())) {
                        _process_sfc_param_changed(mp);
                    }
                }           
                else
                {
                    // Create the mapped param change event if it has changed
                    if (current_value != mp->get_value())
                    {
                        // Send the param changed event
                        auto mapped_param_change = ParamChange(mp->get_path(), mp->get_value(), module());
                        if (displayed)
                            mapped_param_change.display = false;
                        else
                            displayed = mapped_param_change.display;
                        _event_router->post_param_changed_event(new ParamChangedEvent(mapped_param_change));
                    }
                }
            }
            else
            {
                auto sfc_param = static_cast<const SurfaceControlParam *>(param);
                auto value = sfc_param->get_value();

                // System function - is the mapped parameter a multi-position param?
                if (mp->multi_position_param)
                {
                    // Are we changing a switch control value?
                    if (sfc_param->type() == SurfaceControlType::SWITCH)
                    {
                        // For a multi-position param mapped to a system function, we pass
                        // the value in the system function event as the integer position
                        value = param->position;              
                    }
                }

                // Send the System Function event
                auto system_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->get_system_func_type(), 
                                                value,
                                                linked_param,
                                                module());
                system_func.sfc_control_type = sfc_param->type();
                if (system_func.type == SystemFuncType::MULTIFN_SWITCH) 
                {
                    // Calculate the value from the switch number
                    system_func.num = param->param_id - utils::system_config()->get_first_multifn_switch_num();
                }
                _event_router->post_system_func_event(new SystemFuncEvent(system_func));                   
            }              
        }
        else
        {
            // It's a state change
            // Only process this if its mapped to a switch control (for now)
            if (static_cast<const SurfaceControlParam *>(param)->type() == SurfaceControlType::SWITCH)
            {
                // Is the switch ON?
                if (_switch_controls[param->param_id].logical_state)
                {
                    // Set the state param value
                    mp->set_value(1.0);
                }
                else
                {
                    // Set the state param value
                    mp->set_value(0.0);
                }

                // Get all params that support this state change
                auto params = utils::get_params_with_state(mp->state);
                for (Param *p : params)
                {
                    // Is the switch ON?
                    if (_switch_controls[param->param_id].logical_state)
                    {
                        // Push the new param state
                        p = utils::push_param_state(p->get_path(), mp->state);
                    }
                    else
                    {
                        // Pop the param state
                        p = utils::pop_param_state(p->get_path(), mp->state);
                    }
                    if (p)
                    {
                        // If the param is a Surface Control param, proces it
                        auto sfc_param = static_cast<SurfaceControlParam *>(p);
                        if (sfc_param->module == module())
                        {
                            // Are we changing a knob control value?
                            if (sfc_param->type() == SurfaceControlType::KNOB)
                            {
                                // Set the knob control haptic mode
                                _set_knob_control_haptic_mode(static_cast<KnobParam *>(sfc_param));
                                                                
                                // Now set the knob position from the param
                                _set_knob_control_position(static_cast<KnobParam *>(sfc_param));
                            }
                            // Are we changing a switch control value?
                            else if (sfc_param->type() == SurfaceControlType::SWITCH)
                            {
                                // Save the switch value from the param
                                _set_switch_control_value(static_cast<SwitchParam *>(sfc_param));                        
                            }
                        }
                    }

                    // If this state change is also a patch, send a param changed message so that it
                    // can be saved in the patch
                    if (mp->patch_param)
                    {
                        // Send the param changed event
                        auto mapped_param_change = ParamChange(mp->get_path(), mp->get_value(), module());
                        _event_router->post_param_changed_event(new ParamChangedEvent(mapped_param_change));
                    }                                                
                }
            }
        }

        // We need to recurse each mapped param and process it
        // Note: We don't recurse system function params as they are a system action to be performed
        if (mp->type != ParamType::SYSTEM_FUNC)
            _process_param_changed_mapped_params(mp, param, displayed);
    }
}

//----------------------------------------------------------------------------
// _send_control_param_change_events
//----------------------------------------------------------------------------
void SurfaceControlManager::_send_control_param_change_events(const Param *param)
{
    // Only send the control change if OSC is running, as this is the only manager interested in 
    // surface control param changes
    if (utils::is_osc_running())
    {
        // Create the control param change event
        auto mapped_param_change = ParamChange(param->get_path(), param->get_value(), module());
        _event_router->post_param_changed_event(new ParamChangedEvent(mapped_param_change));
    }

    // Send the mapped params changed events
    _process_param_changed_mapped_params(param, nullptr, false);     
}

//----------------------------------------------------------------------------
// _set_knob_control_position
//----------------------------------------------------------------------------
void SurfaceControlManager::_set_knob_control_position(const KnobParam *param, bool robust)
{    
    // No point doing anything if the Surface Control isn't initialised
    if (_surface_control_init)
    {
        // The knob number is stored in the param ID
        uint num = param->param_id;

        // Knob 0 is a special case and we *never* go to position on this knob
        // (it is a relative position knob)
        if (num)
        {
            // Is this knob active?
            if (_surface_control->knob_is_active(num))
            {
                // Get the hardware position
                uint32_t pos = param->get_hw_value();

                // Set the target knob position in the hardware
                int res = _surface_control->set_knob_position(num, pos, robust);
                if (res < 0)
                {
                    // Show the error
                    //DEBUG_BASEMGR_MSG("Could not set the knob(" << num << ") position: " << res);
                }
                //DEBUG_BASEMGR_MSG("Knob(" << num << ") control externally updated: " << pos);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _set_switch_control_value
//----------------------------------------------------------------------------
void SurfaceControlManager::_set_switch_control_value(const SwitchParam *param)
{
    uint num = param->param_id;

    // Save the switch logical state so that it can be toggled at the next 
    // switch press
    _switch_controls[num].logical_state = (int)param->get_value();

    // Set the switch LED state
    _surface_control->set_switch_led_state(num, param->get_value() == 0 ? false : true);
}

//----------------------------------------------------------------------------
// _set_knob_control_haptic_mode
//----------------------------------------------------------------------------
void SurfaceControlManager::_set_knob_control_haptic_mode(const KnobParam *param)
{
    // Set the knob haptic mode
    int res = _surface_control->set_knob_haptic_mode(param->param_id, param->get_haptic_mode());
    if (res < 0)
    {
        // Error setting the knob mode
        MSG("ERROR: Could not set a surface control knob haptic mode: " << res);
    }
}

//----------------------------------------------------------------------------
// _process_physical_knob
//----------------------------------------------------------------------------
void SurfaceControlManager::_process_physical_knob(KnobControl &knob_control, const KnobState &knob_state)
{
    // Get the knob control param
    auto *param = static_cast<KnobParam *>(utils::get_param(KnobParam::ParamPath(knob_control.num).c_str()));
    if (param && (param->module == NinaModule::SURFACE_CONTROL))
    {    
        // Process the knob
        // Check if this knob is not moving to target
        if ((knob_state.state & KnobState::STATE_MOVING_TO_TARGET) == 0)
        {
            // Has the knob just finished moving to target?
            if (knob_control.moving_to_target) {
                // Skip the processing of this knob for n polls, to allow the motor to settle
                knob_control.poll_skip_count--;

                // Is this the last poll to skip?
                if (knob_control.poll_skip_count == 0) {
                    // We can assume the motor has settled into its target position
                    // Reset the delta value to zero, and ensure that for subsequent movement checks, the 
                    // large threshold is used
                    // Note: The large threshold is only used for the first movement after moving to position via
                    // an external update
                    knob_control.position_delta = 0;
                    knob_control.use_large_movement_threshold = true;
                    knob_control.moving_to_large_threshold = false;
                    knob_control.moving_to_target = false;                    
                }
            }
            else {
                // Knob not moving to target
                // Adjust the position delta
                if (knob_state.position > knob_control.position)
                {
                    // Knob position is increasing
                    knob_control.position_delta += (knob_state.position - knob_control.position);
                }
                else
                {
                    // Knob position is decreasing or not moving
                    knob_control.position_delta -= (knob_control.position - knob_state.position);
                }

                // Drift detection
                // Increment the timer and check if we are outside the threshold
                knob_control.polls_since_last_threshold_hit++;
                bool threshold_reached = param->hw_delta_outside_target_threshold(knob_control.position_delta,knob_control.use_large_movement_threshold);

                // If we are beyond the threshold but the timer is expired, then reset the delta
                if((knob_control.polls_since_last_threshold_hit > POLL_THRESH_COUNT) && threshold_reached)
                {
                    knob_control.polls_since_last_threshold_hit = 0;
                    knob_control.position_delta = 0;
                }
                // If the threshold is reached but the timer hasn't expired then reset the timer
                else if(threshold_reached)
                {
                    knob_control.polls_since_last_threshold_hit = 0;
                }

                // Has the knob delta changed so that it is now outside the target threshold?
                if (param->hw_delta_outside_target_threshold(knob_control.position_delta, 
                                                             knob_control.use_large_movement_threshold))
                {
                    // Reset the delta value to zero, and ensure that for subsequent movement checks, the 
                    // large threshold is not used
                    // Note: The large threshold is only used for the first movement after moving to position via
                    // an external update
                    knob_control.position_delta = 0;
                    knob_control.use_large_movement_threshold = false;
                    knob_control.small_movement_threshold_count = 0;

                    // Update the knob parameter
                    param->set_value_from_hw(knob_state.position);

                    // If not in maintenance mode
                    if (!utils::maintenance_mode()) {
                        // Send the control param change events
                        _send_control_param_change_events(param);
                    }                                  
                }
                else {
                    // If we get here there was either no movement or delta movement below the threshold
                    // If NOT using the large threshold
                    if (!knob_control.use_large_movement_threshold) {
                        // Increment the small movement threshold count
                        knob_control.small_movement_threshold_count++;

                        // If there have been N successive checks of this knob with no movement, reset to
                        // a large threshold
                        if (knob_control.small_movement_threshold_count >= SMALL_MOVEMENT_THRESHOLD_MAX_COUNT) {
                            knob_control.use_large_movement_threshold = true;
                            knob_control.moving_to_large_threshold = false;
                        }
                    }
                    else {
                        // We are using the large threshold, has the position exceeded the small threshold?
                        if (param->hw_delta_outside_target_threshold(knob_control.position_delta, false)) {                        
                            if (!knob_control.moving_to_large_threshold) {
                                // Get the start time the knob started moving to the threshold
                                knob_control.large_movement_time_start = std::chrono::steady_clock::now();
                                knob_control.moving_to_large_threshold = true;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            // Knob is moving to target - indicate this in the knob control, and set the poll skip
            // count once the knob has reached the target
            knob_control.moving_to_target = true;
            knob_control.poll_skip_count = KNOB_MOVED_TO_TARGET_POLL_SKIP_COUNT;

            // This is to make sure we start in the threshold exeeded state on a knob move
            knob_control.polls_since_last_threshold_hit = POLL_THRESH_COUNT +1;
        }
    }
}

//----------------------------------------------------------------------------
// _process_physical_switch
//----------------------------------------------------------------------------
void SurfaceControlManager::_process_physical_switch(SwitchControl &switch_control, bool physical_state)
{
    // Send the param change if either not in maintenance mode, or this is one of the soft buttons
    bool send_param_change = !utils::maintenance_mode() || switch_control.num < 3;

    // Get the switch control param
    auto *param = static_cast<SwitchParam *>(utils::get_param(SwitchParam::ParamPath(switch_control.num).c_str()));
    if (param)
    {
        // Get the switch haptic mode
        auto haptic_mode = param->get_haptic_mode();

        // If in maintenance mode force the haptic mode to PUSH
        if (utils::maintenance_mode()) {        
            haptic_mode.switch_mode = SwitchMode::PUSH;
        }

        // Calculate the switch number (zero-based)
        auto switch_num = param->param_id - utils::system_config()->get_first_multifn_switch_num();

        // Is this a multi-function switch?
        if (param->multifn_switch)
        {
            auto mode = utils::get_multifn_switches_mode();
            // Check if this an unused switch, or a switch with no mode, or a switch in keyboard mode
            if (!utils::is_active_multifn_switch(switch_num) ||
                (utils::is_active_multifn_switch(switch_num) &&
                 (mode <= MultifnSwitchesMode::SEQ_REC))) {
                // Override the haptic mode and set this switch to PUSH (or PUSH_NO_LED if the keyboard is active)
                haptic_mode.switch_mode = ((mode == MultifnSwitchesMode::KEYBOARD) ? SwitchMode::PUSH_NO_LED : SwitchMode::PUSH);
                send_param_change = ((mode == MultifnSwitchesMode::KEYBOARD) || (mode == MultifnSwitchesMode::SEQ_REC));
            }
        }

        // If a toggle switch, and the switch been pressed *and* the physical state changed
        if ((haptic_mode.switch_mode == SwitchMode::TOGGLE) || 
            (haptic_mode.switch_mode == SwitchMode::TOGGLE_LED_PULSE))
        {
            if (physical_state && !switch_control.physical_state)
            {
                // If this is a multi-function switch
                if (param->multifn_switch)
                {      
                    if ((utils::is_active_multifn_switch(switch_num) && 
                        (utils::get_multifn_switches_mode() == MultifnSwitchesMode::SINGLE_SELECT)))
                    {          
                        // Will the new switch logical state be OFF?
                        if (!switch_control.logical_state == 0)
                        {
                            // If this is the currently selected position, then don't
                            // allow the switch to be turned off
                            if (param->is_selected_position)
                            {
                                // Can't turn this switch off in position mode
                                // Send the control param change events and return with no further processing
                                if (send_param_change)
                                    _send_control_param_change_events(param);                                
                                return;
                            }
                        }
                        else
                        {
                            // The switch will become logically ON, so it will be the currently
                            // selected position
                            param->is_selected_position = true;
                        }
                    }
                }

                // This is a new press of the switch, toggle the logical switch state
                switch_control.logical_state = !switch_control.logical_state;

                // Set the switch LED state
                _surface_control->set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

                // Update the switch parameter value
                param->set_value(switch_control.logical_state);

                // Send the control param change events
                if (send_param_change)
                    _send_control_param_change_events(param);

                // Is this a multi-function switch?
                if (param->multifn_switch)
                {
                    auto params = utils::get_multifn_switch_params();

                    // If this is a normal active switch
                    if (utils::is_active_multifn_switch(switch_num) && 
                        (utils::get_multifn_switches_mode() == MultifnSwitchesMode::SINGLE_SELECT))
                    {
                        for (SwitchParam *sp : params)
                        {
                            if (utils::is_active_multifn_switch(sp->param_id - utils::system_config()->get_first_multifn_switch_num()) &&
                                (sp->param_id != param->param_id) && (_switch_controls[sp->param_id].logical_state)) {
                                _switch_controls[sp->param_id].logical_state = 0;

                                // Set the switch LED state
                                _surface_control->set_switch_led_state(_switch_controls[sp->param_id].num, false);

                                // Update the switch parameter value
                                sp->set_value(0);
                            }
                        }
                    }
                }
            }

            // If this is a toggle LED pulse switch
            if (haptic_mode.switch_mode == SwitchMode::TOGGLE_LED_PULSE) {
                // If the switch logical state is ON
                if (switch_control.logical_state) {
                    // If the timer has not been specfied
                    if (switch_control.led_pulse_timer == nullptr) {
                        // Start the switch LED timer task
                        switch_control.led_state = true;
                        switch_control.led_pulse_timer = new Timer(TimerType::PERIODIC);
                        switch_control.led_pulse_timer->start(SWITCH_TOGGLE_LED_DUTY, 
                            std::bind(&SurfaceControlManager::_switch_led_pulse_timer_callback, 
                                this, &switch_control));                      
                    }
                }
                else {
                    // If the switch logical state is OFF
                    // Stop and delete the timer
                    if (switch_control.led_pulse_timer) {
                        switch_control.led_pulse_timer->stop();
                        delete switch_control.led_pulse_timer;
                        switch_control.led_pulse_timer = nullptr;

                        // Make sure the LED is OFF
                        _surface_control->set_switch_led_state(switch_control.num, false);
                    }
                }
            }                        
        }
        // Is the switch haptic mode toggle-on-release?
        else if (haptic_mode.switch_mode == SwitchMode::TOGGLE_RELEASE)
        {
            // If the switch has been pressed
            if (physical_state && !switch_control.physical_state) {
                // The switch has been pressed, capture the push time start
                switch_control.push_time_start = std::chrono::steady_clock::now();

                // Send the control param change events
                if (send_param_change)
                    _send_control_param_change_events(param);                
            }
            // If the switch has been released
            else if (!physical_state && switch_control.physical_state) {
                // If the switch has been released, check if the time is within the toggle-release threshold
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                  switch_control.push_time_start).count();
                if (diff < SWITCH_TOGGLE_RELEASE_THRESHOLD) {
                    // Within the toggle-releass threshold - toggle the switch logical state
                    switch_control.logical_state = !switch_control.logical_state;

                    // Set the switch LED state
                    _surface_control->set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

                    // Update the switch parameter value
                    param->set_value(switch_control.logical_state);

                    // Send the control param change events
                    if (send_param_change)
                        _send_control_param_change_events(param);                    
                }
            }
        }
        // Is the switch haptic mode toggle-hold?
        else if (haptic_mode.switch_mode == SwitchMode::TOGGLE_HOLD)
        {
            // If the switch has been pressed
            if (physical_state && !switch_control.physical_state) {
                // The switch has been pressed, capture the push time start
                switch_control.push_time_start = std::chrono::steady_clock::now();
                switch_control.push_time_processed = false;

                // This is a new press of the switch, toggle the logical switch state
                switch_control.logical_state = !switch_control.logical_state;

                // Set the switch LED state
                _surface_control->set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

                // Update the switch parameter value
                param->set_value(switch_control.logical_state);

                // Send the control param change events
                if (send_param_change)
                    _send_control_param_change_events(param);                
            }
            // If the switch is held down
            else if (physical_state && switch_control.physical_state && !switch_control.push_time_processed) {
                // If the switch is held down, check if the time is outside the toggle-hold threshold
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                  switch_control.push_time_start).count();
                if (diff > SWITCH_TOGGLE_HOLD_THRESHOLD) {
                    // Outside the toggle-hold threshold
                    // Send the control param change events again
                    if (send_param_change)
                        _send_control_param_change_events(param);

                    // Indicate we have processed this push time
                    switch_control.push_time_processed = true;                
                }
            }
        }        
        else
        {
            // For all other modes, if the Toggle LED timer is active, stop and delete it
            // This might happen if the haptic mode for the switch is changed
            if (switch_control.led_pulse_timer) {
                switch_control.led_pulse_timer->stop();
                delete switch_control.led_pulse_timer;
                switch_control.led_pulse_timer = nullptr;

                // Make sure the LED is OFF
                _surface_control->set_switch_led_state(switch_control.num, false);                
            }

            // If a push button and the physical state has changed
            if (((haptic_mode.switch_mode == SwitchMode::PUSH) || (haptic_mode.switch_mode == SwitchMode::PUSH_NO_LED)) && (physical_state != switch_control.physical_state))
            {
                // If this is a multi-function key and we are in sequencer record mode and it is latched, just
                // sent the param change
                if (param->multifn_switch && (utils::get_multifn_switches_mode() == MultifnSwitchesMode::SEQ_REC) && switch_control.latched ) {
                    // Send the control param change events if needed
                    if (physical_state && send_param_change)
                        _send_control_param_change_events(param);
                    return;
                }

                // Don't process if the physical state and logical state are the same
                if (switch_control.logical_state != physical_state) {      
                    // Set the switch logical state (same as physical state)
                    switch_control.logical_state = physical_state;

                    // Set the switch LED state for PUSH mode
                    if (haptic_mode.switch_mode == SwitchMode::PUSH) {
                        _surface_control->set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);
                    }

                    // Update the switch parameter value
                    param->set_value(switch_control.logical_state);

                    // Send the control param change events if needed
                    if (send_param_change)
                        _send_control_param_change_events(param);
                }
                else {
                    // In keyboard mode *always* send a param change for a multi-function key
                    if (param->multifn_switch && (utils::get_multifn_switches_mode() == MultifnSwitchesMode::KEYBOARD) && send_param_change)
                        _send_control_param_change_events(param);                    
                }
            }
            // If a latch-push button and the physical state has changed
            else if ((haptic_mode.switch_mode == SwitchMode::LATCH_PUSH) && (physical_state != switch_control.physical_state))
            {
                // If the Toggle LED timer is active, stop and delete it
                if (switch_control.led_pulse_timer) {
                    switch_control.led_pulse_timer->stop();
                    delete switch_control.led_pulse_timer;
                    switch_control.led_pulse_timer = nullptr;                 
                }
                
                // If the switch has been pushed
                if (physical_state) {
                    // If the current logical switch state is OFF
                    if (switch_control.logical_state == 0) {
                        // Set the logical state to ON
                        switch_control.push_time_start = std::chrono::steady_clock::now();
                        switch_control.logical_state = 1.0;
                    }
                    else {
                        // Set the logical state to OFF
                        switch_control.logical_state = 0.0;                    
                    }
                }
                else {
                    // If the current logical switch state is ON
                    if (switch_control.logical_state == 1.0) {
                        // If the switch has been released, check if the time is within the latch threshold
                        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                          switch_control.push_time_start).count();
                        if (diff < SWITCH_PUSH_LATCH_THRESHOLD) {
                            return;
                        }

                        // Greater than the latch threshold, so don't latch the switch
                        switch_control.logical_state = 0.0;
                    }
                    else {
                        return;
                    }             
                }

                // Set the switch LED state
                _surface_control->set_switch_led_state(switch_control.num, switch_control.logical_state == 0 ? false : true);

                // Update the switch parameter value
                param->set_value(switch_control.logical_state);

                // Send the control param change events if needed
                if (send_param_change)
                    _send_control_param_change_events(param);  
            }
        }            
    }
}

//----------------------------------------------------------------------------
// _commit_led_control_states
//----------------------------------------------------------------------------
void SurfaceControlManager::_commit_led_control_states()
{
    // Only process if the Surface Control is initialised
    if (_surface_control_init)
    {
        // Commit all LED states in the hardware
        int res = _surface_control->commit_led_states();
        if (res < 0)
        {
            // Show the error
            //DEBUG_BASEMGR_MSG("Could not commit the LED states: " << res);
        }
    }
}

//----------------------------------------------------------------------------
// _morph_control
//----------------------------------------------------------------------------
void SurfaceControlManager::_morph_control(SurfaceControlType type, uint num)
{
    std::string control_path;

    // Get the Surface Control path
    if (type == SurfaceControlType::KNOB)
        control_path = KnobParam::ParamPath(num);
    else if (type == SurfaceControlType::SWITCH)
        control_path = SwitchParam::ParamPath(num);
    else
    {
        // Can only morph knobs and switches
        return;
    }

    // Get the control param
    auto control_param = utils::get_param(control_path);  

    // Get the mapped params for this control
    auto mapped_params = control_param->get_mapped_params();
    for (Param *mp : mapped_params)
    {
        // Only process mapped DAW params, and only the first one mapped
        if (mp->module == NinaModule::DAW)
        {
            // Set the knob position
            if (control_param->get_normalised_value() != mp->get_value() || _presets_reloaded) {
                control_param->set_value_from_normalised_float(mp->get_value());
                _process_sfc_param_changed(control_param);
            }
            break;
        }
    }            
}

//----------------------------------------------------------------------------
// _switch_led_pulse_timer_callback
//----------------------------------------------------------------------------
void SurfaceControlManager::_switch_led_pulse_timer_callback(SwitchControl *switch_control)
{
    // Toggle the switch LED state
    switch_control->led_state = !switch_control->led_state;
    _surface_control->set_switch_led_state(switch_control->num, switch_control->led_state);
}

//----------------------------------------------------------------------------
// _register_params
//----------------------------------------------------------------------------
void SurfaceControlManager::_register_params()
{
	// Register the surface params
    // Register the Knob controls (knobs + knob switches)
    for (int i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {   
        // Register the knob and knob switch controls
	    utils::register_param(std::move(KnobParam::CreateParam(i)));
        utils::register_param(std::move(KnobSwitchParam::CreateParam(i)));
    }

    // Register the Switch controls
    for (int i=0; i<NUM_PHYSICAL_SWITCHES; i++)
    {
        // Register the switch control
	    utils::register_param(std::move(SwitchParam::CreateParam(i)));
    }
}

//----------------------------------------------------------------------------
// _process_surface_control
//----------------------------------------------------------------------------
static void *_process_surface_control(void* data)
{
    auto sfc_manager = static_cast<SurfaceControlManager*>(data);
    sfc_manager->process_surface_control();

    // To suppress warnings
    return nullptr;
}
