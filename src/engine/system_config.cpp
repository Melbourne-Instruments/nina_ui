/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  system_config.cpp
 * @brief System Config implementation.
 *-----------------------------------------------------------------------------
 */

#include "system_config.h"
#include "utils.h"

// Constants
constexpr uint DEFAULT_OSC_SEND_COUNT = 10;
constexpr uint FIRST_MULTIFN_SWITCH   = 21;

//----------------------------------------------------------------------------
// SystemConfig
//----------------------------------------------------------------------------
SystemConfig::SystemConfig()
{
    // Initialise class data
    _first_multifn_switch_num = FIRST_MULTIFN_SWITCH;
    _mod_src_num = DEFAULT_MOD_SRC_NUM;
    _demo_mode = false;
    _osc_host_ip = "";
    _osc_incoming_port = "";
    _osc_outgoing_port = "";
    _osc_send_count = DEFAULT_OSC_SEND_COUNT;
}

//----------------------------------------------------------------------------
// ~SystemConfig
//----------------------------------------------------------------------------
SystemConfig::~SystemConfig()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// get_layers_num
//----------------------------------------------------------------------------
uint SystemConfig::get_layers_num()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the layers num
    return _layers_num;
}

//----------------------------------------------------------------------------
// set_layers_num
//----------------------------------------------------------------------------
void SystemConfig::set_layers_num(uint num)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the layers num
    _layers_num = num;
}

//----------------------------------------------------------------------------
// set_first_multifn_switch_num
//----------------------------------------------------------------------------
void SystemConfig::set_first_multifn_switch_num(uint num)
{
    // Set the first multi-function switch number
    _first_multifn_switch_num = num;
}

//----------------------------------------------------------------------------
// get_first_multifn_switch_num
//----------------------------------------------------------------------------
uint SystemConfig::get_first_multifn_switch_num()
{
    // Return the first multi-function switch number
    return _first_multifn_switch_num;
}

//----------------------------------------------------------------------------
// set_mod_src_num
//----------------------------------------------------------------------------
void SystemConfig::set_mod_src_num(uint num)
{
    // Set the modulation source number
    _mod_src_num = num;
}

//----------------------------------------------------------------------------
// get_mod_src_num
//----------------------------------------------------------------------------
uint SystemConfig::get_mod_src_num()
{
    // Return the modulation source number
    if (_mod_src_num > 0)
        return _mod_src_num;
    return DEFAULT_MOD_SRC_NUM;
}

//----------------------------------------------------------------------------
// set_patch_modified_threshold
//----------------------------------------------------------------------------
void SystemConfig::set_patch_modified_threshold(uint threshold)
{
    // Set the patch modified threshold
    _patch_modified_threshold = threshold;
}

//----------------------------------------------------------------------------
// get_patch_modified_threshold
//----------------------------------------------------------------------------
uint SystemConfig::get_patch_modified_threshold()
{
    // Return the patch modified threshold
    return _patch_modified_threshold;
}

//----------------------------------------------------------------------------
// set_demo_mode
//----------------------------------------------------------------------------
void SystemConfig::set_demo_mode(bool enabled)
{
    // Enable/disable demo mode
    _demo_mode = enabled;
}

//----------------------------------------------------------------------------
// get_demo_mode
//----------------------------------------------------------------------------
bool SystemConfig::get_demo_mode()
{
    // Return if demo mode is enabled/disabled
    return _demo_mode;
}

//----------------------------------------------------------------------------
// set_demo_mode_timeout
//----------------------------------------------------------------------------
void SystemConfig::set_demo_mode_timeout(uint timeout)
{
    // Set the demo mode timeout
    _demo_mode_timeout = timeout;
}

//----------------------------------------------------------------------------
// get_demo_mode_timeout
//----------------------------------------------------------------------------
uint SystemConfig::get_demo_mode_timeout()
{
    // Return the demo mode timeout
    return _demo_mode_timeout;
}

//----------------------------------------------------------------------------
// set_system_colour
//----------------------------------------------------------------------------
void SystemConfig::set_system_colour(std::string colour)
{
    // Set the system colour
    _system_colour = colour;
}

//----------------------------------------------------------------------------
// get_system_colour
//----------------------------------------------------------------------------
std::string SystemConfig::get_system_colour()
{
    // Return the system colour
    return _system_colour;
}

//----------------------------------------------------------------------------
// init_osc_config
//----------------------------------------------------------------------------
void SystemConfig::init_osc_config(const char *host_ip, const char *incoming_port, const char *outgoing_port)
{
    // Initialise the OSC configuration
    _osc_host_ip = host_ip;
    _osc_incoming_port = incoming_port;
    _osc_outgoing_port = outgoing_port;
}

//----------------------------------------------------------------------------
// set_osc_send_count
//----------------------------------------------------------------------------
void SystemConfig::set_osc_send_count(uint send_count)
{
    // Set the OSC send count
    _osc_send_count = send_count;
}

//----------------------------------------------------------------------------
// osc_host_ip
//----------------------------------------------------------------------------
const char *SystemConfig::osc_host_ip()
{
    // Return the OSC host IP
    return _osc_host_ip;
}

//----------------------------------------------------------------------------
// osc_incoming_port
//----------------------------------------------------------------------------
const char *SystemConfig::osc_incoming_port()
{
    // Return the OSC incoming port
    return _osc_incoming_port;
}

//----------------------------------------------------------------------------
// osc_outgoing_port
//----------------------------------------------------------------------------
const char *SystemConfig::osc_outgoing_port()
{
    // Return the OSC outgoing port
    return _osc_outgoing_port;
}

//----------------------------------------------------------------------------
// osc_send_count
//----------------------------------------------------------------------------
uint SystemConfig::osc_send_count()
{
    // Return the OSC send count
    return _osc_send_count;
}
