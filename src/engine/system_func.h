/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  system_func.h
 * @brief System Function definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SYSTEM_FUNC_H
#define _SYSTEM_FUNC_H

#include <string>
#include "surface_control.h"
#include "common.h"

// External classes
class Param;

// System Function types
enum SystemFuncType : int
{
    UNDO_LAST_PARAM_CHANGE = 0,
    TOGGLE_PATCH_STATE,
    LOAD_PATCH,
    LOAD_PATCH_STATE_B,
    SAVE_PATCH,
    SELECT_PATCH,
    PATCH_SAVE,
    SOFT_BUTTON1,
    SOFT_BUTTON2,
    SOFT_BUTTON3,
    MOD_MATRIX,
    DATA_KNOB,
    MULTIFN_SWITCH,
    LAYER,
    LFO_SHAPE,
    WT_SELECT,
    SET_MOD_SRC_NUM,
    OSC_COARSE,
    LFO_SELECT,
    NOISE_TYPE,
    SEQ_SETTINGS,
    KBD,
    CURRENT_LAYER,
    LOAD_LAYERS,
    SAVE_LAYERS,
    START_SW_UPDATE,
    FINISH_SW_UPDATE,
    SEQ_RUN,
    SEQ_REC,
    SAVE_MORPH,
    SAVE_DEMO_MODE,
    RESET_LAYERS,
    ARP,
    SFC_INIT,
    INIT_PATCH,
    START_CALIBRATION,
    FINISH_CALIBRATION,
    SYSTEM_COLOUR_SET,
    EG_2_LEVEL_MOD_DST,
    UNKNOWN
};

// System Function struct
class SystemFunc
{
public:
    // Public data
    NinaModule from_module;
    SurfaceControlType sfc_control_type;
    SystemFuncType type;
    float value;
    PatchId patch_id;
    Param *linked_param;
    uint num;
    std::string str_value;
    bool result;
    uint layer_num;

    // Helper functions
    static void RegisterParams(void);
    static const std::string TypeName(SystemFuncType type);

    // Constructor/Destructor
    SystemFunc();
    SystemFunc(const char *type_name, float value, NinaModule from_module);
    SystemFunc(SystemFuncType type, float value, Param *linked_param, NinaModule from_module);
    SystemFunc(SystemFuncType type, PatchId id, NinaModule from_module);
    SystemFunc(SystemFuncType type, uint num, NinaModule from_module);
    ~SystemFunc();

private:
    // Private data
    static const char *_type_names[];
};

#endif  // _SYSTEM_FUNC_H
