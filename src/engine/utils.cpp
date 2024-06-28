/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  utils.cpp
 * @brief Utility functions implementation.
 *-----------------------------------------------------------------------------
 */

#include <iostream>
#include <random>
#include <sstream>
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <condition_variable>
#include <utility>
#include <fstream>
#include <atomic>
#include <regex>
#include <sys/sysinfo.h>
#ifndef NO_XENOMAI
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <xenomai/init.h>
#include <rtdm/rtdm.h>
#endif
#include "base_manager.h"
#include "utils.h"

// Constants
#define NINA_RT_TASK_PRIORITY                45
#define RT_TASK_CREATE_DELAY                 10000
constexpr char TEMPO_BPM_NAME[]              = "Tempo BPM";
constexpr char MIDI_CLK_IN_NAME[]            = "MIDI Clock In";
constexpr char MIDI_CLK_IN_PARAM_NAME[]      = "midi_clk_in";
constexpr char MIDI_ECHO_FILTER_NAME[]       = "MIDI Echo Filter";
constexpr char MIDI_ECHO_FILTER_PARAM_NAME[] = "midi_echo_filter";
constexpr char WT_NAME_NAME[]                = "WT Name";
constexpr char WT_NAME_PARAM_NAME[]          = "wt_name";
constexpr char SYSTEM_COLOUR_NAME[]          = "System Colour";
constexpr char SYSTEM_COLOUR_PARAM_NAME[]    = "system_colour";
constexpr char PARAM_DEFAULT[]               = "default";
constexpr uint DEFAULT_NUM_MPE_CHANNELS      = 7;

// Private variables
SystemConfig _system_config = SystemConfig();
std::string _session_uuid;
bool _maintenance_mode = false;
int _morph_knob_num = -1;
bool _morph_on = true;
bool _morph_enabled = false;
bool _prev_morph_enabled = false;
std::mutex _morph_mutex;
std::atomic<uint> _current_layer_num = 0;
LayerInfo _layer_info[] = { LayerInfo(0), LayerInfo(1), LayerInfo(2), LayerInfo(3) };
std::vector<std::unique_ptr<Param>> _nina_params;
std::vector<std::unique_ptr<Param>> _daw_params;
std::mutex _params_mutex;
std::vector<ParamState> _param_states;
std::vector<HapticMode> _haptic_modes;
std::vector<std::string> _params_blacklist;
MultifnSwitchesMode _normal_multifn_switches_mode = MultifnSwitchesMode::NONE;
uint _num_active_multifn_switches = 0;
bool _lfo_2_selected = false;
Param *_lfo_1_tempo_sync_param = 0;
Param *_lfo_1_rate_param = 0;
Param *_lfo_1_sync_rate_param = 0;
Param *_lfo_2_tempo_sync_param = 0;
Param *_lfo_2_rate_param = 0;
Param *_lfo_2_sync_rate_param = 0;
std::mutex _lfo_states_mutex;
std::vector<utils::LfoState> _lfo_states;
Param *_mpe_lower_zone_num_channels_param = 0;
Param *_mpe_upper_zone_num_channels_param = 0;
std::vector<utils::SystemColour> _system_colours;
BaseManager *_daw_mgr;
BaseManager *_midi_device_mgr;
BaseManager *_arp_mgr;
BaseManager *_seq_mgr;
BaseManager *_sw_mgr;
bool _seq_signalled = false;
std::mutex _seq_mutex;
std::condition_variable _seq_cv;
bool _arp_signalled = false;
std::mutex _arp_mutex;
std::condition_variable _arp_cv;

bool _osc_running = false;
const char *_param_refs[] = {
    "Morph_Value",
    "Morph_Mode",
    "toggle_patch_state",
    "Layer_State",
    "VCO_1_Tune_Octave",
    "VCO_2_Tune_Octave",
    "LFO_1_Rate",
    "LFO_1_Sync_Rate",
    "LFO_2_Rate",
    "LFO_2_Sync_Rate",
    "Layer_Num_Voices",
    "Current_Layer",
    "WT_Select",
    "LFO_Shape",
    "LFO_2_Shape",
    "Noise_Type",
    "LFO_1_Tempo_Sync",
    "LFO_2_Tempo_Sync",
    "Midi_Low_Note_Filter",
    "Midi_High_Note_Filter",
    "Midi_Channel_Filter",
    "Layer_Load",
    "All_Notes_Off",
    "MPE_Mode",
    "MPE_Lower_Zone_Num_Channels",
    "MPE_Upper_Zone_Num_Channels",
    "MPE_X",
    "MPE_Y",
    "MPE_Z",
    "FX_Type_Select",
    "Reverb_Level",
    "Chorus_Level",
    "Delay_Level"
};

// Private functions
Param *_get_param(std::string path);
Param *_get_param(std::string path, std::string state, bool preset_param);
Param *_get_param(std::string path, std::string state, std::vector<std::unique_ptr<Param>>& params);


//----------------------------------------------------------------------------
// system_config
//----------------------------------------------------------------------------
SystemConfig *utils::system_config()
{
    // Return a pointer to the system config object
    return &_system_config;
}

//----------------------------------------------------------------------------
// generate_session_uuid
//----------------------------------------------------------------------------
void utils::generate_session_uuid()
{
    static std::random_device              rd;
    static std::mt19937                    gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    _session_uuid = ss.str();
}

//----------------------------------------------------------------------------
// generate_session_uuid
//----------------------------------------------------------------------------
std::string utils::get_session_uuid()
{
    // Return the generated session UUID
    return _session_uuid;
}

//----------------------------------------------------------------------------
// set_maintenance_mode
//----------------------------------------------------------------------------
void utils::set_maintenance_mode()
{
    _maintenance_mode = true;
}

//----------------------------------------------------------------------------
// maintenance_mode
//----------------------------------------------------------------------------
bool utils::maintenance_mode()
{
    return _maintenance_mode;
}

//----------------------------------------------------------------------------
// add_system_colour
//----------------------------------------------------------------------------
void utils::add_system_colour(SystemColour system_colour)
{
    // See if the system colour already exists
    for (SystemColour& sc : _system_colours) {
        if (sc.name == system_colour.name) {
            // Already exists, so don't set
            return;
        }
    }

    // Add the system colour
    _system_colours.push_back(system_colour);
}

//----------------------------------------------------------------------------
// get_system_colour_from_name
//----------------------------------------------------------------------------
utils::SystemColour *utils::get_system_colour_from_name(std::string name)
{
    // Find the system colour
    for (utils::SystemColour& sc : _system_colours) {
        if (sc.name == name) {
            // Return the system colour
            return &sc;
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// get_system_colour_from_colour
//----------------------------------------------------------------------------
utils::SystemColour *utils::get_system_colour_from_colour(std::string colour)
{
    // Find the system colour
    for (utils::SystemColour& sc : _system_colours) {
        if (sc.colour == colour) {
            // Return the system colour
            return &sc;
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// get_system_colour_from_colour
//----------------------------------------------------------------------------
uint utils::get_system_colour_index(std::string colour)
{
    // Find the system colour
    uint index = 0;
    for (utils::SystemColour& sc : _system_colours) {
        if (sc.colour == colour) {
            // Return the system colour index
            return index;
        }
        index++;
    }
    return 0;
}

//----------------------------------------------------------------------------
// init_xenomai
//----------------------------------------------------------------------------
void utils::init_xenomai()
{
#ifndef NO_XENOMAI	
	// Fake command line arguments to pass to xenomai_init(). For some
    // obscure reasons, xenomai_init() crashes if argv is allocated here on
    // the stack, so we alloc it beforehand
    int argc = 2;
    auto argv = new char* [argc + 1];
    for (int i = 0; i < argc; i++)
    {
        argv[i] = new char[32];
    }
    argv[argc] = nullptr;
    std::strcpy(argv[0], "synthia_app");

    // Add cpu affinity argument to xenomai init setting it to all cores
    std::strcpy(argv[1], "--cpu-affinity=");
    for (int i = 0; i < get_nprocs(); i++)
    {
        char arg_cpu_num[4];
        std::sprintf(arg_cpu_num, "%d", (uint8_t)i);

        // Add cpu number to the list
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"        
        std::strncat(argv[1], arg_cpu_num, 2);
#pragma GCC diagnostic pop        

        // Add comma except for last cpu number
        if (i != get_nprocs() - 1)
        {
            std::strncat(argv[1], ",", 2);
        }
    }

    // Initialise Xenomai
    xenomai_init(&argc, (char* const**) &argv);
#endif
}

//----------------------------------------------------------------------------
// create_rt_task
// Note: The priority off all real-time tasks created here is arbitrarily set
// at 45, which is half the Raspa real-time task priority
//------------------------------------:----------------------------------------
int utils::create_rt_task(pthread_t *rt_thread, void *(*start_routine)(void *), void *arg, int sched_policy)
{
#ifndef NO_XENOMAI    
    struct sched_param rt_params = {.sched_priority = NINA_RT_TASK_PRIORITY};
    pthread_attr_t task_attributes;

    // Initialise the RT task
    __cobalt_pthread_attr_init(&task_attributes);
    pthread_attr_setdetachstate(&task_attributes, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setinheritsched(&task_attributes, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&task_attributes, sched_policy);

    // Create the RT thread
    pthread_attr_setschedparam(&task_attributes, &rt_params);    
    int res = __cobalt_pthread_create(rt_thread, &task_attributes, start_routine, arg);
    usleep(RT_TASK_CREATE_DELAY);
    return res;
#else
    (void)rt_thread;
    (void)start_routine;
    (void)arg;
    (void)sched_policy;
    return 0;
#endif
}

//----------------------------------------------------------------------------
// stop_rt_task
//----------------------------------------------------------------------------
void utils::stop_rt_task(pthread_t *rt_thread)
{
#ifndef NO_XENOMAI
        // Note: Need to signal the task to end, TBD
		__cobalt_pthread_join(*rt_thread, nullptr);
		*rt_thread = 0;
#else
    (void)rt_thread;
#endif
}

//----------------------------------------------------------------------------
// rt_task_nanosleep
//----------------------------------------------------------------------------
void utils::rt_task_nanosleep(struct timespec *time)
{
#ifndef NO_XENOMAI    
    // Perform the RT sleep
    __cobalt_clock_nanosleep(CLOCK_REALTIME, 0, time, NULL);
#else
    (void)time;
#endif
}

//----------------------------------------------------------------------------
// rtdm_open
//----------------------------------------------------------------------------
int utils::rtdm_open(const char *path, int oflag)
{
#ifndef NO_XENOMAI    
    return __cobalt_open(path, oflag);
#else
    (void)path;
    (void)oflag;
    return 0;
#endif    
}

//----------------------------------------------------------------------------
// rtdm_ioctl
//----------------------------------------------------------------------------
int utils::rtdm_ioctl(int fd, int request, void *argp)
{
#ifndef NO_XENOMAI    
    return __cobalt_ioctl(fd, request, argp);
#else
    (void)fd;
    (void)request;
    (void)argp;
    return 0;
#endif    
}

//----------------------------------------------------------------------------
// rtdm_close
//----------------------------------------------------------------------------
int utils::rtdm_close(int fd)
{
#ifndef NO_XENOMAI    
    return __cobalt_close(fd);
#else
    (void)fd;
    return 0;
#endif    
}

//----------------------------------------------------------------------------
// get_current_layer_info
//----------------------------------------------------------------------------
LayerInfo &utils::get_current_layer_info()
{
    return _layer_info[_current_layer_num];
}

//----------------------------------------------------------------------------
// get_layer_info
//----------------------------------------------------------------------------
LayerInfo &utils::get_layer_info(uint layer_num)
{
    assert(layer_num < NUM_LAYERS);
    return _layer_info[layer_num];
}

//----------------------------------------------------------------------------
// set_current_layer
//----------------------------------------------------------------------------
void utils::set_current_layer(uint layer_num)
{
    assert(layer_num < NUM_LAYERS);
    _current_layer_num = layer_num;
}

//----------------------------------------------------------------------------
// is_current_layer
//----------------------------------------------------------------------------
bool utils::is_current_layer(uint layer_num)
{
    assert(layer_num < NUM_LAYERS);
    return layer_num == _current_layer_num;
}

//----------------------------------------------------------------------------
// get_default_layers_filename
//----------------------------------------------------------------------------
std::string utils::get_default_layers_filename(uint index, bool with_ext)
{
    char name[sizeof("000_ONE_LAYER.json")];

    // Note - assume the passed index is valid
    (with_ext) ?
        std::sprintf(name, "%03d_ONE_LAYER.json", index) :
        std::sprintf(name, "%03d_ONE_LAYER", index); 
    return name;
}

//----------------------------------------------------------------------------
// init_mpe_handling
//----------------------------------------------------------------------------
void utils::init_mpe_handling()
{
    // Get the MPE Zone Number of Channel params
    if (!_mpe_lower_zone_num_channels_param) {
        _mpe_lower_zone_num_channels_param = get_param_from_ref(ParamRef::MPE_LOWER_ZONE_NUM_CHANNELS);
    }
    if (!_mpe_upper_zone_num_channels_param) {
        _mpe_upper_zone_num_channels_param = get_param_from_ref(ParamRef::MPE_UPPER_ZONE_NUM_CHANNELS);
    }
}

//----------------------------------------------------------------------------
// reset_mpe_params
//----------------------------------------------------------------------------
void utils::reset_mpe_params()
{
    // Reset the MPE params to OFF and no zone channels
    auto param = utils::get_param_from_ref(utils::ParamRef::MPE_MODE);
    if (param) {
        param->set_value(0);
    }
    if (_mpe_lower_zone_num_channels_param) {
        _mpe_lower_zone_num_channels_param->set_value(0);
    }
    if (_mpe_upper_zone_num_channels_param) {
        _mpe_upper_zone_num_channels_param->set_value(0);
    }    
}

//----------------------------------------------------------------------------
// config_mpe_zone_channel_params
//----------------------------------------------------------------------------
std::pair<const Param *, const Param *> utils::config_mpe_zone_channel_params() 
{
    uint lower_zone_num_channels = 0;
    uint upper_zone_num_channels = 0;
    bool lower_zone_active = false;
    bool upper_zone_active = false;
    std::pair<const Param *, const Param *> ret(nullptr, nullptr);

    // Get the Upper and Lower Zone Number of Channel params
    if (_mpe_lower_zone_num_channels_param) {
        lower_zone_num_channels = _mpe_lower_zone_num_channels_param->get_position_value();
    }
    if (_mpe_upper_zone_num_channels_param) {
        upper_zone_num_channels = _mpe_upper_zone_num_channels_param->get_position_value();
    }

    // Go through each Layer
    for (uint i=0; i<NUM_LAYERS; i++) {
        // Get the MPE mode
        auto mode = utils::get_layer_info(i).get_mpe_mode();

        // Are the Lower or Upper Zones active?
        if (!lower_zone_active && (mode == MpeMode::LOWER_ZONE)) {
            lower_zone_active = true;
        }
        else if (!upper_zone_active && (mode == MpeMode::UPPER_ZONE)) {
            upper_zone_active = true;
        }
    }

    // We need to make sure the zone channels allocated are consistent - lower zone taking 
    // the priority
    // Is the lower zone active?
    if (lower_zone_active) {
        // Is the upper zone also active?
        if (upper_zone_active) {
            // Check the number of channels allocated is valid
            if (lower_zone_num_channels == MAX_NUM_MPE_CHANNELS) {
                lower_zone_num_channels--;
                upper_zone_num_channels = 0;
            }
            else if ((lower_zone_num_channels + upper_zone_num_channels) > (MAX_NUM_MPE_CHANNELS - 1)) {
                // Truncate the upper zone number of channels
                upper_zone_num_channels = (MAX_NUM_MPE_CHANNELS - 1) - lower_zone_num_channels;
            }
        }
    }
    else {
        // Make sure the lower zone channels are zero
        if (lower_zone_num_channels) {
            lower_zone_num_channels = 0;
        }
    }

    // Is the upper zone not active?
    if (!upper_zone_active) {
        // Make sure the upper zone channels are zero
        if (upper_zone_num_channels) {
            upper_zone_num_channels = 0;
        }        
    }

    // Now calculate the actual number of positions for each param
    uint lower_zone_param_num_pos = 1;
    uint upper_zone_param_num_pos = 1;
    if (lower_zone_active && upper_zone_active) {
        // Calculate the number of positions for the lower and upper params
        lower_zone_param_num_pos += (MAX_NUM_MPE_CHANNELS - 1) - upper_zone_num_channels;
        upper_zone_param_num_pos += (MAX_NUM_MPE_CHANNELS - 1) - lower_zone_num_channels;
    }
    else if (lower_zone_active) {
        // Max positions are available
        lower_zone_param_num_pos += MAX_NUM_MPE_CHANNELS;
    }
    else if (upper_zone_active) {
        // Max positions available
        upper_zone_param_num_pos += MAX_NUM_MPE_CHANNELS;
    }

    // Update the zone channel params
    if (_mpe_lower_zone_num_channels_param) {
        bool changed = false;

        // Update the value and/or actual num positions if needed
        if ((uint)_mpe_lower_zone_num_channels_param->get_position_value() != lower_zone_num_channels) {
            _mpe_lower_zone_num_channels_param->set_value_from_position(lower_zone_num_channels, true);
            changed = true;
        }
        if (_mpe_lower_zone_num_channels_param->actual_num_positions != lower_zone_param_num_pos) {
            _mpe_lower_zone_num_channels_param->set_actual_num_positions(lower_zone_param_num_pos);
            changed = true;
        }

        // If changed return the Lower Zone Num Channels param
        if (changed) {
            ret.first = _mpe_lower_zone_num_channels_param;
        }
    }
    if (_mpe_upper_zone_num_channels_param) {
        bool changed = false;

        if ((uint)_mpe_upper_zone_num_channels_param->get_position_value() != upper_zone_num_channels) {
            _mpe_upper_zone_num_channels_param->set_value_from_position(upper_zone_num_channels, true);
            changed = true;
        }
        if (_mpe_upper_zone_num_channels_param->actual_num_positions != upper_zone_param_num_pos) {
            _mpe_upper_zone_num_channels_param->set_actual_num_positions(upper_zone_param_num_pos);
            changed = true;
        }

        // If changed return the Upper Zone Num Channels param
        if (changed) {
            ret.second = _mpe_upper_zone_num_channels_param;
        }               
    }
    return ret;    
}

//----------------------------------------------------------------------------
// is_mpe_channel
//----------------------------------------------------------------------------
bool utils::is_mpe_channel(uint channel)
{
    int lower_zone_last_channel = -1;
    int upper_zone_first_channel = -1;

    // Get the last Lower Zone Channel, and first Upper Zone Channel
    if (_mpe_lower_zone_num_channels_param) {
        lower_zone_last_channel = _mpe_lower_zone_num_channels_param->get_position_value();
    }
    if (_mpe_upper_zone_num_channels_param) {
        upper_zone_first_channel = MAX_NUM_MPE_CHANNELS - _mpe_upper_zone_num_channels_param->get_position_value();
    }

    // Is this channel within the lower zone, if any?
    if ((lower_zone_last_channel != -1) && channel && (channel <= (uint)lower_zone_last_channel)) {
        // Lower zone channel
        return true;
    }

    // Is this channel within the upper zone, if any?
    if ((upper_zone_first_channel != -1) && (channel < MAX_NUM_MPE_CHANNELS) && (channel >= (uint)upper_zone_first_channel)) {
        // Upper zone channel
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// get_mpe_mode
//----------------------------------------------------------------------------
MpeMode utils::get_mpe_mode(float mode_value)
{
    // Convert and return the mode
    int mode = (uint)floor(mode_value * MpeMode::NUM_MPE_MODES);
    if (mode > MpeMode::UPPER_ZONE)
        mode = MpeMode::UPPER_ZONE;
    return MpeMode(mode);
}

//----------------------------------------------------------------------------
// get_default_patch_filename
//----------------------------------------------------------------------------
std::string utils::get_default_patch_filename(uint index, bool with_ext)
{
    char name[sizeof("000_BASIC.json")];

    // Note - assume the passed index is valid
    (with_ext) ?
        std::sprintf(name, "%03d_BASIC.json", index) :
        std::sprintf(name, "%03d_BASIC", index); 
    return name;
}

//----------------------------------------------------------------------------
// get_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_params(NinaModule module)
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // Specified module and state?
        if ((p->module == module) && 
            (p->state == get_param_state(p->get_path())))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Now parse the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Specified module and state?
        if ((p->module == module) && 
            (p->state == get_param_state(p->get_path())))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_params(ParamType param_type)
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // Specified type and state?
        if ((p->type == param_type) && 
            ((param_type == ParamType::UI_STATE_CHANGE) || (p->state == get_param_state(p->get_path()))))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Now parse the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Specified type and state?
        if ((p->type == param_type) && 
            ((param_type == ParamType::UI_STATE_CHANGE) || (p->state == get_param_state(p->get_path()))))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_param
//----------------------------------------------------------------------------
Param *utils::get_param(std::string path)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Get the param
    return _get_param(path);
}

//----------------------------------------------------------------------------
// get_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_params(const std::string param_path_regex)
{
    std::vector<Param *> params;
    std::cmatch m;
    const std::regex base_regex(param_path_regex);

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // Does the param path match (regex)
        if (std::regex_match(p->get_path().c_str(), m, base_regex))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Now parse the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Does the param path match (regex)
        if (std::regex_match(p->get_path().c_str(), m, base_regex))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_params_with_state
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_params_with_state(const std::string state)
{
    std::vector<Param *> params;

    // Was a state specified?
    if (state.size() > 0) {
        // Get the params mutex
        std::lock_guard<std::mutex> lock(_params_mutex);

        // First parse the Nina specific params
        for (const std::unique_ptr<Param> &p : _nina_params)
        {
            // Does the param state match
            if ((p->type != ParamType::UI_STATE_CHANGE) && (p->state == state))
            {
                // Yes, add it
                params.push_back(p.get());
            }
        }

        // If nothing found yet
        if (params.size() == 0)
        {
            // Parse the DAW specific params
            for (const std::unique_ptr<Param> &p : _daw_params)
            {
                // Does the param state match
                if ((p->type != ParamType::UI_STATE_CHANGE) && (p->state == state))
                {
                    // Yes, add it
                    params.push_back(p.get());
                }
            }
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_patch_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_patch_params()
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // Patch param and state?
        if (p->patch_param && (p->state == get_param_state(p->get_path())))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Now parse the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Patch param and state?
        if (p->patch_param && (p->state == get_param_state(p->get_path())))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_mod_matrix_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_mod_matrix_params()
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // Mod Matrix param and state?
        if (p->mod_matrix_param && (p->state == get_param_state(p->get_path())))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Now parse the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Mod Matrix param and state?
        if (p->mod_matrix_param && (p->state == get_param_state(p->get_path())))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_global_params
//----------------------------------------------------------------------------
std::vector<Param *> utils::get_global_params()
{
    std::vector<Param *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // Global param and state?
        if (p->global_param && (p->state == get_param_state(p->get_path())))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }

    // Now parse the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Global param and state?
        if (p->global_param && (p->state == get_param_state(p->get_path())))
        {
            // Yes, add it
            params.push_back(p.get());
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// get_param
//----------------------------------------------------------------------------
Param *utils::get_param(std::string path, std::string state)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // If this is a state change param, don't check the state
        // This is because the state variable contains the target state
        if (p->type == ParamType::UI_STATE_CHANGE)
        {
            // Does the parameter path match?
            if (p->cmp_path(path))
            {
                // Param found, return it
                return (p.get());
            }
        }
        else
        {
            // Does the parameter path and passed state match?
            if (p->cmp_path(path) && (p->state == state))
            {              
                // Param found, return it
                return (p.get());
            }
        }
    }

    // Not in the Nina params, try the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Does the parameter path and passed state match?
        if (p->cmp_path(path) && (p->state == state))
        {
            // Param found, return it
            return (p.get());
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// get_param
//----------------------------------------------------------------------------
Param *utils::get_param(ParamType param_type, int param_id)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // If this is a state change param, don't check the state
        // This is because the state variable contains the target state
        if (p->type == ParamType::UI_STATE_CHANGE)
        {
            // Does the parameter type and ID match?
            if ((p->type == param_type) && (p->param_id == param_id))
            {
                // Param found, return it
                return (p.get());
            }
        }
        else
        {
            // Does the parameter type and ID and passed state match?
            if ((p->type == param_type) && (p->param_id == param_id) && (p->state == utils::get_param_state(p->get_path())))
            {              
                // Param found, return it
                return (p.get());
            }
        }
    }

    // Not in the Nina params, try the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Does the parameter type and ID and passed state match?
        if ((p->type == param_type) && (p->param_id == param_id) && (p->state == utils::get_param_state(p->get_path())))
        {
            // Param found, return it
            return (p.get());
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// get_param
//----------------------------------------------------------------------------
Param *utils::get_param(NinaModule module, int param_id)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // If this is a state change param, don't check the state
        // This is because the state variable contains the target state
        if (p->type == ParamType::UI_STATE_CHANGE)
        {
            // Does the parameter type and ID match?
            if ((p->module == module) && (p->param_id == param_id))
            {
                // Param found, return it
                return (p.get());
            }
        }
        else
        {
            // Does the parameter type and ID and passed state match?
            if ((p->module == module) && (p->param_id == param_id) && (p->state == utils::get_param_state(p->get_path())))
            {              
                // Param found, return it
                return (p.get());
            }
        }
    }

    // Not in the Nina params, try the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Does the parameter type and ID and passed state match?
        if ((p->module == module) && (p->param_id == param_id) && (p->state == utils::get_param_state(p->get_path())))
        {
            // Param found, return it
            return (p.get());
        }
    }
    return nullptr;    
}

//----------------------------------------------------------------------------
// get_sys_func_param
//----------------------------------------------------------------------------
Param *utils::get_sys_func_param(SystemFuncType sys_func_type)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // Does the parameter type and system function match?
        if ((p->type == ParamType::SYSTEM_FUNC) && (static_cast<const SystemFuncParam *>(p.get())->get_system_func_type() == sys_func_type))
        {
            // Param found, return it
            return (p.get());
        }
    }
    return nullptr;    
}

//----------------------------------------------------------------------------
// get_param_from_ref
//----------------------------------------------------------------------------
Param *utils::get_param_from_ref(ParamRef ref)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // Does the reference match?
        if (p->ref == _param_refs[ref])
        {
            // Param found, return it
            return (p.get());
        }
    }

    // Not in the Nina params, try the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Does the reference match?
        if (p->ref == _param_refs[ref])
        {
            // Param found, return it
            return (p.get());
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// get_data_knob_param
//----------------------------------------------------------------------------
KnobParam *utils::get_data_knob_param()
{
    return static_cast<KnobParam *>(get_param(KnobParam::ParamPath(0)));
}

//----------------------------------------------------------------------------
// register_param
//----------------------------------------------------------------------------
void utils::register_param(std::unique_ptr<Param> param)
{
    // All params go into the Nina params vector, unless they
    // have the preset flag set to true, in which case they go into
    // the DAW specific params vector
    if (!param->patch_param)
    {
        // These params go into the Nina params vector
        // Does the parameter already exist?
        if (_get_param(param->get_path(), param->state, false) == nullptr)
        {
            auto path = param->get_path();
            auto state = param->state;

            // Add the param
            _nina_params.push_back(std::move(param));

            // Does a param state for this param exist?
            bool found = false;
            for (ParamState &ps : _param_states)
            {
                // Does the param path match?
                if (path == ps.path)
                {
                    // Param found
                    found = true;
                    break;
                }
            }

            // Does a param state need to be added?
            if (!found)
            {
                // Add it to the param states vector
                auto param_state = ParamState();
                param_state.path = path;
                _param_states.push_back(param_state);
            }
        }
    }
    else
    {
        // These params go into the specific DAW params vector
        if (_get_param(param->get_path(), param->state, _daw_params) == nullptr)
        {
            auto path = param->get_path();
            auto state = param->state;

            // Add the param
            _daw_params.push_back(std::move(param));

            // Does a param state for this param exist?
            bool found = false;
            for (ParamState &ps : _param_states)
            {
                // Does the patch match?
                if (path == ps.path)
                {
                    // Param found
                    found = true;
                    break;
                }
            }

            // Does a param state need to be added?
            if (!found)
            {
                // Add it to the param states vector
                auto param_state = ParamState();
                param_state.path = path;
                _param_states.push_back(param_state);
            }           
        }        
    }
}

//----------------------------------------------------------------------------
// register_common_params
//----------------------------------------------------------------------------
void utils::register_common_params()
{
	// Register the Tempo BPM param
	auto param1 = TempoBpmParam::CreateParam();
	param1->param_id = CommonParamId::TEMPO_BPM_PARAM_ID;
	param1->name = TEMPO_BPM_NAME;
	utils::register_param(std::move(param1));

	// Register the MIDI Clock In param
	auto param2 = Param::CreateParam(MIDI_CLK_IN_PARAM_NAME);
	param2->param_id = CommonParamId::MIDI_CLK_IN_PARAM_ID;
	param2->name = MIDI_CLK_IN_NAME;
    param2->patch_param = false;
    param2->patch_state_param = false;
    param2->global_param = true;
	param2->set_value((float)false);
	utils::register_param(std::move(param2));    

	// Register the MIDI Echo Filter param
	auto param3 = Param::CreateParam(MIDI_ECHO_FILTER_PARAM_NAME);
	param3->param_id = CommonParamId::MIDI_ECHO_FILTER_PARAM_ID;
	param3->name = MIDI_ECHO_FILTER_NAME;
    param3->patch_param = false;
    param3->patch_state_param = false;
    param3->global_param = true;
	param3->set_value((float)MidiEchoFilter::ECHO_FILTER / MidiEchoFilter::NUM_ECHO_FILTERS);
	utils::register_param(std::move(param3)); 

	// Register the Wavetable Name param
	auto param4 = Param::CreateParam(WT_NAME_PARAM_NAME);
	param4->param_id = CommonParamId::WT_NAME_PARAM_ID;
	param4->name = WT_NAME_NAME;
    param4->str_param = true;
    param4->patch_param = true;
    param4->patch_layer_param = false;
    param4->patch_common_layer_param = false;
    param4->patch_state_param = true;
	param4->set_value(0.0);
	utils::register_param(std::move(param4));

	// Register the System Colour param
	auto param5 = Param::CreateParam(SYSTEM_COLOUR_PARAM_NAME);
	param5->param_id = CommonParamId::SYSTEM_COLOUR;
	param5->name = SYSTEM_COLOUR_NAME;
    param5->str_param = true;
    param5->patch_param = false;
    param5->patch_layer_param = false;
    param5->patch_common_layer_param = false;
    param5->patch_state_param = false;
	param5->set_value(0.0);
	utils::register_param(std::move(param5));       
}

//----------------------------------------------------------------------------
// reset_param_states
//----------------------------------------------------------------------------
void utils::reset_param_states()
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // For each param state
    for (ParamState &ps : _param_states)
    {
        // Pop the states until we are back at default
        while (ps.state_stack.size() > 1)
            ps.state_stack.pop_back();
    }
}

//----------------------------------------------------------------------------
// push_param_state
//----------------------------------------------------------------------------
Param *utils::push_param_state(std::string path, std::string state)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Does the param exist for this state?
    auto p = _get_param(path, state, false);
    if (p)
    {
        // Find the param state object for this param
        for (ParamState &ps : _param_states)
        {
            // Does the parameter path match?
            if (ps.path == path)
            {
                // Push the new state and return the param
                ps.state_stack.push_back(state);
                break;
            }
        }
    }
    return p;    
}

//----------------------------------------------------------------------------
// pop_and_push_param_state
//----------------------------------------------------------------------------
Param *utils::pop_and_push_param_state(std::string path, std::string push_state, std::string pop_state)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // Get the param for the state to pop and push
    auto p = _get_param(path, push_state, false);

    // For each stored param
    for (ParamState &ps : _param_states)
    {
        // Does the parameter path match?
        if (ps.path == path)
        {
            // Pop the last state if possible
            if ((ps.state_stack.size() > 1) && (ps.state_stack.back() == pop_state)) {
                ps.state_stack.pop_back();
            }

            // Does the param exist for the state to push?
            if (p)
            {
                // Push the new state
                ps.state_stack.push_back(push_state);
            }
            else
            {
                // Get the param for the current state
                p = _get_param(path, ps.state_stack.back(), false);
            }
            break;
        }
    }
    return p;    
}

//----------------------------------------------------------------------------
// pop_param_state
//----------------------------------------------------------------------------
Param *utils::pop_param_state(std::string path, std::string state)
{
    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // For each stored param
    for (ParamState &ps : _param_states)
    {
        // Does the parameter path match?
        if (ps.path == path)
        {
            // Pop the last state if possible
            if ((ps.state_stack.size() > 1) && (ps.state_stack.back() == state)) {
                ps.state_stack.pop_back();
            }

            // Get the param for the current state
            auto p = _get_param(path, ps.state_stack.back(), false);
            if (p)
            {
                // Return the param
                return p;
            }
            break;
        }
    }     
    return nullptr;    
}

//----------------------------------------------------------------------------
// get_param_state
//----------------------------------------------------------------------------
std::string utils::get_param_state(std::string path)
{
    // For each stored param
    for (ParamState &ps : _param_states)
    {
        // Does the parameter path match?
        if (ps.path == path)
            return ps.state_stack.back();
    }
    return PARAM_DEFAULT;    
}

//----------------------------------------------------------------------------
// blacklist_param
//----------------------------------------------------------------------------
void utils::blacklist_param(std::string path)
{
    // Is this param already blacklisted?
    if (!param_is_blacklisted(path))
    {
        // Add the blacklisted param
        _params_blacklist.push_back(path);
    }
}

//----------------------------------------------------------------------------
// param_is_blacklisted
//----------------------------------------------------------------------------
bool utils::param_is_blacklisted(std::string path)
{
    // Parse all blacklisted params to see if it already exists
    for (const std::string &p : _params_blacklist)
    {
        // Is this param already blacklisted?
        if (p == path)
        {
            // Blacklisted param
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// param_has_ref
//----------------------------------------------------------------------------
bool utils::param_has_ref(const Param *param, ParamRef ref)
{
    return param->ref == _param_refs[ref];
}

//----------------------------------------------------------------------------
// init_lfo_handling
//----------------------------------------------------------------------------
void utils::init_lfo_handling()
{
    // If the LFO 1 params are not set, set them
    if (!_lfo_1_tempo_sync_param) {
        _lfo_1_tempo_sync_param = get_param_from_ref(ParamRef::LFO_1_TEMPO_SYNC);
    }
    if (!_lfo_1_rate_param) {
        _lfo_1_rate_param = get_param_from_ref(ParamRef::LFO_1_RATE);
    }
    if (!_lfo_1_sync_rate_param) {
        _lfo_1_sync_rate_param = get_param_from_ref(ParamRef::LFO_1_SYNC_RATE);
    }

    // If the LFO 2 params are not set, set them
    if (!_lfo_2_tempo_sync_param) {
        _lfo_2_tempo_sync_param = get_param_from_ref(ParamRef::LFO_2_TEMPO_SYNC);
    }
    if (!_lfo_2_rate_param) {
        _lfo_2_rate_param = get_param_from_ref(ParamRef::LFO_2_RATE);
    }
    if (!_lfo_2_sync_rate_param) {
        _lfo_2_sync_rate_param = get_param_from_ref(ParamRef::LFO_2_SYNC_RATE);
    }

    // Push the default LFO 1 state
    _lfo_states.clear();
    _lfo_states.push_back(LfoState("default", false, false));
}

//----------------------------------------------------------------------------
// lfo_2_selected
//----------------------------------------------------------------------------
bool utils::lfo_2_selected()
{
    return _lfo_2_selected;
}

//----------------------------------------------------------------------------
// set_lfo_2_selected
//----------------------------------------------------------------------------
void utils::set_lfo_2_selected(bool selected)
{
    _lfo_2_selected = selected;
}

//----------------------------------------------------------------------------
// get_lfo_1_rate_param
//----------------------------------------------------------------------------
Param *utils::get_lfo_1_rate_param()
{
    // Return the LFO 1 param
    return (_lfo_1_tempo_sync_param->get_value() ? _lfo_1_sync_rate_param : _lfo_1_rate_param);
}

//----------------------------------------------------------------------------
// get_lfo_1_tempo_sync_param
//----------------------------------------------------------------------------
Param *utils::get_lfo_1_tempo_sync_param()
{
    // Return the LFO 1 param
    return _lfo_1_tempo_sync_param;
}

//----------------------------------------------------------------------------
// get_req_lfo_1_state
//----------------------------------------------------------------------------
utils::LfoState utils::get_req_lfo_1_state()
{
    // Return the required LFO 1 state
    return (LfoState((_lfo_1_tempo_sync_param->get_value() ? "lfo_1_sync_rate_state" : "default"), false, false));
}

//----------------------------------------------------------------------------
// lfo_1_sync_rate
//----------------------------------------------------------------------------
bool utils::lfo_1_sync_rate()
{
    return (_lfo_1_tempo_sync_param->get_value() != 0);
}

//----------------------------------------------------------------------------
// get_current_lfo_state
//----------------------------------------------------------------------------
utils::LfoState utils::get_current_lfo_state()
{
    // Get the LFO states mutex
    std::lock_guard<std::mutex> lock(_lfo_states_mutex);
    
    return _lfo_states.back();
}

//----------------------------------------------------------------------------
// push_lfo_state
//----------------------------------------------------------------------------
void utils::push_lfo_state(LfoState state)
{
    // Get the LFO states mutex
    std::lock_guard<std::mutex> lock(_lfo_states_mutex);

    _lfo_states.push_back(state);
}

//----------------------------------------------------------------------------
// pop_lfo_state
//----------------------------------------------------------------------------
void utils::pop_lfo_state()
{
    // Get the LFO states mutex
    std::lock_guard<std::mutex> lock(_lfo_states_mutex);
    
    // Don't pop the default state
    if (_lfo_states.size() > 1) {
        _lfo_states.pop_back();
    }
}

//----------------------------------------------------------------------------
// pop_all_lfo_states
//----------------------------------------------------------------------------
void utils::pop_all_lfo_states()
{
    // Get the LFO states mutex
    std::lock_guard<std::mutex> lock(_lfo_states_mutex);
    
    // Pop all LFO states EXCEPT the default state
    while (_lfo_states.size() > 1) {
        _lfo_states.pop_back();
    }
}

//----------------------------------------------------------------------------
// get_lfo_2_rate_param
//----------------------------------------------------------------------------
Param *utils::get_lfo_2_rate_param()
{
    return (_lfo_2_tempo_sync_param->get_value() ? _lfo_2_sync_rate_param : _lfo_2_rate_param);
}

//----------------------------------------------------------------------------
// get_lfo_2_tempo_sync_param
//----------------------------------------------------------------------------
Param *utils::get_lfo_2_tempo_sync_param()
{
    // Return the LFO 2 param
    return _lfo_2_tempo_sync_param;
}

//----------------------------------------------------------------------------
// get_req_lfo_2_state
//----------------------------------------------------------------------------
utils::LfoState utils::get_req_lfo_2_state()
{
    // Return the required LFO 2 state
    return (LfoState((_lfo_2_tempo_sync_param->get_value() ? "lfo_2_sync_rate_state" : "lfo_2_state"), false, true));
}

//----------------------------------------------------------------------------
// lfo_2_sync_rate
//----------------------------------------------------------------------------
bool utils::lfo_2_sync_rate()
{
    return (_lfo_2_tempo_sync_param->get_value() != 0);
}

//----------------------------------------------------------------------------
// set_morph_knob_num
//----------------------------------------------------------------------------
void utils::set_morph_knob_num(uint num)
{
    _morph_knob_num = num;
}

//----------------------------------------------------------------------------
// get_morph_knob_num
//----------------------------------------------------------------------------
int utils::get_morph_knob_num()
{
    return _morph_knob_num;
}

//----------------------------------------------------------------------------
// get_morph_knob_param
//----------------------------------------------------------------------------
KnobParam *utils::get_morph_knob_param()
{
    if ((_morph_knob_num != -1) && (_morph_knob_num < NUM_PHYSICAL_KNOBS))
        return static_cast<KnobParam *>(utils::get_param(KnobParam::ParamPath(_morph_knob_num)));
    return nullptr;
}

//----------------------------------------------------------------------------
// morph_lock
//----------------------------------------------------------------------------
void utils::morph_lock()
{
    // Lock the morph mutex
    _morph_mutex.lock();
}

//----------------------------------------------------------------------------
// morph_unlock
//----------------------------------------------------------------------------
void utils::morph_unlock()
{
    // Unlock the morph mutex
    _morph_mutex.unlock();
}

//----------------------------------------------------------------------------
// set_morph_on
//----------------------------------------------------------------------------
void utils::set_morph_on(bool on)
{
    _morph_on = on;
}

//----------------------------------------------------------------------------
// is_morph_on
//----------------------------------------------------------------------------
bool utils::is_morph_on()
{
    return _morph_on;
}

//----------------------------------------------------------------------------
// set_morph_enabled
//----------------------------------------------------------------------------
void utils::set_morph_enabled(bool enabled)
{
    _morph_enabled = enabled;
}

//----------------------------------------------------------------------------
// is_morph_enabled
//----------------------------------------------------------------------------
bool utils::is_morph_enabled()
{
    return _morph_enabled;
}

//----------------------------------------------------------------------------
// get_prev_morph_state
//----------------------------------------------------------------------------
bool utils::get_prev_morph_state()
{
    return _prev_morph_enabled;
}

//----------------------------------------------------------------------------
// set_prev_morph_state
//----------------------------------------------------------------------------
void utils::set_prev_morph_state()
{
    _prev_morph_enabled = _morph_enabled;
}

//----------------------------------------------------------------------------
// reset_morph_state
//----------------------------------------------------------------------------
void utils::reset_morph_state()
{
    // Reset the morph state
    _morph_enabled = false;
    _prev_morph_enabled = false;
}

//----------------------------------------------------------------------------
// get_morph_mode
//----------------------------------------------------------------------------
MorphMode utils::get_morph_mode(float mode_value)
{
    // Convert and return the mode
    int mode = (uint)floor(mode_value * MorphMode::NUM_MODES);
    if (mode > MorphMode::DJ)
        mode = MorphMode::DJ;
    return MorphMode(mode);
}

//----------------------------------------------------------------------------
// init_haptic_modes
//----------------------------------------------------------------------------
void utils::init_haptic_modes()
{
    // Nothing specific to do here (yet)
}

//----------------------------------------------------------------------------
// add_haptic_mode
//----------------------------------------------------------------------------
void utils::add_haptic_mode(const HapticMode& haptic_mode)
{
    // Make sure the mode does not already exist
    for (const HapticMode &hm : _haptic_modes)
    {
        // Mode already specified?
        if ((hm.name == haptic_mode.name) && (hm.type == haptic_mode.type))
        {
            // Return without adding the passed mode
            return;
        }
    }

    // Add the mode to the hapticl modes vector
    _haptic_modes.push_back(haptic_mode);  
}

//----------------------------------------------------------------------------
// set_default_haptic_mode
//----------------------------------------------------------------------------
bool utils::set_default_haptic_mode(SurfaceControlType control_type,std::string haptic_mode_name)
{
    // Parse the haptic modes
    for (HapticMode &hm : _haptic_modes)
    {
        // Mode found?
        if ((hm.name == haptic_mode_name) && (hm.type == control_type))
        {
            // Found, indicate this is the default mode and return true
            hm.default_mode = true;
            return true;
        }
    }

    // The default haptic mode was not found
    // In this instance, add a default mode so that Nina can continue
    // operating, but with a warning
    auto haptic_mode = HapticMode();
    haptic_mode.type = control_type;
    haptic_mode.name = haptic_mode_name;
    haptic_mode.default_mode = true;
    _haptic_modes.push_back(haptic_mode);
    return false;
}

//----------------------------------------------------------------------------
// get_haptic_mode
//----------------------------------------------------------------------------
const HapticMode& utils::get_haptic_mode(SurfaceControlType control_type, std::string haptic_mode_name)
{
    const HapticMode *default_mode = nullptr;

    // Parse the haptic modes
    for (const HapticMode &hm : _haptic_modes)
    {
        // Mode found?
        if ((hm.name == haptic_mode_name) && (hm.type == control_type))
        {
            // Return the mode
            return hm;
        }

        // Is this the default haptic mode? Save this in case we need
        // to return it if the mode cannot be found
        if (hm.default_mode && (hm.type == control_type))
            default_mode = &hm;
    }

    // The mode could not be found, return the default mode
    // Note: A default haptic mode *always* exists
    return *default_mode;
}

//----------------------------------------------------------------------------
// set_multifn_switches_mode
//----------------------------------------------------------------------------
void utils::set_multifn_switches_mode(MultifnSwitchesMode mode)
{
    _normal_multifn_switches_mode = mode;
}

//----------------------------------------------------------------------------
// get_multifn_switches_mode
//----------------------------------------------------------------------------
MultifnSwitchesMode utils::get_multifn_switches_mode()
{
    return _normal_multifn_switches_mode;
}

//----------------------------------------------------------------------------
// set_num_active_multifn_switches
//----------------------------------------------------------------------------
void utils::set_num_active_multifn_switches(uint num_switches)
{
    // Truncate the number specified if needed
    if ( num_switches > NUM_MULTIFN_SWITCHES) {
        num_switches = NUM_MULTIFN_SWITCHES;
    }
    _num_active_multifn_switches = num_switches;
}

//----------------------------------------------------------------------------
// is_active_multifn_switch
//----------------------------------------------------------------------------
bool utils::is_active_multifn_switch(uint switch_num)
{
    return switch_num < _num_active_multifn_switches;
}

//----------------------------------------------------------------------------
// get_multifn_switch_params
//----------------------------------------------------------------------------
std::vector<SwitchParam *> utils::get_multifn_switch_params()
{
    std::vector<SwitchParam *> params;

    // Get the params mutex
    std::lock_guard<std::mutex> lock(_params_mutex);

    // First parse the Nina specific params
    for (std::unique_ptr<Param> &p : _nina_params)
    {
        // Does the reference match?
        if (p->multifn_switch)
        {
            // Push the param source
            params.push_back(static_cast<SwitchParam *>(p.get()));
        }
    }
    return params;
}

//----------------------------------------------------------------------------
// set_osc_running
//----------------------------------------------------------------------------
void utils::set_osc_running(bool running)
{
    _osc_running = running;
}

//----------------------------------------------------------------------------
// is_osc_running
//----------------------------------------------------------------------------
bool utils::is_osc_running()
{
    return _osc_running;
}

//----------------------------------------------------------------------------
// register_manager
//----------------------------------------------------------------------------
void utils::register_manager(NinaModule module, BaseManager *mgr)
{
    // Parse the NINA module
    switch (module) {
        case NinaModule::DAW:
            _daw_mgr = mgr;
            break;

        case NinaModule::MIDI_DEVICE:
            _midi_device_mgr = mgr;
            break;

        case NinaModule::SEQUENCER:
            _seq_mgr = mgr;
            break;

        case NinaModule::ARPEGGIATOR:
            _arp_mgr = mgr;
            break;

        case NinaModule::SOFTWARE:
            _sw_mgr = mgr;
            break;

        default:
            break;
    }
}

//----------------------------------------------------------------------------
// get_manager
//----------------------------------------------------------------------------
BaseManager *utils::get_manager(NinaModule module)
{
    // Parse the NINA module
    switch (module) {
        case NinaModule::DAW:
            return _daw_mgr;

        case NinaModule::MIDI_DEVICE:
            return _midi_device_mgr;

        case NinaModule::SEQUENCER:
            return _seq_mgr;

        case NinaModule::ARPEGGIATOR:
            return _arp_mgr;

        case NinaModule::SOFTWARE:
            return _sw_mgr;

        default:
            return nullptr;
    }
}

//----------------------------------------------------------------------------
// seq_mutex
//----------------------------------------------------------------------------
std::mutex& utils::seq_mutex()
{
    // Return the mutex
    return _seq_mutex;
}

//----------------------------------------------------------------------------
// seq_signal
//----------------------------------------------------------------------------
void utils::seq_signal()
{
    // Signal the Sequencer
    {
        std::lock_guard lk(_seq_mutex);
        _seq_signalled = true;
    }    
    _seq_cv.notify_all();
}

//----------------------------------------------------------------------------
// seq_signal_without_lock
//----------------------------------------------------------------------------
void utils::seq_signal_without_lock()
{
    // Signal the Sequencer without a lock - assumes the lock has
    // already been aquired
    _seq_signalled = true;  
    _seq_cv.notify_all();
}

//----------------------------------------------------------------------------
// seq_wait
//----------------------------------------------------------------------------
void utils::seq_wait(std::unique_lock<std::mutex>& lk)
{
    // Wait for the Sequencer to be signalled
    _seq_cv.wait(lk, []{return _seq_signalled;});
    _seq_signalled = false;
}

//----------------------------------------------------------------------------
// arp_mutex
//----------------------------------------------------------------------------
std::mutex& utils::arp_mutex()
{
    // Return the mutex
    return _arp_mutex;
}

//----------------------------------------------------------------------------
// arp_signal
//----------------------------------------------------------------------------
void utils::arp_signal()
{
    // Signal the Arpeggiator with a lock
    {
        std::lock_guard lk(_arp_mutex);
        _arp_signalled = true;
    }    
    _arp_cv.notify_all();
}

//----------------------------------------------------------------------------
// arp_signal_without_lock
//----------------------------------------------------------------------------
void utils::arp_signal_without_lock()
{
    // Signal the Arpeggiator without a lock - assumes the lock has
    // already been aquired
    _arp_signalled = true;  
    _arp_cv.notify_all();
}

//----------------------------------------------------------------------------
// arp_wait
//----------------------------------------------------------------------------
void utils::arp_wait(std::unique_lock<std::mutex>& lk)
{
    // Wait for the Arpeggiator to be signalled
    _arp_cv.wait(lk, []{return _arp_signalled;});
    _arp_signalled = false;
}

//----------------------------------------------------------------------------
// tempo_pulse_count
//----------------------------------------------------------------------------
uint utils::tempo_pulse_count(TempoNoteValue note_value) {
    // Parse the note value
    switch (note_value)
    {
        case TempoNoteValue::QUARTER:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 1;

        case TempoNoteValue::EIGHTH:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 2;

        case TempoNoteValue::SIXTEENTH:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 4;

        case TempoNoteValue::THIRTYSECOND:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 8;

        case TempoNoteValue::QUARTER_TRIPLETS:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 1.5;

        case TempoNoteValue::EIGHTH_TRIPLETS:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 3;

        case TempoNoteValue::SIXTEENTH_TRIPLETS:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 6;

        case TempoNoteValue::THIRTYSECOND_TRIPLETS:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 12;

        default:
            return NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT / 1;
    }
}

//----------------------------------------------------------------------------
// tempo_note_value
//----------------------------------------------------------------------------
TempoNoteValue utils::tempo_note_value(int value)
{
    // Make sure the value is valid
    if (value > TempoNoteValue::THIRTYSECOND_TRIPLETS)
        value = TempoNoteValue::THIRTYSECOND_TRIPLETS;
    return TempoNoteValue(value);
}

//----------------------------------------------------------------------------
// _get_param
//----------------------------------------------------------------------------
Param *_get_param(std::string path)
{
    // First check the Nina specific params
    for (const std::unique_ptr<Param> &p : _nina_params)
    {
        // If this is a state change param, don't check the state
        // This is because the state variable contains the target state
        if (p->type == ParamType::UI_STATE_CHANGE)
        {
            // Does the parameter path match?
            if (p->cmp_path(path))
            {
                // Param found, return it
                return (p.get());
            }
        }
        else
        {
            // Does the parameter path and state match?
            if (p->cmp_path(path) && (p->state == utils::get_param_state(p->get_path())))
            {
                // Param found, return it
                return (p.get());
            }
        }
    }

    // Not in the Nina params, try the DAW specific params
    for (const std::unique_ptr<Param> &p : _daw_params)
    {
        // Does the parameter path and state match?
        if (p->cmp_path(path) && (p->state == utils::get_param_state(p->get_path())))
        {
            // Param found, return it
            return (p.get());
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// _get_param
// Note: Private function
//----------------------------------------------------------------------------
Param *_get_param(std::string path, std::string state, bool preset_param)
{
    // Is this a preset parameter?
    if (!preset_param)
    {
        // Search the Nina specific params vector
        for (std::unique_ptr<Param> &p : _nina_params)
        {
            // Does the parameter path and state match?
            if (p->cmp_path(path) && (p->state == state))
                return p.get();
        }
    }
    else
    {
        // Search the DAW specific params
        for (std::unique_ptr<Param> &p : _daw_params)
        {
            // Does the parameter path and state match?
            if (p->cmp_path(path) && (p->state == state))
                return p.get();
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// _get_param
// Note: Private function
//----------------------------------------------------------------------------
Param *_get_param(std::string path, std::string state, std::vector<std::unique_ptr<Param>>& params)
{
    // Search the passed params
    for (std::unique_ptr<Param> &p : params)
    {
        // Does the parameter path and state match?
        if (p->cmp_path(path) && (p->state == state))
            return p.get();
    }
    return nullptr;
}
