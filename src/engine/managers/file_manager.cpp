/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  file_manager.cpp
 * @brief File Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <dirent.h>
#include <sys/stat.h>
#include <regex>
#include "file_manager.h"
#include "midi_device_manager.h"
#include "arpeggiator_manager.h"
#include "utils.h"
#include "logger.h"

// Preset versions - update these whenever the presets change
constexpr char LAYERS_PRESET_VERSION[] = "0.2.0";
constexpr char PATCH_PRESET_VERSION[]  = "0.3.5";

// General modeule constants
constexpr char MANAGER_NAME[]                           = "FileManager";
constexpr char CONFIG_FILE[]                            = "config.json";
constexpr char SYSTEM_COLOURS_FILE[]                    = "system_colours.json";
constexpr char PATCH_FILE_EXT[]                         = ".json";
constexpr char PARAM_ATTRIBUTES_FILE[]                  = "param_attributes.json";
constexpr char PARAM_LISTS_FILE[]                       = "param_lists.json";
constexpr char PARAM_MAP_FILE[]                         = "param_map.json";
constexpr char PARAM_BLACKLIST_FILE[]                   = "param_blacklist.json";
constexpr char PARAM_ALIASES_FILE[]                     = "param_aliases.json";
constexpr char HAPTIC_MODES_FILE[]                      = "haptic_modes.json";
constexpr char CURRENT_LAYERS_FILE[]                    = "current_layers.json";
constexpr char DEFAULT_LAYERS_FILE[]                    = "ONE_LAYER.json";
constexpr char DEFAULT_PATCH_FILE[]                     = "BASIC_PATCH.json";
constexpr char INIT_PATCH_FILE[]                        = "INIT_PATCH.json";
#ifdef INCLUDE_PATCH_HISTORY 
constexpr char PATCH_HISTORY_FILE[]                     = "patch_history.json";
#endif
constexpr char GLOBAL_PARAMS_FILE[]                     = "global_params.json";
#ifdef INCLUDE_PATCH_HISTORY 
constexpr uint SAVE_PATCH_HISTORY_IDLE_INTERVAL_US      = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(500)).count();
#endif
constexpr uint SAVE_CONFIG_FILE_IDLE_INTERVAL_US        = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(2)).count();
constexpr uint SAVE_LAYERS_FILE_IDLE_INTERVAL_US        = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(2)).count();
constexpr uint SAVE_GLOBAL_PARAMS_FILE_IDLE_INTERVAL_US = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(2)).count();
#ifdef INCLUDE_PATCH_HISTORY 
constexpr uint PATCH_HISTORY_MAX_EVENTS                 = 1000;
#endif
constexpr char STATE_PARAM_PATH_PREFIX[]                = "/state/";
constexpr char MULTIFN_SWITCH_PATH[]                    = "sys/multifn_switch";
constexpr uint DEFAULT_PATCH_MODIFIED_THRESHOLD         = std::chrono::seconds(120).count();
constexpr uint DEFAULT_DEMO_MODE_TIMEOUT                = std::chrono::seconds(300).count();
constexpr char DEFAULT_SYSTEM_COLOUR[]                  = "FF0000";

//----------------------------------------------------------------------------
// FileManager
//----------------------------------------------------------------------------
FileManager::FileManager(EventRouter *event_router) : 
    BaseManager(NinaModule::FILE_MANAGER, MANAGER_NAME, event_router)
{
    // Initialise class data
    _daw_manager = 0;
    _arp_listener = 0;
    _daw_listener = 0;
    _seq_listener = 0;
    _sfc_param_change_listener = 0;
    _sfc_system_func_listener = 0;
    _midi_param_change_listener = 0;
    _midi_system_func_listener = 0;
    _osc_param_change_listener = 0;
    _osc_system_func_listener = 0;
    _gui_param_change_listener = 0;
    _gui_system_func_listener = 0;
#ifdef INCLUDE_PATCH_HISTORY 
    _save_patch_history_timer = new Timer(TimerType::ONE_SHOT);
#endif
    _save_config_file_timer = new Timer(TimerType::ONE_SHOT);
    _save_global_params_file_timer = new Timer(TimerType::ONE_SHOT);
    _save_layers_file_timer = new Timer(TimerType::ONE_SHOT);
    _morph_value_param = 0;
    _morph_knob_param = 0;
    _patch_json_doc = nullptr;

    // Open the param blacklist file and parse it
    _open_and_parse_param_blacklist_file();
}

//----------------------------------------------------------------------------
// ~FileManager
//----------------------------------------------------------------------------
FileManager::~FileManager()
{
    // Delete the listeners
    if (_arp_listener)
        delete _arp_listener;
    if (_daw_listener)
        delete _daw_listener;
    if (_seq_listener)
        delete _seq_listener;
    if (_sfc_param_change_listener)
        delete _sfc_param_change_listener;
    if (_sfc_system_func_listener)
        delete _sfc_system_func_listener;
    if (_midi_param_change_listener)
        delete _midi_param_change_listener;        
    if (_midi_system_func_listener)
        delete _midi_system_func_listener;        
    if (_osc_param_change_listener)
        delete _osc_param_change_listener;
    if (_osc_system_func_listener)
        delete _osc_system_func_listener;
    if (_gui_param_change_listener)
        delete _gui_param_change_listener;
    if (_gui_system_func_listener)
        delete _gui_system_func_listener;

    // Delete any specified timers
#ifdef INCLUDE_PATCH_HISTORY     
    if (_save_patch_history_timer)
        delete _save_patch_history_timer;
#endif
    if (_save_config_file_timer)
        delete _save_config_file_timer;
    if (_save_global_params_file_timer)
        delete _save_global_params_file_timer;
    if (_save_layers_file_timer)
        delete _save_layers_file_timer;               
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool FileManager::start()
{
    // Catch all exceptions
    try
    {
        // Get the DAW manager
        _daw_manager = static_cast<DawManager *>(utils::get_manager(NinaModule::DAW));
    
        // Open the config file and parse it
        if (!_open_config_file())
        {
            // This is a critical error
            MSG("An error occurred opening the config file: " << NINA_UDATA_FILE_PATH(CONFIG_FILE));
            NINA_LOG_CRITICAL(module(), "An error occurred opening the config file: {}", NINA_UDATA_FILE_PATH(CONFIG_FILE));
            return false;
        }
        _parse_config();

        // Open the psystem colours file and parse it
        _open_and_parse_system_colours_file();        

        // Open the param aliases file and parse it
        _open_and_parse_param_aliases_file();

        // Open the param map file and parse it
        if (!_open_param_map_file())
        {
            // This is a critical error
            MSG("An error occurred opening the param map file: " << NINA_ROOT_FILE_PATH(PARAM_MAP_FILE));
            NINA_LOG_CRITICAL(module(), "An error occurred opening the param map file: {}", NINA_ROOT_FILE_PATH(PARAM_MAP_FILE));
            return false;
        }
        _parse_param_map();

        // Open and parse the param attributes
        if (!_open_and_parse_param_attributes_file())
        {
            // Log the error
            MSG("An error occurred opening param attributes file: " << NINA_ROOT_FILE_PATH(PARAM_ATTRIBUTES_FILE));
            NINA_LOG_ERROR(module(), "An error occurred opening param attributes file: {}", NINA_ROOT_FILE_PATH(PARAM_ATTRIBUTES_FILE));          
        }        

        // Open and parse the param lists
        if (!_open_and_parse_param_lists_file())
        {
            // Log the error
            MSG("An error occurred opening param lists file: " << NINA_ROOT_FILE_PATH(PARAM_LISTS_FILE));
            NINA_LOG_ERROR(module(), "An error occurred opening param lists file: {}", NINA_ROOT_FILE_PATH(PARAM_LISTS_FILE));          
        }

        // Open and parse the global params
        if (!_open_and_parse_global_params_file()) {
            // Log the error
            MSG("An error occurred opening global params file: " << NINA_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE));
            NINA_LOG_ERROR(module(), "An error occurred opening global params file: {}", NINA_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE));              
        }

        // Open and check the init patch file
        if (!_open_patch_file(NINA_ROOT_FILE_PATH(INIT_PATCH_FILE), _init_patch_json_data)) {
            // Log the error
            MSG("An error occurred opening INIT patch file: " << NINA_ROOT_FILE_PATH(INIT_PATCH_FILE));
            NINA_LOG_ERROR(module(), "An error occurred opening INIT patch file: {}", NINA_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE));              
        }        

        // Initialise the utils LFO and MPE handling
        // Needs to be done here after the param atttributes have been processed, but before the patch is parsed
        utils::init_lfo_handling();
        utils::init_mpe_handling();

        // Open the layers file to setup each layer
        // If the current layers file exists, open it - this will be the normal case
        if (std::filesystem::exists(NINA_UDATA_FILE_PATH(CURRENT_LAYERS_FILE))) {
            // Open the current layers file
            if (!_open_and_parse_layers_file(NINA_UDATA_FILE_PATH(CURRENT_LAYERS_FILE))) {
                // This is a critical error - note the function call above logs any errors
                return false;
            }
        }
        else {
            // The current layers file does not exist - open the actual layers file, and save 
            // it as the current layers file
            if (!_open_and_parse_layers_file(utils::system_config()->get_layers_num())) {
                // This is a critical error - note the function call above logs any errors
                return false;
            }
            _save_current_layers_file();
        }
        MSG("Loaded Layers config: " << _get_layers_filename(utils::system_config()->get_layers_num()));

        // Indicate we are now loading Layers with the DAW
        _set_loading_layers_with_daw(true);

        // Reset the MPE Params - this is so they are set to their defaults if the params
        // are missing from the Layers file
        utils::reset_mpe_params();

        // Get the Morph Value param for patch processing
        _morph_value_param = utils::get_param_from_ref(utils::ParamRef::MORPH_VALUE);

        // Now Parse the patch data for each layer
        for (int i=(NUM_LAYERS-1); i>=0; i--) {
            utils::set_current_layer(i);
            _select_current_layer(i);
            utils::get_current_layer_info().set_patch_modified(false);
            utils::get_current_layer_info().set_patch_state(PatchState::STATE_A);           
            _patch_json_doc = &_layer_patch_json_doc[i];
            if (!_open_patch_file(utils::get_layer_info(i).get_patch_id())) {
                // This error is typically due to the bank not existing
                // Set the Patch ID to be BANK 001, PATCH 001 so that the UI will still run
                MSG("The patch with BANK: " << utils::get_layer_info(i).get_patch_id().bank_num << " PATCH: " << utils::get_layer_info(i).get_patch_id().patch_num << " does not exist");
                MSG("Attempting to load BANK: 1 PATCH: 1");
                NINA_LOG_ERROR(module(), "The patch with BANK: {} PATCH: {} does not exist", utils::get_layer_info(i).get_patch_id().bank_num, utils::get_layer_info(i).get_patch_id().patch_num);
                NINA_LOG_ERROR(module(), "Attempting to load BANK: 1 PATCH: 1");              
                utils::get_layer_info(i).set_patch_id(PatchId());
                if (!_open_patch_file(utils::get_layer_info(i).get_patch_id())) {
                    // This is a critical error that we cannot recover from, the error is logged
                    // in the above call
                    return false;
                }
            }
            _set_current_layer_filename_tag(i);
            _parse_patch(i);

            // Save the patch morph position
            _morph_value_param ?
                utils::get_layer_info(i).set_morph_value(_morph_value_param->get_value()) :
                utils::get_layer_info(i).set_morph_value(0.0f);    
            MSG("Loaded Layer " << (i+1) << " patch: " << _get_patch_filename(utils::get_layer_info(i).get_patch_id()));
        }

        // Indicate we have finished loading Layers with the DAW
        _set_loading_layers_with_daw(false);

        // Once all patches are loaded, we need to set the morph position for each
        // layer in the DAW (if non-zero)
        // This will kick off morphing if needed
        for (int i=(NUM_LAYERS-1); i>=0; i--) {
            if (utils::get_layer_info(i).get_morph_value() && _morph_value_param) {
                // Set the morph position in the DAW
                _daw_manager->set_param(i, _morph_value_param, utils::get_layer_info(i).get_morph_value());
            }
        }

#if defined CHECK_LAYERS_LOAD
        _check_layers_load();
#endif

        // Open the haptic modes file and parse it
        if (!_open_and_parse_haptic_modes_file())
        {
            // Log the error
            MSG("An error occurred opening haptic modes file: " << NINA_ROOT_FILE_PATH(HAPTIC_MODES_FILE));
            NINA_LOG_ERROR(module(), "An error occurred opening haptic modes file: {}", NINA_ROOT_FILE_PATH(HAPTIC_MODES_FILE));          
        }

#ifdef INCLUDE_PATCH_HISTORY 
        // Open (and create if needed) the patch history file
        if (!_open_patch_history_file())
        {
            // Delete the file and try again
            ::unlink(NINA_UDATA_FILE_PATH(PATCH_HISTORY_FILE).c_str());
            if (!_open_patch_history_file())
            {            
                // This is a critical error
                MSG("An error occurred opening patch history file: " << NINA_UDATA_FILE_PATH(PATCH_HISTORY_FILE));
                NINA_LOG_CRITICAL(module(), "An error occurred opening patch history file: {}", NINA_UDATA_FILE_PATH(PATCH_HISTORY_FILE));
                return false;
            }            
        }
#endif
        // Get the morph knob param, if any
        _morph_knob_param = utils::get_morph_knob_param();
        utils::set_morph_on(true);
    }
    catch(const std::exception& e)
    {
        // This is a critical error
        MSG("An exception occurred during the startup of the File Manager: " << e.what());
        NINA_LOG_CRITICAL(module(), "An exception occurred during the startup of the File Manager: {}", e.what());
        return false;
    }
    
    // All OK, call the base manager
    return BaseManager::start();
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void FileManager::stop()
{
    // Stop the various timers
#ifdef INCLUDE_PATCH_HISTORY     
    _save_patch_history_timer->stop();
#endif
    _save_config_file_timer->stop();
    _save_global_params_file_timer->stop();
    _save_layers_file_timer->stop();

    // Call the base manager function
    BaseManager::stop();
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void FileManager::process()
{
    // Listen for param change events from the various modules
    _arp_listener = new EventListener(NinaModule::ARPEGGIATOR, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_arp_listener);
    _daw_listener = new EventListener(NinaModule::DAW, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_daw_listener);
    _seq_listener = new EventListener(NinaModule::SEQUENCER, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_seq_listener);
    _sfc_param_change_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_sfc_param_change_listener);
    _sfc_system_func_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_sfc_system_func_listener);
    _midi_param_change_listener = new EventListener(NinaModule::MIDI_DEVICE, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_midi_param_change_listener);    
    _midi_system_func_listener = new EventListener(NinaModule::MIDI_DEVICE, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_midi_system_func_listener);    
    _osc_param_change_listener = new EventListener(NinaModule::OSC, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_osc_param_change_listener);    
    _osc_system_func_listener = new EventListener(NinaModule::OSC, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_osc_system_func_listener);    
    _gui_param_change_listener = new EventListener(NinaModule::GUI, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_gui_param_change_listener); 
    _gui_system_func_listener = new EventListener(NinaModule::GUI, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_gui_system_func_listener);    

    // Call the base manager to start processing events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void FileManager::process_event(const BaseEvent *event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
        {
            // Process the param changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;
        }

        case EventType::SYSTEM_FUNC:
        {
            // Process the system function event
            _process_system_func_event(static_cast<const SystemFuncEvent *>(event)->system_func());
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
void FileManager::_process_param_changed_event(const ParamChange &param_change)
{
    // Get the param and make sure it exists
    const Param *param = utils::get_param(param_change.path.c_str());
    if (param)
    {
        // If this is an alias param, set it to the actual param
        if (param->alias_param) {
            param = static_cast<const ParamAlias *>(param)->get_alias_param();
        }

        // If this is a patch param
        if (param->patch_param) {
            // For each layer that needs to process this param change
            for (uint i=0; i<NUM_LAYERS; i++) {
                // Is this layer specified in the layers mask?
                if ((param_change.layers_mask & LayerInfo::GetLayerMaskBit(i))) {
                    // Get the patch mutex
                    std::lock_guard<std::mutex> guard(_patch_mutex);

                    // Find the param in the layer JSON patch data
                    auto itr = _find_patch_param(i, param->get_path(), param->layer_1_param);

                    // If it wasn't found and this is a mod matrix param
                    if (!itr && param->mod_matrix_param) {
                        // The Mod Matrix param is not specified in the patch file
                        // We need to add it to the patch as a state param
                        rapidjson::Value obj;
                        rapidjson::Value& json_data = (utils::get_layer_info(i).get_patch_state() == PatchState::STATE_A) ? 
                                _get_layer_patch_state_a_json_data(i) :
                                _get_layer_patch_state_b_json_data(i);           
                        obj.SetObject();
                        obj.AddMember("path", param->get_path(), _patch_json_doc->GetAllocator());
                        obj.AddMember("value", param_change.value, _patch_json_doc->GetAllocator());
                        json_data.PushBack(obj, _patch_json_doc->GetAllocator());
                        itr = json_data.End() - 1;
                    }
                    if (itr)
                    {
        #ifdef INCLUDE_PATCH_HISTORY                 
                        // Stop the save patch history timer
                        _save_patch_history_timer->stop();
        #endif

                        // Is this a patch param (not a layer param)?
                        if (!param->patch_layer_param) {
                            // Indicate the patch has been modified
                            utils::get_layer_info(i).set_patch_modified(true);
                        }

                        // Is this a string value param?
                        if (param->str_param) {
                            // Update the parameter value in the patch data
                            itr->GetObject()["str_value"].SetString(param->get_str_value().c_str(), _patch_json_doc->GetAllocator());
                        }
                        else {
        #ifdef INCLUDE_PATCH_HISTORY                     
                            // Get the current param value
                            auto current_value = itr->GetObject()["value"].GetFloat();
        #endif
                            // Update the parameter value in the patch data
                            itr->GetObject()["value"].SetFloat(param_change.value);

        #ifdef INCLUDE_PATCH_HISTORY 
                            // We ned to update the param change history vector with this change
                            // Firstly check if this parameter is already in the vector
                            bool found = false;
                            for (ParamChangeHistory &pch : _param_change_history)
                            {
                                if (pch.path == param->get_path())
                                {
                                    // If there is actually a change in the value
                                    if (param_change.value != pch.value)
                                    {
                                        // Param already exists and the value has changed; update that change history
                                        pch.value = param_change.value;
                                        found = true;
                                    }
                                }
                            }
                            if (!found)
                            {
                                // If there is actually a change in the value
                                if (param_change.value != current_value) {
                                    // Param not found in the history vector, add it
                                    _param_change_history.push_back(ParamChangeHistory(param->get_path(), param_change.value));
                                }
                            }
        #endif
                            // If this is a layer param, restart the save layers config timer to save the file if no param has changed
                            // for a time interval
                            if (param->patch_layer_param) {
                                _start_save_layers_file_timer();
                            }
                            // Is this the morph param?
                            else if (param == _morph_value_param) {
                                // Save the morph value for this layer
                                utils::get_layer_info(i).set_morph_value(param_change.value);
                            }

        #ifdef INCLUDE_PATCH_HISTORY 
                            // If not morphing restart the save patch history timer to save the file if no param has changed
                            // for a time interval
                            if (!utils::is_morph_enabled())
                                _save_patch_history_timer->start(SAVE_PATCH_HISTORY_IDLE_INTERVAL_US, std::bind(&FileManager::_params_changed_timeout_with_lock, this));
        #endif
                        }
                    }
                }
            }
        }
        // If this is a global param
        else if (param->global_param) {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);

            // Find the param in the JSON global params data
            auto itr = _find_global_param(param_change.path);
            if (itr)
            {
                // Is this a string value param?
                if (param->str_param) {
                    // Update the parameter value in the global param data
                    itr->GetObject()["str_value"].SetString( param->get_str_value().c_str(), _patch_json_doc->GetAllocator());
                }
                else {
                    // Update the parameter value in the global param data
                    itr->GetObject()["value"].SetFloat(param_change.value);
                }

                // Restart the save global params timer to save the file if no param has changed
                // for a time interval
                _start_save_global_params_file_timer();
            }                      
        }
    }
}

//----------------------------------------------------------------------------
// _update_state_params
//----------------------------------------------------------------------------
void FileManager::_update_state_params()
{
    // Parse the available DAW params
    auto params = utils::get_params(NinaModule::DAW);
    for (Param *p : params)
    {
        // Skip alias params
        if (p->alias_param)
            continue;
        
        // Is this a state param?
        if (p->patch_state_param) {
            // Find the param in the JSON patch data
            // Note: State params cannot be layer 1 only params
            auto itr = _find_patch_param(p->get_path(), false);

            // If it wasn't found and this is a mod matrix param
            if (!itr && p->mod_matrix_param) {
                // The Mod Matrix param is not specified in the patch file
                // We need to add it to the patch as a state param
                rapidjson::Value obj;
                rapidjson::Value& json_data = (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ? _get_patch_state_a_json_data() :
                                              _get_patch_state_b_json_data();            
                obj.SetObject();
                obj.AddMember("path", p->get_path(), _patch_json_doc->GetAllocator());
                obj.AddMember("value", p->get_value(), _patch_json_doc->GetAllocator());
                json_data.PushBack(obj, _patch_json_doc->GetAllocator());
                itr = json_data.End() - 1;
            }            
            if (itr)
            {           
                // Is this a string value param?
                if (p->str_param) {
                    auto current_value = itr->GetObject()["value"].GetString();

                    // Did the value change? If so update it
                    if (current_value != p->get_str_value()) {
                        itr->GetObject()["str_value"].SetString(p->get_str_value().c_str(), _patch_json_doc->GetAllocator());
                    }
                }
                else {
                    // Get the current param value
                    auto current_value = itr->GetObject()["value"].GetFloat();

                    // Did the value change? If so update it
                    if (current_value != p->get_value()) {
                        itr->GetObject()["value"].SetFloat(p->get_value());
                    }
                }
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_system_func_event
//----------------------------------------------------------------------------
void FileManager::_process_system_func_event(const SystemFunc &system_func)
{
    // Process the event if it is a file manager system function
    switch (system_func.type)
    {
#ifdef INCLUDE_PATCH_HISTORY        
        case SystemFuncType::UNDO_LAST_PARAM_CHANGE:
        {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);

            // Get the last param change action, if any
            if (_patch_history_json_data.Size() > 0) {
                // Get the last param change
                auto obj = (_patch_history_json_data.End() - 1)->GetObject();

                // Is this a patch history object?
                if ((obj.HasMember("path") && obj["path"].IsString()) &&
                    (obj.HasMember("value") && obj["value"].IsFloat()))
                {
                    // Get the param and update it
                    auto path = obj["path"].GetString();
                    auto param = utils::get_param(path);
                    if (param)
                    {
                        // Update the param value
                        param->set_value(obj["value"].GetFloat());

                        // Send the param changed event
                        auto param_change = ParamChange(path, param->get_value(), module());
                        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

                        // Find the param in the JSON patch data
                        auto itr = _find_patch_param(param_change.path);
                        if (itr)
                        {
                            // Update the parameter value in the patch data
                            itr->GetObject()["value"].SetFloat(param->get_value());
                        }
                    }
                }
                // Is this a patch history event?
                else if (obj.HasMember("event") && obj["event"].IsString())
                {
                    // For future use, ignore for now
                }

                // Remove the last history entry and save the history file
                _patch_history_json_data.PopBack();
                _save_patch_history_file();
            }
        }
        break;   
#endif

        case SystemFuncType::TOGGLE_PATCH_STATE:
        {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);
            const uint &layer_num = utils::get_current_layer_info().layer_num();

#ifdef INCLUDE_PATCH_HISTORY 
            // Stop the save patch history timer
            _save_patch_history_timer->stop();
                
            // If there are any pending patch history changes, process them first
            // Note: Lock obtained above so no need to lock again when processing the
            // pending patch history
            _params_changed_timeout();
#endif

            // Indicate we are now loading a patch with the DAW
            _set_loading_patch_with_daw(layer_num, true);

            // Set the Patch State and parse the patches
            // Lock before performing this action so that the Surface Manager controller doesn't clash with this
            // processing
            utils::morph_lock();
            utils::set_morph_on(false);
            utils::reset_morph_state();

            // Set the new patch state
            auto new_state = (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ? PatchState::STATE_B: PatchState::STATE_A;
            utils::get_current_layer_info().set_patch_state(new_state);

            // Parse the state params and set in the DAW
            auto params = utils::get_patch_params();
            _parse_patch_state_params(true, params, new_state);
            _daw_manager->set_patch_params(layer_num, false, new_state);
            MSG("Selected patch State " << ((new_state == PatchState::STATE_A) ? "A" : "B"));              

            // Set the Morph value and knob params for STATE A/B
            // Note: Assumes they are mapped to each other
            float morph_value = (new_state == PatchState::STATE_A) ? 0.0 : 1.0;
            if (_morph_value_param) {
                // Set the Morph params
                _morph_value_param->set_value(morph_value);
                if (_morph_knob_param) {
                    _morph_knob_param->set_value(morph_value);
                }

                // We also save the value in the patch
                auto itr = _find_patch_param(_morph_value_param->get_path(), false);
                if (itr) {
                    itr->GetObject()["value"].SetFloat(morph_value);
                }                 
            }
            utils::get_layer_info(layer_num).set_morph_value(morph_value);

            // Send an event to get the managers to re-load their presets
            _event_router->post_reload_presets_event(new ReloadPresetsEvent(module(), true));

            // Indicate we have finished loading a patch with the DAW
            _set_loading_patch_with_daw(layer_num, false); 

            // Send the updated Morph value to the DAW
            if (_morph_value_param) {
                _daw_manager->set_param(layer_num, _morph_value_param, morph_value);
            }  

            // Unlock the morph
            utils::set_morph_on(true);
            utils::morph_unlock();
            utils::morph_unlock();

            // Indicate we have finished loading a patch with the DAW
            _set_loading_patch_with_daw(layer_num, false);             
            utils::morph_unlock();            

            // Indicate we have finished loading a patch with the DAW
            _set_loading_patch_with_daw(layer_num, false);             
            break;
        }

        case SystemFuncType::LOAD_PATCH:
        {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);

#ifdef INCLUDE_PATCH_HISTORY 
            // If there are any pending patch history changes, process them first
            // Note: Lock obtained above so no need to lock again when processing the
            // pending patch history
            _save_patch_history_timer->stop();
            _params_changed_timeout();
#endif

            // Try to open the patch file - if it doesn't exist, create it
            auto id = system_func.patch_id;
            const uint &layer_num = system_func.layer_num;
            _patch_json_doc = &_layer_patch_json_doc[layer_num];
            if (_open_patch_file(id))
            {
                // Indicate we are now loading a patch with the DAW
                _set_loading_patch_with_daw(layer_num, true);

                // File opened successfully, set the new ID and parse the patches
                utils::morph_lock();
                utils::set_morph_on(false);
                utils::reset_morph_state();

                // Set the new Patch ID and parse it
                utils::get_layer_info(layer_num).set_patch_id(id);
                utils::get_layer_info(layer_num).set_patch_modified(false);
                _parse_patch(layer_num, false);

#ifdef INCLUDE_PATCH_HISTORY 
                // Delete the patch history entries - does the patch history contain entries?
                if (_patch_history_json_data.HasMember("session_history") && _patch_history_json_data["session_history"].IsArray())
                {
                    // Is there a session history?
                    auto session_history = _patch_history_json_data["session_history"].GetArray();            
                    if (session_history.Size() > 0)
                    {
                        // Clear the patch history               
                        session_history.Erase(session_history.Begin(), session_history.End());
                    }
                }
                _save_patch_history_file();
#endif
                // Save the patch morph position
                _morph_value_param ?
                    utils::get_layer_info(layer_num).set_morph_value(_morph_value_param->get_value()) :
                    utils::get_layer_info(layer_num).set_morph_value(0.0f);  
                MSG("Loaded Layer " << (system_func.layer_num + 1) << " patch: " << _get_patch_filename(id));

                // If this is the current layer, send an event to get the managers to re-load their presets
                if (utils::is_current_layer(layer_num)) {
                    _event_router->post_reload_presets_event(new ReloadPresetsEvent(module()));
                }
                else {
                    // If we didn't load the current layer, then reset the params back to the current layer
                    // settings
                    // Probably should implement better way to handle all of this, seems clunky...
                    _patch_json_doc = &_layer_patch_json_doc[utils::get_current_layer_info().layer_num()];
                    auto params = utils::get_patch_params();
                    _parse_patch_common_params(utils::get_current_layer_info().layer_num(), true, params);
                    _parse_patch_state_params(true, params, utils::get_current_layer_info().get_patch_state());
                }
                
                // Save the updated layer info
                _set_current_layer_filename_tag(layer_num);
                auto itr = _get_layer_json_data(layer_num);
                if (itr) {
                    itr->GetObject()["bank_num"].SetUint(id.bank_num);
                    itr->GetObject()["patch_num"].SetUint(id.patch_num);
                     _start_save_layers_file_timer();
                }

                // Indicate we have finished loading a patch with the DAW
                _set_loading_patch_with_daw(layer_num, false);

                // Once the patch is loaded, we need to set the morph position in the DAW (if non-zero)
                // This will kick off morphing if needed
                if (_morph_value_param && _morph_value_param->get_value()) {
                    // Set the morph position in the DAW
                    _daw_manager->set_param(layer_num, _morph_value_param, _morph_value_param->get_value());
                } 

                // Unlock the morph
                utils::set_morph_on(true);
                utils::morph_unlock();
            }
        }
        break;        

        case SystemFuncType::LOAD_PATCH_STATE_B:
        {
            rapidjson::Document from_patch_json_doc;

            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);

#ifdef INCLUDE_PATCH_HISTORY 
            // If there are any pending patch history changes, process them first
            // Note: Lock obtained above so no need to lock again when processing the
            // pending patch history
            _save_patch_history_timer->stop();
            _params_changed_timeout();
#endif

            // Try to open the from patch file - if it doesn't exist, create it
            auto id = system_func.patch_id;
            if (_open_patch_file(id, from_patch_json_doc))
            {
                const uint &layer_num = utils::get_current_layer_info().layer_num();

                // Indicate we are now loading a patch with the DAW
                _set_loading_patch_with_daw(layer_num, true);

                // File opened successfully, get the from patch State A params and set in State B for 
                // the currently loaded patch
                utils::morph_lock();
                utils::set_morph_on(false);
                utils::reset_morph_state();
                
                // Load the Patch state B params
                _set_patch_state_b_params(from_patch_json_doc);
                _daw_manager->set_patch_params(layer_num, false, PatchState::STATE_B);
                utils::get_current_layer_info().set_patch_modified(true);

                // Set the Morph value and knob params for STATE B
                // Note: Assumes they are mapped to each other
                float morph_value = 1.0;
                if (_morph_value_param) {
                    // Set the Morph params
                    _morph_value_param->set_value(morph_value);
                    if (_morph_knob_param) {
                        _morph_knob_param->set_value(morph_value);
                    }
                    
                    // We also save the value in the patch
                    auto itr = _find_patch_param(_morph_value_param->get_path(), false);
                    if (itr) {
                        itr->GetObject()["value"].SetFloat(morph_value);
                    }                    
                }
                utils::get_layer_info(layer_num).set_morph_value(morph_value); 
                MSG("Loaded Layer " << (layer_num + 1) << " patch State B: " << _get_patch_filename(id));

                // Send an event to get the managers to re-load their presets
                _event_router->post_reload_presets_event(new ReloadPresetsEvent(module()));

                // Indicate we have finished loading a patch with the DAW
                _set_loading_patch_with_daw(layer_num, false);

                // Send the updated Morph value to the DAW
                if (_morph_value_param) {
                    _daw_manager->set_param(layer_num, _morph_value_param, morph_value);
                }  

                // Unlock the morph
                utils::set_morph_on(true);
                utils::morph_unlock();
            }
            else
            {
                // Could not open the from patch file
                DEBUG_BASEMGR_MSG("An error occurred opening the patch file: " << _get_patch_filename(id));
            }
        }
        break;

        case SystemFuncType::SAVE_PATCH:
        {
            auto id = system_func.patch_id;

            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);
            const uint &layer_num = utils::get_current_layer_info().layer_num();

            // Indicate we are now processing a patch with the DAW
            //_set_loading_patch_with_daw(layer_num, true);

#ifdef INCLUDE_PATCH_HISTORY 
            // Stop the save patch history timer
            _save_patch_history_timer->stop();
#endif

            // Save the patch file
            utils::morph_lock();
            utils::set_morph_on(false);
            utils::get_current_layer_info().set_patch_id(id);
            utils::get_current_layer_info().set_patch_modified(false);
            _save_patch_file(id);
            MSG("Saved Layer " << (layer_num + 1) << " patch: " << _get_patch_filename(id));

#ifdef INCLUDE_PATCH_HISTORY 
            // Does the patch history contain entries?
            if (_patch_history_json_data.HasMember("session_history") && _patch_history_json_data["session_history"].IsArray())
            {
                // Is there a session history?
                auto session_history = _patch_history_json_data["session_history"].GetArray();            
                if (session_history.Size() > 0)
                {
                    // Clear the patch history               
                    session_history.Erase(session_history.Begin(), session_history.End());
                }
            }
            _save_patch_history_file();

            // Empty the param change history vector
            _param_change_history.clear();
#endif

            // Save the updated layer info
            _set_current_layer_filename_tag(layer_num);            
            auto itr = _get_current_layer_json_data();
            if (itr) {
                itr->GetObject()["bank_num"].SetUint(id.bank_num);
                itr->GetObject()["patch_num"].SetUint(id.patch_num);
                _start_save_layers_file_timer();
            }

            // Unlock the morph
            utils::set_morph_on(true);
            utils::morph_unlock();                               
        }
        break;

        case SystemFuncType::SAVE_MORPH:
        {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);
            const uint &layer_num = utils::get_current_layer_info().layer_num();

            // Indicate we are now processing a patch with the DAW
            _set_loading_patch_with_daw(layer_num, true);

#ifdef INCLUDE_PATCH_HISTORY 
            // Stop the save patch history timer
            _save_patch_history_timer->stop();
#endif

            // Save the morph in the current state params and DAW
            utils::morph_lock();
            utils::set_morph_on(false);
            utils::reset_morph_state();

            // Set the Morph value and knob params for STATE A/B
            // Note: Assumes they are mapped to each other
            float morph_value = (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ? 0.0 : 1.0;
            if (_morph_value_param) {
                // Set the Morph params
                _morph_value_param->set_value(morph_value);
                if (_morph_knob_param) {
                    // Note: Set the Morph knob param and send a param change so that its position
                    // is updated
                    _morph_knob_param->set_value(morph_value);
                    auto param_change = ParamChange(_morph_knob_param->get_path(), morph_value, module());       
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                }
                
                // We also save the value in the patch
                auto itr = _find_patch_param(_morph_value_param->get_path(), false);
                if (itr) {
                    itr->GetObject()["value"].SetFloat(morph_value);
                }                  
            }
            utils::get_layer_info(layer_num).set_morph_value(morph_value);

            // Update the Patch state params
            _update_state_params();
            _daw_manager->set_patch_params(layer_num, false, utils::get_current_layer_info().get_patch_state());
            utils::get_current_layer_info().set_patch_modified(true);
            MSG("Saved Morph to  " << ((utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ? "State A" : "State B"));
            
            // Indicate we have finished processing a patch with the DAW
            _set_loading_patch_with_daw(layer_num, false);  

            // Send the updated Morph value to the DAW
            if (_morph_value_param) {
                _daw_manager->set_param(layer_num, _morph_value_param, morph_value);
            }            

            // Unlock the morph
            utils::set_morph_on(true);
            utils::morph_unlock();
        }
        break;

        case SystemFuncType::INIT_PATCH:
        {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);
            const uint &layer_num = utils::get_current_layer_info().layer_num();

            // Copy the INIT patch data to the current document
            assert(_patch_json_doc);
            (*_patch_json_doc).CopyFrom(_init_patch_json_data, (*_patch_json_doc).GetAllocator());

            // Indicate we are now processing a patch with the DAW
            _set_loading_patch_with_daw(layer_num, true); 

            // File opened successfully, reset the morph
            utils::morph_lock();
            utils::set_morph_on(false);
            utils::reset_morph_state();
            
            // Parse the patch data
            utils::get_current_layer_info().set_patch_state(PatchState::STATE_A);
            utils::get_current_layer_info().set_patch_modified(true);
            _parse_patch(utils::get_current_layer_info().layer_num(), false);

            // Save the patch morph position
            _morph_value_param ?
                utils::get_layer_info(layer_num).set_morph_value(_morph_value_param->get_value()) :
                utils::get_layer_info(layer_num).set_morph_value(0.0f);             
            MSG("Init Layer " << (layer_num + 1) << " patch: " << _get_patch_filename(utils::get_current_layer_info().get_patch_id()));

#ifdef INCLUDE_PATCH_HISTORY 
            // Delete the patch history entries - does the patch history contain entries?
            if (_patch_history_json_data.HasMember("session_history") && _patch_history_json_data["session_history"].IsArray())
            {
                // Is there a session history?
                auto session_history = _patch_history_json_data["session_history"].GetArray();            
                if (session_history.Size() > 0)
                {
                    // Clear the patch history               
                    session_history.Erase(session_history.Begin(), session_history.End());
                }
            }
            _save_patch_history_file();
#endif

            // Send an event to get the managers to re-load their presets
            _event_router->post_reload_presets_event(new ReloadPresetsEvent(module()));

            // Indicate we have finished processing a patch with the DAW
            _set_loading_patch_with_daw(layer_num, false); 

            // Once the patch is loaded, we need to set the morph position in the DAW (if non-zero)
            // This will kick off morphing if needed
            if (_morph_value_param && _morph_value_param->get_value()) {
                // Set the morph position in the DAW
                _daw_manager->set_param(layer_num, _morph_value_param, _morph_value_param->get_value());
            }

            // Unlock the morph
            utils::set_morph_on(true);
            utils::morph_unlock();
        }
        break;

        case SystemFuncType::SET_MOD_SRC_NUM:
        {
            // Save the new modulation source number
            auto num = system_func.num + 1;
            utils::system_config()->set_mod_src_num(num);
            _config_json_data["mod_src_num"].SetUint(num);
            _start_save_config_file_timer();              
        }
        break;

        case SystemFuncType::CURRENT_LAYER:
        {
            const uint &layer_num = system_func.num;

            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);

            // Has the layer actually changed?
            if (layer_num != utils::get_current_layer_info().layer_num()) {
#ifdef INCLUDE_PATCH_HISTORY                
                // Stop the save patch history timer
                _save_patch_history_timer->stop();
                _params_changed_timeout();
#endif

                // The layer has changed, set the current layer
                utils::morph_lock();
                utils::set_morph_on(false);
                utils::reset_morph_state();
                utils::set_current_layer(layer_num);
                utils::reset_param_states();

                // Parse the common and layer params for this layer
                // Note: Don't load the state params as this layer might be morphed
                _patch_json_doc = &_layer_patch_json_doc[layer_num];
                auto params = utils::get_patch_params();
                _parse_patch_common_params(layer_num, true, params);
                _parse_patch_layer_params(params);

                // Calculate and set the Layer voices
                // Note: We need to do this so that the number of voices available to this Layer
                // is re-calculated
                // Also make sure the MPE Zone Channels are configured correctly
                _calc_and_set_layer_voices();
                (void)utils::config_mpe_zone_channel_params();

                // Set the Morph knob value
                // Note: Assumes they are mapped to each other
                float morph_value = utils::get_current_layer_info().get_morph_value();
                if (_morph_knob_param) {
                    // Note: Set the Morph knob param and send a param change so that its position
                    // is updated
                    _morph_knob_param->set_value(morph_value);
                    auto param_change = ParamChange(_morph_knob_param->get_path(), morph_value, module());       
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));             
                }

#ifdef INCLUDE_PATCH_HISTORY
                // Delete the patch history entries - does the patch history contain entries?
                if (_patch_history_json_data.HasMember("session_history") && _patch_history_json_data["session_history"].IsArray())
                {
                    // Is there a session history?
                    auto session_history = _patch_history_json_data["session_history"].GetArray();            
                    if (session_history.Size() > 0)
                    {
                        // Clear the patch history               
                        session_history.Erase(session_history.Begin(), session_history.End());
                    }
                }
                _save_patch_history_file();
#endif
                MSG("Switched to Layer " << (utils::get_current_layer_info().layer_num() + 1));

                // Are we currently morphing on this layer?
                auto morph_mode_param = utils::get_param_from_ref(utils::ParamRef::MORPH_MODE);
                if (((morph_value > 0.0) && (morph_value < 1.0)) && (morph_mode_param && (morph_mode_param->get_position_value() == MorphMode::DANCE))) {
                    // Enable morphing - the patch params will be retrieved during normal
                    // Surface Manager processing and set the morphed knob positions
                    utils::set_morph_enabled(true);
                }
                else {
                    // Load the patch params for the current state based on the morph knob
                    _parse_patch_state_params(true, params, ((morph_value == 1.0) ? PatchState::STATE_B : PatchState::STATE_A));
                }

                // Send an event to get the managers to re-load their presets
                _event_router->post_reload_presets_event(new ReloadPresetsEvent(module(), true));

                // Unlock morphing
                utils::set_morph_on(true);
                utils::morph_unlock();
            }
        }
        break;

        case SystemFuncType::LOAD_LAYERS:
        {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);
            const uint &layers_num = system_func.num;

#ifdef INCLUDE_PATCH_HISTORY
            // If there are any pending patch history changes, process them first
            // Note: Lock obtained above so no need to lock again when processing the
            // pending patch history
            _save_patch_history_timer->stop();
            _params_changed_timeout();
#endif

            // Try to open the layers file
            if (_open_and_parse_layers_file(layers_num))
            {
                // Indicate we are now processing layers with the DAW
                _set_loading_layers_with_daw(true); 

                // Layers config file opened
                _save_current_layers_file();                
                MSG("Loaded Layers config: " << _get_layers_filename(layers_num));

                // Lock morphing and disable it
                utils::morph_lock();
                utils::set_morph_on(false);
                utils::reset_morph_state();

                // Save the layers config
                utils::system_config()->set_layers_num(layers_num);
                _config_json_data["layers_num"].SetUint(layers_num);
                _start_save_config_file_timer();
                
                // Reset the MPE Params - this is so they are set to their defaults if the params
                // are missing from the Layers file
                utils::reset_mpe_params();

                // Parse the patch data for each layer
                for (int i=(NUM_LAYERS-1); i>=0; i--) {
                    bool parse_patch = true;

                    // Set the layer and parse it
                    utils::set_current_layer(i);
                    utils::get_current_layer_info().set_patch_modified(false);
                    utils::get_current_layer_info().set_patch_state(PatchState::STATE_A);
                    _select_current_layer(i);
                    _patch_json_doc = &_layer_patch_json_doc[i];
                    if (!_open_patch_file(utils::get_layer_info(i).get_patch_id())) {
                        // This is a critical error - note the function above call logs any errors
                        parse_patch = false;
                    }
                    _set_current_layer_filename_tag(i);                   
                    if (parse_patch) {
                        // Parse the patch
                        _parse_patch(i);

                        // Save the patch morph position
                        _morph_value_param ?
                            utils::get_layer_info(i).set_morph_value(_morph_value_param->get_value()) :
                            utils::get_layer_info(i).set_morph_value(0.0f);    
                        MSG("Loaded Layer " << (i+1) << " patch: " << _get_patch_filename(utils::get_layer_info(i).get_patch_id()));
                    }
                }

#if defined CHECK_LAYERS_LOAD
                _check_layers_load();
#endif

#ifdef INCLUDE_PATCH_HISTORY
                // Delete the patch history entries - does the patch history contain entries?
                if (_patch_history_json_data.HasMember("session_history") && _patch_history_json_data["session_history"].IsArray())
                {
                    // Is there a session history?
                    auto session_history = _patch_history_json_data["session_history"].GetArray();            
                    if (session_history.Size() > 0)
                    {
                        // Clear the patch history               
                        session_history.Erase(session_history.Begin(), session_history.End());
                    }
                }
                _save_patch_history_file();
#endif

                // Send an event to get the managers to re-load their presets
                _event_router->post_reload_presets_event(new ReloadPresetsEvent(module()));

                // Indicate we have finished processing patches with the DAW
                _set_loading_layers_with_daw(false);

                // Once all patches are loaded, we need to set the morph position for each
                // layer in the DAW (if non-zero)
                // This will kick off morphing if needed
                for (int i=(NUM_LAYERS-1); i>=0; i--) {
                    if (utils::get_layer_info(i).get_morph_value() && _morph_value_param) {
                        // Set the morph position in the DAW
                        _daw_manager->set_param(i, _morph_value_param, utils::get_layer_info(i).get_morph_value());
                    }
                }

                // Unlock the morph
                utils::set_morph_on(true);
                utils::morph_unlock();              
            }
            break;
        }        

        case SystemFuncType::SAVE_LAYERS:
        {
            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);
            const uint &layers_num = system_func.num;

            // Indicate we are now processing layers with the DAW
            _set_loading_layers_with_daw(true); 

#ifdef INCLUDE_PATCH_HISTORY
            // Stop the save patch history timer
            _save_patch_history_timer->stop();
#endif

            // Save the layers file
            utils::morph_lock();
            _save_layers_file(layers_num);
            utils::morph_unlock();
            MSG("Saved Layers file: " << _get_layers_filename(layers_num));

            // Save the selected layers config
            utils::system_config()->set_layers_num(layers_num);
            _config_json_data["layers_num"].SetUint(layers_num);
            _start_save_config_file_timer();

            // Indicate we have finished processing patches with the DAW
            _set_loading_layers_with_daw(false);            
            break;
        }

        case SystemFuncType::RESET_LAYERS:
        {
            PatchId patches[NUM_LAYERS];

            // Get the patch mutex
            std::lock_guard<std::mutex> guard(_patch_mutex);
 
#ifdef INCLUDE_PATCH_HISTORY
            // If there are any pending patch history changes, process them first
            // Note: Lock obtained above so no need to lock again when processing the
            // pending patch history
            _save_patch_history_timer->stop();
            _params_changed_timeout();
#endif

            // Get the current layer patch IDs
            for (uint i=0; i<NUM_LAYERS; i++) {
                patches[i] = utils::get_layer_info(i).get_patch_id();
            }

            // Try to open the default layers file
            if (_open_and_parse_layers_file(NINA_ROOT_FILE_PATH(DEFAULT_LAYERS_FILE)))
            {
                // Indicate we are now processing layers with the DAW
                _set_loading_layers_with_daw(true); 
               
                // Lock morphing and disable it
                utils::morph_lock();
                utils::set_morph_on(false);
                utils::reset_morph_state();
                
                // Reset the MPE Params - this is so they are set to their defaults if the params
                // are missing from the Layers file
                utils::reset_mpe_params();

                // Parse the layer data for each layer
                auto params = utils::get_patch_params();
                for (int i=(NUM_LAYERS-1); i>=0; i--) {
                    // Select the layer
                    utils::set_current_layer(i);
                    _select_current_layer(i);
                    utils::get_current_layer_info().set_patch_modified(false);
                    utils::get_current_layer_info().set_patch_state(PatchState::STATE_A);

                    // Reset the layer patch IDs
                    utils::get_layer_info(i).set_patch_id(patches[i]);
                    auto itr = _get_current_layer_json_data();
                    if (itr) {
                        itr->GetObject()["bank_num"].SetUint(patches[i].bank_num);
                        itr->GetObject()["patch_num"].SetUint(patches[i].patch_num);
                    }

                    // Now Parse the patch data for each layer
                    // This reloads the patch data and loads the new layer data
                    _patch_json_doc = &_layer_patch_json_doc[i];               
                    _parse_patch(i);

                    // Save the patch morph position
                    _morph_value_param ?
                        utils::get_layer_info(i).set_morph_value(_morph_value_param->get_value()) :
                        utils::get_layer_info(i).set_morph_value(0.0f);    
                    MSG("Loaded Layer " << (i+1) << " patch: " << _get_patch_filename(utils::get_layer_info(i).get_patch_id()));
                }
                MSG("Layers reset");

#if defined CHECK_LAYERS_LOAD
                _check_layers_load();
#endif

                // Send an event to get the managers to re-load their presets
                _event_router->post_reload_presets_event(new ReloadPresetsEvent(module()));

                // Indicate we have finished processing layers with the DAW
                _set_loading_layers_with_daw(false);

                // Once all patches are loaded, we need to set the morph position for each
                // layer in the DAW (if non-zero)
                // This will kick off morphing if needed
                for (int i=(NUM_LAYERS-1); i>=0; i--) {
                    if (utils::get_layer_info(i).get_morph_value() && _morph_value_param) {
                        // Set the morph position in the DAW
                        _daw_manager->set_param(i, _morph_value_param, utils::get_layer_info(i).get_morph_value());
                    }
                }

                // Unlock morph
                utils::set_morph_on(true);
                utils::morph_unlock();
            }
            break;
        }

        case SystemFuncType::SAVE_DEMO_MODE:
        {
            // Save the demo mode state
            _config_json_data["demo_mode"].SetBool(utils::system_config()->get_demo_mode());
            _start_save_config_file_timer();              
        }
        break;         

        case SystemFuncType::SYSTEM_COLOUR_SET:
        {
            // Save the new system colour
            utils::system_config()->set_system_colour(system_func.str_value);
            _config_json_data["system_colour"].SetString(system_func.str_value, _config_json_data.GetAllocator());
            _start_save_config_file_timer();           
            break;
        }

        default:
            // Ignore all other events
            break;        
    }
}

#ifdef INCLUDE_PATCH_HISTORY
//----------------------------------------------------------------------------
// _params_changed_timeout_with_lock
//----------------------------------------------------------------------------
void FileManager::_params_changed_timeout_with_lock()
{
    // Get the patch mutex
    std::lock_guard<std::mutex> guard(_patch_mutex);

    // Proces the params changed timeout
    _params_changed_timeout();
}

//----------------------------------------------------------------------------
// _params_changed_timeout
//----------------------------------------------------------------------------
void FileManager::_params_changed_timeout()
{
    // Are there any param change history entries to process?
    if (_param_change_history.size() > 0)
    {
        // Does any patch history exist
        if (_patch_history_json_data.HasMember("session_history") && _patch_history_json_data["session_history"].IsArray())
        {
            // Get the session history array
            auto session_history = _patch_history_json_data["session_history"].GetArray();
        
            // Now check if adding these change entries will exceed the history max
            if ((session_history.Size() + _param_change_history.size()) > PATCH_HISTORY_MAX_EVENTS)
            {
                // Erase the oldest entries
                int num = (session_history.Size() + _param_change_history.size()) - PATCH_HISTORY_MAX_EVENTS;
                session_history.Erase(session_history.Begin(), session_history.Begin() + num);
            }

            // Add the changed param entries to the history file
            for (const ParamChangeHistory &pch : _param_change_history) {
                // Add the param change history
                rapidjson::Value tmp;
                tmp.SetObject();
                tmp.AddMember("path", std::string(pch.path), _patch_history_json_data.GetAllocator());
                tmp.AddMember("value", pch.value, _patch_history_json_data.GetAllocator());
                session_history.PushBack(tmp, _patch_history_json_data.GetAllocator());
            }

            // Save the patch history file
            _save_patch_history_file();
        }
    }

    // Empty the param change history vector, ready for the next batch of param changes
    _param_change_history.clear();    
}
#endif

//----------------------------------------------------------------------------
// _get_layers_filename
//----------------------------------------------------------------------------
std::string FileManager::_get_layers_filename(uint layers_num)
{
    char layers_file_prefix[sizeof("000_")];
    std::string layers_filename;

    // Format the file prefix
    std::sprintf(layers_file_prefix, "%03d_", layers_num);

    // Open the layers folder
    auto dir = ::opendir(NINA_LAYERS_DIR);
    if (dir != nullptr)
    {
        struct dirent *dirent;

        // Parse each file, looking for the layers file
        while ((dirent = ::readdir(dir)) != nullptr)
        {
            // Is this a regular file?
            if (dirent->d_type == DT_REG)
            {                
                // Is this the layers filename?
                if (std::strncmp(dirent->d_name, layers_file_prefix, (sizeof(layers_file_prefix)-1)) == 0)
                {
                    // Layers filename found
                    layers_filename = NINA_LAYERS_DIR + std::string(dirent->d_name);
                    break;
                }
            }
        }
        ::closedir(dir);
        if (layers_filename.empty())
        {
            // The layers file does not exist - in which case we can safely assume the default is loaded
            layers_filename = NINA_LAYERS_DIR + utils::get_default_layers_filename(layers_num, true);
        }                                           
    }
    return layers_filename;
}


//----------------------------------------------------------------------------
// _get_patch_filename
//----------------------------------------------------------------------------
std::string FileManager::_get_patch_filename(PatchId id, bool full_path)
{
    char bank_file_prefix[sizeof("000_")];
    char patch_file_prefix[sizeof("000_")];
    std::string bank_folder;
    bool bank_folder_found = false;
    std::string patch_filename;
    bool patch_filename_found = false;

    // Format the bank and patch file prefixes
    std::sprintf(bank_file_prefix, "%03d_", id.bank_num);
    std::sprintf(patch_file_prefix, "%03d_", id.patch_num);

    // We need to find the patch bank folder first
    auto dir = ::opendir(NINA_PATCHES_DIR);
    if (dir != nullptr)
    {
        struct dirent *dirent;

        // Parse each directory, looking for the bank folder
        while ((dirent = ::readdir(dir)) != nullptr)
        {
            // Is this a directory?
            if (dirent->d_type == DT_DIR)
            {
                // Is this the bank folder?
                if (std::strncmp(dirent->d_name, bank_file_prefix, (sizeof(bank_file_prefix)-1)) == 0)
                {
                    // Bank folder found
                    bank_folder = NINA_PATCHES_DIR + std::string(dirent->d_name);
                    bank_folder_found = true;
                    break;
                }
            }
        }
        ::closedir(dir);
        if (!bank_folder_found)
        {
            // This should never happen
            return "";
        }

        // Open the bank folder
        dir = ::opendir(bank_folder.c_str());
        if (dir != nullptr)
        {
            struct dirent *dirent;

            // Parse each file, looking for the patch file
            while ((dirent = ::readdir(dir)) != nullptr)
            {
                // Is this a regular file?
                if (dirent->d_type == DT_REG)
                {                
                    // Is this the patch filename?
                    if (std::strncmp(dirent->d_name, patch_file_prefix, (sizeof(patch_file_prefix)-1)) == 0)
                    {
                        // Patch filename found
                        if (full_path) {
                            // Return the full unmodified filename path
                            patch_filename = bank_folder + "/" + std::string(dirent->d_name);
                        }
                        else {
                            // Return the formatted filename
                            auto name = std::string(dirent->d_name);
                            name = name.substr(0, (name.size() - (sizeof(".json") - 1)));
                            uint index = (name[0] == '0') ? 4 : 3;                        
                            name = name.substr(index, (name.size() - index));
                            std::transform(name.begin(), name.end(), name.begin(), ::toupper); 
                            patch_filename = std::regex_replace(name, std::regex{"_"}, " ");
                        }
                        patch_filename_found = true;
                        break;
                    }
                }
            }
            ::closedir(dir);
            if (!patch_filename_found)
            {
                // The patch does not exist - in which case we can safely assume the default is loaded
                auto name = utils::get_default_patch_filename(id.patch_num, true);
                if (full_path) {   
                    patch_filename = bank_folder + "/" + name;
                }
                else {
                    patch_filename = name;
                    patch_filename = patch_filename.substr(0, (patch_filename.size() - (sizeof(".json") - 1)));
                }
                patch_filename_found = true;
            }                                                 
        }
    }
    return patch_filename;
}

//----------------------------------------------------------------------------
// _open_config_file
//----------------------------------------------------------------------------
bool FileManager::_open_config_file()
{
    const char *schema =
#include "../json_schemas/config_schema.json"
;
    
    // Open the config file
    bool ret = _open_json_file(NINA_UDATA_FILE_PATH(CONFIG_FILE), schema, _config_json_data, "{}");
    if (ret)
    {
        // If the JSON data is empty (not an object), make it one!
        if (!_config_json_data.IsObject())
            _config_json_data.SetObject();
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_param_attributes_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_param_attributes_file()
{
    const char *schema =
#include "../json_schemas/param_attributes_schema.json"
;
    rapidjson::Document json_data;

    // Open the param atttributes file
    bool ret = _open_json_file(NINA_ROOT_FILE_PATH(PARAM_ATTRIBUTES_FILE), schema, json_data, false);
    if (ret)
    {
        // If the JSON data is empty its an invalid file
        if (!json_data.IsArray())
            return false;

        // Parse the param attributes file
        // If the JSON data is not an array don't parse it
        if (json_data.IsArray())
        {
            // Iterate through the params
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                // Is the param path present?
                if (itr->GetObject().HasMember("param_path") && itr->GetObject()["param_path"].IsString())
                {
                    // Get the param path and find the param(s)
                    auto param_path = itr->GetObject()["param_path"].GetString();
                    auto params = utils::get_params(param_path);
                    for (Param *p : params)
                    {
                        // Check for the various attributes that can be specified
                        if (itr->GetObject().HasMember("ref") && itr->GetObject()["ref"].IsString())
                        {
                            // Get the reference for this param
                            p->ref = itr->GetObject()["ref"].GetString();

                            // Additional processing
                            // If this param is the morph value param, check the other mapped
                            // params and if a physical control, set it as the morph knob
                            // Note: Will set the first mapped physical control to the morph knob
                            if (utils::param_has_ref(p, utils::ParamRef::MORPH_VALUE)) {
                                auto mapped_params = p->get_mapped_params();
                                for (Param *mp : mapped_params)
                                {
                                    // Is this a physical param?
                                    if (mp->physical_control_param) {
                                        utils::set_morph_knob_num(static_cast<SurfaceControlParam *>(mp)->param_id);
                                        break;
                                    }
                                }
                            }                             
                        }
                        if (itr->GetObject().HasMember("patch") && itr->GetObject()["patch"].IsBool())
                        {
                            // Set this as a patch param
                            p->patch_param = itr->GetObject()["patch"].GetBool();
                        }
                        if (itr->GetObject().HasMember("patch_layer_param") && itr->GetObject()["patch_layer_param"].IsBool())
                        {
                            // Set this as a layer param
                            p->patch_layer_param = itr->GetObject()["patch_layer_param"].GetBool();
                        }                          
                        if (itr->GetObject().HasMember("patch_common_layer_param") && itr->GetObject()["patch_common_layer_param"].IsBool())
                        {
                            // Set this as a common layer param
                            p->patch_common_layer_param = itr->GetObject()["patch_common_layer_param"].GetBool();
                        }                         
                        if (itr->GetObject().HasMember("patch_state_param") && itr->GetObject()["patch_state_param"].IsBool())
                        {
                            // Set this as a state param
                            p->patch_state_param = itr->GetObject()["patch_state_param"].GetBool();
                        }
                        if (itr->GetObject().HasMember("global_param") && itr->GetObject()["global_param"].IsBool())
                        {
                            // Set this as a global param
                            p->global_param = itr->GetObject()["global_param"].GetBool();
                        }
                        if (itr->GetObject().HasMember("layer_1_param") && itr->GetObject()["layer_1_param"].IsBool())
                        {
                            // Set this as a Layer 1 only param
                            p->layer_1_param = itr->GetObject()["layer_1_param"].GetBool();
                        }                                                                                            
                        if (itr->GetObject().HasMember("display_name") && itr->GetObject()["display_name"].IsString())
                        {
                            // Set the display name string - if the string is empty, also set
                            // the param name to an empty string so that it is not shown on the GUI
                            p->display_name = itr->GetObject()["display_name"].GetString();
                            if (p->display_name.size() == 0) {
                                p->name = "";
                            }
                        }
                        if (itr->GetObject().HasMember("display_switch") && itr->GetObject()["display_switch"].IsBool())
                        {
                            // Set this as a switch parameter
                            p->display_switch = itr->GetObject()["display_switch"].GetBool();
                        }
                        if (itr->GetObject().HasMember("num_positions") && itr->GetObject()["num_positions"].IsUint())
                        {
                            // Set the number of positions
                            p->set_multi_position_param_num_positions(itr->GetObject()["num_positions"].GetUint());
                        }                       
                        if (itr->GetObject().HasMember("display_range_min") && itr->GetObject()["display_range_min"].IsInt())
                        {
                            // Set the display range min
                            p->display_range_min = itr->GetObject()["display_range_min"].GetInt();
                        }
                        if (itr->GetObject().HasMember("display_range_max") && itr->GetObject()["display_range_max"].IsInt())
                        {
                            // Set the display range max
                            p->display_range_max = itr->GetObject()["display_range_max"].GetInt();
                        }
                        if (itr->GetObject().HasMember("haptic_mode") && itr->GetObject()["haptic_mode"].IsString())
                        {
                            // Get the mode
                            auto mode = itr->GetObject()["haptic_mode"].GetString();

                            // Is the param a hardware param?
                            if (p->physical_control_param)
                            {
                                // Set the mode
                                static_cast<SurfaceControlParam *>(p)->set_haptic_mode(mode);
                            }
                        }
                        if (itr->GetObject().HasMember("display_strings") && itr->GetObject()["display_strings"].IsArray())
                        {
                            // Parse each Display String
                            auto display_strings = itr->GetObject()["display_strings"].GetArray();
                            for (auto& ds : display_strings)
                            {
                                // If a Display String has been specified
                                if (ds.HasMember("string") && ds["string"].IsString())
                                {
                                    // Add the Diplay String
                                    p->display_strings.push_back(ds["string"].GetString());
                                }                                      
                            }
                        }
                        if (itr->GetObject().HasMember("value_tag") && itr->GetObject()["value_tag"].IsString())
                        {
                            // Get the Value Tag and set
                            p->value_tag = itr->GetObject()["value_tag"].GetString();
                        }                        
                        if (itr->GetObject().HasMember("value_tags") && itr->GetObject()["value_tags"].IsArray())
                        {
                            // Parse each Value Tag
                            auto value_tags = itr->GetObject()["value_tags"].GetArray();
                            for (auto& vt : value_tags)
                            {
                                // If a Value Tag has been specified
                                if (vt.HasMember("string") && vt["string"].IsString())
                                {
                                    // Add the Value Tag
                                    p->value_tags.push_back(vt["string"].GetString());
                                }                                      
                            }
                        }
                        if (itr->GetObject().HasMember("numeric_enum_param") && itr->GetObject()["numeric_enum_param"].IsBool())
                        {
                            // Indicate the param is a numeric enum param
                            p->numeric_enum_param = itr->GetObject()["numeric_enum_param"].GetBool();
                        }
                        if (itr->GetObject().HasMember("always_show") && itr->GetObject()["always_show"].IsBool())
                        {
                            // Indicate this parameter should always be shown
                            // Note: Applies to Mod Matrix entries, these params should always be shown and
                            // cannot be deleted
                            p->always_show = itr->GetObject()["always_show"].GetBool();
                        }
                        if (itr->GetObject().HasMember("set_ui_state") && itr->GetObject()["set_ui_state"].IsString())
                        {
                            // Indicate the set UI state related to this parameter
                            p->set_ui_state = itr->GetObject()["set_ui_state"].GetString();
                        }                       
                        if (itr->GetObject().HasMember("param_list") && itr->GetObject()["param_list"].IsString())
                        {
                            // Set the param list name for this param
                            p->param_list_name = itr->GetObject()["param_list"].GetString();
                        }                                              
                    }
                }
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_param_lists_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_param_lists_file()
{
    const char *schema =
#include "../json_schemas/param_lists_schema.json"
;
    rapidjson::Document json_data;

    // Open the param lists file
    bool ret = _open_json_file(NINA_ROOT_FILE_PATH(PARAM_LISTS_FILE), schema, json_data, false);
    if (ret)
    {
        // If the JSON data is empty its an invalid file
        if (!json_data.IsArray())
            return false;

        // Parse the param lists file
        // If the JSON data is not an array don't parse it
        if (json_data.IsArray())
        {
            // Iterate through the params
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                std::string param_list_name;
                std::string param_list_display_name;
                auto param_list = std::vector<Param *>();
                auto ct_params_list = std::vector<ContextSpecificParams>();

                // Has the param list name been specified?
                if (itr->GetObject().HasMember("name") && itr->GetObject()["name"].IsString())
                {
                    // Get the param list name
                    param_list_name = itr->GetObject()["name"].GetString();
                    param_list_display_name = param_list_name;
                }
                if (itr->GetObject().HasMember("display_name") && itr->GetObject()["display_name"].IsString())
                {
                    // Get the param list display name
                    param_list_display_name = itr->GetObject()["display_name"].GetString();
                }

                // Get the param list
                if (itr->GetObject().HasMember("params") && itr->GetObject()["params"].IsArray())
                {
                    // Parse each param
                    auto params = itr->GetObject()["params"].GetArray();
                    for (auto& p : params)
                    {
                        // If a param path has been specified
                        if (p.HasMember("param") && p["param"].IsString())
                        {
                            // Get the param path and add the param to the list
                            auto param = utils::get_param(p["param"].GetString());
                            if (param) {
                                // If this param is also a separator
                                if (p.HasMember("separator") && p["separator"].IsBool())
                                {
                                    // Indicate if this param is a separator
                                    param->separator = p["separator"].GetBool();
                                }

                                // Add the param to the list                       
                                param_list.push_back(param);
                            }
                        }
                    }
                }

                // Get the context specific params list
                if (itr->GetObject().HasMember("context_specific_params") && itr->GetObject()["context_specific_params"].IsArray())
                {
                    // Parse each context specific param list
                    auto ct_params_obj = itr->GetObject()["context_specific_params"].GetArray();
                    for (auto& ct_param_obj : ct_params_obj)
                    {
                        auto ct_params = ContextSpecificParams();

                        // Get the context param
                        if (ct_param_obj.HasMember("context_param") && ct_param_obj["context_param"].IsString())
                        {
                            // Get the context param
                            auto param = utils::get_param(ct_param_obj["context_param"].GetString());
                            if (param) {
                                ct_params.context_param = param;
                            }
                        }

                        // Get the context param value
                        if (ct_param_obj.HasMember("context_value") && ct_param_obj["context_value"].IsUint())
                        {
                            // Get the context value
                            ct_params.context_value = ct_param_obj["context_value"].GetUint();
                        }                        
                        else if (ct_param_obj.HasMember("context_value") && ct_param_obj["context_value"].IsFloat())
                        {
                            // Get the context value
                            ct_params.context_value = ct_param_obj["context_value"].GetFloat();
                        }

                        // Get the params list
                        if (ct_param_obj.HasMember("params") && ct_param_obj["params"].IsArray())
                        {
                            // Parse each param
                            auto params = ct_param_obj["params"].GetArray();
                            for (auto& p : params)
                            {
                                // If a param path has been specified
                                if (p.HasMember("param") && p["param"].IsString())
                                {
                                    // Get the param path and add the param to the list
                                    auto param = utils::get_param(p["param"].GetString());
                                    if (param) {
                                        // If this param is also a separator
                                        if (p.HasMember("separator") && p["separator"].IsBool())
                                        {
                                            // Indicate if this param is a separator
                                            param->separator = p["separator"].GetBool();
                                        }

                                        // Add the param to the list                                         
                                        ct_params.param_list.push_back(param);
                                    }
                                }                                      
                            }
                        }

                        // Add to the list of context specific params
                        ct_params_list.push_back(ct_params);                                          
                    }
                }                

                // If both the name and param list have been specified
                if ((param_list_name.size() > 0) && ((param_list.size() > 0) || (ct_params_list.size() > 0))) {
                    // Find each common param that uses this list, and set in the param
                    auto params = utils::get_params(ParamType::COMMON_PARAM);
                    for (auto p : params) {
                        if (p->param_list_name == param_list_name) {
                            p->param_list_display_name = param_list_display_name;
                            p->param_list = param_list;
                            p->context_specific_param_list = ct_params_list;
                        }
                    }

                    // Find each module param that uses this list, and set in the param
                    params = utils::get_params(ParamType::MODULE_PARAM);
                    for (auto p : params) {
                        if (p->param_list_name == param_list_name) {
                            p->param_list_display_name = param_list_display_name;
                            p->param_list = param_list;
                            p->context_specific_param_list = ct_params_list;
                        }
                    }                    
                }
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_global_params_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_global_params_file()
{
    const char *schema =
#include "../json_schemas/global_params_schema.json"
;

    // Open the global params file
    bool ret = _open_json_file(NINA_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE), schema, _global_params_json_data, true);
    if (ret)
    {
        // If the JSON data is not an array it is an invalid file
        if (!_global_params_json_data.IsArray())
            return false;

        // Get the global params and parse them
        bool save_file = false;
        auto params = utils::get_global_params();
        for (Param *p : params)
        {
            bool param_missed = true;

            // Check if there is an entry for this param
            for (rapidjson::Value::ValueIterator itr = _global_params_json_data.Begin(); itr != _global_params_json_data.End(); ++itr)
            {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->get_path())
                {
                    // Update the parameter value
                    if (p->str_param) {
                        p->set_str_value(itr->GetObject()["str_value"].GetString());
                    }
                    else {
                        p->set_value(itr->GetObject()["value"].GetFloat());
                    }
                    param_missed = false;
                    break;
                }
            }
            if (param_missed)
            {
                // Param is not specified in the global params file
                // We need to add it to the file
                rapidjson::Value obj;
                obj.SetObject();
                obj.AddMember("path", std::string(p->get_path()), _global_params_json_data.GetAllocator());
                if (p->str_param) {
                    obj.AddMember("str_value", p->get_str_value(), _global_params_json_data.GetAllocator());
                }
                else {
                    obj.AddMember("value", p->get_value(), _global_params_json_data.GetAllocator());
                }
                _global_params_json_data.PushBack(obj, _global_params_json_data.GetAllocator());
                save_file = true;
            }
        }

        // If we need to save the global params file, do so
        if (save_file) {
            _save_global_params_file();
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_layers_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_layers_file(uint layers_num)
{
    return _open_and_parse_layers_file(_get_layers_filename(layers_num));
}

//----------------------------------------------------------------------------
// _open_and_parse_layers_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_layers_file(std::string file_path)
{
    bool layer_found[NUM_LAYERS];
    const char *schema =
#include "../json_schemas/layers_schema.json"
;

    // Open the layers file
    bool ret = _open_json_file(file_path, schema, _layers_json_data, false);
    if (!ret)
    {
        // The layer could not be opened - most likely as it doesn't exist
        MSG("The layers file could not be opened: " << file_path);
        NINA_LOG_INFO(module(), "The layers file could not be opened: {}", file_path);
        MSG("Using default layer settings");
        NINA_LOG_INFO(module(), "Using default layer settings");
        ret = _open_json_file(NINA_ROOT_FILE_PATH(DEFAULT_LAYERS_FILE), schema, _layers_json_data, false);
        if (!ret)
        {
            // This is a critical error
            MSG("An error occurred opening the default layers file: " << NINA_ROOT_FILE_PATH(DEFAULT_LAYERS_FILE));
            NINA_LOG_CRITICAL(module(), "An error occurred opening the default layers file: {}", NINA_ROOT_FILE_PATH(DEFAULT_LAYERS_FILE));
            return false;
        }
    }

    // If the JSON data is empty its an invalid file
    if (!_layers_json_data.IsObject()) {
        // This is a critical error
        MSG("The layers file is an empty file");
        NINA_LOG_CRITICAL(module(), "The layers file is an empty file");        
        return false;
    }

    // Does any layers data exist?
    if (!_layers_json_data.HasMember("layers") || !_layers_json_data["layers"].IsArray()) {
        // This is a critical error
        MSG("The layers file format is invalid");
        NINA_LOG_CRITICAL(module(), "The layers file format is invalid");         
        return false;
    }

    // Does any Common patch data exist?
    if (!_layers_json_data.HasMember("common")) {
        // No - add the Common patches as an empty array
        _layers_json_data.AddMember("common", rapidjson::Value(rapidjson::kArrayType), _layers_json_data.GetAllocator());
    }

    // The Common patch data must be an array or the patch is invalid
    if (!_layers_json_data["common"].IsArray()) {
        // This is a critical error
        MSG("The layers file format is invalid");
        NINA_LOG_CRITICAL(module(), "The layers file format is invalid");         
        return false;
    }

    // Iterate through the layers
    std::memset(layer_found, false, NUM_LAYERS);
    for (rapidjson::Value::ValueIterator itr = _layers_json_data["layers"].Begin(); itr != _layers_json_data["layers"].End(); ++itr)
    {
        int layer_num = -1;
        PatchId patch_id;

        // Has the layer number been specified?
        if (itr->GetObject().HasMember("layer_num") && itr->GetObject()["layer_num"].IsUint())
        {
            // Get the layer number and make sure it is valid
            layer_num = itr->GetObject()["layer_num"].GetUint();
            if ((layer_num == 0) || ((uint)layer_num > NUM_LAYERS)) {
                // This is a critical error
                MSG("A layer number specified in the layers file is invalid");
                NINA_LOG_CRITICAL(module(), "A layer number specified in the layers file is invalid");                 
                return false;
            }
            layer_num--;
        }

        // Has the bank number been specified?
        if (itr->GetObject().HasMember("bank_num") && itr->GetObject()["bank_num"].IsUint())
        {
            // Get the bank number
            patch_id.bank_num = itr->GetObject()["bank_num"].GetUint();
        }

        // Has the patch number been specified?
        if (itr->GetObject().HasMember("patch_num") && itr->GetObject()["patch_num"].IsUint())
        {
            // Get the patch number
            patch_id.patch_num = itr->GetObject()["patch_num"].GetUint();
        }          

        // Layer number specified ok?
        if (layer_num >= 0) {
            // Set the layer info
            utils::get_layer_info(layer_num).set_patch_id(patch_id);
            layer_found[layer_num] = true;
        }                      
    }

    // All layers found ok?
    for (uint i=0; i<NUM_LAYERS; i++) {
        if (!layer_found[i]) {
            // This is a critical error
            MSG("Not all layers were specified in the layers file");
            NINA_LOG_CRITICAL(module(), "Not all layers were specified in the layers file");               
            return false;
        }
    }

    // Calculate and set the Layer voices - we need to do this in case the Layer data
    // has voice allocation settings that are incorrect or invalid
    // Also make sure the MPE Zone Channels are configured correctly
    utils::set_current_layer(0);
    _calc_and_set_layer_voices();
    _calc_and_set_layer_mpe_config();
    return ret;
}

//----------------------------------------------------------------------------
// _open_patch_file
//----------------------------------------------------------------------------
bool FileManager::_open_patch_file(PatchId id)
{
    // Open the param patch file
    assert(_patch_json_doc);
    return _open_patch_file(id, *_patch_json_doc);
}

//----------------------------------------------------------------------------
// _open_patch_file
//----------------------------------------------------------------------------
bool FileManager::_open_patch_file(PatchId id, rapidjson::Document &json_doc)
{
    // Open the specified patch file
    return _open_patch_file(_get_patch_filename(id), json_doc);
}

//----------------------------------------------------------------------------
// _open_patch_file
//----------------------------------------------------------------------------
bool FileManager::_open_patch_file(std::string file_path, rapidjson::Document &json_doc)
{
    const char *schema =
#include "../json_schemas/patch_schema.json"
;

    // If the passed patch filename is empty then we can assume the specified BANK does
    // not exist
    if (file_path.empty()) {
        // Log this error and return without any further processing
        MSG("The patch file could not be opened as the BANK does not exist");
        NINA_LOG_ERROR(module(), "The patch file could not be opened as the BANK does not exist");
        return false;
    }

    // Open the param map file
    bool ret = _open_json_file(file_path, schema, json_doc, false);
    if (!ret) 
    {
        // The patch could not be opened - most likely as it doesn't exist
        MSG("The patch file could not be opened: " << file_path);
        NINA_LOG_INFO(module(), "The patch file could not be opened: {}", file_path);
        MSG("Using default patch settings for patch");
        NINA_LOG_INFO(module(), "Using default patch settings for patch");
        ret = _open_json_file(NINA_ROOT_FILE_PATH(DEFAULT_PATCH_FILE), schema, json_doc, false);
        if (!ret)
        {
            // This is a critical error
            MSG("An error occurred opening the default patch file: " << NINA_ROOT_FILE_PATH(DEFAULT_PATCH_FILE));
            NINA_LOG_CRITICAL(module(), "An error occurred opening the default patch file: {}", NINA_ROOT_FILE_PATH(DEFAULT_PATCH_FILE));
            return false;
        }
    }

    // If the JSON data is empty its an invalid file
    if (!json_doc.IsObject()) {
        // This is a critical error
        MSG("The patch file is an empty file");
        NINA_LOG_CRITICAL(module(), "The patch file is an empty file");        
        return false;        
    }

    // Does any Common patch data exist?
    if (!json_doc.HasMember("common"))
    {
        // No - add the Common patches as an empty array
        json_doc.AddMember("common", rapidjson::Value(rapidjson::kArrayType), json_doc.GetAllocator());
    }

    // The Common patch data must be an array or the patch is invalid
    if (!json_doc["common"].IsArray())
    {
        // Patch file is invalid
        ret = false;
    }

    // Does any State A patch data exist?
    if (!json_doc.HasMember("state_a"))
    {
        // No - add the State A patches as an empty array
        json_doc.AddMember("state_a", rapidjson::Value(rapidjson::kArrayType), json_doc.GetAllocator());
    }

    // The State A patch data must be an array or the patch is invalid
    if (!json_doc["state_a"].IsArray())
    {
        // Patch file is invalid
        ret = false;
    } 

    // Does any State B patch data exist?
    if (!json_doc.HasMember("state_b"))
    {
        // No - add the State B patches as an empty array
        json_doc.AddMember("state_b", rapidjson::Value(rapidjson::kArrayType), json_doc.GetAllocator());
    }

    // The State B patch data must be an array or the patch is invalid
    if (!json_doc["state_b"].IsArray())
    {
        // Patch file is invalid
        ret = false;
    }

    // If an error occurred log it
    if (!ret) {
        // This is a critical error
        MSG("The patch file format is invalid");
        NINA_LOG_CRITICAL(module(), "The patch file format is invalid");        
        return false;
    }        
    return ret;
}

//----------------------------------------------------------------------------
// _open_param_map_file
//----------------------------------------------------------------------------
bool FileManager::_open_param_map_file()
{
    const char *schema =
#include "../json_schemas/param_map_schema.json"
;
    
    // Open the param map file
    bool ret = _open_json_file(NINA_ROOT_FILE_PATH(PARAM_MAP_FILE), schema, _param_map_json_data);
    if (ret)
    {
        // If the JSON data is empty its an invalid file
        if (!_param_map_json_data.IsArray())
            ret = false;       
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_and_parse_param_blacklist_file
//----------------------------------------------------------------------------
void FileManager::_open_and_parse_param_blacklist_file()
{
    const char *schema =
#include "../json_schemas/param_blacklist_schema.json"
;
    rapidjson::Document json_data;
    
    // Open the param blacklist file (don't create it if it doesn't exist)
    if (_open_json_file(NINA_ROOT_FILE_PATH(PARAM_BLACKLIST_FILE), schema, json_data, false))
    {
        // If the JSON data is empty its an invalid file
        if (json_data.IsArray())
        {
            // Iterate through the blacklisted params
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                // Is the blacklist param entry valid?
                if (itr->GetObject().HasMember("param") && itr->GetObject()["param"].IsString())
                {
                    // Blacklist this param
                    utils::blacklist_param(itr->GetObject()["param"].GetString());
                }
            }
        }
    }
}

//----------------------------------------------------------------------------
// _open_and_parse_system_colours_file
//----------------------------------------------------------------------------
void FileManager::_open_and_parse_system_colours_file()
{
    const char *schema =
#include "../json_schemas/system_colours_schema.json"
;
    rapidjson::Document json_data;
    
    // Open the system colours file (don't create it if it doesn't exist)
    if (_open_json_file(NINA_ROOT_FILE_PATH(SYSTEM_COLOURS_FILE), schema, json_data, false))
    {
        // If the JSON data is empty its an invalid file
        if (json_data.IsArray())
        {
            std::vector<std::string> system_colour_names;

            // Iterate through the system colours
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                // Is the blacklist param entry valid?
                if ((itr->GetObject().HasMember("name") && itr->GetObject()["name"].IsString()) &&
                    (itr->GetObject().HasMember("colour") && itr->GetObject()["colour"].IsString()))
                {
                    utils::SystemColour system_colour;

                    // Get the system colour and add it
                    system_colour.name = itr->GetObject()["name"].GetString();
                    system_colour.colour = itr->GetObject()["colour"].GetString();
                    utils::add_system_colour(system_colour);
                    system_colour_names.push_back(system_colour.name);
                }
            }

            // Is the current system colour any of these colours?
            if (utils::get_system_colour_from_colour(utils::system_config()->get_system_colour()) == nullptr) {
                // It doesn't exist, so add it as a custom colour
                utils::SystemColour system_colour;
                system_colour.name = "Custom Colour";
                system_colour.colour = utils::system_config()->get_system_colour();
                utils::add_system_colour(system_colour);
                system_colour_names.push_back(system_colour.name);                
            }

            // Setup the common System Colour param
            auto param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::SYSTEM_COLOUR);
            if (param) {
                // Add the display strings and select the current system colour
                param->display_strings = system_colour_names;
                param->set_multi_position_param_num_positions(system_colour_names.size());
                param->set_value_from_position(utils::get_system_colour_index(utils::system_config()->get_system_colour()), true);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _open_and_parse_param_aliases_file
//----------------------------------------------------------------------------
void FileManager::_open_and_parse_param_aliases_file()
{
    const char *schema =
#include "../json_schemas/param_aliases_schema.json"
;
    rapidjson::Document json_data;
    
    // Open the param aliases file (don't create it if it doesn't exist)
    if (_open_json_file(NINA_ROOT_FILE_PATH(PARAM_ALIASES_FILE), schema, json_data, false))
    {
        // If the JSON data is empty its an invalid file
        if (json_data.IsArray())
        {
            // Iterate through the aliases params
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                Param *param = nullptr;
                std::string alias_path = "";

                // Has a param to alias been specified?
                if (itr->GetObject().HasMember("param") && itr->GetObject()["param"].IsString())
                {
                    // Get this param
                    param = utils::get_param(itr->GetObject()["param"].GetString());
                }

                // Has an alias been specified?
                if (itr->GetObject().HasMember("alias") && itr->GetObject()["alias"].IsString())
                {
                    // Get the alias path
                    alias_path = itr->GetObject()["alias"].GetString();
                }

                // Alias specified correctly?
                if (param && (alias_path.size() > 0)) {
                    // Create the alias param
                    // Copy the parameter and register it
                    std::unique_ptr<ParamAlias> alias_param = std::make_unique<ParamAlias>(alias_path, param);
                    alias_param->clear_mapped_params();
                    utils::register_param(std::move(alias_param));

                    // Map the alias to the param and vice-versa
                    auto ap = utils::get_param(alias_path);
                    if (ap) {
                        param->add_mapped_param(ap);
                        ap->add_mapped_param(param);
                    }                                
                }                
            }
        }
    }
}

#ifdef INCLUDE_PATCH_HISTORY
//----------------------------------------------------------------------------
// _open_patch_history_file
//----------------------------------------------------------------------------
bool FileManager::_open_patch_history_file()
{
    const char *schema =
#include "../json_schemas/patch_history_schema.json"
;
    
    // Open the patch history file
    if (_open_json_file(NINA_UDATA_FILE_PATH(PATCH_HISTORY_FILE), schema, _patch_history_json_data, true))
    {
        // If the JSON data is empty (not an object), make it one!
        if (!_patch_history_json_data.IsObject())
            _patch_history_json_data.SetObject();
        
        // Has the session UUID been specified?
        if (_patch_history_json_data.HasMember("session_uuid") && _patch_history_json_data["session_uuid"].IsString())
        {
            // Does this session UUID match the current?
            if (_patch_history_json_data["session_uuid"].GetString() != utils::get_session_uuid())
            {
                // New session - update the session UUID
                _patch_history_json_data["session_uuid"].SetString(utils::get_session_uuid(), _patch_history_json_data.GetAllocator());

                // Does any session history exist?
                if (_patch_history_json_data.HasMember("session_history") && _patch_history_json_data["session_history"].IsArray())
                {
                    // Get the session history and erase it
                    auto session_history = _patch_history_json_data["session_history"].GetArray();
                    if (session_history.Size() > 0)
                        session_history.Erase(session_history.Begin(), session_history.End());
                }
            }
        }
        else
        {
            // Add the session UUID
            _patch_history_json_data.AddMember("session_uuid", utils::get_session_uuid(), _patch_history_json_data.GetAllocator());
        }      
        
        // Does the session history exist?
        if (!_patch_history_json_data.HasMember("session_history"))
        {
            // Add the session history as an empty array
            _patch_history_json_data.AddMember("session_history", rapidjson::Value(rapidjson::kArrayType), _patch_history_json_data.GetAllocator());
        }
        return true;
    }
    return false;
}
#endif

//----------------------------------------------------------------------------
// _open_and_parse_haptic_modes_file
//----------------------------------------------------------------------------
bool FileManager::_open_and_parse_haptic_modes_file()
{
    const char *schema =
#include "../json_schemas/haptic_modes_schema.json"
;
    rapidjson::Document json_data;

    // Open the haptic modes file
    bool ret = _open_json_file(NINA_ROOT_FILE_PATH(HAPTIC_MODES_FILE), schema, json_data, false);
    if (ret)
    {
        // Initialise the haptic modes
        utils::init_haptic_modes();

        // If the JSON data is empty its an invalid file
        if (!json_data.IsObject())
            return false;

        // Get the default knob and switch haptic modes - these must always be present (schema checks this)
        auto default_knob_haptic_mode = json_data["default_knob_haptic_mode"].GetString();
        auto default_switch_haptic_mode = json_data["default_switch_haptic_mode"].GetString();

        // Have any haptic modes been specified?
        if (json_data.HasMember("haptic_modes") && json_data["haptic_modes"].IsArray())
        {
            // Parse each haptic mode
            auto haptic_modes = json_data["haptic_modes"].GetArray();
            for (auto& mode : haptic_modes)
            {
                // Is the haptic mode entry valid?
                if ((mode.HasMember("control_type") && mode["control_type"].IsString()) &&
                    (mode.HasMember("name") && mode["name"].IsString()))
                {
                    auto haptic_mode = HapticMode();
                    const char *control_type = mode["control_type"].GetString();
                    const char *name = mode["name"].GetString();

                    // Get the control type
                    auto type = SurfaceControl::ControlTypeFromString(control_type);
                    haptic_mode.type = type;
                    haptic_mode.name = name;

                    // Is this a knob?
                    if (type == SurfaceControlType::KNOB)
                    {
                        // Has the knob physical start pos been specified?
                        if (mode.HasMember("knob_start_pos") && mode["knob_start_pos"].IsUint())
                        {
                            // Get the knob physical start pos and check it is valid
                            auto start_pos = mode["knob_start_pos"].GetUint();
                            if (start_pos <= 360)
                            {
                                // Set the knob physical start pos in the control mode
                                haptic_mode.knob_start_pos = start_pos;
                                haptic_mode.knob_actual_start_pos = start_pos;
                            }
                        }

                        // Has the knob physical width been specified?
                        if (mode.HasMember("knob_width") && mode["knob_width"].IsUint())
                        {
                            // Get the knob physical width and check it is valid
                            auto width = mode["knob_width"].GetUint();
                            if (width <= 360)
                            {
                                // Set the knob physical width in the control mode
                                haptic_mode.knob_width = width;
                                haptic_mode.knob_actual_width = width;
                            }
                        }

                        // Has the knob actual start pos been specified?
                        if (mode.HasMember("knob_actual_start_pos") && mode["knob_actual_start_pos"].IsUint())
                        {
                            // Get the knob actual start pos and check it is valid
                            auto start_pos = mode["knob_actual_start_pos"].GetUint();
                            if (start_pos <= 360)
                            {
                                // Set the knob actual start pos in the control mode
                                haptic_mode.knob_actual_start_pos = start_pos;
                            }
                        }

                        // Has the knob actual width been specified?
                        if (mode.HasMember("knob_actual_width") && mode["knob_actual_width"].IsUint())
                        {
                            // Get the knob actual width and check it is valid
                            auto width = mode["knob_actual_width"].GetUint();
                            if (width <= 360)
                            {
                                // Set the knob actual width in the control mode
                                haptic_mode.knob_actual_width = width;
                            }
                        }

                        // Have the number of detents been specified?
                        if (mode.HasMember("knob_num_detents") && mode["knob_num_detents"].IsUint())
                        {
                            // Set the knob number of detents in the control mode
                            haptic_mode.knob_num_detents = mode["knob_num_detents"].GetUint();
                        }

                        // Has the friction been specified?
                        if (mode.HasMember("knob_friction") && mode["knob_friction"].IsUint())
                        {
                            // Set the knob friction in the control mode
                            haptic_mode.knob_friction = mode["knob_friction"].GetUint();
                        }

                        // Has the detent strength been specified?
                        if (mode.HasMember("knob_detent_strength") && mode["knob_detent_strength"].IsUint())
                        {
                            // Set the knob detent strength in the control mode
                            haptic_mode.knob_detent_strength = mode["knob_detent_strength"].GetUint();
                        }

                        // Have the indents been specified?
                        if (mode.HasMember("knob_indents") && mode["knob_indents"].IsArray())
                        {
                            // Parse each indent
                            auto indents_array = mode["knob_indents"].GetArray();
                            for (auto& indent : indents_array)
                            {
                                // Add the indent
                                if ((indent.HasMember("angle") && indent["angle"].IsUint()) && 
                                    (indent.HasMember("hw_active") && indent["hw_active"].IsBool()))
                                {
                                    // Check the angle is valid
                                    uint angle = indent["angle"].GetUint();
                                    if (angle <= 360)
                                    {
                                        // Convert the indent to a hardware value and push the indent
                                        std::pair<bool, uint> knob_indent;
                                        knob_indent.first = indent["hw_active"].GetBool();
                                        knob_indent.second = (angle / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
                                        haptic_mode.knob_indents.push_back(knob_indent);
                                    }
                                }
                            }
                        }
                    }
                    // Is this a switch?
                    else if (type == SurfaceControlType::SWITCH)
                    {
                        // Has the switch mode been specified?
                        if (mode.HasMember("switch_mode") && mode["switch_mode"].IsUint())
                        {
                            // Get the switch mode and set in the control
                            auto switch_mode = mode["switch_mode"].GetUint();
                            if (switch_mode <= uint(SwitchMode::LATCH_PUSH))
                            {
                                // Set the switch mode
                                haptic_mode.switch_mode = static_cast<SwitchMode>(switch_mode);
                            }
                        }
                    }

                    // Add the haptic mode
                    utils::add_haptic_mode(haptic_mode);
                }
            }
        }

        // Set the default knob and switch haptic modes
        if (!utils::set_default_haptic_mode(SurfaceControlType::KNOB, default_knob_haptic_mode))
        {
            // Default mode does not exist
            MSG("The default knob haptic mode " << default_knob_haptic_mode << " does not exist, created a default");
            NINA_LOG_WARNING(module(), "The default knob haptic mode {} does not exist, created a default", default_knob_haptic_mode);          
        }
        if (!utils::set_default_haptic_mode(SurfaceControlType::SWITCH, default_switch_haptic_mode))
        {
            // Default mode does not exist
            MSG("The default switch haptic mode " << default_switch_haptic_mode << " does not exist, created a default");
            NINA_LOG_WARNING(module(), "The default switch haptic mode {} does not exist, created a default", default_switch_haptic_mode);          
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _open_json_file
//----------------------------------------------------------------------------
bool FileManager::_open_json_file(std::string file_path, const char *schema, rapidjson::Document &json_data, bool create, std::string def_contents)
{
    rapidjson::Document schema_data;  
    
    // Open the JSON file
    std::fstream json_file;
    json_data.SetNull();
    json_data.GetAllocator().Clear();    
    json_file.open(file_path, std::fstream::in);
    if (!json_file.good())
    {
        // Couldn't open the file, should we create it?
        if (!create)
        {
            DEBUG_BASEMGR_MSG("JSON file open error: " << file_path);
            return false;
        }            
            
        // Try and create the file
        json_file.open(file_path, std::fstream::out);
        if (!json_file.good())
        {
            DEBUG_BASEMGR_MSG("JSON file create error: " << file_path);
            return false;
        }

        // File is now created, but empty
        // We can close it and clear the JSON data structure
        json_data.Parse(def_contents);
        json_file.close();
        return true;
    }

    // The file is open, check if it is empty
    std::string json_file_contents((std::istreambuf_iterator<char>(json_file)), std::istreambuf_iterator<char>());
    if (json_file_contents.empty())
    {
        // Its empty, so just clear the JSON data and return
        json_data.Parse(def_contents);
        json_file.close();
        return true;
    }

    // Get the JSON file contents and ensure there are no JSON errors
    json_data.Parse(json_file_contents.c_str());
    if (json_data.HasParseError())
    {
        DEBUG_BASEMGR_MSG("JSON file read error: " << file_path);
        json_data.Parse(def_contents);
        json_file.close();
        return false;
    }

    // Get the JSON schema and ensure there are no schema errors
    schema_data.Parse(schema);
    if (schema_data.HasParseError())
    {
        DEBUG_BASEMGR_MSG("JSON schema error: " << file_path);
        json_data.Parse(def_contents);
        json_file.close();
        return false;
    }

    // Now validate the JSON data against the passed schema
    rapidjson::SchemaDocument schema_document(schema_data);
    rapidjson::SchemaValidator schema_validator(schema_document);
    if (!json_data.Accept(schema_validator))
    {
        DEBUG_BASEMGR_MSG("Schema validation failed: " << file_path);
        json_data.Parse(def_contents);
        json_file.close();
        return false;
    }

    // JSON file OK, JSON data read OK
    json_file.close();
    return true;
}

//----------------------------------------------------------------------------
// _parse_config
//----------------------------------------------------------------------------
void FileManager::_parse_config()
{
    bool save_config_file = false;
    uint layers_num;

    // Is the layers num present?
    if (_config_json_data.HasMember("layers_num") && _config_json_data["layers_num"].IsUint())
    {
        // Set the layers num
        layers_num = _config_json_data["layers_num"].GetUint();
    }
    else
    {
        // Set the default layers num
        layers_num = DEFAULT_LAYERS_NUM;

        // Create the layers num entry
        _config_json_data.AddMember("layers_num", DEFAULT_LAYERS_NUM, _config_json_data.GetAllocator());
        save_config_file = true;
    }
    utils::system_config()->set_layers_num(layers_num);
    
    // Has the first multi-function switch number been specified?
    if (_config_json_data.HasMember("first_multifn_switch_num") && _config_json_data["first_multifn_switch_num"].IsUint())
    {
        // Set the morph knob number
        utils::system_config()->set_first_multifn_switch_num(_config_json_data["first_multifn_switch_num"].GetUint());
    }

    // Has the modulation source number been specified?
    if (_config_json_data.HasMember("mod_src_num") && _config_json_data["mod_src_num"].IsUint())
    {
        // Set the modulation source number
        utils::system_config()->set_mod_src_num(_config_json_data["mod_src_num"].GetUint());
    }
    else
    {
        // Create the modulation source number entry
        _config_json_data.AddMember("mod_src_num", DEFAULT_MOD_SRC_NUM, _config_json_data.GetAllocator());
        save_config_file = true;
    }

    // Has the patch modified threshold been specified?
    if (_config_json_data.HasMember("patch_modified_threshold") && _config_json_data["patch_modified_threshold"].IsUint())
    {
        // Set the patch modified threshold
        utils::system_config()->set_patch_modified_threshold(_config_json_data["patch_modified_threshold"].GetUint());
    }
    else
    {
        // Create the patch modified threshold
        _config_json_data.AddMember("patch_modified_threshold", DEFAULT_PATCH_MODIFIED_THRESHOLD, _config_json_data.GetAllocator());
        utils::system_config()->set_patch_modified_threshold(DEFAULT_PATCH_MODIFIED_THRESHOLD);
        save_config_file = true;
    }

    // Has demo mode been specified?
    if (_config_json_data.HasMember("demo_mode") && _config_json_data["demo_mode"].IsBool())
    {
        // Set the demo mode
        utils::system_config()->set_demo_mode(_config_json_data["demo_mode"].GetBool());
    }
    else
    {
        // Create the demo mode
        _config_json_data.AddMember("demo_mode", false, _config_json_data.GetAllocator());
        utils::system_config()->set_demo_mode(false);
        save_config_file = true;
    }

    // Has the demo mode timeout been specified?
    if (_config_json_data.HasMember("demo_mode_timeout") && _config_json_data["demo_mode_timeout"].IsUint())
    {
        // Set the demo mode timeout threshold
        utils::system_config()->set_demo_mode_timeout(_config_json_data["demo_mode_timeout"].GetUint());
    }
    else
    {
        // Create the demo mode timeout threshold
        _config_json_data.AddMember("demo_mode_timeout", DEFAULT_DEMO_MODE_TIMEOUT, _config_json_data.GetAllocator());
        utils::system_config()->set_demo_mode_timeout(DEFAULT_DEMO_MODE_TIMEOUT);
        save_config_file = true;
    }

    // Has the system colour been specified?
    std::string system_colour;
    if (_config_json_data.HasMember("system_colour") && _config_json_data["system_colour"].IsString())
    {
        // Get the specified system colour
        system_colour = _config_json_data["system_colour"].GetString();
    }
    else {
        // Create the system colour
        system_colour = DEFAULT_SYSTEM_COLOUR;
        _config_json_data.AddMember("system_colour", DEFAULT_SYSTEM_COLOUR, _config_json_data.GetAllocator());
        save_config_file = true;
    }

    // Try and convert the system colour to an integer and set the system colour
    try {
        [[maybe_unused]] auto colour = std::stoi(system_colour, nullptr, 16);
    }
    catch (...) {
        system_colour = DEFAULT_SYSTEM_COLOUR;
    }
    utils::system_config()->set_system_colour(system_colour);

    // Get the OSC config values, if present
    if ((_config_json_data.HasMember("osc_host_ip") && _config_json_data["osc_host_ip"].IsString()) &&
        (_config_json_data.HasMember("osc_incoming_port") && _config_json_data["osc_incoming_port"].IsString()) &&
        (_config_json_data.HasMember("osc_outgoing_port") && _config_json_data["osc_outgoing_port"].IsString()))
    {
        // Initialise the OSC config
        utils::system_config()->init_osc_config(_config_json_data["osc_host_ip"].GetString(),
                                                    _config_json_data["osc_incoming_port"].GetString(),
                                                    _config_json_data["osc_outgoing_port"].GetString());
        
        // Has an OSC send count been specified?
        if (_config_json_data.HasMember("osc_send_count") && _config_json_data["osc_send_count"].IsUint())
        {
            // Set the OSC send count
            utils::system_config()->set_osc_send_count(_config_json_data["osc_send_count"].GetUint());
        }                                             
    }
    else
    {
        // Set OSC config defaults to empty strings
        _config_json_data.AddMember("osc_host_ip", "", _config_json_data.GetAllocator());
        _config_json_data.AddMember("osc_incoming_port", "", _config_json_data.GetAllocator());
        _config_json_data.AddMember("osc_outgoing_port", "", _config_json_data.GetAllocator());
        save_config_file = true;        
    }

    // Does the config file need saving?
    if (save_config_file)
        _save_config_file();
}

//----------------------------------------------------------------------------
// _parse_param_map
//----------------------------------------------------------------------------
void FileManager::_parse_param_map()
{
    // Iterate through the param map
    for (rapidjson::Value::ValueIterator itr = _param_map_json_data.Begin(); itr != _param_map_json_data.End(); ++itr)
    {
        // Is the param mapping entry valid?
        if ((itr->GetObject().HasMember("param_1") && itr->GetObject()["param_1"].IsString()) &&
            (itr->GetObject().HasMember("param_2") && itr->GetObject()["param_2"].IsString()))
        {
            std::string param_1_path = itr->GetObject()["param_1"].GetString();
            std::string param_2_path = itr->GetObject()["param_2"].GetString();
            auto param_1 = utils::get_param(param_1_path);
            auto param_2 = utils::get_param(param_2_path);
            Param *linked_param = nullptr;
            std::string state = "default";

            // If the param 1 does not exist, check if it is a MIDI CC or pitchbend param
            if (!param_1)
            {
                // Is this a MIDI CC, Pitch Bend, or Chanpress param?
                if (MidiDeviceManager::IsMidiCcParamPath(param_1_path) ||
                    MidiDeviceManager::IsMidiPitchBendParamPath(param_1_path) ||
                    MidiDeviceManager::IsMidiChanpressParamPath(param_1_path))
                {
                    // Create the source param (dummy param as it doesn't do anything, just allows
                    // the mapping)
                    auto param = DummyParam::CreateParam(param_1_path);
                    param->module = NinaModule::MIDI_DEVICE;
                    if (MidiDeviceManager::IsMidiPitchBendParamPath(param_1_path) ||
                        MidiDeviceManager::IsMidiChanpressParamPath(param_1_path))
                        param->patch_param = false;
                    utils::register_param(std::move(param));
                    param_1 = utils::get_param(param_1_path);                    
                }
                else
                {                
                    // Is this a state change?
                    int pos = param_1_path.find(STATE_PARAM_PATH_PREFIX);
                    if (pos != -1)
                    {
                        // Get the change state string
                        pos = pos + std::strlen(STATE_PARAM_PATH_PREFIX);
                        auto state_change = param_1_path.substr(pos, (param_1_path.size() - pos));

                        // Create a state change param
                        auto state_param_dst = Param::CreateParam(state_change);
                        state_param_dst->type = ParamType::UI_STATE_CHANGE;
                        state_param_dst->state = state_change;
                        state_param_dst->set_path(param_1_path);
                        state_param_dst->patch_param = false;
                        state_param_dst->name = state_change;
                        utils::register_param(std::move(state_param_dst));
                        param_1 = utils::get_param(param_1_path);
                    }
                }                
            }

            // If the param 2 does not exist, check to see if the mapping is to MIDI or
            // a state change
            if (!param_2)
            {
                // Is this a MIDI CC, Pitch Bend, or Chanpress param?
                if (MidiDeviceManager::IsMidiCcParamPath(param_2_path) ||
                    MidiDeviceManager::IsMidiPitchBendParamPath(param_2_path) ||
                    MidiDeviceManager::IsMidiChanpressParamPath(param_2_path))
                {
                    // Create the source param (dummy param as it doesn't do anything, just allows
                    // the mapping)
                    auto param = DummyParam::CreateParam(param_2_path);
                    param->module = NinaModule::MIDI_DEVICE;
                    if (MidiDeviceManager::IsMidiPitchBendParamPath(param_2_path) |
                        MidiDeviceManager::IsMidiChanpressParamPath(param_2_path))
                        param->patch_param = false;                    
                    utils::register_param(std::move(param));
                    param_2 = utils::get_param(param_2_path);                    
                }
                else
                {                
                    // Is this a state change?
                    int pos = param_2_path.find(STATE_PARAM_PATH_PREFIX);
                    if (pos != -1)
                    {
                        // Get the change state string
                        pos = pos + std::strlen(STATE_PARAM_PATH_PREFIX);
                        auto state_change = param_2_path.substr(pos, (param_2_path.size() - pos));

                        // Create a state change param
                        auto state_param_dst = Param::CreateParam(state_change);
                        state_param_dst->type = ParamType::UI_STATE_CHANGE;
                        state_param_dst->state = state_change;
                        state_param_dst->set_path(param_2_path);
                        state_param_dst->patch_param = false;
                        state_param_dst->name = state_change;
                        utils::register_param(std::move(state_param_dst));
                        param_2 = utils::get_param(param_2_path);
                    }
                }
            }

            // Do both the source and destination params exist?
            // Don't bother processing any further if they don't
            if (param_1 && param_2)
            {
                // Does this mapping have a state specified?
                if ((itr->GetObject().HasMember("ui_state") && itr->GetObject()["ui_state"].IsString()))
                {    
                    // Get the state
                    state = itr->GetObject()["ui_state"].GetString();

                    // If the specified state is not default
                    if (state != "default")
                    {            
                        // If either param 1 or 2 are a surface control param, we need to duplicate the param entry
                        // for this state
                        // This means we will have multiple entries the param list for each state of the param
                        if (param_1->physical_control_param)
                        {
                            // Copy the parameter, set the new state, and register it
                            std::unique_ptr<Param> new_param_1 = param_1->clone();
                            new_param_1->state = state;
                            new_param_1->clear_mapped_params();
                            utils::register_param(std::move(new_param_1));
                            param_1 = utils::get_param(param_1_path, state);
                            if (!param_1)
                            {
                                // This should never happen but continue nonetheless
                                continue;
                            }
                        }
                        else if (param_2->physical_control_param)
                        {
                            // Copy the parameter, set the new state, and register it
                            std::unique_ptr<Param> new_param_2 = param_2->clone();
                            new_param_2->state = state;
                            new_param_2->clear_mapped_params();
                            utils::register_param(std::move(new_param_2));
                            param_2 = utils::get_param(param_2_path, state);
                            if (!param_2)
                            {
                                // This should never happen but continue nonetheless
                                continue;
                            }
                        }                                       
                    }
                }

                // Does this mapping have a linked param?
                if ((itr->GetObject().HasMember("linked_param") && itr->GetObject()["linked_param"].IsString()))
                {
                    // Get the linked param
                    linked_param = utils::get_param(itr->GetObject()["linked_param"].GetString());                 
                }

                // Does this mapping have a haptic mode specified?
                // The mode is applicable if the source or destination is a hardware param
                if ((itr->GetObject().HasMember("haptic_mode") && itr->GetObject()["haptic_mode"].IsString()))
                {
                    // Get the mode
                    auto mode = itr->GetObject()["haptic_mode"].GetString();

                    // Is the param 1 a hardware param?
                    if (param_1->physical_control_param)
                    {
                        // Set the mode
                        static_cast<SurfaceControlParam *>(param_1)->set_haptic_mode(mode);
                    }
                    // Is the param 2 a hardware param?
                    else if (param_2->physical_control_param)
                    {
                        // Set the mode
                        static_cast<SurfaceControlParam *>(param_2)->set_haptic_mode(mode);
                    }                    
                }

                // If either param is a multi-function switch, set the param attribute
                if (param_1->get_path() == "/sys/multifn_switch")
                    param_2->multifn_switch = true;
                else if (param_2->get_path() == "/sys/multifn_switch")
                    param_1->multifn_switch = true;

                // Subscribe the mapping
                // Note: If either param does not exist, this function does nothing
                param_1->add_mapped_param(param_2);
                param_2->add_mapped_param(param_1);
                param_1->set_linked_param(linked_param);
                param_2->set_linked_param(linked_param);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _parse_patch
//----------------------------------------------------------------------------
void FileManager::_parse_patch(uint layer_num, bool set_layer_params)
{
    bool current_layer = false;

    // Should we process this patch as the current layer?
    // If: We are not setting the Layer params and this is the currently selected Layer OR
    //     We are setting the Layer params and this is Layer 1
    if ((!set_layer_params && utils::is_current_layer(layer_num)) ||
        (set_layer_params && (layer_num == 0))) {
        current_layer = true;
    }

    // Get the patch params and parse them
    auto params = utils::get_patch_params();

    // When loading patches, the param states and params that map to a state
    // change are reset (only do this for the current layer)
    if (current_layer) {
        utils::reset_param_states();
    }

    // Process the patch common params
    _parse_patch_common_params(layer_num, current_layer, params);

    // When parsing a patch we always load State A as the default
    // However, there is one corner case - if the Morph is at 1.0 then
    // we need to load State B as the default
    // Morph at 0.0 is simply State A, and any other morph value is
    // handled once loaded and morph processing kicks off
    // Note: Morph 0.0/1.0 are morph "off" states and no moprh processing
    // takes place
    auto alt_state = PatchState::STATE_B;
    auto default_state = PatchState::STATE_A;
    if (_morph_value_param && (_morph_value_param->get_value() == 1.0)) {
        // Process State B as the default
        alt_state = PatchState::STATE_A;
        default_state = PatchState::STATE_B;
    }

    // Process the patch alternate (not default) state params
    _parse_patch_state_params(current_layer, params, alt_state);

    // Set the patch alternate (not default) params in the DAW
    _daw_manager->set_patch_params(layer_num, false, alt_state);

    // Process the patch default state params
    _parse_patch_state_params(current_layer, params, default_state);

    // Set the patch default (common + state) params in the DAW
    _daw_manager->set_patch_params(layer_num, true, default_state);

    // Should we process the layer params?
    if (set_layer_params) {
        // Process the patch layer params
        _parse_patch_layer_params(params);

        // Set the patch layer params in the DAW
        _daw_manager->set_patch_layer_params(layer_num);

        // Get the Layer MIDI Channel Filter and set in the layer info structure
        auto param = utils::get_param_from_ref(utils::ParamRef::MIDI_CHANNEL_FILTER);
        if (param) {
            // Set the MIDI channel filter
            utils::get_layer_info(layer_num).set_midi_channel_filter(param->get_position_value());
        }
    }
}

//----------------------------------------------------------------------------
// _parse_patch_layer_params
//----------------------------------------------------------------------------
void FileManager::_parse_patch_layer_params(std::vector<Param *> &params)
{
    // Parse the patch params
    for (Param *p : params)
    {
        // Is this a layer patch param?
        if (p->patch_layer_param)
        {
            bool param_missed = true;

            // Is this a common Layer param?
            rapidjson::Value *json_data;
            if (p->patch_common_layer_param) {
                // Check if there is a patch for this param
                json_data = &_get_patch_common_layer_json_data();
                for (rapidjson::Value::ValueIterator itr = json_data->Begin(); itr != json_data->End(); ++itr)
                {
                    // Does this entry match the param?
                    if (itr->GetObject()["path"].GetString() == p->get_path())
                    {
                        // Update the parameter value
                        p->set_value(itr->GetObject()["value"].GetFloat());
                        param_missed = false;
                        break;
                    }
                }                
            }
            else {
                // Check if there is a patch for this param
                json_data = &_get_patch_layer_json_data();
                for (rapidjson::Value::ValueIterator itr = json_data->Begin(); itr != json_data->End(); ++itr)
                {
                    // Does this entry match the param?
                    if (itr->GetObject()["path"].GetString() == p->get_path())
                    {
                        // Update the parameter value
                        p->set_value(itr->GetObject()["value"].GetFloat());
                        param_missed = false;
                        break;
                    }
                }
            }
            if (param_missed)
            {
                // Param is not specified in the patch file
                // We need to add it to the patch
                rapidjson::Value obj;
                obj.SetObject();
                obj.AddMember("path", std::string(p->get_path()), _layers_json_data.GetAllocator());
                obj.AddMember("value", p->get_value(), _layers_json_data.GetAllocator());
                json_data->PushBack(obj, _layers_json_data.GetAllocator());
            }
        }
    }
}

//----------------------------------------------------------------------------
// _parse_patch_common_params
//----------------------------------------------------------------------------
void FileManager::_parse_patch_common_params(uint layer_num, bool current_layer, std::vector<Param *> &params)
{
    // Parse the patch params
    for (Param *p : params)
    {
        // Is this a common patch param AND it is either not for Layer 1 only, or is for Layer 1 only
        // and we are parsing the params for Layer 1
        if (!p->patch_state_param && !p->patch_layer_param && (!p->layer_1_param || (p->layer_1_param && (layer_num == 0))))
        {
            bool param_missed = true;

            // Check if there is a patch for this param
            rapidjson::Value& json_data = _get_patch_common_json_data();
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->get_path())
                {
                    // Update the parameter value
                    if (p->str_param) {
                        p->set_str_value(itr->GetObject()["str_value"].GetString());
                    }
                    else {
                        p->set_value(itr->GetObject()["value"].GetFloat());

                        // Special case handling if this is the current layer
                        if (current_layer) {
                            // Special case (yet another one) for Arpeggiator enable (a common patch param)
                            // The Arpeggiator enable is not mapped to a surface control, instead the ARP system function is
                            // However, we need to make sure the surface control is set to the Arpeggiator enable value so that it
                            // shows the correct state when the patch is loaded
                            if ((p->module == NinaModule::ARPEGGIATOR) && (p->param_id == ArpeggiatorParamId::ARP_ENABLE_PARAM_ID)) {
                                // We can assume that the ARP system function is mapped to a physical switch
                                // Hence we need to get the ARP system function and it's (one and only) mapped param which
                                // is the switch
                                auto param = utils::get_sys_func_param(SystemFuncType::ARP);
                                if (param) {
                                    // Get the mapped params for the ARP system function (there should only be one, 
                                    // mapped to a surface control)
                                    auto mapped_params = param->get_mapped_params();
                                    if ((mapped_params.size() == 1) && (mapped_params.at(0)->module == NinaModule::SURFACE_CONTROL)) {
                                        // Set the surface control value to the actual ARP enable parameter value
                                        mapped_params.at(0)->set_value_from_param(*p);
                                    }
                                }
                            }
                        }
                    }
                    param_missed = false;
                    break;
                }
            }
            if (param_missed)
            {
                // Param is not specified in the patch file
                // We need to add it to the patch
                // Before we do this, check if it is the LFO 1/2 Tempo Sync param - this has been changed to be
                // a common patch param. In this case search for the (now old) State A param and set it to that value
                if ((p == utils::get_lfo_1_tempo_sync_param()) || (p == utils::get_lfo_2_tempo_sync_param())) {
                    // Find the State A param
                    rapidjson::Value& state_a_json_data = _get_patch_state_a_json_data();
                    for (rapidjson::Value::ValueIterator itr = state_a_json_data.Begin(); itr != state_a_json_data.End(); ++itr)
                    {
                        // Does this entry match the param?
                        if (itr->GetObject()["path"].GetString() == p->get_path())
                        {
                            // Found, set the param value from the old State A param
                            p->set_value(itr->GetObject()["value"].GetFloat());
                            break;
                        }
                    }
                }
                // We also need to heck if the missing parameter is an Effects level
                // If so, we need to make sure the level is set to 1.0 if it is the currently
                // selected effect
                else if (p == utils::get_param_from_ref(utils::ParamRef::REVERB_LEVEL)) {
                    // Set the Reverb Level based on the current FX type
                    p->set_value((_get_fx_type() == FxType::REVERB) ? 1.0 : 0.0);
                }
                else if (p == utils::get_param_from_ref(utils::ParamRef::CHORUS_LEVEL)) {
                    // Set the Chorus Level based on the current FX type
                    p->set_value((_get_fx_type() == FxType::CHORUS) ? 1.0 : 0.0);
                }                    
                else if (p == utils::get_param_from_ref(utils::ParamRef::DELAY_LEVEL)) {
                    // Set the Delay Level based on the current FX type
                    p->set_value((_get_fx_type() == FxType::DELAY) ? 1.0 : 0.0);
                }
                else {
                    // Is this param specified in the INIT patch?
                    auto itr = _find_init_patch_param(p->get_path(), PatchState::STATE_A);
                    if (itr) {
                        // Is this a string value param?
                        if (p->str_param) {
                            // Update the param value from the INIT patch
                            p->set_str_value(itr->GetObject()["str_value"].GetString());
                        }
                        else {
                            // Update the param value from the INIT patch
                            p->set_value(itr->GetObject()["value"].GetFloat());
                        }
                    }
                }              

                // Add the missed param to the patch
                rapidjson::Value obj;
                obj.SetObject();
                obj.AddMember("path", std::string(p->get_path()), _patch_json_doc->GetAllocator());
                if (p->str_param) {
                    obj.AddMember("str_value", p->get_str_value(), _patch_json_doc->GetAllocator());
                }
                else {
                    obj.AddMember("value", p->get_value(), _patch_json_doc->GetAllocator());
                }
                json_data.PushBack(obj, _patch_json_doc->GetAllocator());
            }

            // Process the LFOs and mapped params if this is the current layer
            if (current_layer) {
                // We need to check for the special case of LFO 1 Tempo Sync if LFO 1 is selected
                if (p == utils::get_lfo_1_tempo_sync_param()) {
                    // Clean up the LFO states
                    utils::pop_all_lfo_states();

                    // If the required and current LFO states do not match
                    if (utils::get_req_lfo_1_state().state != utils::get_current_lfo_state().state) {
                        // If LFO 1 is in sync rate mode
                        if (utils::lfo_1_sync_rate()) {
                            auto lfo_state = utils::get_req_lfo_1_state();

                            // Push the LFO 1 sync rate state to the relevant controls
                            auto params = utils::get_params_with_state(lfo_state.state);
                            for (auto p: params) {
                                utils::push_param_state(p->get_path(), lfo_state.state);
                            }
                            utils::push_lfo_state(lfo_state);                     
                        }
                        else {
                            auto lfo_state = utils::get_current_lfo_state();

                            // Pop the current LFO state from the relevant controls
                            auto params = utils::get_params_with_state(lfo_state.state);
                            for (auto p: params) {
                                utils::pop_param_state(p->get_path(), lfo_state.state);
                            }
                            utils::pop_lfo_state();
                        }
                    }
                }

                // Process the mapped params
                _process_patch_mapped_params(p, nullptr);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _parse_patch_state_params
//----------------------------------------------------------------------------
void FileManager::_parse_patch_state_params(bool current_layer, std::vector<Param *> &params, PatchState state)
{
    // Parse the patch params
    for (Param *p : params)
    {
        // Is this a state patch param?
        if (p->patch_state_param)
        {
            bool param_missed = true;

            // Check if there is a patch for this param
            rapidjson::Value& json_data = (state == PatchState::STATE_A) ? _get_patch_state_a_json_data() :
                                          _get_patch_state_b_json_data();
            for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
            {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->get_path())
                {
                    // Update the parameter value
                    if (p->str_param) {
                        p->set_str_value(itr->GetObject()["str_value"].GetString());

                        // Special case handling for wavetables
                        if ((p->type == ParamType::COMMON_PARAM) && (p->param_id == CommonParamId::WT_NAME_PARAM_ID) &&
                            (p->get_str_value().size() > 0)) {
                            struct dirent **dirent = nullptr;
                            int num_files;
                            int file_pos = 0;
                            uint wt_file_count = 0;
                            
                            // Scan the Nina wavetable folder
                            num_files = ::scandir(NINA_WAVETABLES_DIR, &dirent, 0, ::versionsort);
                            if (num_files > 0) {
                                // Process each file in the folder
                                for (uint i=0; i<(uint)num_files; i++) {
                                    // If we've not found the max number of wavetables yet and this a normal file
                                    if ((wt_file_count < MAX_NUM_WAVETABLE_FILES) && (dirent[i]->d_type == DT_REG))
                                    {
                                        // If it has a WAV file extension
                                        auto name = std::string(dirent[i]->d_name);
                                        if (name.substr((name.size() - (sizeof(".wav") - 1))) == ".wav") {
                                            // Is this the specified wavetable?
                                            if (name.substr(0, (name.size() - (sizeof(".wav") - 1))) == 
                                                p->get_str_value()) {
                                                file_pos = wt_file_count;
                                            }
                                            wt_file_count++;
                                        }
                                    }
                                    ::free(dirent[i]);
                                }

                                // Get the WT Select param
                                auto param = utils::get_param("/daw/main/ninavst/WT_Select");
                                if (param) {  
                                    // Set the position value
                                    param->set_multi_position_param_num_positions(wt_file_count);
                                    param->set_value_from_position(file_pos);
                                }                                
                            }
                            if (dirent) {
                                ::free(dirent);
                            }
                        }
                    }
                    else {
                        p->set_value(itr->GetObject()["value"].GetFloat());
                    }
                    param_missed = false;
                    break;
                }
            }
            if (param_missed)
            {
                // If this is a mod matrix param, just set the value to 0.5
                // Do not add it to the patch at this stage
                if (p->mod_matrix_param) {
                    p->set_value(0.5);
                }
                else {
                    // Is this param specified in the INIT patch?
                    auto itr = _find_init_patch_param(p->get_path(), PatchState::STATE_A);
                    if (itr) {
                        // Is this a string value param?
                        if (p->str_param) {
                            // Update the param value from the INIT patch
                            p->set_str_value(itr->GetObject()["str_value"].GetString());
                        }
                        else {
                            // Update the param value from the INIT patch
                            p->set_value(itr->GetObject()["value"].GetFloat());
                        }                
                    }

                    // Param is not specified in the patch file
                    // We need to add it to the patch
                    rapidjson::Value obj;
                    obj.SetObject();
                    obj.AddMember("path", std::string(p->get_path()), _patch_json_doc->GetAllocator());
                    if (p->str_param) {
                        obj.AddMember("str_value", p->get_str_value(), _patch_json_doc->GetAllocator());
                    }
                    else {
                        obj.AddMember("value", p->get_value(), _patch_json_doc->GetAllocator());
                    }
                    json_data.PushBack(obj, _patch_json_doc->GetAllocator());
                }
            }

            // Process the mapped params if this is the current layer
            if (current_layer) {
                _process_patch_mapped_params(p, nullptr);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_patch_mapped_params
//----------------------------------------------------------------------------
void FileManager::_process_patch_mapped_params(const Param *param, const Param *skip_param)
{
    // Get the mapped params
    auto mapped_params = param->get_mapped_params();
    for (Param *mp : mapped_params)
    {
        // Because this function is recursive, we need to skip the param that
        // caused any recursion, so it is not processed twice
        if (skip_param && (mp == skip_param)) {
            continue;
        }

        // Set the mapped param?
        // Only process common and module params (except for the MIDI module)
        if (((mp->type == ParamType::COMMON_PARAM) || (mp->type == ParamType::MODULE_PARAM)) &&
            (mp->module != NinaModule::MIDI_DEVICE)) {
            // Set the source param value from this param
            mp->set_value_from_param(*param);
        }

        // We need to recurse each mapped param and process it
        // Note: We don't recurse system function params as they are a system action to be performed
        if (mp->type != ParamType::SYSTEM_FUNC)
            _process_patch_mapped_params(mp, param);        
    }    
}


//----------------------------------------------------------------------------
// _set_patch_state_b_params
//----------------------------------------------------------------------------
void FileManager::_set_patch_state_b_params(rapidjson::Document &from_patch_json_doc)
{
    // Get the patch params and parse them
    auto params = utils::get_patch_params();
    for (Param *p : params)
    {
        // Is this a state patch param?
        if (p->patch_state_param)
        {
            bool param_missed = true;

            // Get the the from patch State A params
            rapidjson::Value& from_patch_json_data = from_patch_json_doc["state_a"].GetArray();
            for (rapidjson::Value::ValueIterator itr = from_patch_json_data.Begin(); itr != from_patch_json_data.End(); ++itr)
            {
                // Does this entry match the param?
                if (itr->GetObject()["path"].GetString() == p->get_path())
                {                   
                    // Update the parameter value
                    if (p->str_param) {
                        p->set_str_value(itr->GetObject()["str_value"].GetString());
                    }
                    else {
                        p->set_value(itr->GetObject()["value"].GetFloat());
                    }

                    // Find the param in the patch State B data
                    rapidjson::Value& patch_json_data = _get_patch_state_b_json_data();            
                    for (rapidjson::Value::ValueIterator itr = patch_json_data.Begin(); itr != patch_json_data.End(); ++itr)
                    {
                        // Does this entry match the param?
                        if (itr->GetObject()["path"].GetString() == p->get_path())
                        {
                            // Update the parameter value in the patch data
                            if (p->str_param) {
                                itr->GetObject()["str_value"].SetString( p->get_str_value().c_str(), _patch_json_doc->GetAllocator());
                            }
                            else {
                                itr->GetObject()["value"].SetFloat(p->get_value());
                            }
                            param_missed = false;
                            break;                           
                        }
                    } 
                    if (param_missed) {
                        // Param is not specified in the patch file
                        // We need to add it to the patch data
                        rapidjson::Value obj;
                        obj.SetObject();
                        obj.AddMember("path", std::string(p->get_path()), _patch_json_doc->GetAllocator());
                        if (p->str_param) {
                            obj.AddMember("str_value", p->get_str_value(), _patch_json_doc->GetAllocator());
                        }
                        else {
                            obj.AddMember("value", p->get_value(), _patch_json_doc->GetAllocator());
                        }
                        patch_json_data.PushBack(obj, _patch_json_doc->GetAllocator());
                    }
                    break;
                }
            }

            // Get the mapped params
            auto mapped_params = p->get_mapped_params();
            for (Param *mp : mapped_params)
            {
                // Set the source param value from this param
                mp->set_value_from_param(*p);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _save_config_file
//----------------------------------------------------------------------------
void FileManager::_save_config_file()
{
    DEBUG_BASEMGR_MSG("_save_config_file");

    // Save the config file
    _save_json_file(NINA_UDATA_FILE_PATH(CONFIG_FILE), _config_json_data);
}

//----------------------------------------------------------------------------
// _save_current_layers_file
//----------------------------------------------------------------------------
void FileManager::_save_current_layers_file()
{
    // Save the current layers file
    _save_layers_file(NINA_UDATA_FILE_PATH(CURRENT_LAYERS_FILE));
}

//----------------------------------------------------------------------------
// _save_layers_file
//----------------------------------------------------------------------------
void FileManager::_save_layers_file(uint layers_num)
{
    DEBUG_BASEMGR_MSG("_save_layers_file");
    uint revision = 1;

    // When we save the actual layers file, make sure the version and
    // revision are updated

    // Does the version property exist?
    if (!_layers_json_data.HasMember("version"))
    {
        // No - add the version property
        _layers_json_data.AddMember("version", LAYERS_PRESET_VERSION, _layers_json_data.GetAllocator());
    }
    else
    {
        // Set the current version
        if (_layers_json_data.GetObject()["version"].IsString()) {
            _layers_json_data.GetObject()["version"].SetString(LAYERS_PRESET_VERSION);
        }
    }    

    // Does the revision property exist?
    if (!_layers_json_data.HasMember("revision"))
    {
        // No - add the revision property
        _layers_json_data.AddMember("revision", revision, _layers_json_data.GetAllocator());
    }
    else
    {
        // Get the current revision and increment it
        if (_layers_json_data.GetObject()["revision"].IsUint()) {
            revision = _layers_json_data.GetObject()["revision"].GetUint();
            _layers_json_data.GetObject()["revision"].SetUint(revision + 1);
        }
    }

    // Save the specified layers file
    _save_layers_file(_get_layers_filename(layers_num));
}

//----------------------------------------------------------------------------
// _save_layers_file
//----------------------------------------------------------------------------
void FileManager::_save_layers_file(std::string file_path)
{
    DEBUG_BASEMGR_MSG("_save_layers_file");

    // Save the specified layers file
    _save_json_file(file_path, _layers_json_data);
}

//----------------------------------------------------------------------------
// _save_global_params_file
//----------------------------------------------------------------------------
void FileManager::_save_global_params_file()
{
    DEBUG_BASEMGR_MSG("_save_global_params_file");

    // Save the global params file
    _save_json_file(NINA_UDATA_FILE_PATH(GLOBAL_PARAMS_FILE), _global_params_json_data);
}

//----------------------------------------------------------------------------
// _save_patch_file
//----------------------------------------------------------------------------
void FileManager::_save_patch_file(PatchId id)
{
    DEBUG_BASEMGR_MSG("_save_patch_file");
    uint revision = 1;

    // Does the version property exist?
    assert(_patch_json_doc);
    if (!_patch_json_doc->HasMember("version"))
    {
        // No - add the version property
        _patch_json_doc->AddMember("version", PATCH_PRESET_VERSION, _patch_json_doc->GetAllocator());
    }
    else
    {
        // Set the current version
        if (_patch_json_doc->GetObject()["version"].IsString()) {
            _patch_json_doc->GetObject()["version"].SetString(PATCH_PRESET_VERSION);
        }
    }    

    // Does the revision property exist?
    if (!_patch_json_doc->HasMember("revision"))
    {
        // No - add the revision property
        _patch_json_doc->AddMember("revision", revision, _patch_json_doc->GetAllocator());
    }
    else
    {
        // Get the current revision and increment it
        if (_patch_json_doc->GetObject()["revision"].IsUint()) {
            revision = _patch_json_doc->GetObject()["revision"].GetUint();
            _patch_json_doc->GetObject()["revision"].SetUint(revision + 1);
        }
    }

    // Save the patch file
    _save_json_file(_get_patch_filename(id), *_patch_json_doc);
}

//----------------------------------------------------------------------------
// _save_patch_file
//----------------------------------------------------------------------------
void FileManager::_save_patch_file()
{
    // Save the patch file
    _save_patch_file(utils::get_current_layer_info().get_patch_id());
}

#ifdef INCLUDE_PATCH_HISTORY
//----------------------------------------------------------------------------
// _save_patch_history_file
//----------------------------------------------------------------------------
void FileManager::_save_patch_history_file()
{
    DEBUG_BASEMGR_MSG("_save_patch_history_file");

    // Save the patch history file
    _save_json_file(NINA_UDATA_FILE_PATH(PATCH_HISTORY_FILE), _patch_history_json_data);
}
#endif

//----------------------------------------------------------------------------
// _save_json_file
//----------------------------------------------------------------------------
void FileManager::_save_json_file(std::string file_path, const rapidjson::Document &json_data)
{
    char write_buffer[131072];

    // Open the file for writing
    FILE *fp = ::fopen(file_path.c_str(), "w");
    if (fp == nullptr)
    {
        MSG("An error occurred (" << errno << ") writing the file: " << file_path);
        NINA_LOG_ERROR(module(), "An error occurred ({}) writing the file: {}", errno, file_path);
        return;
    }

    // Write the JSON data to the file
    // Note: If the data is larger than the write buffer then the Accept
    // function writes it in chunks
    rapidjson::FileWriteStream os(fp, write_buffer, sizeof(write_buffer));
    rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
    (void)json_data.Accept(writer);
    fclose(fp);
    sync();
}

//----------------------------------------------------------------------------
// _get_current_layer_json_data
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_get_current_layer_json_data()
{
    return _get_layer_json_data(utils::get_current_layer_info().layer_num());
}

//----------------------------------------------------------------------------
// _get_layer_json_data
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_get_layer_json_data(uint layer_num)
{
    // Ensure the layer number is valid
    if (layer_num < NUM_LAYERS) {
        for (rapidjson::Value::ValueIterator itr = _layers_json_data["layers"].Begin(); itr != _layers_json_data["layers"].End(); ++itr)
        {
            // Has the layer number been specified?
            if (itr->GetObject().HasMember("layer_num") && itr->GetObject()["layer_num"].IsUint())
            {
                // Is this the current layer?
                if ((layer_num + 1) == itr->GetObject()["layer_num"].GetUint()) {
                    // Return the iterator
                    return itr;
                }
            }                     
        }
    }
    return nullptr;    
}

//----------------------------------------------------------------------------
// _find_patch_param
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_find_patch_param(uint layer_num, std::string path, bool layer_1_param)
{
    // Search the common Layer json data first
    auto itr = _get_common_layer_obj(path.c_str());
    if (itr) {
        return itr;
    }

    // Not in the common Layer json data, search the Layer json data
    itr = _get_layer_obj(layer_num, path.c_str());
    if (itr) {
        return itr;
    }

    // Not in the Layer json data, search the Common json data
    rapidjson::Value& common_json_data = _get_layer_patch_common_json_data(layer_1_param ? 0 : layer_num);
    for (rapidjson::Value::ValueIterator itr = common_json_data.Begin(); itr != common_json_data.End(); ++itr) {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }

    // Not in the Layer or Common json data, search the State json data
    rapidjson::Value& state_json_data = (utils::get_layer_info(layer_num).get_patch_state() == PatchState::STATE_A) ? 
            _get_layer_patch_state_a_json_data(layer_num) :
            _get_layer_patch_state_b_json_data(layer_num);
    for (rapidjson::Value::ValueIterator itr = state_json_data.Begin(); itr != state_json_data.End(); ++itr) {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }
    return nullptr;    
}

//----------------------------------------------------------------------------
// _find_patch_param
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_find_patch_param(std::string path, bool layer_1_param)
{
    // Search the common Layer json data first
    rapidjson::Value& common_layer_json_data = _get_patch_common_layer_json_data();
    for (rapidjson::Value::ValueIterator itr = common_layer_json_data.Begin(); itr != common_layer_json_data.End(); ++itr)
    {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }

    // Not in the common Layer json data, search the Layer json data
    rapidjson::Value& layer_json_data = _get_patch_layer_json_data();
    for (rapidjson::Value::ValueIterator itr = layer_json_data.Begin(); itr != layer_json_data.End(); ++itr)
    {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }

    // Not in the Layer json data, search the Common json data
    rapidjson::Value& common_json_data = (layer_1_param ? _get_layer_patch_common_json_data(0) : _get_patch_common_json_data());
    for (rapidjson::Value::ValueIterator itr = common_json_data.Begin(); itr != common_json_data.End(); ++itr)
    {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }

    // Not in the Layer or Common json data, search the State json data
    rapidjson::Value& state_json_data = (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ? _get_patch_state_a_json_data() :
                                        _get_patch_state_b_json_data();
    for (rapidjson::Value::ValueIterator itr = state_json_data.Begin(); itr != state_json_data.End(); ++itr)
    {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }                                 
    return nullptr;
}

//----------------------------------------------------------------------------
// _find_global_param
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_find_global_param(std::string path)
{
    // Search the global params json data
    for (rapidjson::Value::ValueIterator itr = _global_params_json_data.Begin(); itr != _global_params_json_data.End(); ++itr)
    {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }                                 
    return nullptr;
}

//----------------------------------------------------------------------------
// _find_init_patch_param
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_find_init_patch_param(std::string path, PatchState state)
{
    // Search the Common json data
    rapidjson::Value& common_json_data = _init_patch_json_data["common"].GetArray();
    for (rapidjson::Value::ValueIterator itr = common_json_data.Begin(); itr != common_json_data.End(); ++itr)
    {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }

    // Not in the Common json data, search the State json data
    rapidjson::Value& state_json_data = (state == PatchState::STATE_A) ? 
                                            _init_patch_json_data["state_a"].GetArray() :
                                            _init_patch_json_data["state_b"].GetArray();
    for (rapidjson::Value::ValueIterator itr = state_json_data.Begin(); itr != state_json_data.End(); ++itr)
    {
        // Does this entry match the param?
        if (itr->GetObject()["path"].GetString() == path) {
            return itr;
        }
    }                                 
    return nullptr;
}

//----------------------------------------------------------------------------
// _get_patch_common_layer_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_common_layer_json_data()
{
    // Return the patch common Layer patch data
    // Note: It is checked earlier if this element exists and is an array
    return _layers_json_data["common"].GetArray();
}

//----------------------------------------------------------------------------
// _get_patch_layer_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_layer_json_data()
{
    // Return the patch specific Layer patch data
    // Note: It is checked earlier if this element exists and is an array
    return _get_current_layer_json_data()->GetObject()["params"].GetArray();
}

//----------------------------------------------------------------------------
// _get_layer_patch_common_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_layer_patch_common_json_data(uint layer_num)
{
    // Return the Layer patch Common patch data
    // Note: It is checked earlier if this element exists and is an array
    return _layer_patch_json_doc[layer_num]["common"].GetArray();
}

//----------------------------------------------------------------------------
// _get_patch_common_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_common_json_data()
{
    // Return the patch Common patch data
    // Note: It is checked earlier if this element exists and is an array
    assert(_patch_json_doc);
    return (*_patch_json_doc)["common"].GetArray();
}

//----------------------------------------------------------------------------
// _get_layer_patch_state_a_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_layer_patch_state_a_json_data(uint layer_num)
{
    // Return the Layer patch State A data
    // Note: It is checked earlier if this element exists and is an array
    return _layer_patch_json_doc[layer_num]["state_a"].GetArray();
}

//----------------------------------------------------------------------------
// _get_patch_state_a_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_state_a_json_data()
{
    // Return the patch State A data
    // Note: It is checked earlier if this element exists and is an array
    assert(_patch_json_doc);
    return (*_patch_json_doc)["state_a"].GetArray();
}

//----------------------------------------------------------------------------
// _get_layer_patch_state_b_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_layer_patch_state_b_json_data(uint layer_num)
{
    // Return the Layer patch State B data
    // Note: It is checked earlier if this element exists and is an array
    return _layer_patch_json_doc[layer_num]["state_b"].GetArray();
}

//----------------------------------------------------------------------------
// _get_patch_state_b_json_data
//----------------------------------------------------------------------------
rapidjson::Value& FileManager::_get_patch_state_b_json_data()
{
    // Return the patch State B data
    // Note: It is checked earlier if this element exists and is an array
    assert(_patch_json_doc);
    return (*_patch_json_doc)["state_b"].GetArray();
}

//----------------------------------------------------------------------------
// _start_save_config_file_timer
//----------------------------------------------------------------------------
void FileManager::_start_save_config_file_timer()
{
    // Stop and start the save config file timer
    _save_config_file_timer->stop();
    _save_config_file_timer->start(SAVE_CONFIG_FILE_IDLE_INTERVAL_US, std::bind(&FileManager::_save_config_file, this));
}

//----------------------------------------------------------------------------
// _start_save_layers_file_timer
//----------------------------------------------------------------------------
void FileManager::_start_save_layers_file_timer()
{
    // Stop and start the save layers file timer
    _save_layers_file_timer->stop();
    _save_layers_file_timer->start(SAVE_LAYERS_FILE_IDLE_INTERVAL_US, std::bind(&FileManager::_save_current_layers_file, this));
}

//----------------------------------------------------------------------------
// _start_save_global_params_file_timer
//----------------------------------------------------------------------------
void FileManager::_start_save_global_params_file_timer()
{
    // Stop and start the save global params file timer
    _save_global_params_file_timer->stop();
    _save_global_params_file_timer->start(SAVE_GLOBAL_PARAMS_FILE_IDLE_INTERVAL_US, std::bind(&FileManager::_save_global_params_file, this));
}

//----------------------------------------------------------------------------
// _calc_and_set_layer_voices
//----------------------------------------------------------------------------
void FileManager::_calc_and_set_layer_voices()
{
    // Get the layer number of voices param (currently selected Layer)
    auto param = utils::get_param_from_ref(utils::ParamRef::LAYER_NUM_VOICES);
    if (param) {
        // Calculate the number of leftover (unused) voices available
        // to this layer
        // This function also checks that the voices allocated to each layer are valid,
        // and makes any required adjustments
        uint leftover_voices = NUM_VOICES;
        for (uint layer=0; layer<NUM_LAYERS; layer++) {
            uint layer_n_voices = 0;

            // Get the number of voices for this layer from the layer config json object
            auto obj = _get_layer_num_voices_obj(layer);
            if (obj) {
                layer_n_voices = std::round(obj->GetObject()["value"].GetFloat() * (NUM_VOICES + 1));
            }

            // If the number of voices specified is more than the number of voices remaning,
            // truncate the nunber of voices and set in the json object
            if (layer_n_voices > leftover_voices) {
                layer_n_voices = leftover_voices;
                if (obj) {
                    obj->GetObject()["value"].SetFloat((float)layer_n_voices / (NUM_VOICES + 1));
                }
            }

            // If this is the current layer, set the number of voices param
            if (utils::is_current_layer(layer)) {
                // Set the voices allocated to this layer
                // Note: Pass true to force the value position to be set (don't check against
                // the actual positions as it may not be valid any longer)                    
                param->set_value_from_position(layer_n_voices, true);                 
            }

            // Save the number of voices for this layer and calculate the remaining voices
            utils::get_layer_info(layer).set_num_voices(layer_n_voices);
            leftover_voices = leftover_voices - layer_n_voices;
            //MSG("Layer " << layer << " voices " << layer_n_voices << ", leftover " << leftover_voices);
        }

        // Set the actual voices available to the current layer
        // Note: This ensures the max voices cannot be exceeded
        param->set_actual_num_positions(param->get_position_value() + leftover_voices + 1);          
    }    
}

//----------------------------------------------------------------------------
// _calc_and_set_layer_mpe_config
//----------------------------------------------------------------------------
void FileManager::_calc_and_set_layer_mpe_config()
{
    uint lower_zone_num_channels = 0;
    uint upper_zone_num_channels = 0;
    bool lower_zone_active = false;
    bool upper_zone_active = false;
    bool update_zone_num_channel = false;

    // Get the Upper and Lower Zone Number of Channels
    auto lower_zone_channels_obj = _get_layer_mpe_lower_zone_num_channels_obj();
    if (lower_zone_channels_obj) {
        lower_zone_num_channels = std::round(lower_zone_channels_obj->GetObject()["value"].GetFloat() * (MAX_NUM_MPE_CHANNELS + 1));
    }
    auto upper_zone_channels_obj = _get_layer_mpe_upper_zone_num_channels_obj();
    if (upper_zone_channels_obj) {
        upper_zone_num_channels = std::round(upper_zone_channels_obj->GetObject()["value"].GetFloat() * (MAX_NUM_MPE_CHANNELS + 1));
    }

    // Go through each Layer
    for (uint i=0; i<NUM_LAYERS; i++) {
        auto mode = MpeMode::OFF;

        // Get the Layer MPE mode
        auto mode_obj = _get_layer_mpe_mode_obj(i);
        if (mode_obj) {
            mode = utils::get_mpe_mode(mode_obj->GetObject()["value"].GetFloat());
        }

        // Set the Layer mode
        utils::get_layer_info(i).set_mpe_mode(mode);

        // Are the Lower or Upper Zones active?
        if (!lower_zone_active && (mode == MpeMode::LOWER_ZONE)) {
            lower_zone_active = true;
        }
        else if (!upper_zone_active && (mode == MpeMode::UPPER_ZONE)) {
            upper_zone_active = true;
        }
    }

    // We need to make sure the zone channels allocated are consistent - lower zone taking 
    // the priority
    // Is the lower zone active?
    if (lower_zone_active) {
        // Is the upper zone also active?
        if (upper_zone_active) {
            // Check the number of channels allocated is valid
            if (lower_zone_num_channels == MAX_NUM_MPE_CHANNELS) {
                lower_zone_num_channels--;
                upper_zone_num_channels = 0;
                update_zone_num_channel = true;
            }
            else if ((lower_zone_num_channels + upper_zone_num_channels) > (MAX_NUM_MPE_CHANNELS - 1)) {
                // Truncate the upper zone number of channels
                upper_zone_num_channels = (MAX_NUM_MPE_CHANNELS - 1) - lower_zone_num_channels;
                update_zone_num_channel = true;
            }
        }
    }
    else {
        // Make sure the lower zone channels are zero
        if (lower_zone_num_channels) {
            lower_zone_num_channels = 0;
            update_zone_num_channel = true;
        }
    }

    // Is the upper zone not active?
    if (!upper_zone_active) {
        // Make sure the upper zone channels are zero
        if (upper_zone_num_channels) {
            upper_zone_num_channels = 0;
            update_zone_num_channel = true;
        }        
    }

    // Should we also update the JSON data?
    if (update_zone_num_channel && upper_zone_channels_obj) {
        lower_zone_channels_obj->GetObject()["value"].SetFloat((float)lower_zone_num_channels / (MAX_NUM_MPE_CHANNELS + 1));
        upper_zone_channels_obj->GetObject()["value"].SetFloat((float)upper_zone_num_channels / (MAX_NUM_MPE_CHANNELS + 1));
    }

    // Now set the actual number of positions for each param
    uint lower_zone_param_num_pos = 1;
    uint upper_zone_param_num_pos = 1;
    if (lower_zone_active && upper_zone_active) {
        // Calculate the number of positions for the lower and upper params
        lower_zone_param_num_pos += (MAX_NUM_MPE_CHANNELS - 1) - upper_zone_num_channels;
        upper_zone_param_num_pos += (MAX_NUM_MPE_CHANNELS - 1) - lower_zone_num_channels;
    }
    else if (lower_zone_active) {
        // Max positions are available
        lower_zone_param_num_pos += MAX_NUM_MPE_CHANNELS;
    }
    else if (upper_zone_active) {
        // Max positions available
        upper_zone_param_num_pos += MAX_NUM_MPE_CHANNELS;
    }

    // Set the Lower/Upper Zone param number of positions
    auto param = utils::get_param_from_ref(utils::ParamRef::MPE_LOWER_ZONE_NUM_CHANNELS);
    if (param && param->actual_num_positions != lower_zone_param_num_pos) {
        param->set_actual_num_positions(lower_zone_param_num_pos);
    }
    param = utils::get_param_from_ref(utils::ParamRef::MPE_UPPER_ZONE_NUM_CHANNELS);
    if (param && param->actual_num_positions != upper_zone_param_num_pos) {
        param->set_actual_num_positions(upper_zone_param_num_pos);
    }
}

//----------------------------------------------------------------------------
// _get_layer_num_voices_obj
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_get_layer_num_voices_obj(uint layer_num)
{   
    // Search the layer json data
    return _get_layer_obj(layer_num, "/daw/main/ninavst/Layer_Num_Voices");
}

//----------------------------------------------------------------------------
// _get_layer_mpe_mode_obj
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_get_layer_mpe_mode_obj(uint layer_num)
{
    // Search the layer json data
    return _get_layer_obj(layer_num, "/daw/main/ninavst/MPE_Mode");
}

//----------------------------------------------------------------------------
// _get_layer_mpe_lower_zone_num_channels_obj
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_get_layer_mpe_lower_zone_num_channels_obj()
{
    // Search the layer json data
    return _get_common_layer_obj("/daw/main/ninavst/MPE_Lower_Zone_Num_Channels");
}

//----------------------------------------------------------------------------
// _get_layer_mpe_upper_zone_num_channels_obj
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_get_layer_mpe_upper_zone_num_channels_obj()
{
    // Sarch the layer json data
    return _get_common_layer_obj("/daw/main/ninavst/MPE_Upper_Zone_Num_Channels");
}

//----------------------------------------------------------------------------
// _get_layer_obj
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_get_common_layer_obj(const char *path)
{  
    // Sarch the common layer json data
    rapidjson::Value& json_data = _get_patch_common_layer_json_data();
    for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
    {
        // Does this entry match the param?
        if (std::strcmp(itr->GetObject()["path"].GetString(), path) == 0) {
            return itr;
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// _get_layer_obj
//----------------------------------------------------------------------------
rapidjson::Value::ValueIterator FileManager::_get_layer_obj(uint layer_num, const char *path)
{
    // Make sure the layer number is valid
    if (layer_num < NUM_LAYERS) {    
        // Sarch the layer json data
        rapidjson::Value& json_data = _get_layer_json_data(layer_num)->GetObject()["params"].GetArray();
        for (rapidjson::Value::ValueIterator itr = json_data.Begin(); itr != json_data.End(); ++itr)
        {
            // Does this entry match the param?
            if (std::strcmp(itr->GetObject()["path"].GetString(), path) == 0) {
                return itr;
            }
        }
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// _select_current_layer
//----------------------------------------------------------------------------
void FileManager::_select_current_layer(uint layer_num)
{
    // Update the current layer value
    auto param = utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER);
    if (param) {
        // Update the current layer param and send to the DAW
        param->set_value_from_position(layer_num);
        _daw_manager->set_param(layer_num, param);
    } 
}

//----------------------------------------------------------------------------
// _set_loading_layers_with_daw
//----------------------------------------------------------------------------
void FileManager::_set_loading_layers_with_daw(bool loading)
{
    // Get the Layer Load param - this is used to indicate we are processing a patch
    auto param = utils::get_param_from_ref(utils::ParamRef::LAYER_LOAD);
    if (param) {
        // Update the param and send to the DAW (all layers)        
        param->set_value((loading ? 1.0 : 0.0));
        _daw_manager->set_param_all_layers(param);
    }
}

//----------------------------------------------------------------------------
// _set_loading_patch_with_daw
//----------------------------------------------------------------------------
void FileManager::_set_loading_patch_with_daw(uint layer_num, bool loading)
{
    // Get the Layer Load param - this is used to indicate we are processing a patch
    auto param = utils::get_param_from_ref(utils::ParamRef::LAYER_LOAD);
    if (param) {
        // Update the param and send to the DAW (all layers)        
        param->set_value((loading ? 1.0 : 0.0));
        _daw_manager->set_param(layer_num, param);
    }
}

//----------------------------------------------------------------------------
// _set_current_layer_filename_tag
//----------------------------------------------------------------------------
void FileManager::_set_current_layer_filename_tag(uint layer_num)
{
    // Update the current layer value tag as the layer filename for the specified layer
    auto param = utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER);
    if (param) {
        param->value_tags.at(layer_num) = _get_patch_filename(utils::get_layer_info(layer_num).get_patch_id(), false);
    } 
}

//----------------------------------------------------------------------------
// _get_fx_type
//----------------------------------------------------------------------------
FxType FileManager::_get_fx_type()
{
    int type = FxType::CHORUS;

    // Get the current FX type
    auto param = utils::get_param_from_ref(utils::ParamRef::FX_TYPE_SELECT);
    if (param) {
        // Convert the type
        type = (uint)floor(param->get_value() * FxType::NUM_FX_TYPES);
        if (type > FxType::REVERB)
            type = FxType::REVERB;
    }
    return FxType(type);
}

#if defined CHECK_LAYERS_LOAD
//----------------------------------------------------------------------------
// _check_layers_load
//----------------------------------------------------------------------------
void FileManager::_check_layers_load()
{
    // Sleep for a bit to make sure the layers are all processed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Get the Current Layer and Layer State params as we need to set these
    auto current_layer_param = utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER);
    auto layer_state_param = utils::get_param_from_ref(utils::ParamRef::LAYER_STATE);
    if (current_layer_param && layer_state_param)
    {
        // Process each layer in reverse
        for (int i=0; i>=0; i--) {

            // Select the layer and state B params
            current_layer_param->set_value_from_position(i);
            _daw_manager->set_param(current_layer_param);
            layer_state_param->set_value(0);
            _daw_manager->set_param(layer_state_param);
            utils::set_patch_state(PatchState::STATE_A);
            _patch_json_doc = &_layer_patch_json_doc[i];

            // Get the state A params
            auto param_values = _daw_manager->get_param_values(false);
            for (auto itr=param_values.begin(); itr != param_values.end(); ++itr) {
                // Find the param
                auto itr2 = _find_patch_param(itr->first->get_path());
                if (itr2) {
                    // Value the same?
                    auto current_value = itr2->GetObject()["value"].GetFloat();
                    if (current_value != itr->second) {
                        MSG("Layer patch param is incorrect: " << itr->first->get_path());
                        MSG("DAW value: " << itr->second << ", Patch value: " << current_value);
                    }                
                }
                else {
                    // If this is a mod matrix param with the value 0.5 then its ok if its missing
                    if (!itr->first->mod_matrix_param || (itr->second != 0.5)) {
                        MSG("Layer patch param missing: " << itr->first->get_path());
                    }
                }
            }
        }
    }
}
#endif
