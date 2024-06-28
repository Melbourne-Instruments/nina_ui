/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  daw_manager.h
 * @brief DAW Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include <regex>
#include "daw_manager.h"
#include "sequencer_manager.h"
#include "sushi_client.h"
#include "utils.h"
#include "logger.h"
#include "layer_info.h"

// Constants
constexpr char MANAGER_NAME[]                 = "DawManager";
constexpr uint REGISTER_PARAMS_RETRY_COUNT    = 50;
constexpr uint PARAM_ID_LAYERS_MASK_BIT_SHIFT = 27;
constexpr uint PARAM_ID_LAYER_NUM_BIT_MASK    = ~0x78000000;
constexpr char MAIN_TRACK_NAME[]              = "main";

//----------------------------------------------------------------------------
// DawManager
//----------------------------------------------------------------------------
DawManager::DawManager(EventRouter *event_router) : 
    BaseManager(NinaModule::DAW, MANAGER_NAME, event_router)
{
    std::vector<std::pair<int,int>> param_blocklist;

    // Initialise class data
    _sushi_controller = sushi_controller::CreateSushiController();
    _sfc_param_changed_listener = 0;
    _mid_listener = 0;
    _fm_param_changed_listener = 0;
    _osc_listener = 0;
    _gui_listener = 0;
    _main_track_id = -1;

    // Register the DAW params
    _register_params();

    // Retrieve the Sushi build info
    auto build_info = _sushi_controller->system_controller()->get_build_info();
    if (build_info.first == sushi_controller::ControlStatus::OK) {
        _sushi_verson.version = build_info.second.version;
        _sushi_verson.commit_hash = build_info.second.commit_hash;
    }
    else {
        _sushi_verson.version = "Unknown";
    }    

    // Register the param change notification listener
    _sushi_controller->notification_controller()->subscribe_to_parameter_updates(
        std::bind(&DawManager::_param_update_notification,
        this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3), param_blocklist);     
}

//----------------------------------------------------------------------------
// ~DawManager
//----------------------------------------------------------------------------
DawManager::~DawManager()
{
    // Clean up the event listeners
    if (_sfc_param_changed_listener)
        delete _sfc_param_changed_listener;
    if (_mid_listener)
        delete _mid_listener;
    if (_fm_param_changed_listener)
        delete _fm_param_changed_listener;     
    if (_osc_listener)
        delete _osc_listener;
    if (_gui_listener)
        delete _gui_listener;
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void DawManager::process()
{
    // Create and add the various event listeners
    _sfc_param_changed_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_sfc_param_changed_listener);
    _mid_listener = new EventListener(NinaModule::MIDI_DEVICE, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_mid_listener);	
    _fm_param_changed_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_fm_param_changed_listener);	
    _osc_listener = new EventListener(NinaModule::OSC, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_osc_listener);
    _gui_listener = new EventListener(NinaModule::GUI, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_gui_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void DawManager::process_event(const BaseEvent *event)
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
        
        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// process_midi_event_direct
//----------------------------------------------------------------------------
void DawManager::process_midi_event_direct(const snd_seq_event_t *event)
{
    auto data = *event;

    // If the main track ID was found
    if (_main_track_id != -1) {
        // Parse the MIDI message type
        switch (data.type)
        {
            case SND_SEQ_EVENT_NOTEOFF: {
                // Send the NOTE OFF message to Sushi
                // Normalise midi velocity by dividing by max midi velocity
                _sushi_controller->keyboard_controller()->send_note_off(_main_track_id, data.data.note.channel, data.data.note.note, ((float)data.data.note.velocity)/127.0);
                break;
            }

            case SND_SEQ_EVENT_NOTEON: {
                // Send the NOTE ON message to Sushi
                // Normalise midi velocity by dividing by max midi velocity
                _sushi_controller->keyboard_controller()->send_note_on(_main_track_id, data.data.note.channel, data.data.note.note, ((float)data.data.note.velocity)/127.0);
                break;
            }

            case SND_SEQ_EVENT_KEYPRESS: {
                // Send the key pressure event message to Sushi
                // Normalise midi velocity by dividing by max midi velocity                
                _sushi_controller->keyboard_controller()->send_note_aftertouch(_main_track_id,data.data.note.channel, data.data.note.note,((float)data.data.note.velocity)/127.0);
                break;
            }

            default:
                // Unknown message type ignore it
                break;
        }
    }
}

//----------------------------------------------------------------------------
// get_patch_state_params
//----------------------------------------------------------------------------
bool DawManager::get_patch_state_params()
{
    bool ret = false;

    // Get the Layer State param - this is used to get the processor ID used to retrieve the
    // state param values
    auto param = utils::get_param_from_ref(utils::ParamRef::LAYER_STATE);
    if (param)
    {
        // Get the patch params
        auto patch_params = _sushi_controller->parameter_controller()->get_parameter_values(param->processor_id);
        auto itr = patch_params.second.begin();

        // Parse the available DAW params
        auto params = utils::get_params(NinaModule::DAW);
        for (Param *p : params)
        {
            // Skip params not for this processor or an alias param
            if ((p->processor_id != param->processor_id) || p->alias_param)
                continue;
            
            // The parameter IDs are sequential, but some will be blacklisted, so
            // skip any of these
            while (p->param_id != itr->parameter_id)
                itr++;

            // Is this a state param and did the value change?
            if (p->patch_state_param && (p->get_value() != itr->value)) {
                // Yes, update the param value
                p->set_value(itr->value);
                ret = true;
            }
            itr++;
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// set_patch_layer_params
//----------------------------------------------------------------------------
void DawManager::set_patch_layer_params(uint layer_num)
{
    // Get the Layer State param - this is used to get the processor ID used to retrieve the
    // state param values
    auto param = utils::get_param_from_ref(utils::ParamRef::LAYER_STATE);
    if (param)
    {
        std::vector<sushi_controller::ParameterValue> param_values;

        // Parse the available DAW params
        auto params = utils::get_params(NinaModule::DAW);
        for (const Param *p : params)
        {
            // If this is a layer param
            if (p->patch_layer_param) {
                // Add the value to set to send
                auto param_value = sushi_controller::ParameterValue();
                param_value.processor_id = p->processor_id;
                param_value.parameter_id = _encode_param_id(p->param_id, LayerInfo::GetLayerMaskBit(layer_num));
                param_value.value = p->get_value();
                param_values.push_back(param_value);          
            }
        }

        // Send the values to Sushi
        if (param_values.size() > 0) {
            _sushi_controller->parameter_controller()->set_parameter_values(param_values);
        }
    }
}

//----------------------------------------------------------------------------
// set_patch_params
//----------------------------------------------------------------------------
void DawManager::set_patch_params(uint layer_num, bool include_cmn_params, PatchState state)
{
    // Get the Layer State param as we need to set this, and the Morph Value param to make
    // sure it is NOT sent with these params
    // The Morph Value param is sent separately if needed, once the patch is loaded
    auto layer_state_param = utils::get_param_from_ref(utils::ParamRef::LAYER_STATE);
    auto morph_value_param = utils::get_param_from_ref(utils::ParamRef::MORPH_VALUE); 
    if (layer_state_param && morph_value_param)
    {
        std::vector<sushi_controller::ParameterValue> param_values;
        uint layer_mask_bit = LayerInfo::GetLayerMaskBit(layer_num);

        // Set the Layer State param first
        auto value = (state == PatchState::STATE_A) ? 0 : 1;
        _sushi_controller->parameter_controller()->set_parameter_value(layer_state_param->processor_id, 
                                                                       _encode_param_id(layer_state_param->param_id, layer_mask_bit),
                                                                       value);

        // Parse the available DAW params
        auto params = utils::get_params(NinaModule::DAW);
        for (Param *p : params)
        {
            // Skip params if:
            // - Common and common should not be included
            // - The Layer State and Load params (already sent)
            //   Note: Assume they are sequential params
            // - If this is an alias param
            // - If this is a patch layer param
            // - If this is the Morph Value param
            if ((!p->patch_state_param && !include_cmn_params) ||
                (p->param_id == layer_state_param->param_id) ||
                (p->param_id == (layer_state_param->param_id + 1)) ||
                p->alias_param ||
                p->patch_layer_param ||
                (p->param_id == morph_value_param->param_id)) {
                continue; 
            }

            // Add the value to set to send
            auto param_value = sushi_controller::ParameterValue();
            param_value.processor_id = p->processor_id;
            param_value.parameter_id = _encode_param_id(p->param_id, layer_mask_bit);
            param_value.value = p->get_value();
            param_values.push_back(param_value);
        }

        // Send the values to Sushi
        _sushi_controller->parameter_controller()->set_parameter_values(param_values);

        // We also need to set the tempo in Sushi (if we are including the common params)
        auto param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::TEMPO_BPM_PARAM_ID);
        if (param && include_cmn_params) {
            // Set the tempo in Sushi
            _sushi_controller->transport_controller()->set_tempo(param->get_value());
        }
    }
}

//----------------------------------------------------------------------------
// set_param
//----------------------------------------------------------------------------
void DawManager::set_param(uint layer_num, const Param *param)
{
    // If this is a DAW param
    if (param->module == NinaModule::DAW) {
        // Send the param change to Sushi
        _sushi_controller->parameter_controller()->set_parameter_value(param->processor_id, 
                                                                      _encode_param_id(param->param_id, LayerInfo::GetLayerMaskBit(layer_num)),
                                                                      param->get_value());
    }   
}

//----------------------------------------------------------------------------
// set_param
//----------------------------------------------------------------------------
void DawManager::set_param(uint layer_num, const Param *param, float value)
{
    // If this is a DAW param
    if (param->module == NinaModule::DAW) {
        // Send the param change to Sushi
        _sushi_controller->parameter_controller()->set_parameter_value(param->processor_id, 
                                                                      _encode_param_id(param->param_id, LayerInfo::GetLayerMaskBit(layer_num)),
                                                                      value);
    }   
}

//----------------------------------------------------------------------------
// set_param_direct
//----------------------------------------------------------------------------
void DawManager::set_param_direct(uint layer_mask, const Param *param)
{
    // If this is a DAW param
    if (param->module == NinaModule::DAW) {
        // Send the param change to Sushi
        _sushi_controller->parameter_controller()->set_parameter_value(param->processor_id, 
                                                                      _encode_param_id(param->param_id, layer_mask),
                                                                      param->get_value());
    }   
}

//----------------------------------------------------------------------------
// set_param_all_layers
//----------------------------------------------------------------------------
void DawManager::set_param_all_layers(const Param *param)
{
    // If this is a DAW param
    if (param->module == NinaModule::DAW) {
        // Send the param change to Sushi - all Layers
        uint layer_mask = 0;
        for (uint i=0; i<NUM_LAYERS; i++) {
            layer_mask |= LayerInfo::GetLayerMaskBit(i);
        }
        _sushi_controller->parameter_controller()->set_parameter_value(param->processor_id, 
                                                                      _encode_param_id(param->param_id, layer_mask),
                                                                      param->get_value());
    }   
}

//----------------------------------------------------------------------------
// get_sushi_version
//----------------------------------------------------------------------------
SushiVersion DawManager::get_sushi_version()
{
    return _sushi_verson;
}

#if defined CHECK_LAYERS_LOAD
//----------------------------------------------------------------------------
// get_params
//----------------------------------------------------------------------------
std::vector<std::pair<Param *,float>> DawManager::get_param_values(bool state_only)
{
    std::vector<std::pair<Param *,float>> param_values;

    // Get the Layer State param - this is used to get the processor ID used to retrieve the
    // state param values
    auto param = utils::get_param_from_ref(utils::ParamRef::LAYER_STATE);
    if (param)
    {
        // Get the patch params
        auto patch_params = _sushi_controller->parameter_controller()->get_parameter_values(param->processor_id);
        auto itr = patch_params.second.begin();

        // Parse the available DAW params
        auto params = utils::get_params(NinaModule::DAW);
        for (Param *p : params)
        {
            // Skip params not for this processor or an alias param
            if ((p->processor_id != param->processor_id) || p->alias_param)
                continue;
            
            // The parameter IDs are sequential, but some will be blacklisted, so
            // skip any of these
            while (p->param_id != itr->parameter_id)
                itr++;

            // Add this param?
            if (p->patch_param || p->patch_state_param || p->patch_layer_param) {
                param_values.push_back(std::pair<Param *, float>(p, itr->value));
            }
            itr++;
        }
    }
    return param_values;
}
#endif

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void DawManager::_process_param_changed_event(const ParamChange &data)
{
    // Get the param, check if it exists and is for the DAW
    const Param *param = utils::get_param(data.path.c_str());
    if (param && (param->module == NinaModule::DAW))
    {
        // Send the param change to Sushi
        _sushi_controller->parameter_controller()->set_parameter_value(param->processor_id, 
                                                                       _encode_param_id(param->param_id, data.layers_mask),
                                                                       data.value);
    }
    // Not for the DAW, is it however the Tempo BPM param (special case for Sushi)
    else if(data.path == utils::get_param(ParamType::COMMON_PARAM, CommonParamId::TEMPO_BPM_PARAM_ID)->get_path())
    {
        // Set the tempo in Sushi
        _sushi_controller->transport_controller()->set_tempo(data.value);
    }
}

//----------------------------------------------------------------------------
// _param_update_notification
//----------------------------------------------------------------------------
void DawManager::_param_update_notification(int processor_id, int parameter_id, float value)
{
    // Find the param to update
    auto params = utils::get_params(NinaModule::DAW);
    for (Param *p : params)
    {
        // Match?
        if ((p->param_id == parameter_id) && (p->processor_id == processor_id))
        {
            // If this is the Morphing param, we use this to enable/disable morphing for the system
            if (p->get_path() == "/daw/main/ninavst/Morphing") {
                // Enable/disable morphing
                utils::set_morph_enabled((value == 1.0) ? true : false);
            }
            break;          
        }
    }
}

//----------------------------------------------------------------------------
// _encode_param_id
//----------------------------------------------------------------------------
inline int DawManager::_encode_param_id(int parameter_id, uint layers_mask)
{
    assert(layers_mask < 0x10);

    // Mask off the Param ID layer number bits
    parameter_id &= PARAM_ID_LAYER_NUM_BIT_MASK;

    // Set the layer number bits in the Param ID
    parameter_id |= (layers_mask << PARAM_ID_LAYERS_MASK_BIT_SHIFT);
    return parameter_id;
}

//----------------------------------------------------------------------------
// _register_params
//----------------------------------------------------------------------------
void DawManager::_register_params()
{
    uint num_tracks = 0;
    uint retry_count = REGISTER_PARAMS_RETRY_COUNT;
    std::pair<sushi_controller::ControlStatus, std::vector<sushi_controller::TrackInfo>> tracks;
    std::pair<sushi_controller::ControlStatus, std::vector<sushi_controller::ParameterInfo>> track_params;
    std::pair<sushi_controller::ControlStatus, std::vector<sushi_controller::ProcessorInfo>> track_processors;
    std::pair<sushi_controller::ControlStatus, std::vector<sushi_controller::ParameterInfo>> proc_params;

    // Retry until we get tracks/params from Sushi
    MSG("Registering DAW params from Sushi...");
    while (retry_count--)
    {
        // Get a list of tracks in Sushi
        tracks = _sushi_controller->audio_graph_controller()->get_all_tracks();

        // Was the track data received ok?
        if (tracks.first != sushi_controller::ControlStatus::OK)
        {
            // No - try again after waiting 100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // One or more tracks reported by Sushi - process each track
        MSG("Connected to the Sushi Controller, retry attempts: " << (REGISTER_PARAMS_RETRY_COUNT-retry_count));
        num_tracks = tracks.second.size();
        for (sushi_controller::TrackInfo ti : tracks.second)
        {
            // Initialise the track path as the track name + delimiter
            std::string track_path = ti.name + "/";
            track_params = _sushi_controller->parameter_controller()->get_track_parameters(ti.id);

            // Get each parameter on the track
            for (sushi_controller::ParameterInfo param : track_params.second)
            {
                // Use helper fn to fill out fields in our parameter struct. use track path to complete path
                auto nina_param = DawManager::_cast_sushi_param(ti.id, param, track_path);
                if (nina_param)
                    utils::register_param(std::move(nina_param));
            }
            track_processors = _sushi_controller->audio_graph_controller()->get_track_processors(ti.id);

            // Get each processor on the track
            for (sushi_controller::ProcessorInfo pi : track_processors.second)
            {
                std::string processor_path = track_path + pi.name + "/";
                proc_params = _sushi_controller->parameter_controller()->get_processor_parameters(pi.id);

                // Get each parameter on the processor
                for (sushi_controller::ParameterInfo param : proc_params.second)
                {
                    // Use helper fn to fill out fields in our parameter struct. use track path to complete path
                    auto nina_param = DawManager::_cast_sushi_param(pi.id, param, processor_path);
                    if (nina_param)
                        utils::register_param(std::move(nina_param));
                }
            }

            // Save the track ID if this is the main track
            if (ti.name == MAIN_TRACK_NAME) {
                _main_track_id = ti.id;
            }
        }

        // Sushi tracks processed, so we can break from the retry loop
        break;        
    }
    
    // Show a warning if we couldn't communicate with Sushi
    if (tracks.first != sushi_controller::ControlStatus::OK)
    {
        MSG("WARNING: Could not retrieve track data from Sushi, check if it is running");
        NINA_LOG_WARNING(NinaModule::DAW, "Could not retrieve track data from Sushi, check if it is running");
    }
    // Show a warning if the main track was not found
    else if (_main_track_id == -1)
    {
        MSG("WARNING: Main track not found, check the Sushi configiration");
        NINA_LOG_WARNING(NinaModule::DAW, "WARNING: Main track not found, check the Sushi configiration");
    }     
    // Show a warning if no tracks were processed
    else if (num_tracks == 0)
    {
        MSG("WARNING: Sushi did not report any tracks, no DAW params registered");
        NINA_LOG_WARNING(NinaModule::DAW, "Sushi did not report any tracks, no DAW params registered");
    }
}

//----------------------------------------------------------------------------
// _cast_sushi_param
//----------------------------------------------------------------------------
std::unique_ptr<Param> DawManager::_cast_sushi_param(int processor_id, const sushi_controller::ParameterInfo &sushi_param, std::string path_prefix)
{
    // Get the param default value from the Sushi plugin
    auto value = _sushi_controller->parameter_controller()->get_parameter_value(processor_id, sushi_param.id);
    if (value.first == sushi_controller::ControlStatus::OK)
    {
        // Cast the Sushi param to a Nina Param
        // We also ensure the param is not blacklisted
        // Note: In the path replace any spaces with underscores
        auto param = Param::CreateParam(this, std::regex_replace(path_prefix + sushi_param.name, std::regex{" "}, "_"));
        if (!utils::param_is_blacklisted(param->get_path()))
        {
            // Create the param
            param->name = sushi_param.name;
            param->set_value(value.second);
            param->processor_id = processor_id;
            param->param_id = sushi_param.id;

            // Check if this is a Mod Matrix param
            if (param->get_path().substr(0, (sizeof("/daw/main/ninavst/Mod_")-1)) == "/daw/main/ninavst/Mod_")
            {
                // Find the modulation src/dst delimiter
                auto src_dst_str = param->get_path().substr((sizeof("/daw/main/ninavst/Mod_")-1),
                                                            (param->get_path().size() - (sizeof("/daw/main/ninavst/Mod_")-1)));
                int pos = src_dst_str.find(':');
                if (pos != -1)
                {
                    // Get the modulation source name
                    auto src_name = src_dst_str.substr(0, pos);

                    // If the position for the destination is valid
                    if (((uint)pos + 1) < src_dst_str.size())
                    {
                        // Get the modulation destination name
                        auto dst_name = src_dst_str.substr((pos + 1), (src_dst_str.size() - pos));

                        // Indicate this param is a valid modulation matrix entry
                        param->mod_matrix_param = true;
                        param->mod_src_name = std::regex_replace(src_name, std::regex{"_"}, " ");
                        param->mod_dst_name = std::regex_replace(dst_name, std::regex{"_"}, " ");    
                    }
                }
            }
            return param;
        }

        // If we get here the param is blacklisted
        NINA_LOG_INFO(NinaModule::DAW, "DAW param blacklisted: {}", param->get_path());
    }
    return nullptr;
}
