/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  osc_manager.h
 * @brief OSC manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include "common.h"
#include "osc_manager.h"
#include "logger.h"
#include "utils.h"

// Constants
constexpr int SEND_POLL_TIME              = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(200)).count();
constexpr int CHANGE_PARAMS_IDLE_INTERVAL = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(250)).count();

// Static functions
static int _osc_receive_control_handler(const char *path, const char *types, lo_arg **argv,
                                        int, void *, void *user_data);
static void _osc_err_handler(int num, const char *msg, const char *where);

//----------------------------------------------------------------------------
// OscManager
//----------------------------------------------------------------------------
OscManager::OscManager(EventRouter *event_router) : 
    BaseManager(NinaModule::OSC, "OscManager", event_router)
{
    // Initialise class data
    _osc_server = 0;
    _send_addr = 0;
    _send_count = 0;
    _sfc_listener = 0;
    _fm_reload_presets_listener = 0;
    _fm_param_changed_listener = 0;
    _send_timer = new Timer(TimerType::PERIODIC);
    _param_change_timer = new Timer(TimerType::ONE_SHOT);
}

//----------------------------------------------------------------------------
// ~OscManager
//----------------------------------------------------------------------------
OscManager::~OscManager()
{
    // Stop the OSC timer tasks
    if (_send_timer)
    {
        _send_timer->stop();
        delete _send_timer;
        _send_timer = 0;
    }
    if (_param_change_timer)
    {
        _param_change_timer->stop();
        delete _param_change_timer;
        _param_change_timer = 0;
    }

    // Stop and free the server
    if (_osc_server)
    {
        lo_server_thread_stop(_osc_server);
        lo_server_thread_free(_osc_server);
    }
    if (_send_addr)
        lo_address_free(_send_addr);

    // Free any allocated objects
    if (_sfc_listener)
        delete _sfc_listener;
    if (_fm_reload_presets_listener)
        delete _fm_reload_presets_listener;
    if (_fm_param_changed_listener)
        delete _fm_param_changed_listener;
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool OscManager::start()
{
    // Are any of the OSC config parameters not specified?
    if ((std::strlen(utils::system_config()->osc_host_ip()) == 0) || 
        (std::strlen(utils::system_config()->osc_incoming_port()) == 0) || 
        (std::strlen(utils::system_config()->osc_outgoing_port()) == 0))
    {
        // OSC config not defined
        DEBUG_BASEMGR_MSG("OSC config not defined, OSC not running");
        NINA_LOG_WARNING(module(), "OSC config not defined, OSC not running");
        return false;
    }

    // Initialise the OSC server
    // Note: Must be done before processing the preset values
    if (!_initialise_osc_server())
    {
        // Error initialising the OSC server, show the error
        utils::set_osc_running(false);
        DEBUG_BASEMGR_MSG("Could not initialise the OSC server, check your OSC configuration");
        NINA_LOG_WARNING(module(), "Could not initialise the OSC server, check your OSC configuration");
        return false;        
    }

    // Indicate OSC is running
    utils::set_osc_running(true);

	// Before starting the OSC Manager, process all the preset values
	_process_presets();

	// Start the OSC send timer periodic thread
	_send_timer->start(SEND_POLL_TIME, std::bind(&OscManager::_osc_send_callback, this));

    // All ok, call the base manager
    return BaseManager::start();
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void OscManager::process()
{
    // Register the listeners
    _sfc_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_sfc_listener);
    _fm_reload_presets_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_fm_reload_presets_listener);
    _fm_param_changed_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_fm_param_changed_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void OscManager::process_event(const BaseEvent *event)
{
	// Process the event depending on the type
	switch (event->type())
	{
		case EventType::PARAM_CHANGED:
		{
			// Process the Param Changed event
			_process_param_change_event(static_cast<const ParamChangedEvent *>(event)->param_change());
			break;
		}

		case EventType::RELOAD_PRESETS:
		{
			// Process the presets
			_process_presets();
			break;
        }

		default:
            // Event unknown, we can ignore it
            break;
	}
}

//----------------------------------------------------------------------------
// osc_control_receive_value
//----------------------------------------------------------------------------
void OscManager::osc_control_receive_value(std::string control_path, float value)
{
    // Get the param, make sure it exists
    Param *param = utils::get_param(control_path.c_str());
    if (param)
    {
        // Update the param value
        // Note: Assumes all OSC controls are normalised floats
        param->set_value_from_normalised_float(value);

        // Send the param change event
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

        // Process the mapped params for this param change
        _process_param_changed_mapped_params(param, value, nullptr, param_change.display);
    }
}

//----------------------------------------------------------------------------
// _initialise_osc_server
//----------------------------------------------------------------------------
bool OscManager::_initialise_osc_server()
{
    const char *host_ip = utils::system_config()->osc_host_ip();
    const char *incoming_port = utils::system_config()->osc_incoming_port();
    const char *outgoing_port = utils::system_config()->osc_outgoing_port();

    // Create a new OSC server thread
    _osc_server = lo_server_thread_new(incoming_port, _osc_err_handler);
    if (_osc_server == NULL)
    {
        // An error occurred creating the server thread
        return false;
    }

    // Get the OSC send count
    _send_count = utils::system_config()->osc_send_count();

    // Get the send address
    _send_addr = lo_address_new(host_ip, outgoing_port);

    // Start the OSC server thread
    lo_server_thread_start(_osc_server);

    // Add control methods for all Surface Control params
    std::vector<Param *> params = utils::get_params(NinaModule::SURFACE_CONTROL);
    for (const Param *p : params)
    {
        // Only add physical control params
        if (p->physical_control_param)
        {
            // Add a control method for each param
            lo_server_thread_add_method(_osc_server, p->get_path().c_str(), "f", _osc_receive_control_handler, this);
        }
    }

    // Add control methods for all DAW params
    params = utils::get_params(NinaModule::DAW);
    for (const Param *p : params)
    {
        // Add a control method for each param
        lo_server_thread_add_method(_osc_server, p->get_path().c_str(), "f", _osc_receive_control_handler, this);
    }

    // Add control methods for all System Func params
    params = utils::get_params(ParamType::SYSTEM_FUNC);
    for(const Param *p : params)
    {
        // Skip alias params
        if (!p->alias_param) {
            // Add a control method for each System Func param
            lo_server_thread_add_method(_osc_server, p->get_path().c_str(), "f", _osc_receive_control_handler, this);
        }
    }
    return true;
}

//----------------------------------------------------------------------------
// _process_param_change_event
//----------------------------------------------------------------------------
void OscManager::_process_param_change_event(const ParamChange &param_change)
{
    // Get the OSC mutex
    std::lock_guard<std::mutex> guard(_osc_mutex);

    // Is this physical Surface Control param?
    const Param *param = utils::get_param(param_change.path.c_str());
    if (param && (param->module == NinaModule::SURFACE_CONTROL) && param->physical_control_param)
    {
        // Stop the param change timer
        _param_change_timer->stop();

        // We ned to update the param changes vector with this change
        // Firstly check if this param change already in the vector
        bool found = false;
        for (OscParamChange &pc : _param_changes) {
            if (pc.path == param->get_path()) {
                // Param already exists; update that param change
                pc.value = param->get_normalised_value();
                found = true;
            }
        }
        if (!found) {
            // Param not found in the deltas vector, add it
            _param_changes.push_back(OscParamChange(param->get_path(), param->get_normalised_value()));
        }
        
        // Start the param change timer to send the changed params if no param 
        // has changed for a time interval
        _param_change_timer->start(CHANGE_PARAMS_IDLE_INTERVAL, std::bind(&OscManager::_params_changed_timeout, this));
    }
    else
    {
        // Is this a DAW param?
        if (param && (param->module == NinaModule::DAW))
        {
            // Stop the param change timer
            _param_change_timer->stop();

            // We ned to update the param changes vector with this change
            // Firstly check if this param change already in the vector
            bool found = false;
            for (OscParamChange &pc : _param_changes) {
                if (pc.path == param->get_path()) {
                    // Param already exists; update that delta
                    pc.value = param->get_normalised_value();
                    found = true;
                }
            }
            if (!found) {
                // Param not found in the deltas vector, add it
                _param_changes.push_back(OscParamChange(param->get_path(), param->get_normalised_value()));
            }
            
            // Start the param change timer to send the changed params if no param 
            // has changed for a time interval
            _param_change_timer->start(CHANGE_PARAMS_IDLE_INTERVAL, std::bind(&OscManager::_params_changed_timeout, this));
        }
    }     
}

//----------------------------------------------------------------------------
// _process_param_changed_mapped_params
//----------------------------------------------------------------------------
void OscManager::_process_param_changed_mapped_params(const Param *param, float value, const Param *skip_param, bool displayed)
{
    // Get the mapped params
    auto mapped_params = param->get_mapped_params();
    for (Param *mp : mapped_params)
    {
        // Because this function is recursive, we need to skip the param that
        // caused any recursion, so it is not processed twice
        if (skip_param && (mp == skip_param))
            continue;

        // Is this a System Func param?
        if (mp->type == ParamType::SYSTEM_FUNC)
        {
            // Is the parent param a physical control?
            if (param && param->physical_control_param) {
                auto sfc_param = static_cast<const SurfaceControlParam *>(param);

                // Is the mapped parameter a multi-position param?
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
                                              mp->get_linked_param(),
                                              module());
                system_func.sfc_control_type = sfc_param->type();
                if (system_func.type == SystemFuncType::MULTIFN_SWITCH) 
                {
                    // Calculate the value from the switch number
                    system_func.num = param->param_id - utils::system_config()->get_first_multifn_switch_num();
                }
                _event_router->post_system_func_event(new SystemFuncEvent(system_func));
            }
            else {
                // Create the system function event
                auto system_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->get_system_func_type(), value, mp->get_linked_param(), module());

                // Send the system function event
                _event_router->post_system_func_event(new SystemFuncEvent(system_func));
            }

            // Note: We don't recurse system function params as they are a system action to be performed
        }
        else
        {
            // Only process if something has actually changed
            if (mp->get_value() != value)
            {
                // Update the param value
                // Note: Assumes all OSC controls are normalised floats
                mp->set_value_from_normalised_float(value);

                // Send the param change event - only show the first param we can actually display
                // on the GUI
                auto param_change = ParamChange(mp, module());
                if (displayed)
                    param_change.display = false;
                else
                    displayed = param_change.display;
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

                // We need to recurse each mapped param and process it
                _process_param_changed_mapped_params(mp, value, param, displayed);                
            }
        }
    }
}

//----------------------------------------------------------------------------
// _params_changed_timeout
//----------------------------------------------------------------------------
void OscManager::_params_changed_timeout()
{
    // Get the OSC mutex
    std::lock_guard<std::mutex> guard(_osc_mutex);

    // Create a new bundle with all param changes
    auto osc_bundle = OscBundle();
    for (OscParamChange pc : _param_changes)
    {
        // Add to the bundle
        auto osc_msg = lo_message_new();
        lo_message_add(osc_msg, "f", pc.value);
        lo_bundle_add_message(osc_bundle.bundle, pc.path.c_str(), osc_msg);
    }
    _param_changes.clear();
    osc_bundle.id = _send_bundles.size() + 1;
    _send_bundles.push_back(osc_bundle);
}

//----------------------------------------------------------------------------
// _process_presets
//----------------------------------------------------------------------------
void OscManager::_process_presets()
{
    // Get the OSC mutex
    std::lock_guard<std::mutex> guard(_osc_mutex);

    // Parse the Surface Control params
    std::vector<Param *> params = utils::get_params(NinaModule::SURFACE_CONTROL);
    auto osc_bundle1 = OscBundle();
    for (const Param *p : params)
    {
        // Only send physical controls
        if (p->physical_control_param)
        {
            // Add to the bundle
            auto osc_msg = lo_message_new();
            lo_message_add(osc_msg, "f", p->get_normalised_value());
            lo_bundle_add_message(osc_bundle1.bundle, p->get_path().c_str(), osc_msg);
        }
    }
    osc_bundle1.id = _send_bundles.size() + 1;
    _send_bundles.push_back(osc_bundle1);    
    
    // Parse the DAW params
    params = utils::get_params(NinaModule::DAW);
    auto osc_bundle2 = OscBundle();
    for (const Param *p : params)
    {
        // Skip alias params
        if (!p->alias_param) {
            // Add to the bundle
            auto osc_msg = lo_message_new();
            lo_message_add(osc_msg, "f", p->get_normalised_value());
            lo_bundle_add_message(osc_bundle2.bundle, p->get_path().c_str(), osc_msg);
        }
    }
    osc_bundle2.id = _send_bundles.size() + 1;
    _send_bundles.push_back(osc_bundle2);
}

//----------------------------------------------------------------------------
// _osc_send_callback
//----------------------------------------------------------------------------
void OscManager::_osc_send_callback()
{
	// Get the OSC mutex
	std::lock_guard<std::mutex> guard(_osc_mutex);
    bool send_ok = true;

    // If send bundles exist
    for (auto it = _send_bundles.begin(); it != _send_bundles.end(); it++)
    {
        // Send the bundle of control updates to OSC
        for (uint i=0; i<_send_count; i++)
        {
            // Send the bundle
            if (lo_send_bundle(_send_addr, it->bundle) < 0)
            {
                // An error occurred sending the bundle
                // In this case we stop processing and try again after waiting
                // the send poll time
                send_ok = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Clear the sent bundles if all sent ok
    if (send_ok)
    {
        // Free memory allocated to the bundles 
        for (auto it = _send_bundles.begin(); it != _send_bundles.end(); it++)
            lo_bundle_free_recursive(it->bundle);

        // Clear the send bundles vector
        _send_bundles.clear();
    }
}

//----------------------------------------------------------------------------
// _osc_receive_control_handler
//----------------------------------------------------------------------------
static int _osc_receive_control_handler(const char *path, const char *types, lo_arg **argv,
                                        int, void *, void *user_data)
{
    // Handle the receive control value event
    std::string std_path(path);
    auto control_handler = static_cast<OscManager *>(user_data);
    if (std::strcmp(types, "f") == 0)
        control_handler->osc_control_receive_value(std_path, argv[0]->f);
    return 0;
}

//----------------------------------------------------------------------------
// _osc_err_handler
//----------------------------------------------------------------------------
static void _osc_err_handler([[maybe_unused]] int num, [[maybe_unused]] const char *msg, [[maybe_unused]] const char *where)
{
    // Show the error
    DEBUG_MSG("An OSC error occurred: " << num << ", " << msg << ", " << where);
}
