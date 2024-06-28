/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  param.h
 * @brief Parameter class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _PARAM_H
#define _PARAM_H

#include <memory>
#include <string>
#include <cstring>
#include <functional>
#include <mutex>
#include "tempo.h"
#include "surface_control.h"
#include "system_func.h"
#include "common.h"

// External classes
class BaseManager;

// Common Param IDs
enum CommonParamId : int
{
    TEMPO_BPM_PARAM_ID,
    WT_NAME_PARAM_ID,
    MIDI_CLK_IN_PARAM_ID,
    MIDI_ECHO_FILTER_PARAM_ID,
    SYSTEM_COLOUR
};

// Param Type
enum class ParamType
{
    COMMON_PARAM,
	MODULE_PARAM,
    SYSTEM_FUNC,
    UI_STATE_CHANGE
};

// Patch State
enum class PatchState
{
    STATE_A,
    STATE_B
};

// Current Param State
struct ParamState
{
    std::string path;
    std::vector<std::string> state_stack;

    ParamState()
    {
        // There is always a default state
        state_stack.push_back("default");
    }
};

// Context Specific Param
struct ContextSpecificParams
{
    Param *context_param;
    float context_value;
    std::vector<Param *> param_list;
};

// Param class
class Param
{
public:
    // General attributes
    ParamType type;
    NinaModule module;
    int param_id;
    int processor_id;
    bool str_param;
    bool patch_param;
    bool patch_layer_param;
    bool patch_common_layer_param;
    bool patch_state_param;
    bool layer_1_param;
    bool mod_matrix_param;
    bool global_param;
    bool physical_control_param;
    bool multifn_switch;
    bool always_show;
    std::string ref;
    std::string set_ui_state;
    std::string state;
    bool alias_param;
    
    // Position based attributes
    bool multi_position_param;
    uint num_positions;
    uint actual_num_positions;
    bool position_param;
    uint position;
    bool is_selected_position;

    // Display attributes
    std::string name;
    std::string display_name;
    bool display_switch;
    int display_range_min;
    int display_range_max;
    std::vector<std::string> display_strings;
    std::string value_tag;
    std::vector<std::string> value_tags;
    bool numeric_enum_param;
    std::string haptic_mode;
    std::string mod_src_name;
    std::string mod_dst_name;
    bool separator;

    // Param list
    std::string param_list_name;
    std::string param_list_display_name;
    std::vector<Param *> param_list;
    std::vector<ContextSpecificParams> context_specific_param_list;

    // Helper functions
    static std::unique_ptr<Param> CreateParam(std::string name);  
    static std::unique_ptr<Param> CreateParam(const BaseManager *msg, std::string name);
    static std::unique_ptr<Param> CreateParam(NinaModule module, std::string name);
    static std::string ParamPath(std::string name);
    static std::string ParamPath(const BaseManager *msg, std::string name);
    static std::string ParamPath(NinaModule module, std::string name);

    // Constructor
    Param(const Param &param);
    Param(NinaModule module);
    virtual std::unique_ptr<Param> clone() const;

    // Destructor
    virtual ~Param();

    // Public functions
    const char *get_name() const;
    virtual std::string get_path() const;
    virtual void set_path(std::string path);
    virtual bool cmp_path(std::string path);
    virtual float get_value() const;
    virtual float get_normalised_value() const;
    virtual float get_value_from_normalised_float(float value) const;
    virtual int get_position_value() const;
    virtual uint get_num_positions() const;
    virtual std::string get_str_value() const;
    virtual void set_value(float value);
    virtual void set_value_from_normalised_float(float value);
    virtual void set_value_from_param(const Param &param);
    virtual void set_value_from_position(uint position, bool force=false);
    virtual void set_multi_position_param_num_positions(uint num_positions);
    virtual void set_actual_num_positions(uint num_positions);
    virtual void set_position_param(uint position);
    virtual void set_str_value(std::string str_value);
    virtual std::pair<std::string, bool> get_value_as_string() const;
    virtual std::string get_position_as_string(uint pos) const;
    void add_mapped_param(Param *param);
    void set_linked_param(Param *linked_param);
    const std::vector<Param *>& get_mapped_params() const;
    Param *get_linked_param() const;
    void clear_mapped_params();
    std::string get_value_tag() const;
    std::vector<Param *> get_param_list() const;

protected:
    // Protected variables
    std::string _path;
    mutable std::mutex _mutex;
    float _value;
    std::string _str_value;
    bool _normalised_value;
    float _position_increment;
    float _physical_position_increment;
    std::vector<Param *> _mapped_params;
    Param *_linked_param;

    // Protected functions
    virtual float _to_normalised_float() const;
    virtual float _from_normalised_float(float value) const;
};

// Param Alias class
class ParamAlias : public Param
{
public:
    // Constructor
    ParamAlias(std::string path, Param *param);
    ParamAlias(const ParamAlias &param);

    // Destructor
    ~ParamAlias();

    // Public functions
    float get_value() const;
    float get_normalised_value() const;
    std::pair<std::string, bool> get_value_as_string() const;
    void set_value(float value);
    void set_value_from_param(const Param &param);
    void set_value_from_normalised_float(float value);
    Param *get_alias_param() const;

private:
    // Private member variables
    Param *_alias_param;
};

// Surface Control Param class
class SurfaceControlParam : public Param
{
public:
    // Constructor
    SurfaceControlParam(const SurfaceControlParam &param);
    SurfaceControlParam(SurfaceControlType type, uint control_num);
    virtual std::unique_ptr<Param> clone() const;

    // Destructor
    ~SurfaceControlParam();

    // Public functions
    SurfaceControlType type() const;
    const HapticMode& get_haptic_mode() const;
    void set_haptic_mode(const std::string haptic_mode_name);

private:
    // Private variables
    SurfaceControlType _type;
    std::string _haptic_mode_name;    
};

// Knob Param class
class KnobParam : public SurfaceControlParam
{
public:
    uint32_t last_pos;

    // Helper functions
    static std::unique_ptr<KnobParam> CreateParam(unsigned int control_num);
    static std::string ParamPath(unsigned int control_num);

    // Constructor
    KnobParam(const KnobParam &param);
    KnobParam(unsigned int control_num);
    std::unique_ptr<Param> clone() const;

    // Destructor
    ~KnobParam();

    // Public functions
    float get_value() const;
    uint get_position_value(uint prev_pos);
    void set_position_param(uint position, uint num_selectable_positions);
    void set_relative_position_param(float pos);
    void reset_param();
    uint32_t get_hw_value() const;
    void set_value_from_hw(uint32_t hw_value);
    bool hw_delta_outside_target_threshold(int32_t hw_delta, bool use_large_threshold);
    bool hw_value_within_target_threshold(uint32_t hw_value, uint32_t hw_target);
    void set_relative_pos(uint32_t hw_value);

private:
    bool _relative_pos_control;
    int _last_actual_pos;
    uint _num_selectable_positions;
    uint32_t _relative_pos_offset;
};

// Switch Param class
class SwitchParam : public SurfaceControlParam
{
public:
    // Helper functions
    static std::unique_ptr<SwitchParam> CreateParam(unsigned int control_num);
    static std::string ParamPath(unsigned int control_num);
    static std::string MultifnSwitchParamPath(uint control_index);

    // Constructor
    SwitchParam(const SwitchParam &param);
    SwitchParam(unsigned int control_num);
    std::unique_ptr<Param> clone() const;
    
    // Destructor
    ~SwitchParam();

    // Public functions
    float get_value() const;
    void set_value(float value);

protected:
    // Protected functions
    float _from_normalised_float(float value) const;
};

// Knob Switch Param class
class KnobSwitchParam : public SwitchParam
{
public:
    // Helper functions
    static std::unique_ptr<KnobSwitchParam> CreateParam(unsigned int control_num);
    static std::string ParamPath(unsigned int control_num);

    // Constructor
    KnobSwitchParam(const KnobSwitchParam &param);
    KnobSwitchParam(unsigned int control_num);
    std::unique_ptr<Param> clone() const;
    
    // Destructor
    ~KnobSwitchParam();
};

// Button Param class
class ButtonParam : public Param
{
public:
    // Helper functions
    static std::unique_ptr<ButtonParam> CreateParam(std::string name);
    static std::unique_ptr<ButtonParam> CreateParam(const BaseManager *msg, std::string name);

    // Constructor
    ButtonParam(const ButtonParam& param);
    ButtonParam(NinaModule module);
    std::unique_ptr<Param> clone() const;

    // Destructor
    ~ButtonParam();

    // Public functions
    void set_value(float value);

protected:
    // Protected functions
    float _from_normalised_float(float value) const;
};

// Tempo BPM Param class
class TempoBpmParam : public Param
{
public:
    // Helper functions
    static std::unique_ptr<TempoBpmParam> CreateParam();
    static std::unique_ptr<TempoBpmParam> CreateParam(const BaseManager *msg);
    static std::string ParamPath();

    // Constructor
    TempoBpmParam(const TempoBpmParam& param); 
    TempoBpmParam(NinaModule module);
    std::unique_ptr<Param> clone() const;

    // Destructor
    ~TempoBpmParam();

    // Public functions
    void set_value(float val);    

protected:
    // Protected functions
    float _to_normalised_float() const;
    float _from_normalised_float(float value) const;
};

// Tempo Note Value Param class
class TempoNoteValueParam : public Param
{
public:
    // Helper functions
    static std::unique_ptr<TempoNoteValueParam> CreateParam();
    static std::unique_ptr<TempoNoteValueParam> CreateParam(const BaseManager *msg);

    // Constructor
    TempoNoteValueParam(const TempoNoteValueParam& param); 
    TempoNoteValueParam(NinaModule module);
    std::unique_ptr<Param> clone() const;

    // Destructor
    ~TempoNoteValueParam();

    // Public functions
    void set_value(float val);
    TempoNoteValue get_tempo_note_value() const;
    void set_tempo_note_value(TempoNoteValue val);
};

// System Func Param class
class SystemFuncParam : public Param
{
public:
    // Helper functions
    static std::unique_ptr<SystemFuncParam> CreateParam(SystemFuncType system_func_type);
    static std::string ParamPath(SystemFuncType system_func_type);

    // Constructor
    SystemFuncParam(const SystemFuncParam &param);
    SystemFuncParam(SystemFuncType system_func_type);
    virtual std::unique_ptr<Param> clone() const;
    
    // Destructor
    ~SystemFuncParam();

    // Public functions
    SystemFuncType get_system_func_type() const;

private:
    // Private variables
    SystemFuncType _system_func_type;
};

// Dummy Param class
class DummyParam : public Param
{
public:
    // Helper functions
    static std::unique_ptr<DummyParam> CreateParam(std::string path);

    // Constructor
    DummyParam(NinaModule module);
    DummyParam(const DummyParam& param);
    std::unique_ptr<Param> clone() const;

    // Destructor
    ~DummyParam();
};

// Param change
struct ParamChange
{
    // Constructor
    ParamChange() {}
    ParamChange(std::string path, float value, NinaModule from_module);
    ParamChange(const Param *param, NinaModule from_module);

    // Public variables    
    std::string path;
    float value;
    NinaModule from_module;
    bool display;
    uint layers_mask;
};

// Param Change History
struct ParamChangeHistory
{
    std::string path;
    float value;

    // Constructor
    ParamChangeHistory() {}
    ParamChangeHistory(std::string path, float value)
    {
        this->path = path;
        this->value = value;
    }    
};

#endif  // _PARAM_H
