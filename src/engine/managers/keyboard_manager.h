/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  keyboard_manager.h
 * @brief Keyboard Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _KEYBOARD_MANAGER_H
#define _KEYBOARD_MANAGER_H

#include <atomic>
#include <random>
#include <algorithm>
#include "base_manager.h"
#include "midi_device_manager.h"
#include "event.h"
#include "event_router.h"
#include "param.h"
#include "timer.h"

// KBD Midi Channel
enum KdbMidiChannel : int {
    CURRENT_LAYER_CH,
    LAYER_1_CH,
    CH_1,
    CH_2,
    CH_3,
    CH_4,
    CH_5,
    CH_6,
    CH_7,
    CH_8,
    CH_9,
    CH_10,
    CH_11,
    CH_12,
    CH_13,
    CH_14,
    CH_15,
    CH_16,
    NUM_KBD_MIDI_CHANNELS
};

// Keyboard Param IDs
enum KeyboardParamId : int
{
    ENABLE_PARAM_ID,
    MIDI_CHANNEL_PARAM_ID,
    FIRST_NOTE_PARAM_ID   
};

// Keyboard Manager class
class KeyboardManager: public BaseManager
{
public:
    // Constructor
    KeyboardManager(EventRouter *event_router);

    // Destructor
    ~KeyboardManager();

    // Public functions
    bool start();
    void process();
    void process_event(const BaseEvent *event);

private:
    // Private variables
    EventListener *_sfc_listener;
    EventListener *_gui_listener;
    std::mutex _kbd_mutex;
    std::atomic<bool> _enable;
    unsigned char _base_note;
    std::vector<snd_seq_event_t> _played_notes;
    KdbMidiChannel _kdb_midi_channel;
    uint _midi_channel;

    // Private functions
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_system_func_event(const SystemFunc &data);
    void _process_param_value(const Param &param, float value);
    void _show_keyboard();
    void _set_multifn_switch(uint index, bool on);
    void _register_params();
};


#endif  // _KEYBOARD_MANAGER_H
