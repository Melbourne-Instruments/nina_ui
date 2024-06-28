/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  param.cpp
 * @brief Param implementation.
 *-----------------------------------------------------------------------------
 */

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <math.h>
#include "param.h"
#include "base_manager.h"
#include "utils.h"

// Constants
constexpr char ARPEGGIATOR_PARAM_PATH_PREFIX[]     = "/arp/";
constexpr char COMMON_PARAM_PATH_PREFIX[]          = "/cmn/";
constexpr char DAW_PARAM_PATH_PREFIX[]             = "/daw/";
constexpr char MIDI_CONTROL_PATH_PREFIX[]          = "/mid/";
constexpr char SEQUENCER_PARAM_PATH_PREFIX[]       = "/seq/";
constexpr char SURFACE_CONTROL_PARAM_PATH_PREFIX[] = "/sfc/";
constexpr char SYSTEM_FUNC_PARAM_PATH_PREFIX[]     = "/sys/";
constexpr char KEYBOARD_PARAM_PATH_PREFIX[]        = "/kbd/";
constexpr char PATH_KNOB_CONTROL_NAME[]            = "Knob_";
constexpr char PATH_SWITCH_CONTROL_NAME[]          = "Switch_";
constexpr char PATH_KNOB_SWITCH_CONTROL_NAME[]     = "Knob_Switch_";
constexpr char KNOB_BASE_NAME[]                    = "Knob";
constexpr char SWITCH_BASE_NAME[]                  = "Switch";
constexpr char KNOB_SWITCH_BASE_NAME[]             = "Knob Switch";
constexpr char CONTROL_STATE_NAME[]                = "/state/";
constexpr char TEMPO_BMP_PARAM_NAME[]              = "tempo_bpm";
constexpr char TEMPO_NOTE_VALUE_PARAM_NAME[]       = "tempo_note_value";
constexpr uint16_t KNOB_HW_VALUE_NORMAL_THRESHOLD  = (FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR * (300.0f/360.0f)) / 1401.0f;
constexpr uint16_t KNOB_HW_VALUE_LARGE_THRESHOLD   = (KNOB_HW_VALUE_NORMAL_THRESHOLD * 2);
constexpr uint16_t KNOB_HW_INDENT_WIDTH            = 0.02 * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
constexpr uint16_t KNOB_HW_INDENT_THRESHOLD        = 5;

//---------------------------
// Param class implementation
//---------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<Param> Param::CreateParam(std::string name) 
{ 
    // Create as common param (no associated module)
    auto param = std::make_unique<Param>(NinaModule::ANY);
    param->type = ParamType::COMMON_PARAM;
    param->_path = Param::ParamPath(name);
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<Param> Param::CreateParam(const BaseManager *mgr, std::string name) 
{
    // Create as module param
    auto param = std::make_unique<Param>(mgr->module());
    param->_path = Param::ParamPath(mgr->module(), name);
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<Param> Param::CreateParam(NinaModule module, std::string name) 
{
    // Create as module param
    auto param = std::make_unique<Param>(module);
    param->_path = Param::ParamPath(module, name);
    return param;
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string Param::ParamPath(std::string param_name)
{
    return COMMON_PARAM_PATH_PREFIX + param_name;
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string Param::ParamPath(const BaseManager *mgr, std::string param_name)
{
    return Param::ParamPath(mgr->module(), param_name);
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string Param::ParamPath(NinaModule module, std::string param_name)
{
    std::string path_prefix;

    // Set the path based on the module
    switch (module)
    {
        case DAW:
            path_prefix = DAW_PARAM_PATH_PREFIX;
            break;

        case SEQUENCER:
            path_prefix = SEQUENCER_PARAM_PATH_PREFIX;
            break;

        case ARPEGGIATOR:
            path_prefix = ARPEGGIATOR_PARAM_PATH_PREFIX;
            break;

        case SURFACE_CONTROL:
            path_prefix = SURFACE_CONTROL_PARAM_PATH_PREFIX;
            break;

        case MIDI_DEVICE:
            path_prefix = MIDI_CONTROL_PATH_PREFIX;
            break;

        case KEYBOARD:
            path_prefix = KEYBOARD_PARAM_PATH_PREFIX;
            break;

        default:
            // Shouldn't get here, set any empty string for the prefix
            path_prefix = "";
            break;
    }
    return (path_prefix + param_name);
}

//----------------------------------------------------------------------------
// Param
//----------------------------------------------------------------------------
Param::Param(const Param& param)
{
    type = param.type;
    module = param.module;
    name = param.name;
    processor_id = param.processor_id;
    param_id = param.param_id;
    str_param = param.str_param;
    patch_param = param.patch_param;
    patch_layer_param = param.patch_layer_param;
    patch_common_layer_param = param.patch_common_layer_param;
    patch_state_param = param.patch_state_param;
    physical_control_param = param.physical_control_param;
    multi_position_param = param.multi_position_param;
    position_param = param.position_param;
    position = param.position;
    is_selected_position = param.is_selected_position;
    multifn_switch = param.multifn_switch;
    mod_matrix_param = param.mod_matrix_param;
    global_param = param.global_param;
    layer_1_param = param.layer_1_param;
    state = param.state;
    alias_param = param.alias_param;
    _path = param._path;
    _value = param._value;
    _normalised_value = param._normalised_value;
    _position_increment = param._position_increment;
    _mapped_params = param._mapped_params;
    _linked_param = param._linked_param;
    num_positions = param.num_positions;
    actual_num_positions = param.actual_num_positions;
    display_name = param.display_name;
    display_switch = param.display_switch;
    display_range_min = param.display_range_min;
    display_range_max = param.display_range_max;
    numeric_enum_param = param.numeric_enum_param;
    haptic_mode = param.haptic_mode;
    always_show = param.always_show;
    ref = param.ref;
    set_ui_state = param.set_ui_state;
    param_list_name = param.param_list_name;
    param_list_display_name = param.param_list_display_name;
    param_list = param.param_list;
    value_tag = param.value_tag;
    value_tags = param.value_tags;
    separator = param.separator;
}

//----------------------------------------------------------------------------
// Param
//----------------------------------------------------------------------------
Param::Param(NinaModule module)
{
    // Initialise class data
    type = ParamType::MODULE_PARAM;
    this->module = module;
    name = "";
    processor_id = -1;
    param_id = -1;
    str_param = false;
    patch_param = true;
    patch_layer_param = false;
    patch_common_layer_param = false;
    patch_state_param = true;
    physical_control_param = false;
    multi_position_param = false;
    _position_increment = 0;
    position_param = false;
    position = 0;
    is_selected_position = false;
    multifn_switch = false;
    mod_matrix_param = false;
    global_param = false;
    layer_1_param = false;
    always_show = false;
    ref = "";
    set_ui_state = "";
    state = "default";
    alias_param = false;
    num_positions = 0;
    actual_num_positions = 0;
    display_name = "";
    display_switch = false;
    display_range_min = 0;
    display_range_max = 100;
    numeric_enum_param = false;
    haptic_mode = "default";
    mod_src_name = "";
    mod_dst_name = "";
    _path = "";
    _value = 0.0;
    _normalised_value = true;
    _mapped_params.clear();
    _linked_param = nullptr;    
    param_list_name = "";
    param_list_display_name = "";
    value_tag = "";
    value_tags.clear();
    separator = false;
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> Param::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<Param>(*this);
}

//----------------------------------------------------------------------------
// ~Param
//----------------------------------------------------------------------------
Param::~Param()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// get_name
//----------------------------------------------------------------------------
const char *Param::get_name() const
{
    // Use the display name if specified
    if (display_name.size() > 0)
        return display_name.c_str();
    return name.c_str();
}

//----------------------------------------------------------------------------
// get_path
//----------------------------------------------------------------------------
std::string Param::get_path() const
{
    // Return the param path
    return _path;
}

//----------------------------------------------------------------------------
// set_path
//----------------------------------------------------------------------------
void Param::set_path(std::string path)
{
    _path = path;
}

//----------------------------------------------------------------------------
// cmp_path
//----------------------------------------------------------------------------
bool Param::cmp_path(std::string path)
{
    // Compare the passed control path with this param path
    return get_path() == path;
}

//----------------------------------------------------------------------------
// get_value
//----------------------------------------------------------------------------
float Param::get_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the param value
    return _value;
}

//----------------------------------------------------------------------------
// get_normalised_value
//----------------------------------------------------------------------------
float Param::get_normalised_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Return the normalised param value
    return _to_normalised_float();
}

//----------------------------------------------------------------------------
// get_value_from_normalised_float
//----------------------------------------------------------------------------
float Param::get_value_from_normalised_float(float value) const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Make sure the passed value is capped
    if (value < 0.0)
        value = 0.0;
    else if (value > 1.0)
        value = 1.0;

    // Convert to a non-normalised value
    return _from_normalised_float(value);
}

//----------------------------------------------------------------------------
// get_position_value
//----------------------------------------------------------------------------
int Param::get_position_value() const
{
    int val = -1;

    // Is this a position param?
    if (num_positions)
    {
        // Calculate the position
        auto v_inc = _value + _position_increment/2;
        if (position_param || (v_inc < 1.0)) {
            auto v = fmod(v_inc, 1.0);
            val = floor(v / _position_increment);
        }

        // Clip in case it exceeds the max positions
        if ((uint)val >= num_positions)
        {
            // Clip the returned value
            val = num_positions - 1;
        }
    }
    return val;
}

//----------------------------------------------------------------------------
// get_num_positions
//----------------------------------------------------------------------------
uint Param::get_num_positions() const
{
    return num_positions <= actual_num_positions ? num_positions : actual_num_positions;
}

//----------------------------------------------------------------------------
// get_str_value
//----------------------------------------------------------------------------
std::string Param::get_str_value() const
{
    return _str_value;
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void Param::set_value(float value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Make sure the to set is clipped
    if (value < 0.0)
        value = 0.0;
    else if (value > 1.0)
        value = 1.0;

    // Set the value
    _value = value;
}

//----------------------------------------------------------------------------
// set_value_from_normalised_float
//----------------------------------------------------------------------------
void Param::set_value_from_normalised_float(float value)
{
    // Convert to a non-normalised value
    _value = get_value_from_normalised_float(value);
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void Param::set_value_from_param(const Param &param)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // If the passed param is the same as this param, do not process
    if (this == &param) {
        return;
    }

    // If the param to set from is an alias
    if (param.alias_param) {
        // If the alias is the same as this param, do not process
        if (static_cast<const ParamAlias *>(&param)->get_alias_param() == this) {
            return;
        }
    }

    // We need to check for the special cases of param before
    // processing as a normal value param
    // Firstly check if this param is a multi-position param
    // Multi-position params can only have a number of fixed values,
    // based on a position
    if (multi_position_param)
    {
        // If we are setting from a physical control
        if (param.physical_control_param) {
            // Calculate the physical pos
            uint pos = num_positions;
            auto v_inc = param.get_value() + _physical_position_increment/2;
            if (v_inc < 1.0) {
                auto v = fmod(v_inc, 1.0);
                pos = floor(v / _physical_position_increment);
            }

            // Clip in case it exceeds the max positions
            if (pos >= num_positions)
            {
                // Clip the returned value
                pos = num_positions - 1;
            }

            // Calculate the multi-position value as a float
            _value = pos * _position_increment;
        }
    }
    // Check if the passed param is a multi-position param
    else if (param.multi_position_param)
    {
        // If we are setting a physical control
        if (physical_control_param) {
            // We need to calculate the integer position from the
            // multi-position float value
            auto pos = floor(param.get_value() / param._position_increment);

            // Convert it to a physical position
            _value = pos * param._physical_position_increment;
        }         
    }
    // Process as a normal value param
    else 
    {
        // Is this a normalised value?
        if (_normalised_value)
        {
            // Is the passed param also normalised?
            if (param._normalised_value)
            {
                // No conversion necessary
                _value = param.get_value();
            }
            else
            {
                // Get the passed param normalised value
                _value = param.get_normalised_value();
            }
        }
        else
        {
            // Is the passed param also not normalised?
            if (!param._normalised_value)
            {
                // No conversion necessary
                _value = param.get_value();
            }
            else
            {
                // Convert the passed param to normalised, and then the value
                // from that normalised param value
                _value = _from_normalised_float(param.get_normalised_value());
            }
        }
    }
}

//----------------------------------------------------------------------------
// set_value_from_position
//----------------------------------------------------------------------------
void Param::set_value_from_position(uint position, bool force)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    if ((num_positions > 0) && ((position < actual_num_positions) || force))
    {
        // Calculate the multi-position value as a float
        _value = position * _position_increment;
    }
}

//----------------------------------------------------------------------------
// set_multi_position_param_num_positions
//----------------------------------------------------------------------------
void Param::set_multi_position_param_num_positions(uint num_positions)
{
    // Set this param as multi-position
    this->multi_position_param = true;

    // Update the position and position increment if needed
    if (this->num_positions < num_positions)
    {
        // Update the number of positions
        this->num_positions = num_positions;
        this->actual_num_positions = num_positions;

        // Calculate the position increment
        this->_position_increment = 1.0 / num_positions;
        this->_physical_position_increment = 1.0 / (num_positions-1);
    }
}

//----------------------------------------------------------------------------
// set_actual_num_positions
//----------------------------------------------------------------------------
void Param::set_actual_num_positions(uint num_positions)
{
    // If a multi-position param
    if (this->multi_position_param) {
        // Update the actual number of positions
        if (num_positions < this->num_positions) {
            this->actual_num_positions = num_positions;
        }
        else {
            this->actual_num_positions = this->num_positions;
        }
    }
}

//----------------------------------------------------------------------------
// set_position_param
//----------------------------------------------------------------------------
void Param::set_position_param(uint position)
{
    // Set this param as a position param
    this->position_param = true;
    this->position = position;
    this->is_selected_position = false;
}

//----------------------------------------------------------------------------
// set_str_value
//----------------------------------------------------------------------------
void Param::set_str_value(std::string str_value)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Set the string value
    _str_value = str_value;
}

//----------------------------------------------------------------------------
// _to_normalised_float
//----------------------------------------------------------------------------
float Param::_to_normalised_float() const
{
    // Note: This is a virtual function, and should be overriden when
    // needed
    // It it's base form, no conversion is done
    return _value;
}

//----------------------------------------------------------------------------
// _from_normalised_float
//----------------------------------------------------------------------------
float Param::_from_normalised_float(float value) const
{
    // Note: This is a virtual function, and should be overriden when
    // needed
    // It it's base form, no conversion is done
    return value;
}

//----------------------------------------------------------------------------
// get_value_as_string
//----------------------------------------------------------------------------
std::pair<std::string, bool> Param::get_value_as_string() const
{
    float val = _value;

    // Is this a display switch?
    if (display_switch)
    {
        // Show the switch value
        uint switch_val = (uint)_value;
        if (switch_val < display_strings.size())
            return std::pair<std::string, bool>(display_strings.at(switch_val), numeric_enum_param);
        if (switch_val)
            return std::pair<std::string, bool>("On", false);
        return std::pair<std::string, bool>("Off", false);
    }

    // Is this a position based param?
    if (num_positions > 0)
    {
        val = roundf(_value / _position_increment);
        if (val >= num_positions)
            val = num_positions - 1;
        if (val < display_strings.size())
            return std::pair<std::string, bool>(display_strings.at(val), numeric_enum_param);
        if (display_range_min && display_range_max) {
            uint range;
            if (display_range_min < 0) {
                range = std::abs(display_range_min) + display_range_max;
            }
            else {
                range = display_range_max - display_range_min;
            }
            val *= range / (num_positions - 1);
        }
        if (display_range_min)
            val += display_range_min;
        return std::pair<std::string, bool>(std::to_string(static_cast<int>(val)), true);
    }

    // Is this a normalised value?
    if (_normalised_value)
    {
        // Convert the float value to an integer
        int min = display_range_min;
        int max = display_range_max;
        if (display_range_min < 0) {
            min = 0;
            max += std::abs(display_range_min);
        }
        auto value = roundf(min + ((max - min) * val));
        if (display_range_min < 0) {
            value += display_range_min;
        }
        return std::pair<std::string, bool>(std::to_string(static_cast<int>(value)), true);
    }
    else
    {
        // Just return the non-normalised value
        return std::pair<std::string, bool>(std::to_string(static_cast<int>(val)), true);
    }
}

//----------------------------------------------------------------------------
// get_position_as_string
//----------------------------------------------------------------------------
std::string Param::get_position_as_string(uint pos) const
{
    // Is this a display switch?
    if (display_switch)
    {
        // Return the switch value
        if (pos)
            return "On";
        return "Off";
    }

    // Is this a position based param?
    if (num_positions > 0)
    {
        if (pos < display_strings.size())
            return display_strings.at(pos);
        if (display_range_min)
            pos += display_range_min;
        return std::to_string(static_cast<int>(pos));
    }
    return "";
}

//----------------------------------------------------------------------------
// add_mapped_param
//----------------------------------------------------------------------------
void Param::add_mapped_param(Param *param)
{
    _mapped_params.push_back(param);
}

//----------------------------------------------------------------------------
// set_linked_param
//----------------------------------------------------------------------------
void Param::set_linked_param(Param *linked_param)
{
    _linked_param = linked_param;
}

//----------------------------------------------------------------------------
// get_mapped_params
//----------------------------------------------------------------------------
const std::vector<Param *>& Param::get_mapped_params() const
{
    return _mapped_params;
}

//----------------------------------------------------------------------------
// get_linked_param
//----------------------------------------------------------------------------
Param *Param::get_linked_param() const
{
    return _linked_param;
}

//----------------------------------------------------------------------------
// clear_mapped_params
//----------------------------------------------------------------------------
void Param::clear_mapped_params()
{
    _mapped_params.clear();
}

//----------------------------------------------------------------------------
// clear_mapped_params
//----------------------------------------------------------------------------
std::string Param::get_value_tag() const
{
    // Is this an enum param?
    if (get_num_positions() > 0) {
        // Does it have an equivalent value tag?
        auto pos_value = get_position_value();
        if ((pos_value >=0) && ((uint)pos_value < value_tags.size())) {
            return value_tags[pos_value];
        }
    }
    return value_tag;
}

//----------------------------------------------------------------------------
// get_param_list
//----------------------------------------------------------------------------
std::vector<Param *> Param::get_param_list() const
{
    // Does this param have a basic param list?
    if (param_list.size() > 0) {
        return param_list;
    }
    // Does this param have a context param list?
    else if (context_specific_param_list.size() > 0) {
        // Go through each context specific param list
        for (const ContextSpecificParams& csp : context_specific_param_list) {
            // If the context param exists
            if (csp.context_param) {
                // If the context param value matches
                if (csp.context_param->multi_position_param) {
                    if (csp.context_param->get_position_value() == csp.context_value) {
                        return csp.param_list;
                    }
                }
                else if (csp.context_param->get_value() == csp.context_value) {
                    return csp.param_list;
                }
            }
        }
    }
    return std::vector<Param *>();
}

//--------------------------------
// ParamAlias class implementation
//--------------------------------

//----------------------------------------------------------------------------
// ParamAlias
//----------------------------------------------------------------------------
ParamAlias::ParamAlias(std::string path, Param *param) : Param(*param)
{
    _path = path;
    patch_param = false;
    _alias_param = param;
    alias_param = true;
}

//----------------------------------------------------------------------------
// ParamAlias
//----------------------------------------------------------------------------
ParamAlias::ParamAlias(const ParamAlias& param) : Param(param)
{
    // Initialise the class variables
    _alias_param = param._alias_param;
}

//----------------------------------------------------------------------------
// ~ParamAlias
//----------------------------------------------------------------------------
ParamAlias::~ParamAlias()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// get_value
//----------------------------------------------------------------------------
float ParamAlias::get_value() const
{
    assert(_alias_param);
    return _alias_param->get_value();
}

//----------------------------------------------------------------------------
// get_normalised_value
//----------------------------------------------------------------------------
float ParamAlias::get_normalised_value() const
{
    assert(_alias_param);
    return _alias_param->get_normalised_value();
}

//----------------------------------------------------------------------------
// get_value_as_string
//----------------------------------------------------------------------------
std::pair<std::string, bool> ParamAlias::get_value_as_string() const
{
    assert(_alias_param);
    return _alias_param->get_value_as_string();
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void ParamAlias::set_value(float value)
{
    assert(_alias_param);
    _alias_param->set_value(value);
}

//----------------------------------------------------------------------------
// set_value_from_param
//----------------------------------------------------------------------------
void ParamAlias::set_value_from_param(const Param &param)
{
    assert(_alias_param);
    _alias_param->set_value_from_param(param);
}

//----------------------------------------------------------------------------
// set_value_from_normalised_float
//----------------------------------------------------------------------------
void ParamAlias::set_value_from_normalised_float(float value)
{
    assert(_alias_param);
    _alias_param->set_value_from_normalised_float(value);
}

//----------------------------------------------------------------------------
// get_alias_param
//----------------------------------------------------------------------------
Param *ParamAlias::get_alias_param() const
{
    assert(_alias_param);
    return _alias_param;   
}

//-----------------------------------------
// SurfaceControlParam class implementation
//-----------------------------------------

//----------------------------------------------------------------------------
// SurfaceControlParam
//----------------------------------------------------------------------------
SurfaceControlParam::SurfaceControlParam(const SurfaceControlParam& param) : Param(param)
{
    _type = param._type;
    _haptic_mode_name = param._haptic_mode_name;
}

//----------------------------------------------------------------------------
// SurfaceControlParam
//----------------------------------------------------------------------------
SurfaceControlParam::SurfaceControlParam(SurfaceControlType type, uint control_num) 
    : Param(NinaModule::SURFACE_CONTROL)
{
    // Initialise the knob variables
    param_id = control_num;
    patch_param = false;
    patch_layer_param = false;
    patch_common_layer_param = false;
    patch_state_param = false;
    physical_control_param = true;
    _type = type;
    _haptic_mode_name = "";
}

//----------------------------------------------------------------------------
// ~SurfaceControlParam
//----------------------------------------------------------------------------
SurfaceControlParam::~SurfaceControlParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> SurfaceControlParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<SurfaceControlParam>(*this);
}

//----------------------------------------------------------------------------
// type
//----------------------------------------------------------------------------
SurfaceControlType SurfaceControlParam::type() const
{
    // Return the control type
    return _type;
}

//----------------------------------------------------------------------------
// get_haptic_mode
//----------------------------------------------------------------------------
const HapticMode& SurfaceControlParam::get_haptic_mode() const
{
    // Return the haptic mode
    return utils::get_haptic_mode(_type, _haptic_mode_name);
}

//----------------------------------------------------------------------------
// set_haptic_mode
//----------------------------------------------------------------------------
void SurfaceControlParam::set_haptic_mode(const std::string haptic_mode_name)
{
    // Save the haptic mode name
    _haptic_mode_name = haptic_mode_name;
}

//-------------------------------
// KnobParam class implementation
//-------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<KnobParam> KnobParam::CreateParam(unsigned int control_num) 
{ 
    auto param = std::make_unique<KnobParam>(control_num);
    return param;
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string KnobParam::ParamPath(unsigned int control_num)
{
    return SURFACE_CONTROL_PARAM_PATH_PREFIX + std::string(PATH_KNOB_CONTROL_NAME) + std::to_string(control_num);
}

//----------------------------------------------------------------------------
// KnobParam
//----------------------------------------------------------------------------
KnobParam::KnobParam(const KnobParam& param) : SurfaceControlParam(param)
{
    // Clone any knob specific data
    _relative_pos_offset = param._relative_pos_offset;
    last_pos = param.last_pos;
    _relative_pos_control = param._relative_pos_control;
}

//----------------------------------------------------------------------------
// KnobParam
//----------------------------------------------------------------------------
KnobParam::KnobParam(unsigned int control_num) 
    : SurfaceControlParam(SurfaceControlType::KNOB, control_num)
{
    // Initialise the knob variables
    name = KNOB_BASE_NAME + std::string(" ") + std::to_string(control_num);
    _path = KnobParam::ParamPath(control_num);
    _relative_pos_offset = 0;
    last_pos = 0;
    _relative_pos_control = false;
}

//----------------------------------------------------------------------------
// ~KnobParam
//----------------------------------------------------------------------------
KnobParam::~KnobParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> KnobParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<KnobParam>(*this);
}

//----------------------------------------------------------------------------
// get_value
//----------------------------------------------------------------------------
float KnobParam::get_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // We need to check for the special cases of param before
    // processing as a normal value param
    // Firstly check if this param is a multi-position param
    // Multi-position params can only have a number of fixed values,
    // based on a position
    if (position_param)
    {
        float val = roundf(_value / _position_increment);
        if (val >= this->num_positions)
            val = 0;
        return val;
    }
    return _value;
}

//----------------------------------------------------------------------------
// get_position_value
//----------------------------------------------------------------------------
uint KnobParam::get_position_value(uint prev_pos)
{
    auto value = prev_pos;
    bool increment = true;

    // If this knob has a number of positions defined
    if (num_positions > 0)
    {
        // Get the new position
        uint new_pos = Param::get_position_value();

        // If the last position is valid, and the new and last differ
        if ((_last_actual_pos != -1) && (new_pos != (uint)_last_actual_pos))
        {
            // Calc the difference between the two positions
            auto diff = std::abs((int)new_pos - _last_actual_pos);

            // Is the new pos less than the last?
            if (new_pos < (uint)_last_actual_pos)
            {
                // If the difference is less than half the number of positions,
                // decrement - otherwise assume a wrap-around and increment
                if ((uint)diff < (num_positions / 2))
                    increment = false;
            }
            else {
                // If the difference is greater than or equal to half the number 
                // of positions, assume a wrap-around and decrement - otherwise increment           
                if ((uint)diff >= (num_positions / 2))
                    increment = false;
            }
            // Increment if less than the max positions
            if (increment && (value < (_num_selectable_positions - 1))) {
                value++;
            }
            // Decrement if greater than zero
            else if (!increment && (value > 0)) {
                value--;
            }
        }
        _last_actual_pos = new_pos;
    }
    return value;
}

//----------------------------------------------------------------------------
// set_position_param
//----------------------------------------------------------------------------
void KnobParam::set_position_param(uint position, uint num_selectable_positions)
{
    // Set this param as a position param
    this->position_param = true;
    this->_relative_pos_control = false;

    // Update the number of positions
    this->num_positions = position;

    // Calculate the position increment
    this->_position_increment = 1.0 / position;
    this->_num_selectable_positions = num_selectable_positions;
    _last_actual_pos = -1;
}

//----------------------------------------------------------------------------
// set_relative_position_param
//----------------------------------------------------------------------------
void KnobParam::set_relative_position_param(float pos)
{
    position_param = false;
    num_positions = 0;
    last_pos = pos * KNOB_HW_VALUE_MAX_VALUE;
    set_relative_pos(get_hw_value());
    _relative_pos_control = true;
}

//----------------------------------------------------------------------------
// set_relative_pos
//----------------------------------------------------------------------------
void KnobParam::set_relative_pos(uint32_t hw_value)
{
    uint32_t val = hw_value;

    // Set the relative position based on the last position value
    if (last_pos > 0) {
        if (val < last_pos) {
            val += KNOB_HW_VALUE_MAX_VALUE - last_pos;
        }
        else {
            val -= last_pos;
        }
    }
    _relative_pos_offset = val;
}

//----------------------------------------------------------------------------
// reset_param
//----------------------------------------------------------------------------
void KnobParam::reset_param()
{
    // Reset this knob as either a fixed position or relative position control
    position_param = false;
    _relative_pos_control = false;
    _relative_pos_offset = 0;
    last_pos = 0;
}

//----------------------------------------------------------------------------
// get_hw_value
//----------------------------------------------------------------------------
uint32_t KnobParam::get_hw_value() const
{
    uint32_t knob_min = 0;
    uint32_t knob_max = FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
    uint32_t hw_value;

    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Get the knob control haptic mode
    auto mode = get_haptic_mode();
    auto knob_width = mode.knob_actual_width;

    // If the knob does not have the full 360 degrees of movement
    if (knob_width < 360)
    {
        // Has the knob start pos been specified?
        if (mode.knob_actual_start_pos != -1)
        {
            // Make sure the start pos and width do not exceed the knob limits
            if ((mode.knob_actual_start_pos + knob_width) > 360)
            {
                // Truncate the knob width
                knob_width = 360 - mode.knob_actual_start_pos;
            }

            // Calculate the knob min and max
            knob_min = (mode.knob_actual_start_pos / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
            knob_max = ((mode.knob_actual_start_pos + knob_width) / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
        }
        else
        {
            // Normalise the knob position from from the specified width
            float offset = ((360.0f - float(mode.knob_actual_width))/360.0f) / 2.0f;
            knob_min = offset * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
            knob_max = (1.0 - offset) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
        }
    }

    // Convert the normalised value to a knob position
    hw_value = (_value * (knob_max - knob_min)) + knob_min;

    // Make sure the min/max knob value has not been exceeded
    if (hw_value > knob_max)
        hw_value = knob_max;
    if (hw_value < knob_min)
        hw_value = knob_min;

    // Does this knob currently have haptic indents?
    if (mode.knob_indents.size() > 0)
    {
        uint32_t indent_range_start = knob_min;
        uint32_t indent_range_end;
        uint32_t knob_range_start = knob_min;
        uint32_t knob_range_end;
        bool is_indent = false;
        uint index = 0;

        // The knob has indents
        // Check if the knob position is any indent
        // Note: Assumes the indents are in increasing order
        while (true)
        {
            uint32_t deadzone_start = knob_min;
            uint32_t deadzone_end = knob_max;

            // If the knob position is an indent (with a small threshold), just break from 
            // the search and use that value
            if ((hw_value > (mode.knob_indents[index].second - KNOB_HW_INDENT_THRESHOLD)) && 
                (hw_value < (mode.knob_indents[index].second + KNOB_HW_INDENT_THRESHOLD)))
            {
                // The knob position is an indent
                hw_value = mode.knob_indents[index].second;
                is_indent = true;
                break;                
            }

            // Make sure we can safely calculate the start of the deadzone, otherwise
            // use the minimum knob position
            if ((mode.knob_indents[index].second - (KNOB_HW_INDENT_WIDTH >> 1)) > knob_min)
            {
                // Calculate the start
                deadzone_start = mode.knob_indents[index].second - (KNOB_HW_INDENT_WIDTH >> 1);
            }

            // Make sure we can safely calculate the end of the deadzone, otherwise
            // use the maximum knob position
            if ((mode.knob_indents[index].second + (KNOB_HW_INDENT_WIDTH >> 1)) < knob_max)
            {
                // Calculate the end
                deadzone_end = mode.knob_indents[index].second + (KNOB_HW_INDENT_WIDTH >> 1);
            }

            // If the knob position is less than this indent, break from the search
            if (hw_value < mode.knob_indents[index].second)
            {
                // Set the range end
                indent_range_end = mode.knob_indents[index].second;
                knob_range_end = deadzone_start;
                break;
            }

            // Set the range start to this indent
            indent_range_start = mode.knob_indents[index].second;
            knob_range_start = deadzone_end;

            // Have we parsed the last indent? If so, stop parsing the indents, otherwise check
            // the next indent
            if (index >= (mode.knob_indents.size() - 1))
            {
                // Set the range end
                indent_range_end = knob_max;
                knob_range_end = knob_max;
                break;
            }

            // Process the next indent
            index++;
        }

        // Is the knob position not an indent?
        if (!is_indent)
        {
            // Convert the scaled knob position to a real position, taking into account
            // the indent deadzone
            float indent_range = indent_range_end - indent_range_start;
            float knob_range = knob_range_end - knob_range_start;
            hw_value = (uint32_t)(knob_range_start + ((knob_range)*((hw_value - indent_range_start) / indent_range)));
        }
    }

    // Make sure the min/max knob value has not been exceeded
    if (hw_value > knob_max)
        hw_value = knob_max;
    if (hw_value < knob_min)
        hw_value = knob_min;    
    return hw_value;
}

//----------------------------------------------------------------------------
// set_value_from_hw
//----------------------------------------------------------------------------
void KnobParam::set_value_from_hw(uint32_t hw_value)
{
    uint32_t knob_min = 0;
    uint32_t knob_max = FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;

    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Get the knob control haptic mode (param_id is the knob number)
    auto mode = get_haptic_mode();
    auto knob_width = mode.knob_actual_width;

    // If the knob does not have the full 360 degrees of movement
    if (knob_width < 360)
    {
        // Has the knob start pos been specified?
        if (mode.knob_actual_start_pos != -1)
        {
            // Make sure the start pos and width do not exceed the knob limits
            if ((mode.knob_actual_start_pos + knob_width) > 360)
            {
                // Truncate the knob width
                knob_width = 360 - mode.knob_actual_start_pos;
            }

            // Calculate the knob min and max
            knob_min = (mode.knob_actual_start_pos / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
            knob_max = ((mode.knob_actual_start_pos + knob_width) / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
        }
        else
        {
            // Normalise the knob position from from the specified width
            float offset = ((360.0f - float(mode.knob_actual_width))/360.0f) / 2.0f;
            knob_min = offset * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
            knob_max = (1.0 - offset) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
        }
    }

    // Is this a relative position control?
    if (!position_param && _relative_pos_control)
    {
        uint32_t new_hw_value = hw_value;

        // Get the relative hw position
        if (hw_value < _relative_pos_offset) {
            new_hw_value += (KNOB_HW_VALUE_MAX_VALUE - _relative_pos_offset);
        }
        else {
            new_hw_value -= _relative_pos_offset;
        }

        // Calculate the difference between the new and last positions
        int diff = new_hw_value - last_pos;
        if (diff > (32767/2)) {
            _relative_pos_offset = hw_value;
            hw_value = knob_min;
        }
        else if (diff < -(32767/2)) {
            _relative_pos_offset = hw_value;
            hw_value = knob_max;
        }
        else {
            hw_value = new_hw_value;
        }
        last_pos = hw_value;
    }

    // Make sure the min/max knob value has not been exceeded
    if (hw_value > knob_max)
        hw_value = knob_max;
    if (hw_value < knob_min)
        hw_value = knob_min;        

    // Does this knob have haptic detents?
    /*if (mode.knob_num_detents > 0)
    {
        // Calculate the haptic offset
        auto offset = (1.0 / mode.knob_num_detents) / 2.0;
        //MSG(offset *FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR);
        hw_value += offset;
        if (hw_value > knob_max)
            hw_value = knob_max;        
    }*/

    // Does this knob currently have haptic indents?
    if (mode.knob_indents.size() > 0)
    {
        uint32_t indent_range_start = knob_min;
        uint32_t indent_range_end;
        uint32_t knob_range_start = knob_min;
        uint32_t knob_range_end;
        bool in_deadzone = false;
        uint index = 0;

        // The knob has indents
        // Check if the knob position is within any indent deadzone
        // Note: Assumes the indents are in increasing order
        while (true)
        {
            uint32_t deadzone_start = knob_min;
            uint32_t deadzone_end = knob_max;

            // Make sure we can safely calculate the start of the deadzone, otherwise
            // use the minimum knob position
            if ((mode.knob_indents[index].second - (KNOB_HW_INDENT_WIDTH >> 1)) > knob_min)
            {
                // Calculate the start
                deadzone_start = mode.knob_indents[index].second - (KNOB_HW_INDENT_WIDTH >> 1);
            }

            // Make sure we can safely calculate the end of the deadzone, otherwise
            // use the maximum knob position
            if ((mode.knob_indents[index].second + (KNOB_HW_INDENT_WIDTH >> 1)) < knob_max)
            {
                // Calculate the end
                deadzone_end = mode.knob_indents[index].second + (KNOB_HW_INDENT_WIDTH >> 1);
            }

            // If the knob position is less than the start of this indent deadzone, break
            // from the search
            if (hw_value < deadzone_start)
            {
                // Set the range end
                indent_range_end = mode.knob_indents[index].second;
                knob_range_end = deadzone_start;
                break;
            }

            // If the knob position is less than or equal to the end of the indent deadzone, indicate the
            // knob position is within a deadzone and break from the search
            if (hw_value <= deadzone_end) 
            {
                in_deadzone = true;
                break;
            }

            // Set the range start to this indent
            indent_range_start = mode.knob_indents[index].second;
            knob_range_start = deadzone_end;

            // Have we parsed the last indent? If so, stop parsing the indents, otherwise check
            // the next indent
            if (index >= (mode.knob_indents.size() - 1))
            {
                // Set the range end
                indent_range_end = knob_max;
                knob_range_end = knob_max;
                break;
            }

            // Process the next indent
            index++;
        }

        // Is the knob position within a deadzone?
        if (in_deadzone)
        {
            // Set the knob position to be the indent position for processing
            hw_value = mode.knob_indents[index].second;
        }
        else
        {
            // We need to scale the knob position so that we get the full range of values even with
            // the deadzone
            float indent_range = indent_range_end - indent_range_start;
            float knob_range = knob_range_end - knob_range_start;
            hw_value = (uint32_t)(indent_range_start + ((indent_range)*((hw_value - knob_range_start) / knob_range)));
        }
    }

    // Normalise the knob position to a value between 0.0 and 1.0
    _value = ((float)hw_value - (float)knob_min) / ((float)knob_max - (float)knob_min);

    // Make sure the value is clipped to the min/max
    if (_value < 0.0)
    {
        _value = 0.0;
    }
    else if (_value > 1.0)
    {
        _value = 1.0;
    }
}

//----------------------------------------------------------------------------
// hw_delta_outside_target_threshold
//----------------------------------------------------------------------------
bool KnobParam::hw_delta_outside_target_threshold(int32_t hw_delta, bool use_large_threshold)
{
    // Is the h/w delta within the hardware threshold?
    if (use_large_threshold)
        return ((hw_delta < -KNOB_HW_VALUE_LARGE_THRESHOLD) || (hw_delta > KNOB_HW_VALUE_LARGE_THRESHOLD));
    return ((hw_delta < -KNOB_HW_VALUE_NORMAL_THRESHOLD) || (hw_delta > KNOB_HW_VALUE_NORMAL_THRESHOLD));
}

//----------------------------------------------------------------------------
// hw_value_within_target_threshold
//----------------------------------------------------------------------------
bool KnobParam::hw_value_within_target_threshold(uint32_t hw_value, uint32_t hw_target)
{
    uint32_t min_hw_threshold_value;
    uint32_t max_hw_threshold_value;

    // Calculate the min h/w threshold value
    // Is the target less than the threshold?
    if (hw_target < KNOB_HW_VALUE_NORMAL_THRESHOLD)
        min_hw_threshold_value = 0;
    else
        min_hw_threshold_value = hw_target - KNOB_HW_VALUE_NORMAL_THRESHOLD;

    // Calculate the max h/w threshold value
    // Is the (target + threshold) greater than the maximum h/w value allowed?
    if ((hw_target + KNOB_HW_VALUE_NORMAL_THRESHOLD) > KNOB_HW_VALUE_MAX_VALUE)
        max_hw_threshold_value = KNOB_HW_VALUE_MAX_VALUE;
    else
        max_hw_threshold_value = hw_target + KNOB_HW_VALUE_NORMAL_THRESHOLD;

    // Is the h/w value within the target threshold?
    // Return a boolean to indicate the result
    return (hw_value >= min_hw_threshold_value) && (hw_value <= max_hw_threshold_value);
}

//---------------------------------
// SwitchParam class implementation
//---------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<SwitchParam> SwitchParam::CreateParam(unsigned int control_num) 
{
    return std::make_unique<SwitchParam>(control_num);
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string SwitchParam::ParamPath(unsigned int control_num)
{
    return SURFACE_CONTROL_PARAM_PATH_PREFIX + std::string(PATH_SWITCH_CONTROL_NAME) + std::to_string(control_num);
}

//----------------------------------------------------------------------------
// MultifnSwitchParamPath
//----------------------------------------------------------------------------
std::string SwitchParam::MultifnSwitchParamPath(uint control_index)
{
    return SURFACE_CONTROL_PARAM_PATH_PREFIX + std::string(PATH_SWITCH_CONTROL_NAME) + 
           std::to_string(utils::system_config()->get_first_multifn_switch_num() + control_index);
}

//----------------------------------------------------------------------------
// SwitchParam
//----------------------------------------------------------------------------
SwitchParam::SwitchParam(const SwitchParam& param) : SurfaceControlParam(param)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// SwitchParam
//----------------------------------------------------------------------------
SwitchParam::SwitchParam(unsigned int control_num) 
    : SurfaceControlParam(SurfaceControlType::SWITCH, control_num)
{
    // Initialise the switch variables
    name = SWITCH_BASE_NAME + std::string(" ") + std::to_string(control_num);
    _path = SwitchParam::ParamPath(control_num);
}

//----------------------------------------------------------------------------
// ~SwitchParam
//----------------------------------------------------------------------------
SwitchParam::~SwitchParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> SwitchParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<SwitchParam>(*this);
}

//----------------------------------------------------------------------------
// get_value
//----------------------------------------------------------------------------
float SwitchParam::get_value() const
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // A switch value can only be 0 or 1
    if (_value < 0.5)
    {
        return 0.0;
    }
    return 1.0;
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void SwitchParam::set_value(float value)
{
    // Always normalise as a switch can only be a 0 or 1
    set_value_from_normalised_float(value);
}

//----------------------------------------------------------------------------
// _from_normalised_float
//----------------------------------------------------------------------------
float SwitchParam::_from_normalised_float(float value) const
{
    // A switch value can only be 0 or 1
    if (value < 0.5)
    {
        return 0.0;
    }
    return 1.0;
}

//-------------------------------------
// KnobSwitchParam class implementation
//-------------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<KnobSwitchParam> KnobSwitchParam::CreateParam(unsigned int control_num) 
{
    return std::make_unique<KnobSwitchParam>(control_num);
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string KnobSwitchParam::ParamPath(unsigned int control_num)
{
    return SURFACE_CONTROL_PARAM_PATH_PREFIX + std::string(PATH_KNOB_SWITCH_CONTROL_NAME) + std::to_string(control_num);
}

//----------------------------------------------------------------------------
// KnobSwitchParam
//----------------------------------------------------------------------------
KnobSwitchParam::KnobSwitchParam(const KnobSwitchParam& param) : SwitchParam(param)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// KnobSwitchParam
//----------------------------------------------------------------------------
KnobSwitchParam::KnobSwitchParam(unsigned int control_num) 
    : SwitchParam(control_num)
{
    // Initialise the switch variables
    name = KNOB_SWITCH_BASE_NAME + std::string(" ") + std::to_string(control_num);
    _path = KnobSwitchParam::ParamPath(control_num);
}

//----------------------------------------------------------------------------
// ~KnobSwitchParam
//----------------------------------------------------------------------------
KnobSwitchParam::~KnobSwitchParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> KnobSwitchParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<KnobSwitchParam>(*this);
}

//-------------------------------
// ButtonParam class implementation
//-------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<ButtonParam> ButtonParam::CreateParam(std::string name) 
{
    // Create as common param
    auto param = std::make_unique<ButtonParam>(NinaModule::ANY);
    param->type = ParamType::COMMON_PARAM;
    param->_path = Param::ParamPath(name);
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<ButtonParam> ButtonParam::CreateParam(const BaseManager *mgr, std::string name) 
{
    // Create as module param
    auto param = std::make_unique<ButtonParam>(mgr->module());
    param->_path = Param::ParamPath(mgr, name);
    return param;
}

//----------------------------------------------------------------------------
// ButtonParam
//----------------------------------------------------------------------------
ButtonParam::ButtonParam(const ButtonParam& param) : Param(param)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// ButtonParam
//----------------------------------------------------------------------------
ButtonParam::ButtonParam(NinaModule module) : Param(module)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> ButtonParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<ButtonParam>(*this);
}

//----------------------------------------------------------------------------
// ~ButtonParam
//----------------------------------------------------------------------------
ButtonParam::~ButtonParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void ButtonParam::set_value(float value)
{
    // Always normalise as a button can only be a 0 or 1
    set_value_from_normalised_float(value);
}

//----------------------------------------------------------------------------
// _from_normalised_float
//----------------------------------------------------------------------------
float ButtonParam::_from_normalised_float(float value) const
{
    // A button value can only be 0 or 1
    if (value < 0.5)
    {
        return 0.0;
    }
    return 1.0;
}

//-----------------------------------
// TempoBpmParam class implementation
//-----------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<TempoBpmParam> TempoBpmParam::CreateParam() 
{
    // Create as common param
    auto param = std::make_unique<TempoBpmParam>(NinaModule::ANY);
    param->type = ParamType::COMMON_PARAM;
    param->_path = Param::ParamPath(TEMPO_BMP_PARAM_NAME);
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<TempoBpmParam> TempoBpmParam::CreateParam(const BaseManager *mgr) 
{
    // Create as module param
    auto param = std::make_unique<TempoBpmParam>(mgr->module());
    param->_path = Param::ParamPath(mgr, TEMPO_BMP_PARAM_NAME);
    return param;
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string TempoBpmParam::ParamPath()
{
    return COMMON_PARAM_PATH_PREFIX + std::string(TEMPO_BMP_PARAM_NAME);
}

//----------------------------------------------------------------------------
// TempoBpmParam
//----------------------------------------------------------------------------
TempoBpmParam::TempoBpmParam(const TempoBpmParam& param) : Param(param)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// TempoBpmParam
//----------------------------------------------------------------------------
TempoBpmParam::TempoBpmParam(NinaModule module) : Param(module)
{
    // Initialise class data
    _normalised_value = false;
    _value = DEFAULT_TEMPO_BPM;
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> TempoBpmParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<TempoBpmParam>(*this);
}

//----------------------------------------------------------------------------
// ~TempoBpmParam
//----------------------------------------------------------------------------
TempoBpmParam::~TempoBpmParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void TempoBpmParam::set_value(float val)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // Make sure the tempo is clipped to the min/max values
    if (val < MIN_TEMPO_BPM)
        val = MIN_TEMPO_BPM;
    else if (val > MAX_TEMPO_BPM)
        val = MAX_TEMPO_BPM;

    // Set the value
    _value = val;
}

//----------------------------------------------------------------------------
// _to_normalised_float
//----------------------------------------------------------------------------
float TempoBpmParam::_to_normalised_float() const
{
    // Truncate the tempo to min/max
    auto tempo = _value;
    if (tempo < MIN_TEMPO_BPM)
    {
        tempo = MIN_TEMPO_BPM;
    }
    else if (tempo > MAX_TEMPO_BPM)
    {
        tempo = MAX_TEMPO_BPM;
    }

    // Convert the tempo to normalised float
    return (tempo - MIN_TEMPO_BPM) / (MAX_TEMPO_BPM - MIN_TEMPO_BPM);
}

//----------------------------------------------------------------------------
// _from_normalised_float
//----------------------------------------------------------------------------
float TempoBpmParam::_from_normalised_float(float value) const
{
    // Truncate the passed normalised float to min/max
    if (value < 0.0)
    {
        value = 0.0;
    }
    else if (value > 1.0)
    {
        value = 1.0;
    }

    // Convert the normalised float to a value between the min and max bpm
    return roundf(MIN_TEMPO_BPM + ((MAX_TEMPO_BPM - MIN_TEMPO_BPM) * value));
}

//-----------------------------------------
// TempoNoteValueParam class implementation
//-----------------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<TempoNoteValueParam> TempoNoteValueParam::CreateParam() 
{
    // Create as common param
    auto param = std::make_unique<TempoNoteValueParam>(NinaModule::ANY);
    param->type = ParamType::COMMON_PARAM;
    param->_path = Param::ParamPath(TEMPO_NOTE_VALUE_PARAM_NAME);
    return param;
}

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<TempoNoteValueParam> TempoNoteValueParam::CreateParam(const BaseManager *mgr) 
{
    // Create as module param
    auto param = std::make_unique<TempoNoteValueParam>(mgr->module());
    param->_path = Param::ParamPath(mgr, TEMPO_NOTE_VALUE_PARAM_NAME);
    return param;
}

//----------------------------------------------------------------------------
// TempoNoteValueParam
//----------------------------------------------------------------------------
TempoNoteValueParam::TempoNoteValueParam(const TempoNoteValueParam& param) : Param(param)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// TempoNoteValueParam
//----------------------------------------------------------------------------
TempoNoteValueParam::TempoNoteValueParam(NinaModule module) : Param(module)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> TempoNoteValueParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<TempoNoteValueParam>(*this);
}

//----------------------------------------------------------------------------
// ~TempoNoteValueParam
//----------------------------------------------------------------------------
TempoNoteValueParam::~TempoNoteValueParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// set_value
//----------------------------------------------------------------------------
void TempoNoteValueParam::set_value(float val)
{
    // Get the mutex
    std::lock_guard<std::mutex> lock(_mutex);

    // We need to check if the patch has an old (superceeded) value first
    // Get the old tempo note value divider value
    float divider = 1.0 / OldTempoNoteValue::OLD_NUM_TEMPO_NOTE_VALUES;

    // Is this an old tempo note value?
    if ((val > 0.0) && (std::fmod(val, divider) == 0.0)) {
        // Old value - we need to convert to the new tempo value
        uint old_note_val = (uint)floor(val * OldTempoNoteValue::OLD_NUM_TEMPO_NOTE_VALUES);
        if (old_note_val > OldTempoNoteValue::OLD_THIRTYSECOND)
            old_note_val = OldTempoNoteValue::OLD_THIRTYSECOND;
        switch (OldTempoNoteValue(old_note_val)) {
            case OldTempoNoteValue::OLD_WHOLE:
                set_tempo_note_value(TempoNoteValue::QUARTER);
                return;

            case OldTempoNoteValue::OLD_QUARTER:
                set_tempo_note_value(TempoNoteValue::SIXTEENTH);
                return;

            default:
                set_tempo_note_value(TempoNoteValue::THIRTYSECOND);
                return;
        }     
    }

    // Set the value
    _value = val;
}

//----------------------------------------------------------------------------
// get_tempo_note_value
//----------------------------------------------------------------------------
TempoNoteValue TempoNoteValueParam::get_tempo_note_value() const
{
    // Get the divider value
    float divider = 1.0 / TempoNoteValue::NUM_TEMPO_NOTE_VALUES;

    // Now divide the actual value by the divider, and get the integer value
    int val = floor(_value / divider);

    // Return the Tempo Note Value
    return TempoNoteValue(val);
}

//----------------------------------------------------------------------------
// set_tempo_note_value
//----------------------------------------------------------------------------
void TempoNoteValueParam::set_tempo_note_value(TempoNoteValue val)
{
    // Get the divider value
    float divider = 1.0 / TempoNoteValue::NUM_TEMPO_NOTE_VALUES;

    // Save the value as a float
    _value = val * divider;
}

//-------------------------------------
// SystemFuncParam class implementation
//-------------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<SystemFuncParam> SystemFuncParam::CreateParam(SystemFuncType system_func_type) 
{ 
    return std::make_unique<SystemFuncParam>(system_func_type);
}

//----------------------------------------------------------------------------
// ParamPath
//----------------------------------------------------------------------------
std::string SystemFuncParam::ParamPath(SystemFuncType system_func_type)
{
    return SYSTEM_FUNC_PARAM_PATH_PREFIX + SystemFunc::TypeName(system_func_type);
}

//----------------------------------------------------------------------------
// SystemFuncParam
//----------------------------------------------------------------------------
SystemFuncParam::SystemFuncParam(const SystemFuncParam& param) : Param(param)
{
    _system_func_type = param._system_func_type;
}

//----------------------------------------------------------------------------
// SystemFuncParam
//----------------------------------------------------------------------------
SystemFuncParam::SystemFuncParam(SystemFuncType system_func_type) : Param(NinaModule::ANY)
{
    // Indicate this is a system func param type
    type = ParamType::SYSTEM_FUNC;
    patch_param = false;
    patch_layer_param = false;
    patch_common_layer_param = false;
    patch_state_param = false;
    _system_func_type = system_func_type;
    _path = SystemFuncParam::ParamPath(system_func_type);
}

//----------------------------------------------------------------------------
// ~SystemFuncParam
//----------------------------------------------------------------------------
SystemFuncParam::~SystemFuncParam()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> SystemFuncParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<SystemFuncParam>(*this);
}

//----------------------------------------------------------------------------
// get_system_func_type
//----------------------------------------------------------------------------
SystemFuncType SystemFuncParam::get_system_func_type() const
{
    // Return the system func type
    return _system_func_type;
}

//--------------------------------
// DummyParam class implementation
//--------------------------------

//----------------------------------------------------------------------------
// CreateParam
//----------------------------------------------------------------------------
std::unique_ptr<DummyParam> DummyParam::CreateParam(std::string path) 
{
    // Create the dummy param
    auto param = std::make_unique<DummyParam>(NinaModule::ANY);
    param->type = ParamType::COMMON_PARAM;
    param->_path = path;
    param->name = path;
    param->patch_param = false;
    param->patch_layer_param = false;
    param->patch_common_layer_param = false;
    param->patch_state_param = false;
    return param;
}

//----------------------------------------------------------------------------
// TempoNoteValueParam
//----------------------------------------------------------------------------
DummyParam::DummyParam(NinaModule module) : Param(module)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// DummyParam
//----------------------------------------------------------------------------
DummyParam::DummyParam(const DummyParam& param) : Param(param)
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// clone
//----------------------------------------------------------------------------
std::unique_ptr<Param> DummyParam::clone() const
{
    // Copy and return a pointer to the param
    return std::make_unique<DummyParam>(*this);
}

//----------------------------------------------------------------------------
// ~DummyParam
//----------------------------------------------------------------------------
DummyParam::~DummyParam()
{
    // Nothing specific to do
}

//---------------------------------
// ParamChange class implementation
//---------------------------------

//----------------------------------------------------------------------------
// ParamChange
//----------------------------------------------------------------------------
ParamChange::ParamChange(std::string path, float value, NinaModule from_module)
{
    this->path = path;
    this->value = value;
    this->from_module = from_module;
    this->display = true;
    this->layers_mask = LayerInfo::GetLayerMaskBit(utils::get_current_layer_info().layer_num());
}

//----------------------------------------------------------------------------
// ParamChange
//----------------------------------------------------------------------------
ParamChange::ParamChange(const Param *param, NinaModule from_module)
{
    this->path = param->get_path();
    this->value = param->get_value();
    this->from_module = from_module;
    this->display = (std::strlen(param->get_name()) > 0) &&
                    ((param->module != NinaModule::SURFACE_CONTROL) &&
                        (param->module != NinaModule::MIDI_DEVICE) &&
                        (param->type != ParamType::UI_STATE_CHANGE));
    this->layers_mask = LayerInfo::GetLayerMaskBit(utils::get_current_layer_info().layer_num());
}
