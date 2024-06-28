/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  daw_manager.h
 * @brief DAW Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _DAW_MANAGER_H
#define _DAW_MANAGER_H

#include "base_manager.h"
#include "midi_device_manager.h"
#include "event.h"
#include "param.h"
#include "event_router.h"
#include "sushi_client.h"

// Sushi version
struct SushiVersion
{
    std::string version;
    std::string commit_hash;
};

// DAW Manager class
class DawManager: public BaseManager
{
public:
    // Constructor
    DawManager(EventRouter *event_router);

    // Destructor
    ~DawManager();

    // Public functions
    void process();
    void process_event(const BaseEvent *event);
    void process_midi_event_direct(const snd_seq_event_t *event);
    bool get_patch_state_params();
    void set_patch_layer_params(uint layer_num);
    void set_patch_params(uint layer_num, bool include_cmn_params, PatchState state);
    void set_param(uint layer_num, const Param *param);
    void set_param(uint layer_num, const Param *param, float value);
    void set_param_direct(uint layer_mask, const Param *param);
    void set_param_all_layers(const Param *param);
    SushiVersion get_sushi_version();
#if defined CHECK_LAYERS_LOAD
    std::vector<std::pair<Param *,float>> get_param_values(bool state_only);
#endif

private:
    // Private variables
    EventListener *_sfc_param_changed_listener;
    EventListener *_mid_listener;
    EventListener *_fm_param_changed_listener;
    EventListener *_osc_listener;
    EventListener *_gui_listener;
    std::shared_ptr<sushi_controller::SushiController> _sushi_controller;
    int _main_track_id;
    SushiVersion _sushi_verson;

    // Private functions
    void _process_midi_event(const snd_seq_event_t &data);
    void _process_param_changed_event(const ParamChange &data);
    void _param_update_notification(int processor_id, int parameter_id, float value);
    inline int _encode_param_id(int parameter_id, uint layers_mask);
    void _register_params();
    std::unique_ptr<Param> _cast_sushi_param(int processor_id, const sushi_controller::ParameterInfo &sushi_param, std::string path_prefix);
};

#endif  // _DAW_MANAGER_H
