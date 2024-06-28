/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  sequencer_manager.h
 * @brief Sequencer Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SEQUENCER_MANAGER_H
#define _SEQUENCER_MANAGER_H

#include <atomic>
#include "base_manager.h"
#include "event.h"
#include "event_router.h"
#include "utils.h"
#include "layer_info.h"
#include "param.h"
#include "timer.h"

// Sequencer Param IDs
enum SequencerParamId : int
{
    REC_PARAM_ID,
    RUN_PARAM_ID,
    NUM_STEPS_PARAM_ID,
    TEMPO_NOTE_VALUE_PARAM_ID,
    HOLD_PARAM_ID, 
    STEP_1_ID
};

// Sequencer State
enum class SeqState
{
    IDLE,
    PROGRAMMING,
    START_PLAYING,
    PLAYING_NOTEON,
    PLAYING_NOTEOFF,
    PLAYING_LAST_NOTEOFF
};

// Step Type
enum class StepType
{
    NORMAL,
    START_TIE,
    TIE,
    END_TIE,
    REST
};

// Sequencer Step
struct SeqStep
{
    static const uint max_notes_per_step = 12;
    StepType step_type;
    std::array<snd_seq_event_t, max_notes_per_step> notes;
    int num_notes;
};

// Sequencer Manager class
class SequencerManager : public BaseManager
{
public:
    // Constructor
    SequencerManager(EventRouter *event_router);

    // Destructor
    ~SequencerManager();

    // Public functions
    bool start();
    void process();
    void process_event(const BaseEvent *event);
    void process_midi_event_direct(const snd_seq_event_t *event);

private:
    // Private variables
    static const uint _max_steps = 16;
    EventListener *_midi_param_change_listener;
    EventListener *_sfc_listener;
    EventListener *_fm_reload_presets_listener;
    EventListener *_fm_param_changed_listener;
    EventListener *_gui_listener;
    EventListener *_sfc_system_func_listener;
    std::thread *_tempo_event_thread;
    bool _fsm_running;
    bool _reset_fsm;
    uint _tempo_pulse_count;
    uint _note_duration_pulse_count;
    std::array<SeqStep, _max_steps> _seq_steps;
    uint _num_steps;
    uint _num_active_steps;
    uint _num_selected_steps;
    uint _step;
    int _tie_start_step;
    int _tie_end_step;
    std::atomic<bool> _program;
    std::atomic<bool> _enable;
    std::atomic<bool> _hold;
    bool _prev_program;
    bool _prev_enable;
    SeqState _seq_state;
    snd_seq_event_t _note_on;
    std::vector<unsigned char> _played_notes;
    unsigned char _base_note;
    unsigned char _preset_base_note;
    uint _step_note_index;
    std::vector<snd_seq_event_t> _sent_notes;
    Param *_num_steps_param;
    unsigned char _current_midi_channel = 0;
    std::vector<snd_seq_event_t> _idle_sent_notes;

    // Private functions
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_presets(bool from_ab_toggle);
    void _process_system_func_event(const SystemFunc &data);
    void _process_param_value(const Param& param, float value, NinaModule from_module=NinaModule::ANY);
    void _register_params();
    void _process_tempo_event();
    bool _process_fsm();
    void _send_seq_note();
    void _stop_seq_note();
    void _stop_seq();
    void _clear_idle_sent_notes();
    void _send_note(const snd_seq_event_t &note);
    void _set_sys_func_switch(SystemFuncType system_func_type, bool set);
    void _set_multifn_switch(uint index, bool on);
    void _reset_multifn_switches(bool force=false);
    void _reset_seq_steps();
    void _set_seq_step_param(uint step);
};

#endif // _SEQUENCER_MANAGER_H
