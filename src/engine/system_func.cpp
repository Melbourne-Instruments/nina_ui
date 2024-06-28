/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  system_func.cpp
 * @brief System Func implementation.
 *-----------------------------------------------------------------------------
 */

#include <iostream>
#include "system_func.h"
#include "param.h"
#include "utils.h"
#include "common.h"


// Static data
// System Func type names
const char *SystemFunc::_type_names[] = {
    "undo_last_param_change",
    "toggle_patch_state",
    "load_patch",
    "load_morph",
    "save_patch",
    "select_patch",
    "patch_save",
    "soft_button1",
    "soft_button2",
    "soft_button3",
    "select_mod_matrix",
    "data_knob",
    "multifn_switch",
    "layer",
    "lfo_shape",
    "wt_select",
    "set_mod_src_num",
    "osc_coarse",
    "lfo_select",
    "noise_type",
    "seq_settings",
    "kbd",
    "current_layer",
    "load_layers",
    "save_layers",
    "start_sw_update",
    "finish_sw_update",
    "seq_run",
    "seq_rec",
    "save_morph",
    "save_demo_mode",
    "reset_layers",
    "arp",
    "sfc_init",
    "init_patch",
    "start_calibration",
    "finish_calibration",
    "system_colour_set",
    "eg_2_level_mod_dst"
};

//----------------------------------------------------------------------------
// RegisterParams
//----------------------------------------------------------------------------
void SystemFunc::RegisterParams()
{
    // Register each system func param
    auto param = SystemFuncParam::CreateParam(SystemFuncType::UNDO_LAST_PARAM_CHANGE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::TOGGLE_PATCH_STATE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD_PATCH);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD_PATCH_STATE_B);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SAVE_PATCH);
    utils::register_param(std::move(param));                         
    param = SystemFuncParam::CreateParam(SystemFuncType::SELECT_PATCH);
    utils::register_param(std::move(param));                           
    param = SystemFuncParam::CreateParam(SystemFuncType::PATCH_SAVE);
    utils::register_param(std::move(param));                           
    param = SystemFuncParam::CreateParam(SystemFuncType::SOFT_BUTTON1);
    utils::register_param(std::move(param));                           
    param = SystemFuncParam::CreateParam(SystemFuncType::SOFT_BUTTON2);
    utils::register_param(std::move(param));                           
    param = SystemFuncParam::CreateParam(SystemFuncType::SOFT_BUTTON3);
    utils::register_param(std::move(param));                           
    param = SystemFuncParam::CreateParam(SystemFuncType::MOD_MATRIX);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::DATA_KNOB);
    utils::register_param(std::move(param));                                                             
    param = SystemFuncParam::CreateParam(SystemFuncType::MULTIFN_SWITCH);
    utils::register_param(std::move(param));                             
    param = SystemFuncParam::CreateParam(SystemFuncType::LAYER);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::LFO_SHAPE);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::WT_SELECT);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::SET_MOD_SRC_NUM);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::OSC_COARSE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::LFO_SELECT);
    utils::register_param(std::move(param));                                                                 
    param = SystemFuncParam::CreateParam(SystemFuncType::NOISE_TYPE);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::SEQ_SETTINGS);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::KBD);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::CURRENT_LAYER);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::LOAD_LAYERS);
    utils::register_param(std::move(param));                              
    param = SystemFuncParam::CreateParam(SystemFuncType::SAVE_LAYERS);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::START_SW_UPDATE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::FINISH_SW_UPDATE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SEQ_RUN);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SEQ_REC);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SAVE_MORPH);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SAVE_DEMO_MODE);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::RESET_LAYERS);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::ARP);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SFC_INIT);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::INIT_PATCH);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::START_CALIBRATION);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::FINISH_CALIBRATION);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::SYSTEM_COLOUR_SET);
    utils::register_param(std::move(param));
    param = SystemFuncParam::CreateParam(SystemFuncType::EG_2_LEVEL_MOD_DST);
    utils::register_param(std::move(param));
}

//----------------------------------------------------------------------------
// TypeName
//----------------------------------------------------------------------------
const std::string SystemFunc::TypeName(SystemFuncType type)
{
    // Return the type name
    return _type_names[static_cast<int>(type)];
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc()
{
    // Initialise class data
    result = false;
    linked_param = 0;
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc(SystemFuncType type, float value, Param *linked_param, NinaModule from_module)
{
    // Initialise class data
    this->type = type;
    this->value = value;
    this->linked_param = linked_param;
    this->from_module = from_module;
    this->result = false;
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc(SystemFuncType type, PatchId id, NinaModule from_module)
{
    // Initialise class data
    this->type = type;
    this->value = 0;
    this->patch_id = id;
    this->from_module = from_module;
    this->layer_num = utils::get_current_layer_info().layer_num();
}

//----------------------------------------------------------------------------
// SystemFunc
//----------------------------------------------------------------------------
SystemFunc::SystemFunc(SystemFuncType type, uint num, NinaModule from_module)
{
    // Initialise class data
    this->type = type;
    this->num = num;
    this->from_module = from_module;
    this->result = false;
}

//----------------------------------------------------------------------------
// ~SystemFunc
//----------------------------------------------------------------------------
SystemFunc::~SystemFunc()
{
    // Nothing specific to do
}
