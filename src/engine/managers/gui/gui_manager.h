/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  gui_manager.h
 * @brief GUI Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _GUI_MANAGER_H
#define _GUI_MANAGER_H

#include <mqueue.h>
#include <fstream>
#include <map>
#include "base_manager.h"
#include "daw_manager.h"
#include "sw_manager.h"
#include "midi_device_manager.h"
#include "event.h"
#include "event_router.h"
#include "param.h"
#include "gui_msg.h"
#include "gui_state.h"

enum class SoftButton {
    SOFT_BUTTON_1,
    SOFT_BUTTON_2,
    SOFT_BUTTON_3
};

struct ModMatrixEntryIndexes
{
    uint src_index;
    uint dst_index;
    ModMatrixEntryIndexes() 
    {
        src_index = 0;
        dst_index = 0;
    };
    ModMatrixEntryIndexes(uint src, uint dst)
    {
        src_index = src;
        dst_index = dst;
    }
};

// GUI Manager class
class GuiManager: public BaseManager
{
public:
    // Constructor
    GuiManager(EventRouter *event_router);

    // Destructor
    ~GuiManager();

    // Public functions
    bool start();
    void process();
    void process_event(const BaseEvent *event);
    void process_msd_event();

private:
    // Private variables
    SwManager *_sw_manager;
    EventListener *_arp_listener;
    EventListener *_seq_listener;
    EventListener *_sfc_param_changed_listener;
    EventListener *_sfc_system_func_listener;
    EventListener *_fm_reload_presets_listener;
    EventListener *_fm_param_changed_listener;
    EventListener *_midi_event_listener;
    EventListener *_midi_param_changed_listener;
    EventListener *_midi_system_func_listener;
    EventListener *_osc_param_changed_listener;
    EventListener *_osc_system_func_listener;
    EventListener *_sw_update_system_func_listener;
    mqd_t _gui_mq_desc;
    Timer *_gui_msg_send_timer;
    Timer *_param_changed_timer;    
    Timer *_activity_timer;
    Timer *_demo_mode_timer;
    std::thread *_msd_event_thread;
    bool _run_msd_event_thread;
    uint _start_demo_mode_count;
    bool _demo_mode;
    std::mutex _gui_mutex;      
    GuiState _gui_state;
    ManagePatchState _manage_patch_state;
    SelectPatchState _select_patch_state;
    SavePatchState _save_patch_state;
    ManageLayersState _manage_layers_state;
    SaveLayersState _save_layers_state;
    SeqUiState _seq_ui_state;
    EditNameState _edit_name_state;
    SystemMenuState _system_menu_state;
    SwUpdateState _sw_update_state;
    BankManagmentState _bank_management_state;
    ImportBankState _import_bank_state;
    ExportBankState _export_bank_state;
    ClearBankState _clear_bank_state;
    WtManagmentState _wt_management_state;
    BackupState _backup_state;
    CalibrateState _calibrate_state;
    ProgressState _progress_state;
    bool _param_change_available;
    ParamChange _param_change;
    bool _show_param_list;
    Param *_param_shown_root;
    Param *_param_shown;
    int _param_shown_index;
    Param *_filename_param;
    std::chrono::_V2::steady_clock::time_point _param_shown_start_time{};
    uint _param_timeout;
    std::vector<Param *> _params_list;
    bool _editing_param;
    bool _showing_param_shortcut;
    uint _num_list_items;
    std::map<uint, std::string> _list_items;
    std::vector<std::string> _filenames;
    std::string _edit_name;
    std::string _save_edit_name;
    int _selected_layers_num;
    int _selected_layers_index;
    int _selected_bank_num;
    int _selected_bank_index;
    int _selected_patch_num;
    int _selected_patch_index;
    std::string _selected_bank_folder_name;
    int _selected_mod_matrix_src_index;
    uint _selected_mod_matrix_entry;
    uint _selected_char_index;
    uint _selected_list_char;
    uint _selected_system_menu_item;
    uint _selected_bank_management_item;
    uint _selected_bank_archive;
    std::string _selected_bank_archive_name;
    uint _selected_bank_dest;
    std::string _selected_bank_dest_name;    
    uint _selected_wt_management_item;
    uint _reload_presets_from_select_patch_load;
    std::vector<std::string> _pushed_control_states;
    std::vector<ModMatrixEntryIndexes> _mod_matrix_entry_indexes;
    std::vector<std::string> _mod_matrix_src_names;
    std::vector<std::string> _mod_matrix_dst_names;
    std::vector<std::string> _mod_matrix_states;
    std::vector<std::string> _mod_matrix_lfo_states;
    bool _seq_recording;
    bool _show_full_system_menu;
    bool _kbd_enabled;
    bool _new_mod_matrix_param_list;
    bool _showing_reset_layers_screen;
    bool _showing_wt_prune_confirm_screen;
    bool _layers_load;
    GuiScopeMode _scope_mode;
    Eg2LevelModState _eg_2_level_mod_state;
    bool _show_scope;

    // Private functions
    void _process_param_changed_event(const ParamChange &data);
    void _process_system_func_event(const SystemFunc &data);
    void _process_reload_presets(bool from_ab_toggle=false);
    void _process_midi_event(const snd_seq_event_t &seq_event);

    void _process_manage_mod_matrix(bool selected);
    void _process_manage_layer(bool selected, Param *linked_param);
    void _process_manage_layers(bool selected, bool save);
    void _process_manage_patch(bool selected, bool save_patch);
    void _process_eg_2_level_mod_dst(float value);
    void _process_arp(bool selected, Param *linked_param);
    void _process_seq_settings(bool selected, Param *linked_param);
    void _process_seq_run(bool selected);
    void _process_seq_rec(bool selected);
    void _process_kbd(bool selected, Param *linked_param);
    void _process_osc_coarse(bool selected, Param *linked_param);
    void _process_lfo_select(bool selected);
    void _process_lfo_shape(bool selected, Param *linked_param);
    void _process_wt_select(bool selected, Param *linked_param);
    void _process_type(bool selected, Param *linked_param);
    void _process_system_func_param(SystemFuncType sys_func, bool selected, Param *param);
    void _process_soft_button_1(bool selected);
    void _process_soft_button_2(bool selected);
    void _process_soft_button_3(bool selected);
    void _process_shown_param_update_data_knob(KnobParam &data_knob);
    void _process_select_layers_data_knob(KnobParam &data_knob);
    void _process_select_bank_data_knob(KnobParam &data_knob);
    void _process_select_patch_data_knob(KnobParam &data_knob);
    void _process_system_menu_data_knob(KnobParam &data_knob);
    void _process_bank_management_data_knob(KnobParam &data_knob);
    void _process_wt_management_data_knob(KnobParam &data_knob);
    void _process_manage_layer_multifn_switch(uint switch_index);
    void _process_select_bank_multifn_switch(uint switch_index);
    void _process_select_patch_multifn_switch(uint switch_index);
    void _process_manage_mod_matrix_multifn_switch(uint switch_index);
    void _process_layer_multifn_switch(uint switch_index);
    void _process_select_layers_load_enter_switch();
    void _process_select_layers_save_enter_switch();
    void _process_select_bank_enter_switch();
    void _process_select_patch_load_enter_switch();
    void _process_select_patch_save_enter_switch();
    void _process_show_param_soft_button_1();
    void _process_bank_management_soft_button_1();
    void _process_show_param_soft_button_2(bool pressed);
    void _process_select_layers_soft_button_2(bool pressed);
    void _process_select_bank_soft_button_2(bool pressed);
    void _process_select_patch_soft_button_2(bool pressed);
    void _process_show_param_soft_button_3();
    void _process_mod_matrix_dst_soft_button_3();
    void _process_system_menu_soft_button_3();
    void _process_sw_update_soft_button_3();
    void _process_bank_management_soft_button_3();
    void _process_wt_management_soft_button_3();
    void _process_backup_soft_button_3();
    void _process_edit_layers_name_exit();
    void _process_edit_bank_name_exit();
    void _process_edit_patch_name_exit();
    void _process_param_changed_timeout();
    void _process_demo_mode_timeout();

    void _show_home_screen(bool update=false);
    void _show_system_menu_screen();
    void _show_select_layers_load_screen();
    void _show_select_layers_save_screen();
    void _show_select_bank_screen();
    void _show_select_patch_load_screen();
    void _show_select_patch_save_screen();
    void _show_select_mod_matrix_dst_screen();
    void _show_edit_name_select_char_screen();
    void _show_edit_name_change_char_screen();
    void _show_system_menu_about_screen();
    void _show_start_sw_update_screen(std::string sw_version);
    void _show_finish_sw_update_screen(std::string sw_version, bool result);
    void _show_start_auto_calibrate_screen();
    void _show_finish_auto_calibrate_screen(bool result);    
    void _show_sys_menu_calibrate_screen(CalMode mode);
    void _show_sys_menu_factory_calibrate_screen();
    void _show_sys_menu_run_diag_script_confirm_screen();
    void _show_sys_menu_run_diag_script_screen();
    void _show_sys_menu_bank_management_screen();
    void _show_sys_menu_select_bank_archive_screen();
    void _show_sys_menu_select_dest_bank_screen();
    void _show_sys_menu_bank_import_method_screen();
    void _show_sys_menu_bank_import_screen(bool merge);
    void _show_sys_menu_bank_export_screen();
    void _show_sys_menu_bank_add_screen();
    void _show_sys_menu_bank_clear_confirm_screen();
    void _show_sys_menu_bank_clear_screen();
    void _show_sys_menu_wt_management_screen();
    void _show_sys_menu_wt_import_screen();
    void _show_sys_menu_wt_export_screen();
    void _show_sys_menu_wt_prune_confirm_screen();
    void _show_sys_menu_wt_prune_screen();
    void _show_sys_menu_backup_screen();
    void _show_sys_menu_restore_screen();
    void _show_sys_menu_global_settings_screen();
    void _show_reset_layers_confirm_screen();
    void _show_conf_screen(const char *line1, const char *line2);
    void _set_tempo_status(uint tempo);
    std::string _mod_matrix_param_path(uint _src_index, uint _dst_index);
    bool _mod_maxtrix_src_is_not_an_lfo(uint src_index);
    bool _mod_maxtrix_src_is_lfo_2(uint src_index);

    void _post_param_update();
    void _post_normal_param_update();
    void _post_mod_matrix_param_update();
    void _post_layer_param_update(uint layer);
    void _post_param_update_value(bool select_list_item);
    void _post_normal_param_update_value(bool select_list_item);
    void _post_mod_matrix_param_update_value(bool select_list_item);
    void _post_layer_param_update_value(bool select_list_item);    
    void _post_enum_list_param_update();
    void _post_enum_list_param_update_value(uint value);
    void _post_file_browser_param_update();
    void _post_soft_button_state_update(uint state, SoftButton soft_button);
    void _post_update_selected_list_item(uint selected_item);
    void _clear_warning_screen();
    void _post_gui_msg(const GuiMsg &msg);
    void _gui_send_callback();
    void _process_param_changed_mapped_params(const Param *param, float value, const Param *skip_param);
    void _activity_timer_callback();

    void _config_data_knob(int num_selectable_positions=-1, float pos=-1.0);
    void _config_soft_button_2(bool as_edit_button);
    void _config_sys_func_switches(bool enable);
    void _config_switch(SystemFuncType system_func_type, std::string haptic_mode);
    void _reset_sys_func_switches(SystemFuncType except_system_func_type=SystemFuncType::UNKNOWN);
    void _reset_param_shortcut_switches();
    void _set_sys_func_switch(SystemFuncType system_func_type, bool set);
    void _set_switch(std::string path, bool set);
    void _reset_multifn_switches(bool force=false);
    void _config_multifn_switches(uint num_active, int selected, MultifnSwitchesMode mode=MultifnSwitchesMode::SINGLE_SELECT);
    void _select_multifn_switch(uint index);

    void _pop_and_push_back_controls_state(std::string state);
    void _push_back_controls_state(std::string state);
    void _pop_back_controls_state();
    void _push_controls_state(std::string state);
    void _pop_controls_state(std::string state);

    std::map<uint, std::string> _parse_layers_folder();
    std::map<uint, std::string> _parse_patches_folder();
    std::map<uint, std::string> _parse_bank_folder(const std::string bank_folder_path);
    std::vector<std::string> _parse_wavetable_folder();
    bool _open_bank_folder(uint bank_index, std::string& full_path, std::string& folder_name);
    bool _get_patch_filename(uint patch_index, std::string bank_folder_path, std::string& filename);
    std::string _format_folder_name(const char *folder);
    std::string _format_filename(const char *filename);
    std::string _get_edit_name_from_index(uint index);
    int _index_from_list_items(uint key);
    std::pair<uint, std::string> _list_item_from_index(uint index);
    int _get_root_param_list_index(const Param *param);
    int _get_param_list_index(const Param *root_param, const Param *param);
    void _start_param_change_timer();
    void _stop_param_change_timer();
    void _start_demo_mode_timer();
    void _stop_demo_mode_timer();

    void _reset_gui_state_and_show_home_screen(SystemFuncType sys_func=SystemFuncType::UNKNOWN);
    void _reset_gui_state(SystemFuncType sys_func=SystemFuncType::UNKNOWN);
    void _reset_lfo_state();
    bool _can_show_param();
    bool _show_param_as_enum_list(const Param *param);
    void _set_eg_2_level_dst_control(const Param *dst_param, bool set_knob_pos);
    Param *_get_mod_matrix_eg_2_level_param();
    Param *_get_mod_matrix_morph_param();
    bool _is_mod_matrix_eg_2_src_param(const Param *param);
    bool _is_mod_matrix_eg_2_level_dst_param(const Param *param);
    bool _is_mod_matrix_morph_dst_param(const Param *param);
    bool _mod_matrix_param_active(const Param *param);

    void _start_stop_seq_run(bool start);
    void _start_stop_seq_rec(bool start);
    void _enable_kbd(bool enable);

    inline void _strcpy_to_gui_msg(char *dest, const char *src);
    bool _char_is_charset_valid(char c);
    uint _char_to_charset_index(char c);
    char _charset_index_to_char(uint index);
    void _string_toupper(std::string& str);
    std::string _to_string(int val, int width=-1);
};

#endif  // _GUI_MANAGER_H
