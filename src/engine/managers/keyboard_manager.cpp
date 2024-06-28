/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  keyboard_manager.h
 * @brief Keyboard Manager class implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include "keyboard_manager.h"
#include "utils.h"

// Constants
constexpr char ENABLE_NAME[]             = "Keyboard Enable";
constexpr char ENABLE_PARAM_NAME[]       = "enable";
constexpr char MIDI_CHANNEL_NAME[]       = "MIDI Channel";
constexpr char MIDI_CHANNEL_PARAM_NAME[] = "midi_channel";
constexpr char FIRST_NOTE_NAME[]         = "First Note";
constexpr char FIRST_NOTE_PARAM_NAME[]   = "first_note";
constexpr uint DEFAULT_FIRST_NOTE        = 60;
constexpr uint NOTES_IN_OCTAVE           = 12;
constexpr bool OCTAVE_ACC_NOTES[]        = {false, true, false, true, false, false, true, false, true, false, true, false};

//----------------------------------------------------------------------------
// KeyboardManager
//----------------------------------------------------------------------------
KeyboardManager::KeyboardManager(EventRouter *event_router) : 
    BaseManager(NinaModule::KEYBOARD, "KeyboardManager", event_router)
{
    // Initialise class data
    _sfc_listener = 0;
    _gui_listener = 0;
    _enable = false;
    _base_note = DEFAULT_FIRST_NOTE;
    _kdb_midi_channel = KdbMidiChannel::CURRENT_LAYER_CH;
    _midi_channel = 0;

    // Register the Arpeggiator params
    _register_params();  
}

//----------------------------------------------------------------------------
// ~KeyboardManager
//----------------------------------------------------------------------------
KeyboardManager::~KeyboardManager()
{
    // Clean up the event listeners
    if (_sfc_listener)
        delete _sfc_listener;
    if (_gui_listener)
        delete _gui_listener;
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool KeyboardManager::start()
{
    // Now get all Arpeggiator params
    auto params = utils::get_params(NinaModule::KEYBOARD);

    // Parse the Keyboard params
    for (const Param *p : params)
    {
        // Process the param value
        _process_param_value(*p, p->get_value());					
    }

    // Call the base manager
    return BaseManager::start();	
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void KeyboardManager::process()
{
    // Create and add the various event listeners
    _sfc_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_sfc_listener);       
    _gui_listener = new EventListener(NinaModule::GUI, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_gui_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void KeyboardManager::process_event(const BaseEvent *event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::SYSTEM_FUNC:
        {
            // Process the System Function event
            _process_system_func_event(static_cast<const SystemFuncEvent *>(event)->system_func());        
            break;            
        }

        case EventType::PARAM_CHANGED:
        {
            // Process the Param Changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;
        } 	

        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void KeyboardManager::_process_param_changed_event(const ParamChange &data)
{
    // Get the param and make sure it exists
    const Param *param = utils::get_param(data.path.c_str());
    if (param)
    {
        // Process the param value
        _process_param_value(*param, data.value);	
    }
}

//----------------------------------------------------------------------------
// _process_system_func_event
//----------------------------------------------------------------------------
void KeyboardManager::_process_system_func_event(const SystemFunc &system_func)
{
    // Get the keyboard mutex
    std::lock_guard<std::mutex> guard(_kbd_mutex);

    // If the keyboard is enabled
    if (_enable) {
        // Parse the system function
        switch (system_func.type)
        {
            case SystemFuncType::MULTIFN_SWITCH:
            {
                // Make sure the note is within range
                if ((_base_note + system_func.num) < 128) {
                    // Create the note on/off event
                    snd_seq_event_t note;
                    snd_seq_ev_clear(&note);
                    if (system_func.value) {
                        // Create the note ON
                        snd_seq_ev_set_noteon(&note, _midi_channel, (_base_note + system_func.num), 127);

                        // Push it to the vector of played notes
                        _played_notes.push_back(note);
                    }
                    else {
                        // Create the note OFF
                        snd_seq_ev_set_noteoff(&note, _midi_channel, (_base_note + system_func.num), 0);

                        // Remove it from the vector of played notes
                        for (auto it = _played_notes.begin(); it != _played_notes.end(); it++)
                        {
                            // Erase the note if found
                            if (it->data.note.note == note.data.note.note)
                            {
                                _played_notes.erase(it);
                                break;
                            }
                        }                
                    }

                    // Send the MIDI event
                    utils::get_manager(NinaModule::SEQUENCER)->process_midi_event_direct(&note);
                    //if (note.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON)
                    //    DEBUG_BASEMGR_MSG("Send note: " << (int)note.data.note.note << ": ON");
                    //else
                    //    DEBUG_BASEMGR_MSG("Send note: " << (int)note.data.note.note << ": OFF");            
                    break;
                }
            }

            default:
                break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_param_value
//----------------------------------------------------------------------------
void KeyboardManager::_process_param_value(const Param &param, float value)
{
    // Is this a keyboard parameter?
    if (param.module == module()) {
        // Process the keyboard param value based on the param ID
        switch (param.param_id)
        {
        case KeyboardParamId::ENABLE_PARAM_ID:
            {
                // Get the keyboard mutex
                std::lock_guard<std::mutex> guard(_kbd_mutex);

                // Update the enable value
                _enable = (value == 0) ? false : true;

                // Show the keyboard (if enabled)
                if (_enable) {
                    _show_keyboard();
                }

                // If we are disabling, make sure any played notes are sent out
                // as note offs
                if (!_enable) {
                    for (auto it = _played_notes.begin(); it != _played_notes.end(); it++)
                    {
                        // Send the note OFF
                        it->type = snd_seq_event_type::SND_SEQ_EVENT_NOTEOFF;
                        it->data.note.velocity = 0;
                        utils::get_manager(NinaModule::SEQUENCER)->process_midi_event_direct(&*it);
                    }
                    _played_notes.clear();
                }
            }
            break;

        case KeyboardParamId::MIDI_CHANNEL_PARAM_ID:
            {
                // Get the keyboard mutex
                std::lock_guard<std::mutex> guard(_kbd_mutex);

                // Convert to the KBD MIDI Channel
                int val = (uint)floor(value * KdbMidiChannel::NUM_KBD_MIDI_CHANNELS);
                if (val > KdbMidiChannel::CH_16) {
                    val = KdbMidiChannel::CH_16;
                }

                // Set the new KBD MIDI Channel
                _kdb_midi_channel = KdbMidiChannel(val);

                // Parse the KBD MIDI channel to set the actual channel
                uint channel = 0;
                switch (_kdb_midi_channel) {
                    case KdbMidiChannel::CURRENT_LAYER_CH: {
                        // Set the MIDI channel to the current layer channel
                        channel = utils::get_current_layer_info().get_midi_channel_filter();
                        if (channel) {
                            channel--;
                        }                        
                        break;
                    }

                    case KdbMidiChannel::LAYER_1_CH: {
                        // Set the MIDI channel to the layer 1 channel
                        channel = utils::get_layer_info(0).get_midi_channel_filter();
                        if (channel) {
                            channel--;
                        }
                        break;
                    }

                    default: {
                        // Set the MIDI channel to the specified channel
                        channel = _kdb_midi_channel - KdbMidiChannel::CH_1;
                        break;                    
                    }
                }

                // If the channel is actually changing
                if (_midi_channel != channel) {
                    // Send a note off for all held notes in the current channel
                    for (auto it = _played_notes.begin(); it != _played_notes.end(); it++)
                    {
                        snd_seq_event note;
                        snd_seq_ev_set_noteoff(&note, _midi_channel, (it->data.note.note), 0);
                        utils::get_manager(NinaModule::SEQUENCER)->process_midi_event_direct(&note);
                    }
                    _played_notes.clear();

                    // Set the new channel
                    _midi_channel = channel;
                }
            }
            break;

        case KeyboardParamId::FIRST_NOTE_PARAM_ID:
            {
                // Get the keyboard mutex
                std::lock_guard<std::mutex> guard(_kbd_mutex);
                
                // Send a note off for all held notes
                for (auto it = _played_notes.begin(); it != _played_notes.end(); it++)
                {
                    snd_seq_event note;
                    snd_seq_ev_set_noteoff(&note, _midi_channel, (it->data.note.note), 0);
                    utils::get_manager(NinaModule::SEQUENCER)->process_midi_event_direct(&note);
                }
                _played_notes.clear();

                // Update the first note param
                _base_note = value * 128;

                // Update the keyboard (if enabled)
                if (_enable) {
                    _show_keyboard();
                }
            }
            break;

        default:
            break;
        }
    }
    // Is this a current layer change?
    else if (param.get_path() == utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER)->get_path()) {
        // Get the keyboard mutex
        std::lock_guard<std::mutex> guard(_kbd_mutex);
      
        // If we should set the KBD MIDI Channel for the current Layer
        if (_kdb_midi_channel == KdbMidiChannel::CURRENT_LAYER_CH) {
            uint layer = (uint)param.get_position_value();
            auto layer_channel = utils::get_layer_info(layer).get_midi_channel_filter();
            if (layer_channel) {
                layer_channel--;
            }

            // If the channel is changing
            if (layer_channel != _midi_channel) {
                // Send a note off for all held notes in the current channel
                for (auto it = _played_notes.begin(); it != _played_notes.end(); it++)
                {
                    snd_seq_event note;
                    snd_seq_ev_set_noteoff(&note, _midi_channel, (it->data.note.note), 0);
                    utils::get_manager(NinaModule::SEQUENCER)->process_midi_event_direct(&note);
                }
                _played_notes.clear();

                // Set the new channel
                _midi_channel = layer_channel;             
            }
        }
    }
    // Is this a MIDI channel change?
    else if (param.get_path() == utils::get_param_from_ref(utils::ParamRef::MIDI_CHANNEL_FILTER)->get_path()) {
        // Get the keyboard mutex
        std::lock_guard<std::mutex> guard(_kbd_mutex);

        // If we should set the KBD MIDI Channel for the current Layer or Layer 1?
        auto layer_num = utils::get_current_layer_info().layer_num();
        if ((_kdb_midi_channel == KdbMidiChannel::CURRENT_LAYER_CH) || 
            ((_kdb_midi_channel == KdbMidiChannel::LAYER_1_CH) && (layer_num == 0))) {
            uint layer_channel = (uint)param.get_position_value();
            if (layer_channel) {
                layer_channel--;
            }

            // If the channel is changing
            if (layer_channel != _midi_channel) {
                // Send a note off for all held notes in the current channel
                for (auto it = _played_notes.begin(); it != _played_notes.end(); it++)
                {
                    snd_seq_event note;
                    snd_seq_ev_set_noteoff(&note, _midi_channel, (it->data.note.note), 0);
                    utils::get_manager(NinaModule::SEQUENCER)->process_midi_event_direct(&note);
                }
                _played_notes.clear();

                // Set the new channel
                _midi_channel = layer_channel;       
            }
        }          
    }
}

//----------------------------------------------------------------------------
// _show_keyboard
//----------------------------------------------------------------------------
void KeyboardManager::_show_keyboard()
{
    // Calculate the offset into the octave from the base note
    auto offset = _base_note % NOTES_IN_OCTAVE;

    // Process all notes from the base note for each multi-function switch
    for (uint i=0; i<NUM_MULTIFN_SWITCHES; i++) {
        // Wrap the offset around if needed
        if (offset >= NOTES_IN_OCTAVE) {
            offset = 0;
        }

        // Only process notes within range, otherwise clear the switch LED
        if ((_base_note + i) < 128) {
            _set_multifn_switch(i, OCTAVE_ACC_NOTES[offset]);
        }
        else {
            _set_multifn_switch(i, false);
        }
        offset++;
    }
}

//----------------------------------------------------------------------------
// _select_multifn_switch
//----------------------------------------------------------------------------
void KeyboardManager::_set_multifn_switch(uint index, bool on)
{
    // If the multi-function switches are in keyboard mode
    if (utils::get_multifn_switches_mode() == MultifnSwitchesMode::KEYBOARD) {
        // If the index is within range
        if (index < NUM_MULTIFN_SWITCHES) {
            // Select the specified multi-function key
            auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::SET_SWITCH_VALUE, NinaModule::KEYBOARD);
            sfc_func.control_path = SwitchParam::MultifnSwitchParamPath(index);
            sfc_func.set_switch = on;
            _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
        }
    }    
}

//----------------------------------------------------------------------------
// _register_params
//----------------------------------------------------------------------------
void KeyboardManager::_register_params()
{
    // Register the Enable param
    auto param = ButtonParam::CreateParam(this, ENABLE_PARAM_NAME);
    param->param_id = KeyboardParamId::ENABLE_PARAM_ID;
    param->name = ENABLE_NAME;
    param->set_value((float)_enable);
    param->patch_param = false;
    utils::register_param(std::move(param));

    // Register the MIDI channel param
    auto param2 = Param::CreateParam(this, MIDI_CHANNEL_PARAM_NAME);
    param2->param_id = KeyboardParamId::MIDI_CHANNEL_PARAM_ID;
    param2->name = MIDI_CHANNEL_NAME;
    param2->set_value((float)CURRENT_LAYER_CH / NUM_KBD_MIDI_CHANNELS);
    utils::register_param(std::move(param2));

    // Register the First Note param
    auto param3 = Param::CreateParam(module(), FIRST_NOTE_PARAM_NAME);
    param3->param_id = KeyboardParamId::FIRST_NOTE_PARAM_ID;
    param3->name = FIRST_NOTE_NAME;
    param3->set_value((float)DEFAULT_FIRST_NOTE / 128);
    param3->patch_param = false;
    utils::register_param(std::move(param3));     
}
