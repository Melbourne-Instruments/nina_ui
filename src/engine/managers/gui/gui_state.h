/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  gui_state.h
 * @brief GUI State definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _GUI_STATE_H
#define _GUI_STATE_H

#include "common.h"

// GUI States
enum class GuiState
{
    HOME_SCREEN,
    SYSTEM_MENU,
    PARAM_UPDATE,
    MOD_MATRIX_DST,
    MANAGE_LAYERS,
    MANAGE_PATCH,
    SW_UPDATE,
    BACKUP,
    CALIBRATE,
    WAVETABLE_MANAGEMENT,
    BANK_MANAGMENT,
    RUN_DIAG_SCRIPT,
    INVALID
};

// Manage Patch States
enum class ManagePatchState {
    LOAD_PATCH,
    SAVE_PATCH
};

// Select Patch States
enum class SelectPatchState {
    SELECT_PATCH,
    SELECT_BANK
};

// Save Patch States
enum class SavePatchState {
    PATCH_SELECT,
    PATCH_SAVE
};

// Manage Layers States
enum class ManageLayersState {
    SETUP_LAYERS,
    LOAD_LAYERS,
    SAVE_LAYERS
};

// Save Layers States
enum class SaveLayersState {
    LAYERS_SELECT,
    LAYERS_SAVE
};

// Sequencer UI States
enum class SeqUiState {
    SEQ_IDLE,
    SEQ_RUNNING,
    SEQ_RECORDING
};

// Edit Name States
enum class EditNameState {
    NONE,
    SELECT_CHAR,
    CHANGE_CHAR
};

// System Menu States
enum SystemMenuState {
    SHOW_OPTIONS,
    OPTION_ACTIONED
};

// System Menu Option
enum SystemMenuOption : int {
    CALIBRATE,
    CAL_VCA,
    CAL_FILTER,
    FACTORY_CALIBRATE,
    RUN_DIAG_SCRIPT,
    GLOBAL_SETTINGS,
    BANK_MANAGMENT,
    WAVETABLE_MANAGEMENT,
    BACKUP,
    RESTORE_BACKUP,
    STORE_DEMO_MODE,
    ABOUT
};

// Software Update States
enum class SwUpdateState {
    SW_UPDATE_STARTED,
    SW_UPDATE_FINISHED
};

// Backup States
enum class BackupState {
    BACKUP_STARTED,
    BACKUP_FINISHED
};

// Calibrate States
enum class CalibrateState {
    CALIBRATE_STARTED,
    CALIBRATE_FINISHED
};

// Bank Management States
enum class BankManagmentState {
    SHOW_LIST,
    IMPORT,
    EXPORT,
    ADD,
    CLEAR
};

// Import Bank States
enum class ImportBankState {
    NONE,
    SELECT_ARCHIVE,
    SELECT_DEST,
    IMPORT_METHOD
};

// Export Bank State
enum class ExportBankState {
    NONE,
    SELECT_BANK
};

// Clear Bank State
enum class ClearBankState {
    NONE,
    SELECT_BANK,
    CONFIRM
};

// Wavetable Management States
enum class WtManagmentState {
    SHOW_LIST,
    IMPORT,
    EXPORT,
    PRUNE
};

// General progress states
enum class ProgressState {
    NOT_STARTED,
    FAILED,
    FINISHED
};

// EG2 Level Mod state
enum class Eg2LevelModState {
    EG_2_LEVEL,
    MORPH
};

#endif  // _GUI_STATE_H
