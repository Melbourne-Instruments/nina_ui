/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  midi_device_manager.h
 * @brief MIDI Device Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _MIDI_DEVICE_MANAGER_H
#define _MIDI_DEVICE_MANAGER_H

#include <thread>
#include <algorithm>
#include <vector>
#include <mutex>
#include <tuple>
#include "base_manager.h"
#include "event_router.h"
#include "event.h"
#include "timer.h"

// Constants
constexpr uint NUM_FILTER_SAMPLES = 5;

// MIDI Device Manager class
class MidiDeviceManager: public BaseManager
{
public:
    // Helper functions
    static bool IsMidiCcParamPath(std::string param_name);
    static bool IsMidiPitchBendParamPath(std::string param_name);
    static bool IsMidiChanpressParamPath(std::string param_name);

    // Constructor
    MidiDeviceManager(EventRouter *event_router);

    // Destructor
    ~MidiDeviceManager();

    // Public functions
    bool start();
    void stop();
    void process();
    void process_event(const BaseEvent *event);
    void process_midi_devices();
    void process_midi_event();
    void process_midi_event_queue();

private:
    // Private variables
    EventListener *_sfc_listener;
    EventListener *_osc_listener;
    EventListener *_fm_listener;
    snd_seq_t *_seq_handle;
    int _seq_client;
    int _seq_port;
    snd_midi_event_t *_serial_snd_midi_event;
    int _serial_midi_port_handle;
    std::thread *_midi_devices_thread;
    bool _run_midi_devices_thread;
    std::thread *_midi_event_thread;
    bool _run_midi_event_thread;
    std::thread *_midi_event_queue_thread;
    bool _run_midi_event_queue_thread;
    int _bank_select_index[NUM_LAYERS];
    Timer *_tempo_timer;
    uint _midi_clock_count;
    std::chrono::system_clock::time_point _midi_clock_start;
    float _tempo_filter_state;
    uint _mesurement_num;
    std::array<uint,NUM_FILTER_SAMPLES> _midi_clock_mesurements;
    Param *_tempo_param;
    Param *_midi_clk_in_param;
    Param *_midi_echo_filter_param;
    Param *_mpe_x_param;
    Param *_mpe_y_param;
    Param *_mpe_z_param;
    Param *_pitch_bend_param;
    Param *_chanpress_param;
    std::vector<std::tuple<snd_seq_event_t, uint64_t>> _cc_prev_messages;
    std::chrono::_V2::system_clock::time_point _start_time;
    std::mutex _midi_event_queue_mutex;
    std::vector<snd_seq_event_t> _midi_event_queue_a;
    std::vector<snd_seq_event_t> _midi_event_queue_b;
    std::vector<snd_seq_event_t> *_push_midi_event_queue;
    std::vector<snd_seq_event_t> *_pop_midi_event_queue;
    
    // Private functions
    void _process_reload_presets();
    void _process_param_changed_event(const ParamChange &param_change);
    void _process_midi_param_changed(Param *param);
    void _process_high_priority_midi_event(const snd_seq_event_t *seq_event);
    void _process_normal_midi_event(const snd_seq_event_t *seq_event);
    void _process_param_changed_mapped_params(uint layers_mask, const Param *param, float value, const Param *skip_param, bool displayed);
    void _start_stop_seq_run(bool start);
    void _open_seq_midi();
    void _close_seq_midi();
    void _open_serial_midi();
    void _close_serial_midi();
    void _tempo_timer_callback();
    bool _is_high_priority_midi_event(snd_seq_event_type_t type);
    uint _get_layers_mask(unsigned char channel);
    inline MidiEchoFilter _get_midi_echo_filter();
};

#endif  // _MIDI_DEVICE_MANAGER_H
