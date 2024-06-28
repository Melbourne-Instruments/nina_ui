/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  utils.h
 * @brief Utility functions.
 *-----------------------------------------------------------------------------
 */
#ifndef _UTILS_H
#define _UTILS_H

#include <memory>
#include <vector>
#include <mutex>
#include <map>
#include <pthread.h>
#include "system_config.h"
#include "param.h"
#include "surface_control.h"
#include "layer_info.h"

namespace utils
{
    enum ParamRef
    {
        MORPH_VALUE,
        MORPH_MODE,
        TOGGLE_PATCH_STATE,
        LAYER_STATE,
        VCO_1_TUNE_OCTAVE,
        VCO_2_TUNE_OCTAVE,
        LFO_1_RATE,
        LFO_1_SYNC_RATE,
        LFO_2_RATE,
        LFO_2_SYNC_RATE,
        LAYER_NUM_VOICES,
        CURRENT_LAYER,
        WT_SELECT,
        LFO_SHAPE,
        LFO_2_SHAPE,
        NOISE_TYPE,
        LFO_1_TEMPO_SYNC,
        LFO_2_TEMPO_SYNC,
        MIDI_LOW_NOTE_FILTER,
        MIDI_HIGH_NOTE_FILTER,
        MIDI_CHANNEL_FILTER,
        LAYER_LOAD,
        ALL_NOTES_OFF,
        MPE_MODE,
        MPE_LOWER_ZONE_NUM_CHANNELS,
        MPE_UPPER_ZONE_NUM_CHANNELS,
        MPE_X,
        MPE_Y,
        MPE_Z,
        FX_TYPE_SELECT,
        REVERB_LEVEL,
        CHORUS_LEVEL,
        DELAY_LEVEL
    };

    struct LfoState
    {
        std::string state;
        bool mod_matrix;
        bool lfo_2;
        LfoState() {
            state = "";
            mod_matrix = false;
            lfo_2 = false;
        }
        LfoState(std::string state, bool mod_matrix, bool lfo_2) {
            this->state = state;
            this->mod_matrix = mod_matrix;
            this->lfo_2 = lfo_2;
        }    
    };

    struct SystemColour
    {
        std::string name;
        std::string colour;
    };

    // System utilities
    SystemConfig *system_config();
    void generate_session_uuid();
    std::string get_session_uuid();
    void set_maintenance_mode();
    bool maintenance_mode();
    void add_system_colour(SystemColour system_colour);
    SystemColour *get_system_colour_from_name(std::string name);
    SystemColour *get_system_colour_from_colour(std::string colour);
    uint get_system_colour_index(std::string colour);

    // Xenomai real-time utilities
    void init_xenomai();
    int create_rt_task(pthread_t *rt_thread, void *(*start_routine)(void *), void *arg, int sched_policy);
    void stop_rt_task(pthread_t *rt_thread);
    void rt_task_nanosleep(struct timespec *time);
    int rtdm_open(const char *path, int oflag);
    int rtdm_ioctl(int fd, int request, void *argp);
    int rtdm_close(int fd);

    // Layer utilities
    LayerInfo &get_current_layer_info();
    LayerInfo &get_layer_info(uint layer_num);
    void set_current_layer(uint layer_num);
    bool is_current_layer(uint layer_num);
    std::string get_default_layers_filename(uint index, bool with_ext);

    // MPE utilities
    void init_mpe_handling();
    void reset_mpe_params();
    std::pair<const Param *, const Param *> config_mpe_zone_channel_params();
    bool is_mpe_channel(uint channel);
    MpeMode get_mpe_mode(float mode_value);

    // Param utilities
    bool get_patch_modified();
    std::string get_default_patch_filename(uint index, bool with_ext);
    std::vector<Param *> get_params(NinaModule module);
    std::vector<Param *> get_params(ParamType param_type);
    std::vector<Param *> get_params(const std::string param_path_regex);
    std::vector<Param *> get_params_with_state(const std::string state);
    std::vector<Param *> get_patch_params();
    std::vector<Param *> get_mod_matrix_params();
    std::vector<Param *> get_global_params();
    Param *get_param(std::string path);
    Param *get_param(std::string path, std::string state);
    Param *get_param(ParamType param_type, int param_id);
    Param *get_param(NinaModule module, int param_id);
    Param *get_sys_func_param(SystemFuncType sys_func_type);
    Param *get_param_from_ref(ParamRef ref);
    KnobParam *get_data_knob_param();
    void register_param(std::unique_ptr<Param> param);
    void register_common_params();
    void reset_param_states();
    Param *push_param_state(std::string path, std::string state);
    Param *pop_and_push_param_state(std::string path, std::string push_state, std::string pop_state);
    Param *pop_param_state(std::string path, std::string state);
    std::string get_param_state(std::string path);
    void blacklist_param(std::string path);
    bool param_is_blacklisted(std::string path);
    bool param_has_ref(const Param *param, ParamRef ref);

    // LFO 1/2 param utilities
    void init_lfo_handling();
    void push_lfo_state(LfoState state);
    void pop_lfo_state();
    void pop_all_lfo_states();
    LfoState get_current_lfo_state();
    bool lfo_2_selected();
    void set_lfo_2_selected(bool selected);
    Param *get_lfo_1_rate_param();
    Param *get_lfo_1_tempo_sync_param();
    LfoState get_req_lfo_1_state();
    bool lfo_1_sync_rate();
    Param *get_lfo_2_rate_param();
    Param *get_lfo_2_tempo_sync_param();
    LfoState get_req_lfo_2_state();
    bool lfo_2_sync_rate();  
    
    // Morph param utilities
    void set_morph_knob_num(uint num);
    int get_morph_knob_num();
    KnobParam *get_morph_knob_param();
    void morph_lock();
    void morph_unlock();
    void set_morph_on(bool on);
    bool is_morph_on();    
    void set_morph_enabled(bool enabled);
    bool is_morph_enabled();
    bool get_prev_morph_state();
    void set_prev_morph_state();
    void reset_morph_state();
    MorphMode get_morph_mode(float mode_value);

    // Haptic utilities
    void init_haptic_modes();
    void add_haptic_mode(const HapticMode& haptic_mode);
    bool set_default_haptic_mode(SurfaceControlType control_type,std::string haptic_mode_name);
    const HapticMode& get_haptic_mode(SurfaceControlType control_type, std::string haptic_mode_name);

    // Multi-function switches utilities
    void set_multifn_switches_mode(MultifnSwitchesMode mode);
    MultifnSwitchesMode get_multifn_switches_mode();
    void set_num_active_multifn_switches(uint num_switches);
    bool is_active_multifn_switch(uint switch_num);
    std::vector<SwitchParam *> get_multifn_switch_params();

    // OSC utilities
    void set_osc_running(bool running);
    bool is_osc_running();

    // Manager utilties
    void register_manager(NinaModule module, BaseManager *mgr);
    BaseManager *get_manager(NinaModule module);

    // Seq/Arp utilties
    std::mutex& seq_mutex();
    void seq_signal();
    void seq_signal_without_lock();
    void seq_wait(std::unique_lock<std::mutex>& lk);
    std::mutex& arp_mutex();
    void arp_signal();
    void arp_signal_without_lock();
    void arp_wait(std::unique_lock<std::mutex>& lk);
    uint tempo_pulse_count(TempoNoteValue note_value);
    TempoNoteValue tempo_note_value(int value);
}

#endif  // _UTILS_H
