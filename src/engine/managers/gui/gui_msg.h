/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  gui_msg.h
 * @brief GUI Message definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _GUI_MSG_H
#define _GUI_MSG_H

#include <mqueue.h>

// Constants
constexpr uint LIST_MAX_LEN      = 128;
constexpr uint STD_STR_LEN       = 40;
constexpr uint EDIT_NAME_STR_LEN = 20;

// GUI message type
enum GuiMsgType : int
{
    SET_LEFT_STATUS = 0,
    SET_LAYER_STATUS,
    SET_MIDI_STATUS,
    SET_TEMPO_STATUS,
    SHOW_HOME_SCREEN,
    SHOW_LIST_ITEMS,
    LIST_SELECT_ITEM,
    SET_SOFT_BUTTONS,
    SET_SOFT_BUTTONS_STATE,
    SHOW_PARAM_UPDATE,
    PARAM_UPDATE_VALUE,
    ENUM_PARAM_UPDATE,
    ENUM_PARAM_UPDATE_VALUE,
    EDIT_NAME,
    EDIT_NAME_SELECT_CHAR,
    EDIT_NAME_CHANGE_CHAR,
    SHOW_CONF_SCREEN,
    SHOW_WARNING_SCREEN,
    CLEAR_BOOT_WARNING_SCREEN,
    SET_SYSTEM_COLOUR
};

// GUI scope mode
enum GuiScopeMode : int
{
    SCOPE_MODE_OFF,
    SCOPE_MODE_OSC,
    SCOPE_MODE_XY
};

struct LeftStatus
{
    char status[STD_STR_LEN];
};

struct LayerStatus
{
    char status[STD_STR_LEN];
};

struct MidiStatus
{
    bool midi_active;
};

struct TempoStatus
{
    char tempo_value[STD_STR_LEN];
};

struct HomeScreen
{
    char patch_name[STD_STR_LEN];
    bool patch_modified;
    GuiScopeMode scope_mode;
};

struct ListItems
{
    uint num_items;
    uint selected_item;
    char list_items[LIST_MAX_LEN][STD_STR_LEN];
    bool process_enabled_state;
    bool list_item_enabled[LIST_MAX_LEN];
};

struct ListSelectItem
{
    uint selected_item;
    bool wt_list;
};

struct SoftButtons
{
    char button1[STD_STR_LEN];
    char button2[STD_STR_LEN];
    char button3[STD_STR_LEN];
};

struct SoftButtonsState
{
    int state_button1;
    int state_button2;
    int state_button3;
};

struct ParamUpdate
{
    char name[STD_STR_LEN];
    char value_string[STD_STR_LEN];
    char display_string[STD_STR_LEN];
    char value_tag[STD_STR_LEN];    
    uint num_items;
    uint selected_item;
    char list_items[LIST_MAX_LEN][STD_STR_LEN];
    bool list_item_enabled[LIST_MAX_LEN];
    bool list_item_separator[LIST_MAX_LEN];
    bool show_scope;
};

struct ParamUpdateValue
{
    char value_string[STD_STR_LEN];
    char display_string[STD_STR_LEN];
    char value_tag[STD_STR_LEN]; 
    int selected_item;
};

struct EnumParamUpdate
{
    char name[STD_STR_LEN];
    uint num_items;
    uint selected_item;
    char list_items[LIST_MAX_LEN][STD_STR_LEN];
    bool wt_list;
};

struct EditName
{
    char name[STD_STR_LEN];
};

struct EditNameSelectChar
{
    uint selected_char;
};

struct EditNameChangeChar
{
    uint change_char;
};

struct ConfirmationScreen
{
    char line_1[STD_STR_LEN];
    char line_2[STD_STR_LEN];
};

struct WarningScreen
{
    bool show;
    bool show_hourglass;
    char line_1[STD_STR_LEN];
    char line_2[STD_STR_LEN];
};

struct SetSystemColour
{
    char colour[STD_STR_LEN];
};

// GUI message
struct GuiMsg
{
    GuiMsgType type;
    union {
        LeftStatus left_status;
        LayerStatus layer_status;
        MidiStatus midi_status;
        TempoStatus tempo_status;
        HomeScreen home_screen;
        ListItems list_items;
        ListSelectItem list_select_item;
        SoftButtons soft_buttons;
        SoftButtonsState soft_buttons_state;
        ParamUpdate param_update;
        ParamUpdateValue param_update_value;
        EnumParamUpdate enum_param_update;
        EditName edit_name;
        EditNameSelectChar edit_name_select_char;
        EditNameChangeChar edit_name_change_char;
        ConfirmationScreen conf_screen;
        WarningScreen warning_screen;
        SetSystemColour set_system_colour;
    };

    // Constructors
    GuiMsg() { std::memset(&param_update, 0, sizeof(param_update)); }
    GuiMsg(GuiMsgType t) : type(t) { std::memset(&param_update, 0, sizeof(param_update)); }
    ~GuiMsg() {}
};

#endif  // _GUI_MSG_H
