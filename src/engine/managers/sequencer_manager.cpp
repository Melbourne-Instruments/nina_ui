/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  sequencer_manager.h
 * @brief Sequencer Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include "sequencer_manager.h"
#include "keyboard_manager.h"
#include "utils.h"

// Constants
constexpr unsigned char MAX_MIDI_NOTE  = 108;
constexpr char REC_NAME[]              = "Sequencer Record";
constexpr char REC_PARAM_NAME[]        = "rec";
constexpr char RUN_NAME[]              = "Sequencer Run";
constexpr char RUN_PARAM_NAME[]        = "run";
constexpr char NUM_STEPS_NAME[]        = "Sequencer Num Steps";
constexpr char NUM_STEPS_PARAM_NAME[]  = "num_steps";
constexpr char TEMPO_NOTE_VALUE_NAME[] = "Tempo Note Value";
constexpr char HOLD_NAME[]             = "Hold";
constexpr char HOLD_PARAM_NAME[]       = "hold";
constexpr char STEP_PARAM_NAME[]       = "step_";
constexpr char STEP_NAME[]             = "Step ";
constexpr unsigned char TIE_START_STEP = 0x80;
constexpr unsigned char TIE_STEP       = 0x40;
constexpr unsigned char TIE_END_STEP   = 0x20;
constexpr unsigned char REST_STEP      = 0x10;
constexpr unsigned char STEP_ATTR_MASK = (TIE_START_STEP+TIE_STEP+TIE_END_STEP+REST_STEP);

//----------------------------------------------------------------------------
// SequencerManager
//----------------------------------------------------------------------------
SequencerManager::SequencerManager(EventRouter *event_router) : 
    BaseManager(NinaModule::SEQUENCER, "SequencerManager", event_router)
{
    // Initialise class data
    _tempo_event_thread = 0;
    _fsm_running = true;
    _reset_fsm = false;
    _tempo_pulse_count = utils::tempo_pulse_count(TempoNoteValue::QUARTER);
    _note_duration_pulse_count = _tempo_pulse_count >> 1;
    _num_steps = 0;
    _num_active_steps = 0;
    _num_selected_steps = 0;
    _step = 0;
    _program = false;
    _enable = false;
    _hold = false;
    _prev_program = _program;
    _prev_enable = _enable;
    _midi_param_change_listener = 0;
    _sfc_listener = 0;
    _fm_reload_presets_listener = 0;
    _fm_param_changed_listener = 0;
    _gui_listener = 0;
    _sfc_system_func_listener = 0;
    _seq_state = SeqState::IDLE;
    _base_note = 0xFF;
    _preset_base_note = 0xFF;
    _step_note_index = 0;
    _tie_start_step = -1;
    _tie_end_step = -1;
    _idle_sent_notes.clear();
    _reset_seq_steps();
    
    // Register the Sequencer params
    _register_params();

    // Get the number of steps param - we do this for efficiency
    _num_steps_param = utils::get_param(NinaModule::SEQUENCER, SequencerParamId::NUM_STEPS_PARAM_ID);
}

//----------------------------------------------------------------------------
// ~SequencerManager
//----------------------------------------------------------------------------
SequencerManager::~SequencerManager()
{
    // Stop the FSM
    {
        std::lock_guard<std::mutex> lk(utils::seq_mutex());
        _fsm_running = false;
    }
    utils::seq_signal_without_lock();

    // Stop the tempo event thread
    if (_tempo_event_thread) {
        // Wait for the tempo event thread to finish and delete it
        if (_tempo_event_thread->joinable())
            _tempo_event_thread->join();
        delete _tempo_event_thread;
        _tempo_event_thread = 0;
    }

    // Clean up the event listeners
    if (_midi_param_change_listener)
        delete _midi_param_change_listener;
    if (_sfc_listener)
        delete _sfc_listener;
    if (_fm_reload_presets_listener)
        delete _fm_reload_presets_listener;
    if (_fm_param_changed_listener)
        delete _fm_param_changed_listener;
    if (_gui_listener)
        delete _gui_listener;     
    if (_sfc_system_func_listener)
        delete _sfc_system_func_listener;     
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool SequencerManager::start()
{
    // Before starting the Sequencer, process all the preset values
    _process_presets(false);

	// Start the tempo event thread
	_tempo_event_thread = new std::thread(&SequencerManager::_process_tempo_event, this);

    // Call the base manager
    return BaseManager::start();			
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void SequencerManager::process()
{
    // Create and add the various event listeners
    _midi_param_change_listener = new EventListener(NinaModule::MIDI_DEVICE, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_midi_param_change_listener);
    _sfc_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_sfc_listener);
    _fm_reload_presets_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_fm_reload_presets_listener);	
    _fm_param_changed_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_fm_param_changed_listener);	
    _gui_listener = new EventListener(NinaModule::GUI, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_gui_listener);    
    _sfc_system_func_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_sfc_system_func_listener);    

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void SequencerManager::process_event(const BaseEvent *event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
        {
            // Process the Param Changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;
        }

        case EventType::RELOAD_PRESETS:
        {
            // Process the presets
            _process_presets(static_cast<const ReloadPresetsEvent *>(event)->from_ab_toggle());
            break;
        }
        
        case EventType::SYSTEM_FUNC:
        {
            // Process the System Function event
            _process_system_func_event(static_cast<const SystemFuncEvent *>(event)->system_func());        
            break;            
        }
        
        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// process_midi_event_direct
//----------------------------------------------------------------------------
void SequencerManager::process_midi_event_direct(const snd_seq_event_t *event)
{
    auto seq_event = *event;
    
    // Get the Sequencer mutex
    std::lock_guard<std::mutex> guard(utils::seq_mutex());

    // If the note doesn't match the layer 1 channel filter then dont pass it on 
    if (!utils::get_layer_info(0).check_midi_channel_filter(seq_event.data.note.channel)) {
        _send_note(seq_event);
        return;
    }

    // Update the variable used to set the midi channel, is is so we still set the MIDI 
    // output to be the last received channel, even when the voice 1 filter is set to omni
    _current_midi_channel = seq_event.data.note.channel;

    // Are we in programming mode?
    if (_program)
    {
        // If there are steps to fill
        if ((_num_steps < _max_steps)) {
            // If this is a note on event, add it to the current step 
            if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity != 0)) {
                // Make sure the max notes per step are not exceeded
                if (_step_note_index < SeqStep::max_notes_per_step) {
                    // If this is the first note of the first step
                    if ((_num_steps == 0) && (_step_note_index == 0))
                    {
                        // Set the base note
                        _base_note = seq_event.data.note.note;
                    }

                    // Add the note to the step
                    DEBUG_BASEMGR_MSG("Added note: " << (int)seq_event.data.note.note << " to step " << _num_steps);
                    _seq_steps[_num_steps].notes[_step_note_index] = seq_event;
                    _seq_steps[_num_steps].notes[_step_note_index].data.note.note -= _base_note;
                    _step_note_index++;

                    // Add the note to the played notes vector
                    // Once this vector is empty, programming will move to the next step
                    _played_notes.push_back(seq_event.data.note.note);
                }
            }
            // If this is a note off event
            else if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF) || 
                     ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity == 0))) {
                // Remove the note from the played notes
                bool found = false;
                for (auto it = _played_notes.begin(); it != _played_notes.end(); it++)
                {
                    // Erase the note if found
                    if (*it == seq_event.data.note.note)
                    {
                        _played_notes.erase(it);
                        found = true;
                        break;
                    }
                }

                // Are there any notes left? If none, move to the next step
                if (found && _played_notes.size() == 0)
                {
                    // Update the step
                    DEBUG_BASEMGR_MSG("Sequencer Step " << _num_steps << ": Programmed " << _step_note_index << " notes");
                    _seq_steps[_num_steps].num_notes = _step_note_index;

                    // Are we tying this step to other steps?
                    if ((_tie_start_step != -1) && (_tie_end_step != -1) && 
                        ((uint)_tie_start_step == _num_steps) && ((uint)_tie_end_step > (uint)_tie_start_step)) {
                        // Yes, indicate this is the first step in the tie
                        DEBUG_BASEMGR_MSG("Start Tie: " << _num_steps);
                        _seq_steps[_num_steps].step_type = StepType::START_TIE;
                        _set_seq_step_param(_num_steps);
                        _num_steps++;

                        // Now loop through and set any middle steps to continue the tie
                        for (; _num_steps<(uint)_tie_end_step; _num_steps++) {
                            _seq_steps[_num_steps].step_type = StepType::TIE;
                            _set_seq_step_param(_num_steps);
                        }

                        // Set the last step to indicate this is the last step in the tie
                        DEBUG_BASEMGR_MSG("End Tie: " << _num_steps);
                        _seq_steps[_num_steps].step_type = StepType::END_TIE;
                        _set_seq_step_param(_num_steps);
                    }
                    else {
                        // Not tying notes, just set this sequencer step
                        _set_seq_step_param(_num_steps);
                    }

                    // Increment for the next step
                    _num_steps++;
                    _step_note_index = 0;
                    if (_num_steps < _max_steps) {
                        // If we just processed a tie, reset both tie step switches
                        if ((_tie_start_step != -1) && (_tie_end_step != -1)) {
                            _set_multifn_switch((uint)_tie_start_step, false);
                            _set_multifn_switch((uint)_tie_end_step, false);
                        }
                        else {
                            _set_multifn_switch((_num_steps - 1), false);
                        }
                        _set_multifn_switch(_num_steps, true);
                        _tie_start_step = -1;
                        _tie_end_step = -1;
                    }
                    else if (_num_steps == _max_steps) {
                        // Reset the multi-function keys
                        _reset_multifn_switches();                        
                    }

                    // Update the number of steps parameter
                    _num_steps_param->set_value((_num_steps - 1.f) / _max_steps);
                    _num_selected_steps = _num_steps;
                }            
            }
        }

        // Forward the MIDI event note
        _send_note(seq_event);
    }
    // Is the Sequencer on and has steps?
    else if (_enable && _num_steps)
    {
        // If a note-on
        if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity != 0))
        {
            // Add the note to the played notes
            _played_notes.push_back(seq_event.data.note.note);

            // Set the new base note and start the sequence immediately if not playing
            _base_note = seq_event.data.note.note;

            //_note_off_received = false;
            if (!_hold && (_seq_state == SeqState::START_PLAYING)) {
                // Reset the Seq FSM
                // Note: Lock already aquired at the start of this function
                _reset_fsm = true;
                utils::seq_signal_without_lock();
            }
        }
        // If a note-off
        else if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF) || 
                 ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity == 0)))
        {
            // Remove the note from the played notes
            for (auto it = _played_notes.begin(); it != _played_notes.end(); it++)
            {
                // Erase the note if found
                if (*it == seq_event.data.note.note)
                {
                    _played_notes.erase(it);
                    break;
                }
            }

            // Are there any notes left? If so, set the new base note
            if (_played_notes.size() > 0)
            {
                // Set the new base note
                _base_note = _played_notes.back();			
            }
        }
    }
    else
    {
        // If a note-on
        if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity != 0)) {
            // Add the note to the idle sent notes
            _idle_sent_notes.push_back(seq_event);
        }
        // If a note-off
        else if ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF) || 
                 ((seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) && (seq_event.data.note.velocity == 0))) {
            // Remove the note from the idle sent notes
            for (auto it = _idle_sent_notes.begin(); it != _idle_sent_notes.end(); it++) {
                // Erase the note if found
                if (((*it).data.note.note == seq_event.data.note.note) &&
                    ((*it).data.note.channel == seq_event.data.note.channel)) {
                    _idle_sent_notes.erase(it);
                    break;
                }
            }
        }
                  
        // Forward the MIDI event note
        _send_note(seq_event);
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void SequencerManager::_process_param_changed_event(const ParamChange &param_change)
{
    // Get the param, check if it exists and is for the Sequencer
    const Param *param = utils::get_param(param_change.path.c_str());
    if (param && ((param->module == module()) || (param->module == NinaModule::KEYBOARD))) {
        // Get the Sequencer mutex
        std::lock_guard<std::mutex> guard(utils::seq_mutex());
        
        // Process the param value
        _process_param_value(*param, param_change.value, param_change.from_module);
    }
}

//----------------------------------------------------------------------------
// _process_presets
//----------------------------------------------------------------------------
void SequencerManager::_process_presets(bool from_ab_toggle)
{
    // If not from an A/B toggle - or layer change
    if (!from_ab_toggle) {    
        // Get the mutex lock
        std::unique_lock<std::mutex> lk(utils::seq_mutex());

        // The Program mode in the sequencer should be reset whenever the presets
        // are loaded
        auto *param =utils::get_param(Param::ParamPath(this, REC_PARAM_NAME).c_str());
        if (param)
        {
            // Reset the sequencer
            param->set_value((float)false);
        }

        // Reset the sequencer settings
        _num_steps = 0;
        _step_note_index = 0;
        _base_note = 0xFF;
        _tie_start_step = -1;
        _tie_end_step = -1;
        _reset_seq_steps();
        _played_notes.clear();

        // Parse the Sequencer params
        for (const Param *p : utils::get_params(module()))
        {
            // Process the initial param value
            _process_param_value(*p, p->get_value());
        }
    }    
}

//----------------------------------------------------------------------------
// _process_system_func_event
//----------------------------------------------------------------------------
void SequencerManager::_process_system_func_event(const SystemFunc &system_func)
{
    // Get the Sequencer mutex
    std::lock_guard<std::mutex> guard(utils::seq_mutex());

    // If we are programming
    if (_program) {
        // Parse the system function
        switch (system_func.type)
        {
            case SystemFuncType::MULTIFN_SWITCH:
            {
                // Only process if the multi-function switches are in Sequencer record mode
                if (utils::get_multifn_switches_mode() == MultifnSwitchesMode::SEQ_REC) {
                    // If the step key is pressed then insert a rest at the current step, and move
                    // to the next step
                    if ((system_func.num == _num_steps) && system_func.value) {
                        // Add a rest by simply having no notes defined for this step
                        DEBUG_BASEMGR_MSG("Added rest to step " << _num_steps);
                        DEBUG_BASEMGR_MSG("Sequencer Step " << _num_steps << ": Programmed " << _step_note_index << " notes");

                        // If a tie is set, clear it
                        if ((_tie_start_step != -1) && (_tie_end_step != -1)) {
                            _set_multifn_switch(_tie_end_step, false);
                            _tie_start_step = -1;
                            _tie_end_step = -1;
                        }

                        // Now add the rest
                        _seq_steps[_num_steps].step_type = StepType::REST;
                        _set_seq_step_param(_num_steps);
                        _num_steps++;
                        _step_note_index = 0;
                        if (_num_steps < _max_steps) {
                            _set_multifn_switch((_num_steps - 1), false);
                            _set_multifn_switch(_num_steps, true);
                        }
                        else if (_num_steps == _max_steps) {
                            // Reset the multi-function keys
                            _reset_multifn_switches();                      
                        }

                        // Update the number of steps parameter
                        _num_steps_param->set_value((_num_steps - 1.f) / _max_steps);
                        _num_selected_steps = _num_steps;                                   
                    }
                    // If the key being pressed is after the current step then turn that key on to indicate a tie will now
                    // be inserted
                    else if ((system_func.num > _num_steps) && system_func.value) {
                        bool set_tie = true;

                        // If a tie is already set
                        if ((_tie_start_step != -1) && (_tie_end_step != -1)) {
                            // If this is the same tie end step, clear the tie
                            _set_multifn_switch(_tie_end_step, false);
                            if ((uint)_tie_end_step == system_func.num) {
                                _tie_start_step = -1;
                                _tie_end_step = -1;
                                set_tie = false;
                            }
                        }

                        // If we should set the tie
                        if (set_tie) {
                            // Indicate this step will now be tied
                            _tie_start_step = _num_steps;
                            _tie_end_step = system_func.num;
                            _set_multifn_switch(system_func.num, true);
                        }
                    }
                }
                break;
            }

            default:
                break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_param_value
//----------------------------------------------------------------------------
void SequencerManager::_process_param_value(const Param& param, float value, NinaModule from_module)
{
    // Note: Lock the SEQ mutex *before* calling this function

    // Process the keyboard param value based on the param ID
    if (param.module == NinaModule::KEYBOARD) {
        switch (param.param_id)
        {
            case KeyboardParamId::ENABLE_PARAM_ID:
            {
                // If we are programming AND the KBD is now disabled
                if (_program && (param.get_value() == 0)) {
                    // Put the multi-function switches into Sequencer Record mode
                    utils::set_multifn_switches_mode(MultifnSwitchesMode::SEQ_REC);

                    // Select the current step if valid
                    if (_num_steps < _max_steps) {
                        _set_multifn_switch(_num_steps, true);
                    }
                }
                break;              
            }

            default:
                // No specific processing
                break;
        }
    }
    else
    {
        // Process the sequencer param value based on the param ID
        switch (param.param_id)
        {
            case SequencerParamId::REC_PARAM_ID:	
            {
                // Update the program value
                // This will be processed during the next processing loop of the FSM
                _program = (value == 0) ? false : true;

                // Clear the sequence step parameters if we are starting programming
                if (_program) {
                    for (uint i=0; i<_max_steps; i++) {
                        auto param = utils::get_param(Param::ParamPath(this, STEP_PARAM_NAME + std::to_string(i + 1)));
                        if (param) {
                            param->set_str_value("00FFFFFFFFFFFFFFFFFFFFFFFF");
                            auto param_change = ParamChange(param, module());
                            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                        }                                    
                    }
                    utils::set_multifn_switches_mode(MultifnSwitchesMode::SEQ_REC);
                }
                else {
                    // Have any steps been programmed?
                    if (_num_steps) {
                        // Send the number of steps parameter change
                        auto param_change = ParamChange(_num_steps_param, module());
                        param_change.display = false;
                        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                    }

                    // Reset the multi-function keys
                    _reset_multifn_switches();
                }

                // Signal the FSM
                // Note: Lock already aquired at the start of this function
                _reset_fsm = true;
                utils::seq_signal_without_lock();
                break;
            }

            case SequencerParamId::RUN_PARAM_ID:
            {
                // Update the enable value
                // This will be processed during the next processing loop of the FSM
                _enable = (value == 0) ? false : true;       

                // If we are enabling, signal the FSM immediately and make sure the RUN button is on
                if (_enable) {
                    // Make sure the RUN button is on                     
                    _set_sys_func_switch(SystemFuncType::SEQ_RUN, true);
                }
                else {
                    // If we receive this RUN stop param change from MIDI, reset the RUN button and multi-function keys
                    if (from_module == NinaModule::MIDI_DEVICE) {
                        _set_sys_func_switch(SystemFuncType::SEQ_RUN, false);
                        _reset_multifn_switches(true);
                    }
                }

                // Signal the FSM
                // Note: Lock already aquired at the start of this function
                _reset_fsm = true;
                utils::seq_signal_without_lock();                
                break;					
            }

            case SequencerParamId::NUM_STEPS_PARAM_ID:
            {
                // Set the number of steps to play
                // Note 1: If set to more steps than are available, it will be limited
                // to the actual steps available
                // Note 2: Min steps is 1
                auto num_selected_steps = (value * _max_steps) + 1;
                if (num_selected_steps > _num_steps) {
                    // Set the number of active steps
                    _num_active_steps = _num_steps;
                }
                else {
                    // Set the number of active steps
                    _num_active_steps = num_selected_steps;

                    // If we are playing and the number of steps is now less than previously
                    if ((_seq_state != SeqState::IDLE) && (_seq_state != SeqState::PROGRAMMING)) {
                        // If we are reducing the number of steps
                        if (num_selected_steps < _num_selected_steps) {
                            // Make sure the last step LED is switched off
                            _set_multifn_switch((_num_selected_steps - 1), false);
                        }
                    }
                }
                _num_selected_steps = num_selected_steps;				
            }
            break;

            case SequencerParamId::TEMPO_NOTE_VALUE_PARAM_ID:
            {
                // Set the tempo pulse count from the updated note value
                _tempo_pulse_count = utils::tempo_pulse_count(utils::tempo_note_value(static_cast<const TempoNoteValueParam&>(param).get_tempo_note_value()));
                _note_duration_pulse_count = _tempo_pulse_count >> 1; 
                break;                  
            }

            case SequencerParamId::HOLD_PARAM_ID:
            {
                // Update the hold parameter
                _hold = (value == 0) ? false : true;
                break;		
            }

            case SequencerParamId::STEP_1_ID:
            default:
                // Processing for sequencer steps
                // Ensure the step ID is valid
                if ((uint)param.param_id < (SequencerParamId::STEP_1_ID + _max_steps)) {
                    auto step_index = param.param_id - SequencerParamId::STEP_1_ID;

                    // Reset the number of notes for this step
                    _seq_steps[step_index].num_notes = -1;

                    // Must be a string param
                    if (param.str_param && (param.get_str_value().size() == (2 + (SeqStep::max_notes_per_step * 2)))) {
                        auto notes = param.get_str_value();
                        bool valid_step = true;

                        // Get the note attributes
                        auto str_attr = notes.substr(0, 2);
                        auto attr = std::stoi(str_attr, nullptr, 16);

                        // Is this a normal or start tie note
                        if ((attr == 0) || (attr == TIE_START_STEP)) {
                            // Set the note type
                            _seq_steps[step_index].step_type = ((attr == TIE_START_STEP) ? StepType::START_TIE : StepType::NORMAL);

                            // Process the notes for this step
                            for (uint i=0; i<SeqStep::max_notes_per_step; i++) {
                                auto str_note = notes.substr((2 + (i*2)), 2);
                                auto note = std::stoi(str_note, nullptr, 16);

                                // If this step has no note, stop processing notes for this step
                                if (note == 0xFF) {
                                    if (i == 0) {
                                        valid_step = false;
                                    }
                                    break;
                                }

                                // If this is the first note of step 1, set the base note
                                if ((step_index == 0) && (i == 0)) {
                                    _preset_base_note = note;
                                }

                                // Convert the key to a char and insert into the step sequence
                                snd_seq_event_t ev;
                                snd_seq_ev_clear(&ev);
                                snd_seq_ev_set_noteon(&ev, 0, (note - _preset_base_note), 127);
                                _seq_steps[step_index].notes[i] = ev;
                                _seq_steps[step_index].num_notes = i + 1;
                            }
                        }
                        // Is this a tie note
                        else if (attr == TIE_STEP) {
                            _seq_steps[step_index].step_type = StepType::TIE;
                        }
                        // Is this an end tie note
                        else if (attr == TIE_END_STEP) {
                            _seq_steps[step_index].step_type = StepType::END_TIE;
                        }
                        // Is this a rest note
                        else if (attr == REST_STEP) {
                            _seq_steps[step_index].step_type = StepType::REST;
                        }

                        // If this step is valid, update the number of steps (and base note if not set)
                        if (valid_step) {
                            _num_steps = step_index + 1;
                            if ((step_index == 0) && (_base_note == 0xFF)) {
                                _base_note = _preset_base_note;
                            }
                        }
                    }
                }
                break;         
        }        
    }
}

//----------------------------------------------------------------------------
// _process_tempo_event
//----------------------------------------------------------------------------
void SequencerManager::_process_tempo_event()
{
    uint pulse_count = 0;

	// Do forever until stopped
	while (true) {
        // Get the mutex lock
        std::unique_lock<std::mutex> lk(utils::seq_mutex());

        // Wait for a tempoo event to be signalled
        utils::seq_wait(lk);

        // If the FSM is running
        if (_fsm_running) {
            // Has the FSM been reset or the required number of tempo pulses reached?
            if (_reset_fsm || (++pulse_count >= _note_duration_pulse_count)) {
                // Process the FSN
                bool start_playing = _process_fsm();

                // Set the next note duration pulse count to check
                // Note: Wait for one MIDI clock before when we start playing - we do this to ensure
                // the clock is running before sending note-on events                
                _note_duration_pulse_count = _tempo_pulse_count - _note_duration_pulse_count;
                pulse_count = start_playing ? (_note_duration_pulse_count - 1) : 0;
                _reset_fsm = false;
            }
        }
        else {
            // Quit the tempo event thread
            break;
        }        
    }
}

//----------------------------------------------------------------------------
// _process_fsm
//----------------------------------------------------------------------------
bool SequencerManager::_process_fsm()
{
    SeqState prev_seq_state = _seq_state;

    // Has the program state changed?
    if (_prev_program != _program)
    {
        // Entering programming mode?
        if (_program) {
            // Clear any idle sent notes, and enter programming
            _clear_idle_sent_notes();
            _seq_state = SeqState::PROGRAMMING;
        }
        else
        {
            // Entering enable mode and have sequencer steps been programmed?
            if (_enable && _num_steps)
            {
                if (_num_selected_steps > _num_steps) {
                    _num_active_steps = _num_steps;
                }
                else {
                    _num_active_steps = _num_selected_steps;
                }
                _played_notes.clear();
                _seq_state = SeqState::START_PLAYING;

                // Return TRUE to indicate we are starting playing
                _prev_program = _program;
                return true;
            }
            else
                _seq_state = SeqState::IDLE;
        }
        _prev_program = _program;
    }
    // Has the enable mode changed?
    else if (_prev_enable != _enable)
    {   
        // Entering enable mode and have sequencer steps been programmed?
        if (_enable && _num_steps)
        {
            // If enabling (with steps programmed), clear any idle sent notes
            if (_enable) {
                _clear_idle_sent_notes();
            }

            if (_num_selected_steps > _num_steps) {
                _num_active_steps = _num_steps;
            }
            else {
                _num_active_steps = _num_selected_steps;
            }
            _played_notes.clear();
            _seq_state = SeqState::START_PLAYING;

            // Return TRUE to indicate we are starting playing
            _prev_enable = _enable;
            return true;     
        }
        else
            _seq_state = SeqState::IDLE;
        _prev_enable = _enable;
    }

    // Run the state machine
    switch (_seq_state)
    {
    case SeqState::IDLE:
        // In this state, the sequencer is idle and waiting to either be played or
        // programmed
        // If not already idle
        if (prev_seq_state != SeqState::IDLE)
        {
            // The Sequencer has just been made idle
            // Play any note-offs to finish the Sequencer
            _stop_seq();
        }
        break;

    case SeqState::PROGRAMMING:
        // In this mode we are waiting for keys to be pressed for each step
        // If not already in programming mode
        if (prev_seq_state != SeqState::PROGRAMMING)
        {
            // We are entering programming mode
            // Play any note-offs to finish the Sequencer
            _stop_seq();

            // Reset the sequencer step count
            _num_steps = 0;
            _step_note_index = 0;
            _base_note = 0xFF;
            _tie_start_step = -1;
            _tie_end_step = -1;
            _reset_seq_steps();
            _played_notes.clear();
            _set_multifn_switch(0, true);
        }
        break;

    case SeqState::START_PLAYING:
        // Wait for a base note to be set
        if (_hold || (_played_notes.size() > 0))
        {
            // In this mode we are starting to play the sequence
            // Reset the step sequence and current note
            _step = 0;

            // Get the next Sequencer note to play and play it
            _send_seq_note();
            _set_multifn_switch((_num_active_steps - 1), false);
            _set_multifn_switch(0, true);

            // Change the state to indicate we are now playing a note-on
            _seq_state = SeqState::PLAYING_NOTEON;
        }
        break;        

    case SeqState::PLAYING_NOTEON:
        // In this state, we have just played a note-on, and now need to
        // play a note-off
        _stop_seq_note();

        // If we are not in hold mode, just play the sequence once
        if ((!_hold) && (_step == _num_active_steps) && (_played_notes.size() == 0))
        {
            // Enter the playing last note-off state
            _seq_state = SeqState::PLAYING_LAST_NOTEOFF;
        }
        else
        {
            // Change the state to indicate we are now playing a note-off
            _seq_state = SeqState::PLAYING_NOTEOFF;
        }
        break;

    case SeqState::PLAYING_NOTEOFF:
        // In this state, we have just played a note-off, and now need to
        // play the next note
        _send_seq_note();

        // Clear the previous step switch and set the current step switch
        if (_step == 0) {
            _set_multifn_switch((_num_active_steps - 1), false);
        }
        else {
            _set_multifn_switch((_step - 1), false);
        }
        _set_multifn_switch(_step, true);

        // Change the state to indicate we are now playing a note-on
        _seq_state = SeqState::PLAYING_NOTEON;
        break;

    case SeqState::PLAYING_LAST_NOTEOFF:
        // After this state, return to the start playing state
        _seq_state = SeqState::START_PLAYING;
        break;
    }
    return false;
}

//----------------------------------------------------------------------------
// _send_seq_note
//----------------------------------------------------------------------------
void SequencerManager::_send_seq_note()
{
  // Wrap the step if it has reached the maximum
    if (_step >= _num_active_steps) {
        _step = 0;

        // If the step counter has wrapped, then send note offs for all held notes so that ties arn't left hanging
        if (_sent_notes.size() > 0) {
            // Play each note-off
            for (uint i = 0; i < _sent_notes.size(); i++) {
                auto note_off = _sent_notes[i];
                note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
                note_off.data.note.velocity = 0;
                note_off.data.note.channel = _current_midi_channel;
                _send_note(note_off);
            }
            _sent_notes.clear();
        }
  }

    // If the tie state is normal or start tie, then play the notes ON
    // normally
    if (_seq_steps[_step].step_type <= StepType::START_TIE) {
        // Get the next Sequencer notes to play and send them
        _sent_notes.clear();
        if (_seq_steps[_step].num_notes > 0) {
            // Play each note-on
            for (uint i=0; i<(uint)_seq_steps[_step].num_notes; i++) {
                auto note_on = _seq_steps[_step].notes[i];
                note_on.data.note.note += _base_note;
                if (note_on.data.note.note > MAX_MIDI_NOTE)
                    note_on.data.note.note = MAX_MIDI_NOTE;
                note_on.data.note.velocity = 127;      
                note_on.data.note.channel = _current_midi_channel;
                _send_note(note_on);
                _sent_notes.push_back(note_on);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _stop_seq_note
//----------------------------------------------------------------------------
void SequencerManager::_stop_seq_note()
{
    // If the tie state is either none or end tie, play all the recently played 
    // notes, except as a note-off with 0 velocity
    if ((_seq_steps[_step].step_type == StepType::NORMAL) || 
        (_seq_steps[_step].step_type == StepType::END_TIE)) {
        // Send the note OFF for each note
        _stop_seq();
    }
    _step++;    
}

//----------------------------------------------------------------------------
// _stop_seq
//----------------------------------------------------------------------------
void SequencerManager::_stop_seq()
{
    // If there are any sent notes, play each note OFF
    if (_sent_notes.size() > 0) {
        // Play each note-off
        for (uint i=0; i<_sent_notes.size(); i++) {
            auto note_off = _sent_notes[i];
            note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
            note_off.data.note.velocity = 0;
            note_off.data.note.channel = _current_midi_channel;
            _send_note(note_off);
        }
        _sent_notes.clear();
    }
}

//----------------------------------------------------------------------------
// _clear_idle_sent_notes
//----------------------------------------------------------------------------
void SequencerManager::_clear_idle_sent_notes()
{
    // If there are any sent notes in the idle state, play each note OFF
    if (_idle_sent_notes.size() > 0) {
        // Play each note-off
        for (uint i=0; i<_idle_sent_notes.size(); i++) {
            auto note_off = _idle_sent_notes[i];
            note_off.type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
            note_off.data.note.velocity = 0;
            _send_note(note_off);
        }
        _idle_sent_notes.clear();
    }
}

//----------------------------------------------------------------------------
// _send_note
//----------------------------------------------------------------------------
void SequencerManager::_send_note(const snd_seq_event_t &note)
{
    //if (note.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON)
    //    DEBUG_BASEMGR_MSG("Send note: " << (int)note.data.note.note << ": ON");
    //else
    //    DEBUG_BASEMGR_MSG("Send note: " << (int)note.data.note.note << ": OFF");

    // Send the note directly to the Arpeggiator
    utils::get_manager(NinaModule::ARPEGGIATOR)->process_midi_event_direct(&note);
}

//----------------------------------------------------------------------------
// _set_sys_func_switch
//----------------------------------------------------------------------------
void SequencerManager::_set_sys_func_switch(SystemFuncType system_func_type, bool set)
{
    // Get the switch associated with the system function and set ut
    auto param = utils::get_param(SystemFuncParam::ParamPath(system_func_type).c_str());
    if (param) {
        auto mapped_params = param->get_mapped_params();
        for (Param *mp : mapped_params) {
            auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::SET_SWITCH_VALUE, NinaModule::SEQUENCER);
            sfc_func.control_path = mp->get_path();
            sfc_func.set_switch = set;
            _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
        }
    }
}

//----------------------------------------------------------------------------
// _select_multifn_switch
//----------------------------------------------------------------------------
void SequencerManager::_set_multifn_switch(uint index, bool on)
{
    // If the multi-function switches are not in use
    if ((_program && utils::get_multifn_switches_mode() == MultifnSwitchesMode::SEQ_REC) ||
        utils::get_multifn_switches_mode() == MultifnSwitchesMode::NONE) {
        // If the index is within range
        if (index < NUM_MULTIFN_SWITCHES) {
            // Select the specified multi-function key
            auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::SET_SWITCH_VALUE, NinaModule::SEQUENCER);
            sfc_func.control_path = SwitchParam::MultifnSwitchParamPath(index);
            sfc_func.set_switch = on;
            _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
        }
        else {
            // Clear all the multi-function keys
            _reset_multifn_switches();        
        }
    }    
}

//----------------------------------------------------------------------------
// _reset_multifn_switches
//----------------------------------------------------------------------------
void SequencerManager::_reset_multifn_switches(bool force)
{
    // If we are in record mode or force reset
    if ((utils::get_multifn_switches_mode() == MultifnSwitchesMode::SEQ_REC) || force) {
        // Reset the multi-function keys
        auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::RESET_MULTIFN_SWITCHES, NinaModule::SEQUENCER);
        _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
        utils::set_multifn_switches_mode(MultifnSwitchesMode::NONE);
    }
}

//----------------------------------------------------------------------------
// _reset_seq_steps
//----------------------------------------------------------------------------
void SequencerManager::_reset_seq_steps()
{
    // Reset the sequence steps
    for (uint i=0; i<_max_steps; i++) {
        _seq_steps.at(i).num_notes = -1;
        _seq_steps.at(i).step_type = StepType::NORMAL;
    }
}

//----------------------------------------------------------------------------
// _set_seq_step_param
//----------------------------------------------------------------------------
void SequencerManager::_set_seq_step_param(uint step)
{
    // Get the sequence param
    auto param = utils::get_param(Param::ParamPath(this, STEP_PARAM_NAME + std::to_string(step + 1)));
    if (param) {
        std::string step_notes = "00FFFFFFFFFFFFFFFFFFFFFFFF";
        unsigned char attr = 0;
        char str_note[3];

        // If this is normal notes or the start of a tie
        if (_seq_steps[_num_steps].step_type <= StepType::START_TIE) {
            // Set the step attributes
            attr = ((_seq_steps[_num_steps].step_type == StepType::START_TIE) ? TIE_START_STEP : 0);
            std::sprintf(str_note, "%02X", (char)attr);
            step_notes.replace(0, 2, str_note);             

            // Process each note
            for (uint i=0; i<_step_note_index; i++) {
                unsigned char note = _seq_steps[_num_steps].notes[i].data.note.note;
                note += _base_note;
                if (note > MAX_MIDI_NOTE) {
                    note = MAX_MIDI_NOTE;
                }
                std::sprintf(str_note, "%02X", (char)note);
                step_notes.replace((2 + (i*2)), 2, str_note);                
            }
        }
        else {
            // If this is a tie step
            if (_seq_steps[_num_steps].step_type == StepType::TIE) {
                attr = TIE_STEP;
            }
            // If this is an end tie step
            else if (_seq_steps[_num_steps].step_type == StepType::END_TIE) {
                attr = TIE_END_STEP;
            }
            // If this is a rest ntoe
            else if (_seq_steps[_num_steps].step_type == StepType::REST) {
                attr = REST_STEP;
            }

            // Set the step attributes
            std::sprintf(str_note, "%02X", (char)attr);
            step_notes.replace(0, 2, str_note);  
        }
        param->set_str_value(step_notes);
        auto param_change = ParamChange(param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }
}

//----------------------------------------------------------------------------
// _register_params
//----------------------------------------------------------------------------
void SequencerManager::_register_params()
{
    // Register the Record param
    auto param = ButtonParam::CreateParam(this, REC_PARAM_NAME);
    param->param_id = SequencerParamId::REC_PARAM_ID;
    param->name = REC_NAME;
    param->set_value((float)_program);
    param->patch_param = false;
    utils::register_param(std::move(param));

    // Register the Run param
    param = ButtonParam::CreateParam(this, RUN_PARAM_NAME);
    param->param_id = SequencerParamId::RUN_PARAM_ID;
    param->name = RUN_NAME;
    param->set_value((float)_enable);
    param->patch_param = false;
    utils::register_param(std::move(param));

    // Register the Number of Steps param
    auto param2 = Param::CreateParam(this, NUM_STEPS_PARAM_NAME);
    param2->param_id = SequencerParamId::NUM_STEPS_PARAM_ID;
    param2->name = NUM_STEPS_NAME;
    param2->set_value(1.0 - (1/_max_steps));
    utils::register_param(std::move(param2));

	// Register the Tempo Note Value param
	auto param3 = TempoNoteValueParam::CreateParam(this);
	param3->param_id = SequencerParamId::TEMPO_NOTE_VALUE_PARAM_ID;
	param3->name = TEMPO_NOTE_VALUE_NAME;
	param3->set_tempo_note_value(TempoNoteValue::QUARTER);
	utils::register_param(std::move(param3));

	// Register the Hold param
	param = ButtonParam::CreateParam(this, HOLD_PARAM_NAME);
	param->param_id = SequencerParamId::HOLD_PARAM_ID;
	param->name = HOLD_NAME;
	param->set_value((float)true);
	utils::register_param(std::move(param));

    // Register the step notes param
    for (uint i=0; i<_max_steps; i++) {
        auto param = Param::CreateParam(this, STEP_PARAM_NAME + std::to_string(i + 1));
        param->param_id = SequencerParamId::STEP_1_ID + i;
        param->name = STEP_NAME + std::to_string(i + 1);
        param->str_param = true;
        param->set_str_value("00FFFFFFFFFFFFFFFFFFFFFFFF");
        param->patch_state_param = false;
        param->layer_1_param = true;
        utils::register_param(std::move(param));
    }  
}
