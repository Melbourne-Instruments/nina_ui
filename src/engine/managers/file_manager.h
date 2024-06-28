/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  file_manager.h
 * @brief File Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _FILE_MANAGER_H
#define _FILE_MANAGER_H

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/document.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/filewritestream.h"
#include "base_manager.h"
#include "daw_manager.h"
#include "event.h"
#include "event_router.h"
#include "param.h"
#include "system_func.h"
#include "timer.h"

// File Manager class
class FileManager : public BaseManager
{
public:
    // Constructor
    FileManager(EventRouter *event_router);

    // Destructor
    ~FileManager();

    // Functions
    bool start();
    void stop();
    void process();
    void process_event(const BaseEvent *event);

private:
    DawManager *_daw_manager;
    EventListener *_arp_listener;
    EventListener *_daw_listener;
    EventListener *_seq_listener;
    EventListener *_sfc_param_change_listener;
    EventListener *_sfc_system_func_listener;
    EventListener *_midi_param_change_listener;
    EventListener *_midi_system_func_listener;
    EventListener *_osc_param_change_listener;
    EventListener *_osc_system_func_listener;
    EventListener *_gui_param_change_listener;
    EventListener *_gui_system_func_listener;
    rapidjson::Document _config_json_data;
    rapidjson::Document _layers_json_data;
    rapidjson::Document _layer_patch_json_doc[NUM_LAYERS];
    rapidjson::Document *_patch_json_doc;
#ifdef INCLUDE_PATCH_HISTORY    
    rapidjson::Document _patch_history_json_data;
#endif
    rapidjson::Document _param_map_json_data;
    rapidjson::Document _global_params_json_data;
    rapidjson::Document _init_patch_json_data;
    std::mutex _patch_mutex;
#ifdef INCLUDE_PATCH_HISTORY     
    Timer *_save_patch_history_timer;
#endif
    Timer *_save_config_file_timer;
    Timer *_save_global_params_file_timer;
    Timer *_save_layers_file_timer;
#ifdef INCLUDE_PATCH_HISTORY    
    std::vector<ParamChangeHistory> _param_change_history;
#endif
    Param *_morph_value_param;
    KnobParam *_morph_knob_param;

    void _process_param_changed_event(const ParamChange &param_change);
    void _process_system_func_event(const SystemFunc &system_func);
    void _params_changed_timeout_with_lock();
    void _params_changed_timeout();
    void _update_state_params();
    std::string _get_layers_filename(uint layers_num);
    std::string _get_patch_filename(PatchId id, bool full_path=true);
#ifdef INCLUDE_PATCH_HISTORY     
    std::string _get_patch_history_filename();
#endif
    void _open_and_parse_param_blacklist_file();
    bool _open_config_file();
    void _open_and_parse_system_colours_file();
    void _open_and_parse_param_aliases_file();
    bool _open_and_parse_param_attributes_file();
    bool _open_and_parse_param_lists_file();
    bool _open_and_parse_global_params_file();
    bool _open_param_map_file();
    bool _open_and_parse_layers_file(uint layers_num);
    bool _open_and_parse_layers_file(std::string file_path);
    bool _open_patch_file(PatchId id);
    bool _open_patch_file(PatchId id, rapidjson::Document &json_doc);
    bool _open_patch_file(std::string file_path, rapidjson::Document &json_doc);
#ifdef INCLUDE_PATCH_HISTORY     
    bool _open_patch_history_file();
#endif
    bool _open_and_parse_haptic_modes_file();
    bool _open_json_file(std::string file_path, const char *schema, rapidjson::Document &json_data, bool create=true, std::string def_contents="[]");
    void _parse_config();
    void _parse_param_map();
    void _parse_patch(uint layer_num, bool set_layer_params=true);
    void _parse_patch_layer_params(std::vector<Param *> &params);
    void _parse_patch_common_params(uint layer_num, bool current_layer, std::vector<Param *> &params);
    void _parse_patch_state_params(bool current_layer, std::vector<Param *> &params, PatchState state);
    void _process_patch_mapped_params(const Param *param, const Param *skip_param);
    void _set_patch_state_b_params(rapidjson::Document &from_patch_json_doc);
    void _save_config_file();
    void _save_current_layers_file();
    void _save_layers_file(uint layers_num);
    void _save_layers_file(std::string file_path);
    void _save_global_params_file();
    void _save_patch_file(PatchId id);
    void _save_patch_file();
#ifdef INCLUDE_PATCH_HISTORY 
    void _save_patch_history_file();
#endif
    void _save_json_file(std::string file_path, const rapidjson::Document &json_data);
    rapidjson::Value::ValueIterator _get_current_layer_json_data();
    rapidjson::Value::ValueIterator _get_layer_json_data(uint layer_num);
    rapidjson::Value::ValueIterator _find_patch_param(uint layer_num, std::string path, bool layer_1_param);
    rapidjson::Value::ValueIterator _find_patch_param(std::string path, bool layer_1_param);
    rapidjson::Value::ValueIterator _find_global_param(std::string path);
    rapidjson::Value::ValueIterator _find_init_patch_param(std::string path, PatchState state);
    rapidjson::Value& _get_patch_common_layer_json_data();
    rapidjson::Value& _get_patch_layer_json_data();
    rapidjson::Value& _get_layer_patch_common_json_data(uint layer_num);
    rapidjson::Value& _get_patch_common_json_data();
    rapidjson::Value& _get_layer_patch_state_a_json_data(uint layer_num);
    rapidjson::Value& _get_patch_state_a_json_data();
    rapidjson::Value& _get_layer_patch_state_b_json_data(uint layer_num);
    rapidjson::Value& _get_patch_state_b_json_data();
    void _start_save_config_file_timer();
    void _start_save_layers_file_timer();
    void _start_save_global_params_file_timer();
    void _calc_and_set_layer_voices();
    void _calc_and_set_layer_mpe_config();
    rapidjson::Value::ValueIterator _get_layer_num_voices_obj(uint layer_num);
    rapidjson::Value::ValueIterator _get_layer_mpe_mode_obj(uint layer_num);
    rapidjson::Value::ValueIterator _get_layer_mpe_lower_zone_num_channels_obj();
    rapidjson::Value::ValueIterator _get_layer_mpe_upper_zone_num_channels_obj();
    rapidjson::Value::ValueIterator _get_common_layer_obj(const char *path);
    rapidjson::Value::ValueIterator _get_layer_obj(uint layer_num, const char *path);
    void _set_loading_layers_with_daw(bool loading);
    void _set_loading_patch_with_daw(uint layer_num, bool loading);
    void _select_current_layer(uint layer_num);
    void _set_current_layer_filename_tag(uint layer_num);
    FxType _get_fx_type();
#if defined CHECK_LAYERS_LOAD
    void _check_layers_load();
#endif
};

#endif // _FILE_MANAGER_H
