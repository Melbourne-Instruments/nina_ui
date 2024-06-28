/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  ui_manager.h
 * @brief UI Manager class implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/reboot.h>
#include "gui_manager.h"
#include "surface_control_manager.h"
#include "sequencer_manager.h"
#include "arpeggiator_manager.h"
#include "keyboard_manager.h"
#include "utils.h"
#include "logger.h"
#include "version.h"

// Constants
constexpr char GUI_MSG_QUEUE_NAME[]            = "/nina_msg_queue";
constexpr uint GUI_MSG_QUEUE_SIZE              = 50;
constexpr uint SEND_POLL_TIME                  = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(17)).count();
constexpr uint PARAM_CHANGED_TIMEOUT           = std::chrono::milliseconds(5000).count();
constexpr uint PARAM_CHANGED_SHOWN_THRESHOLD   = std::chrono::milliseconds(50).count();
constexpr uint ACTIVITY_TIMER_TIMEOUT          = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(500)).count();
constexpr uint DEMO_MODE_TIMEOUT_SHORT         = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(1000)).count();
constexpr uint FILENAME_MAX_SIZE               = 20;
constexpr uint NUM_CHARSET_CHARS               = (1 + 26 + 10 + 1);
constexpr uint DEFAULT_CHARSET_CHAR            = ' ';
constexpr char OSC_COARSE_STATE[]              = "osc_coarse_state";
constexpr char LFO_2_STATE[]                   = "lfo_2_state";
constexpr char LFO_1_SYNC_RATE_STATE[]         = "lfo_1_sync_rate_state";
constexpr char LFO_2_SYNC_RATE_STATE[]         = "lfo_2_sync_rate_state";
constexpr char MOD_MATRIX_LFO_1_STATE_SUFFIX[] = "_lfo_1";
constexpr char MOD_MATRIX_LFO_2_STATE_SUFFIX[] = "_lfo_2";
constexpr char PARAM_LIST_MORPH[]              = "param_list_morph";
constexpr uint MAX_MOD_MATRIX_SRC              = 16;
constexpr uint LFO_1_MOD_MAXTRIX_SRC_INDEX     = 2;
constexpr uint LFO_2_MOD_MAXTRIX_SRC_INDEX     = 14;
constexpr char VOICE_0_CALIBRATION_FILE[]      = "voice_0_filter.model";
constexpr char INIT_PATCH_LIST_TEXT[]          = "000 <INIT PATCH>";
constexpr uint WT_NUM_EXCEEDED_ERROR_CODE      = 127;
constexpr char WT_ERROR_FILENAME[]             = "/tmp/wavetable_error.txt";
constexpr uint TRUNC_SERIAL_NUM_SIZE           = 8;
constexpr char MOD_MATRIX_EG_2_SRC_NAME[]      = "Amp Envelope";

// Private variables
const char *_system_menu_options_cal[] = {
    "RE-CALIBRATE",
    "MIX VCA CALIBRATION",
    "FILTER CALIBRATION",
    "FACTORY SOAK TEST",
    "RUN DIAGNOSTIC SCRIPT",
    "GLOBAL SETTINGS",
    "BANK/PRESET MANAGEMENT",
    "WAVETABLE MANAGEMENT",
    "BACKUP",
    "RESTORE BACKUP",
    "STORE DEMO MODE: ",
    "ABOUT"
};
const char *_system_menu_options_not_cal[] = {
    "CALIBRATE",
    "MIX VCA CALIBRATION",
    "FILTER CALIBRATION",
    "FACTORY SOAK TEST",
    "RUN DIAGNOSTIC SCRIPT",
    "GLOBAL SETTINGS",
    "BANK/PRESET MANAGEMENT",
    "WAVETABLE MANAGEMENT",
    "BACKUP",
    "RESTORE BACKUP",
    "STORE DEMO MODE: ",
    "ABOUT"
};

// Static functions
static void *_process_msd_event(void* data);

//----------------------------------------------------------------------------
// GuiManager
//----------------------------------------------------------------------------
GuiManager::GuiManager(EventRouter *event_router) : 
    BaseManager(NinaModule::GUI, "GuiManager", event_router)
{
    mq_attr attr;

    // Open the GUI Message Queue
    std::memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = GUI_MSG_QUEUE_SIZE;
    attr.mq_msgsize = sizeof(GuiMsg);
    _gui_mq_desc = ::mq_open(GUI_MSG_QUEUE_NAME, (O_CREAT|O_WRONLY|O_NONBLOCK),
                             (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),
                             &attr);
    if (_gui_mq_desc == (mqd_t)-1)
    {
        // Error opening the GUI Message Queue
        MSG("ERROR: Could not open the GUI Message Queue: " << errno);
    }

    // Initialise class data
    _arp_listener = 0;
    _seq_listener = 0;
    _sfc_param_changed_listener = 0;
    _sfc_system_func_listener = 0;
    _fm_reload_presets_listener = 0;
    _fm_param_changed_listener = 0;
    _midi_event_listener = 0;
    _midi_param_changed_listener = 0;
    _midi_system_func_listener = 0;
    _osc_param_changed_listener = 0;
    _osc_system_func_listener = 0;
    _sw_update_system_func_listener = 0;
    _gui_msg_send_timer = new Timer(TimerType::PERIODIC);
    _activity_timer = new Timer(TimerType::ONE_SHOT);
    _param_changed_timer = new Timer(TimerType::ONE_SHOT);
    _demo_mode_timer = new Timer(TimerType::PERIODIC);
    _msd_event_thread = nullptr;
    _run_msd_event_thread = false;
    _demo_mode = false;
    _gui_state = GuiState::HOME_SCREEN;
    _manage_patch_state = ManagePatchState::LOAD_PATCH;
    _select_patch_state = SelectPatchState::SELECT_PATCH;
    _save_patch_state = SavePatchState::PATCH_SELECT;
    _manage_layers_state = ManageLayersState::SETUP_LAYERS;
    _save_layers_state = SaveLayersState::LAYERS_SELECT;
    _seq_ui_state = SeqUiState::SEQ_IDLE;
    _edit_name_state = EditNameState::NONE;
    _system_menu_state = SystemMenuState::SHOW_OPTIONS;
    _sw_update_state = SwUpdateState::SW_UPDATE_STARTED;
    _bank_management_state = BankManagmentState::SHOW_LIST;
    _import_bank_state = ImportBankState::NONE;
    _export_bank_state = ExportBankState::NONE;
    _clear_bank_state = ClearBankState::NONE;
    _wt_management_state = WtManagmentState::SHOW_LIST;
    _backup_state = BackupState::BACKUP_STARTED;
    _calibrate_state = CalibrateState::CALIBRATE_STARTED;
    _progress_state = ProgressState::NOT_STARTED;
    _show_param_list = false;
    _param_change_available = false;
    _param_shown_root = nullptr;
    _param_shown = nullptr;
    _param_shown_index = -1;
    _filename_param = nullptr;
    _param_timeout = 0;
    _params_list.clear();
    _editing_param = false;
    _showing_param_shortcut = false;
    _num_list_items = 0;
    _list_items.clear();
    _edit_name = "";
    _selected_layers_num = -1;
    _selected_bank_num = -1;
    _selected_patch_num = -1;
    _selected_layers_index = -1;
    _selected_bank_index = -1;
    _selected_patch_index = -1;
    _selected_bank_folder_name = "";
    _selected_mod_matrix_src_index = -1;
    _selected_mod_matrix_entry = 0;
    _selected_char_index = 0;
    _selected_list_char = 0;
    _selected_system_menu_item = 0;
    _selected_bank_management_item = 0;
    _selected_bank_archive = 0;
    _selected_bank_archive_name = "";
    _selected_bank_dest = 0;
    _selected_bank_dest_name = "";
    _selected_wt_management_item = 0;
    _reload_presets_from_select_patch_load = 0;
    _seq_recording = false;
    _show_full_system_menu = false;
    _kbd_enabled = false;
    _new_mod_matrix_param_list = false;
    _showing_reset_layers_screen = false;
    _showing_wt_prune_confirm_screen = false;
    _layers_load = false;
    _scope_mode = GuiScopeMode::SCOPE_MODE_OSC;
    _eg_2_level_mod_state = Eg2LevelModState::EG_2_LEVEL;
    _show_scope = true;
}

//----------------------------------------------------------------------------
// ~GuiManager
//----------------------------------------------------------------------------
GuiManager::~GuiManager()
{
    // Stop the demo mode timer task
    if (_demo_mode_timer)
    {
        _demo_mode_timer->stop();
        delete _demo_mode_timer;
        _demo_mode_timer = 0;
    }
    
    // Stop the GUI message timer task
    if (_gui_msg_send_timer)
    {
        _gui_msg_send_timer->stop();
        delete _gui_msg_send_timer;
        _gui_msg_send_timer = 0;
    }

    // Stop the activity timer task
    if (_activity_timer)
    {
        _activity_timer->stop();
        delete _activity_timer;
        _activity_timer = 0;
    }

    // Stop the param changed timer task
    if (_param_changed_timer)
    {
        _stop_param_change_timer();
        delete _param_changed_timer;
        _param_changed_timer = 0;
    }     

    // MSD event task running?
    if (_msd_event_thread != 0)
    {
        // Stop the MSD event task
        _run_msd_event_thread = false;
		if (_msd_event_thread->joinable())
			_msd_event_thread->join(); 
        _msd_event_thread = 0;       
    }

    // Is the GUI Message Queue open?
    if (_gui_mq_desc != (mqd_t)-1)
    {
        // Close the GUI Message Queue - don't unlink, this is done by the GUI app
        ::mq_close(_gui_mq_desc);
        _gui_mq_desc = (mqd_t)-1;
    }

    // Clean up the event listeners
    if (_arp_listener)
        delete _arp_listener;
    if (_seq_listener)
        delete _seq_listener;
    if (_sfc_param_changed_listener)
        delete _sfc_param_changed_listener;
    if (_sfc_system_func_listener)
        delete _sfc_system_func_listener;
    if (_fm_reload_presets_listener)
        delete _fm_reload_presets_listener;
    if (_fm_param_changed_listener)
        delete _fm_param_changed_listener;
    if (_midi_event_listener)
        delete _midi_event_listener;
    if (_midi_param_changed_listener)
        delete _midi_param_changed_listener;
    if (_midi_system_func_listener)
        delete _midi_system_func_listener;
    if (_osc_param_changed_listener)
        delete _osc_param_changed_listener;
    if (_osc_system_func_listener)
        delete _osc_system_func_listener;
    if (_sw_update_system_func_listener)
        delete _sw_update_system_func_listener;
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool GuiManager::start()
{
    // Get the SOFTWARE manager
    _sw_manager = static_cast<SwManager *>(utils::get_manager(NinaModule::SOFTWARE));

	// Start the GUI message send timer periodic thread
	_gui_msg_send_timer->start(SEND_POLL_TIME, std::bind(&GuiManager::_gui_send_callback, this));

    // Process the presets
    _process_reload_presets();

    // Get the Mod Matrix params
    uint index = 0;
    auto params = utils::get_mod_matrix_params();
    for (Param *p : params)
    {
        // Is this source name already in the list of names?
        if (!p->alias_param && (std::find(_mod_matrix_src_names.begin(), _mod_matrix_src_names.end(), p->mod_src_name) ==
                                          _mod_matrix_src_names.end()))
        {
            if (index < MAX_MOD_MATRIX_SRC) {
                // Add the Mod Matrix source name
                _mod_matrix_src_names.push_back(p->mod_src_name);

                // Does this param have a UI state to enter when the source is selected?
                if (p->set_ui_state.size() > 0)
                {
                    // Add the UI state
                    _mod_matrix_states.push_back(p->set_ui_state);
                    if (!_mod_maxtrix_src_is_lfo_2(index)) {
                        _mod_matrix_lfo_states.push_back(p->set_ui_state + MOD_MATRIX_LFO_2_STATE_SUFFIX);
                    }
                    else {
                        _mod_matrix_lfo_states.push_back(p->set_ui_state + MOD_MATRIX_LFO_1_STATE_SUFFIX);
                    }
                }
                else
                {
                    // Add an empty string to indicate no state
                    _mod_matrix_states.push_back("");
                    _mod_matrix_lfo_states.push_back("");
                }
                index++;
            }
        }

        // Is this destination name already in the list of names?
        if (!p->alias_param && (std::find(_mod_matrix_dst_names.begin(), _mod_matrix_dst_names.end(), p->mod_dst_name) ==
                                          _mod_matrix_dst_names.end()))
        {
            // Add the Mod Matrix destination name
            _mod_matrix_dst_names.push_back(p->mod_dst_name);
        }
    }

    // All ok, call the base manager
    return BaseManager::start();
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void GuiManager::process()
{
    // Create and add the various event listeners
    _arp_listener = new EventListener(NinaModule::ARPEGGIATOR, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_arp_listener);
    _seq_listener = new EventListener(NinaModule::SEQUENCER, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_seq_listener);
    _sfc_param_changed_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_sfc_param_changed_listener);
    _sfc_system_func_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_sfc_system_func_listener);    
    _fm_reload_presets_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_fm_reload_presets_listener);
    _fm_param_changed_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_fm_param_changed_listener);
    _midi_event_listener = new EventListener(NinaModule::MIDI_DEVICE, EventType::MIDI, this);
    _event_router->register_event_listener(_midi_event_listener);
    _midi_param_changed_listener = new EventListener(NinaModule::MIDI_DEVICE, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_midi_param_changed_listener);
    _midi_system_func_listener = new EventListener(NinaModule::MIDI_DEVICE, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_midi_system_func_listener);
    _osc_param_changed_listener = new EventListener(NinaModule::OSC, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_osc_param_changed_listener);
    _osc_system_func_listener = new EventListener(NinaModule::OSC, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_osc_system_func_listener);
    _sw_update_system_func_listener = new EventListener(NinaModule::SOFTWARE, EventType::SYSTEM_FUNC, this);
    _event_router->register_event_listener(_sw_update_system_func_listener);

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void GuiManager::process_event(const BaseEvent *event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::PARAM_CHANGED:
        {
            // Process the Param Changed event - and re-start the demo mode timer
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            _start_demo_mode_timer();            
            break;
        }

        case EventType::SYSTEM_FUNC:
        {
            // Process the System Function event - and re-start the demo mode timer
            _process_system_func_event(static_cast<const SystemFuncEvent *>(event)->system_func());
            _start_demo_mode_timer();            
            break;            
        }

        case EventType::RELOAD_PRESETS:
            // Process reloading of the presets
            // Note re-starting the demo mode is done within this function
            _process_reload_presets(static_cast<const ReloadPresetsEvent *>(event)->from_ab_toggle());
            break;

        case EventType::MIDI:
            // Process the MIDI event - and re-start the demo mode timer
            _process_midi_event(static_cast<const MidiEvent *>(event)->seq_event());
            _start_demo_mode_timer();            
            break;

        default:
            // Event unknown, we can ignore it
            break;
    }
}

//----------------------------------------------------------------------------
// process_msd_event
//----------------------------------------------------------------------------
void GuiManager::process_msd_event()
{
    bool msd_mounted = _sw_manager->msd_mounted();

    // Do until exited
    while (_run_msd_event_thread) {
        // Sleep for a second
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Get the MSD status - has it changed?
        bool current_msd_mounted = _sw_manager->msd_mounted();
        if (msd_mounted != current_msd_mounted) {
            // Refresh the System Menu if being shown
            if ((_gui_state == GuiState::SYSTEM_MENU) && (_system_menu_state == SystemMenuState::SHOW_OPTIONS)) {
                _show_system_menu_screen();
            }
            else if ((_gui_state == GuiState::BANK_MANAGMENT) && (_bank_management_state == BankManagmentState::SHOW_LIST)) {
                _show_sys_menu_bank_management_screen();
            }
            else if ((_gui_state == GuiState::WAVETABLE_MANAGEMENT) && (_wt_management_state == WtManagmentState::SHOW_LIST)) {
                _show_sys_menu_wt_management_screen();
            }
            msd_mounted = current_msd_mounted;
        }
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void GuiManager::_process_param_changed_event(const ParamChange &data)
{
    // Check for the special case of Tempo BPM - this is shown in the status bar
    if (data.path == TempoBpmParam::ParamPath()) {
        // Update the Tempo Status
        _set_tempo_status(static_cast<uint>(data.value));        
    }

    // Check if we should display this param change event
    if (data.display && _can_show_param())
    {
        // Get the GUI mutex
        std::lock_guard<std::mutex> guard(_gui_mutex);

        // Get the changed param
        auto param = utils::get_param(data.path.c_str());
        if (param) {
            // If we are currently showing a param and the param in this change event is different
            if (_param_shown && (_param_shown != param)) {
                // Check for how long we have shown the current param
                // If less than a specific threshold, then don't show this param change event
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - 
                                                                                  _param_shown_start_time).count();
                if (diff < PARAM_CHANGED_SHOWN_THRESHOLD) {
                    // Don't show this param change
                    return;
                }
            }

            // Check we can show this param (don't process surface control or state change params)
            if ((std::strlen(param->get_name()) > 0) && (param->get_param_list().size() > 0) &&
                ((param->module != NinaModule::SURFACE_CONTROL) && (param->type != ParamType::UI_STATE_CHANGE)))
            {
                _param_timeout = PARAM_CHANGED_TIMEOUT;

                // If we are showing mod matrix destinations
                if ((_gui_state == GuiState::MOD_MATRIX_DST) && _param_shown_root) {
                    // If this param is not in the root param list, don't show it
                    if (_get_root_param_list_index(param) == -1) {
                        return;
                    }
                }

                // If this param is not the currently shown param
                if (_param_shown != param) {
                    // Exit edit mode if we are currently editing
                    if (_editing_param) {
                        _editing_param = false;
                        _showing_param_shortcut = false;
                        _show_param_list = true;
                        _reset_param_shortcut_switches();                        
                        _config_soft_button_2(true);
                    }
                    // If we are in the Mod Matrix GUI state
                    else if (_gui_state == GuiState::MOD_MATRIX_DST) {
                        // Make sure the list is shown in case this param is
                        // not displayed yet
                        _show_param_list = true;
                    }
                }

                // If this param is not the root param
                int index = -1;
                if (_param_shown_root != param) {
                    // Find the param in the current root param list
                    index = _get_root_param_list_index(param);
                    if (index >= 0) {
                        // Param was found, so set it as the param to show
                        // Note: The shown index is calculated for Mod Matrix processing, so don't
                        // update it here if in the Mod Matrix state
                        _param_shown = param;
                        if (_gui_state != GuiState::MOD_MATRIX_DST) {
                            _param_shown_index = index;
                        }                        
                        _filename_param = nullptr;       
                    }
                    else {
                        // Param not found, which means this param now becomes
                        // the new root param
                        // Firstly check if there is currently a root param shown
                        if (_param_shown_root == nullptr) {
                            // No - configure soft button 2 as an EDIT button                        
                            _config_soft_button_2(true);
                        }                     
                        _param_shown_root = param;
                        if (!_show_param_list) {
                            _show_param_list = true;
                            _reset_param_shortcut_switches();
                        }
                        _set_sys_func_switch(SystemFuncType::SEQ_SETTINGS, false);
                    }
                }
                if (index == -1) {
                    // Find the param in the current root param list
                    index = _get_root_param_list_index(param);
                    if (index >= 0) {
                        // Param was found, so set it as the param to show
                        _param_shown = param;
                        _param_shown_index = index;
                        _filename_param = nullptr;
                    }                    
                }
                if (index >= 0) {
                    // Restart the param timer
                    _start_param_change_timer();

                    // Show this param change
                    _param_shown_start_time = std::chrono::steady_clock::now();
                    _param_change = data;
                    _param_change_available = true; 
                }
            }
        }
    }   
}

//----------------------------------------------------------------------------
// _process_reload_presets
//----------------------------------------------------------------------------
void GuiManager::_process_reload_presets(bool from_ab_toggle)
{
    // Always show the Tempo Status in the status bar
    auto param = utils::get_param(TempoBpmParam::ParamPath());
    if (param) {
        _set_tempo_status(static_cast<uint>(param->get_value()));
    }

    // If not loaded via the select patch load screen
    if (_reload_presets_from_select_patch_load == 0)
    {
        // If we are in the patch load state and this reload was due to an A/B toggle
        _stop_param_change_timer();
        if ((_gui_state == GuiState::MANAGE_PATCH) && (_manage_patch_state == ManagePatchState::LOAD_PATCH) && from_ab_toggle) {
            // If we are currently selecting a patch
            if (_select_patch_state == SelectPatchState::SELECT_PATCH) {
                // Update the soft buttons text
                auto msg = GuiMsg();
                msg.type = GuiMsgType::SET_SOFT_BUTTONS;
                _strcpy_to_gui_msg(msg.soft_buttons.button1, "BANK");
                _strcpy_to_gui_msg(msg.soft_buttons.button2, "RENAME");
                (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ?
                    _strcpy_to_gui_msg(msg.soft_buttons.button3, "LOAD") :
                    _strcpy_to_gui_msg(msg.soft_buttons.button3, "LOAD B");
                _post_gui_msg(msg);
            }
        }
        else {
            // Reset the UI state and show the home screen
            _config_sys_func_switches(true);
            _reset_gui_state_and_show_home_screen();
        }

        // Re-start the demo mode timer
        _start_demo_mode_timer();        
    }
    else {
        // Update the selected bank and patch numbers
        auto id = utils::get_current_layer_info().get_patch_id();
        _selected_bank_num = id.bank_num;
        _selected_patch_num = id.patch_num;

        // Layers load?
        if (_layers_load) {
            // Show the default screen
            _stop_param_change_timer();
            _config_sys_func_switches(true);
            _reset_gui_state_and_show_home_screen();
            _set_sys_func_switch(SystemFuncType::TOGGLE_PATCH_STATE, false);

            // Reset the current layer status to layer 1
            auto msg = GuiMsg();
            msg.type = GuiMsgType::SET_LAYER_STATUS;
            _strcpy_to_gui_msg(msg.layer_status.status, "L1");
            _post_gui_msg(msg); 
            _layers_load = false;
        }        
        else {
            // Reset the LFO state to show LFO 1
            utils::set_lfo_2_selected(false);
            _reset_lfo_state();
            _set_sys_func_switch(SystemFuncType::LFO_SELECT, false);

            // When a patch is loaded, OSC Tune Fine is shown, so reset OSC Coarse if set
            _set_sys_func_switch(SystemFuncType::OSC_COARSE, false);
            _pop_back_controls_state();
        }      
        _reload_presets_from_select_patch_load--;
    }
}

//----------------------------------------------------------------------------
// _process_midi_event
//----------------------------------------------------------------------------
void GuiManager::_process_midi_event(const snd_seq_event_t &seq_event)
{
    // If this is a note ON
    if (seq_event.type == snd_seq_event_type::SND_SEQ_EVENT_NOTEON) {
        // Are we editing the Layer MIDI low note filter?
        if ((_param_shown == utils::get_param_from_ref(utils::ParamRef::MIDI_LOW_NOTE_FILTER)) && _editing_param) {
            // Update the low note from the keypress
            _param_shown->set_value_from_position(seq_event.data.note.note);

            // Post a param change message
            auto param_change = ParamChange(_param_shown->get_path(), _param_shown->get_value(), module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

            // Update the value shown
            _post_param_update_value(false);
        }
        // Are we editing a Layer MIDI high note filter?
        else if ((_param_shown == utils::get_param_from_ref(utils::ParamRef::MIDI_HIGH_NOTE_FILTER)) && _editing_param) {
            // Update the high note from the keypress
            _param_shown->set_value_from_position(seq_event.data.note.note);

            // Post a param change message
            auto param_change = ParamChange(_param_shown->get_path(), _param_shown->get_value(), module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

            // Update the value shown       
            _post_param_update_value(false);
        }        
    }

    // Timer not active?
    if (!_activity_timer->is_running())
    {
        // Show the MIDI activity indicator
        auto msg = GuiMsg(GuiMsgType::SET_MIDI_STATUS);
        msg.midi_status.midi_active = true;
        _post_gui_msg(msg);
    }
    else
    {
        // Stop the timer before restarting it
        _activity_timer->stop();        
    }

    // Start the activity timer
    _activity_timer->start(ACTIVITY_TIMER_TIMEOUT, std::bind(&GuiManager::_activity_timer_callback, this));
}

//----------------------------------------------------------------------------
// _activity_timer_callback
//----------------------------------------------------------------------------
void GuiManager::_activity_timer_callback()
{
    // Clear the activity indicator
    auto msg = GuiMsg(GuiMsgType::SET_MIDI_STATUS);
    msg.midi_status.midi_active = false;    
    _post_gui_msg(msg);    
}

//----------------------------------------------------------------------------
// _process_system_func_event
//----------------------------------------------------------------------------
void GuiManager::_process_system_func_event(const SystemFunc &system_func)
{
    // Parse the system function
    switch (system_func.type)
    {
        case SystemFuncType::MOD_MATRIX:
        {
            // Process the manage Mod Matrix system function
            _process_manage_mod_matrix(system_func.value);
            break;
        }

        case SystemFuncType::LAYER:
        {
            // Process the Layer system function
            _process_manage_layer(system_func.value, system_func.linked_param);
            break;
        }

        case SystemFuncType::SELECT_PATCH:
        {
            // Process the Load Patch/Layers system function
            (_gui_state == GuiState::MANAGE_LAYERS) ?
                _process_manage_layers(system_func.value, false) :
                _process_manage_patch(system_func.value, false);
            break;
        }

        case SystemFuncType::PATCH_SAVE:
        {
            // Process the Save Patch/Layers system function
            (_gui_state == GuiState::MANAGE_LAYERS) ?
                _process_manage_layers(system_func.value, true) :
                _process_manage_patch(system_func.value, true);
            break;
            break;
        }

        case SystemFuncType::OSC_COARSE:
        {
            // Process the OSC Coarse system function
            _process_osc_coarse(system_func.value, system_func.linked_param);
            break;
        }

        case SystemFuncType::LFO_SELECT:
        {
            // Process the LFO Select system function
            _process_lfo_select(system_func.value);
            break;
        }        

        case SystemFuncType::LFO_SHAPE:
        {
            // Process the LFO Shape system function
            _process_lfo_shape(system_func.value, system_func.linked_param);
            break;
        }

        case SystemFuncType::WT_SELECT:
        {
            // Process the Wavetable Select system function
            _process_wt_select(system_func.value, system_func.linked_param);
            break;
        }

        case SystemFuncType::NOISE_TYPE:
        {
            // Process the Type system function
            _process_type(system_func.value, system_func.linked_param);
            break;
        }

        case SystemFuncType::SOFT_BUTTON1:
        {
            // Process soft button 1
            _process_soft_button_1(system_func.value);
            break;
        }
    
        case SystemFuncType::SOFT_BUTTON2:
        {
            // Process soft button 2
            _process_soft_button_2(system_func.value);
            break;
        }
    
        case SystemFuncType::SOFT_BUTTON3:
        {
            // Process soft button 3
            _process_soft_button_3(system_func.value);
            break;
        }

        case SystemFuncType::MULTIFN_SWITCH:
        {
            // Process depending on the current state
            switch (_gui_state)
            {
                case GuiState::MOD_MATRIX_DST:
                    // Process the keypress for manage Mod Matrix
                    _process_manage_mod_matrix_multifn_switch(system_func.num);
                    break;

                case GuiState::MANAGE_PATCH:
                    // If the keyboard is disabled, process the keypress for manage patch
                    if (!_kbd_enabled) {
                        (_select_patch_state == SelectPatchState::SELECT_BANK) ?
                            _process_select_bank_multifn_switch(system_func.num) :
                            _process_select_patch_multifn_switch(system_func.num);
                    }
                    break;

                case GuiState::MANAGE_LAYERS:
                    // If the keyboard is disabled, process the keypress for manage patch
                    if (!_kbd_enabled && (_manage_layers_state == ManageLayersState::SETUP_LAYERS)) {
                        _process_manage_layer_multifn_switch(system_func.num);
                    }
                    break;

                default:
                    // No processing
                    break;
            }
            break;
        }

        case SystemFuncType::DATA_KNOB:
        {
            auto knob_param = utils::get_data_knob_param();
            if (knob_param)
            {
                switch (_gui_state)
                {
                    case GuiState::PARAM_UPDATE:
                    case GuiState::MOD_MATRIX_DST:
                        // Process the data knob for a param update
                        _process_shown_param_update_data_knob(*knob_param);
                        break;

                    case GuiState::MANAGE_PATCH:
                        // Process the data knob for select bank/patch
                        _select_patch_state == SelectPatchState::SELECT_BANK ?
                            _process_select_bank_data_knob(*knob_param) :
                            _process_select_patch_data_knob(*knob_param);
                        break;

                    case GuiState::MANAGE_LAYERS:
                        // Process the data knob for select layers
                        (_manage_layers_state == ManageLayersState::SETUP_LAYERS) ?
                            _process_shown_param_update_data_knob(*knob_param) :
                            _process_select_layers_data_knob(*knob_param);
                        break;                        

                    case GuiState::SYSTEM_MENU:
                        // Process the data knob for the system menu
                        _process_system_menu_data_knob(*knob_param);
                        break;

                    case GuiState::BANK_MANAGMENT:
                        // Process the data knob for bank managenebt
                        _process_bank_management_data_knob(*knob_param);
                        break;

                    case GuiState::WAVETABLE_MANAGEMENT:
                        // Process the data knob for wavetable managenebt
                        _process_wt_management_data_knob(*knob_param);
                        break;

                    default:
                        break;          
                }
            }        
            break;                      
        }

        case SystemFuncType::EG_2_LEVEL_MOD_DST:
        {
            // Process the EG2 Level mod destination function
            _process_eg_2_level_mod_dst(system_func.value);
            break;
        }

        case SystemFuncType::ARP:
        {
            // Process the Arpeggiator function
            _process_arp(system_func.value, system_func.linked_param);
            break;             
        }

        case SystemFuncType::SEQ_SETTINGS:
        {
            // Process the Sequencer system function
            _process_seq_settings(system_func.value, system_func.linked_param);
            break;          
        }

        case SystemFuncType::SEQ_RUN:
        {
            // Process the Sequencer run function
            _process_seq_run(system_func.value);
            break;          
        }

        case SystemFuncType::SEQ_REC:
        {
            // Process the Sequencer record function
            _process_seq_rec(system_func.value);
            break;          
        }

        case SystemFuncType::KBD:
        {
            // Process the Keyboard function
            _process_kbd(system_func.value, system_func.linked_param);
            break;            
        }

        case SystemFuncType::START_SW_UPDATE:
        {
            // Show the starting software update screen
            _show_start_sw_update_screen(system_func.str_value);
            break;       
        }        

        case SystemFuncType::FINISH_SW_UPDATE:
        {
            // Show the finishing software update screen
            _show_finish_sw_update_screen(system_func.str_value, system_func.result);
            break;                       
        }

        case SystemFuncType::START_CALIBRATION:
        {
            // Show the start auto calibration screen
            _show_start_auto_calibrate_screen();
            break;       
        }        

        case SystemFuncType::FINISH_CALIBRATION:
        {
            // Show the finish auto calibratate screen
            _show_finish_auto_calibrate_screen(system_func.result);
            break;                       
        }

        case SystemFuncType::SFC_INIT:
        {
            // The surface has been initialised - clear the boot warning screen
            auto msg = GuiMsg();
            msg.type = GuiMsgType::CLEAR_BOOT_WARNING_SCREEN;
            _post_gui_msg(msg);
            break;                  
        }

        default:
            break;        
    }
}

//----------------------------------------------------------------------------
// _process_manage_mod_matrix
//----------------------------------------------------------------------------
void GuiManager::_process_manage_mod_matrix(bool selected)
{   
    // Selected functionality?
    if (selected)
    {
        // Reset the GUI state and set the state to Mod Matrix Destination
        _stop_param_change_timer();
        _reset_gui_state();
        _gui_state = GuiState::MOD_MATRIX_DST;              

        // Get the current (saved) mod matrix source
        _selected_mod_matrix_src_index = utils::system_config()->get_mod_src_num() - 1;
        _show_select_mod_matrix_dst_screen();

        // If the keyboard is active, disable it
        _enable_kbd(false);

        // Make sure the other system function switches are reset and disable the
        // param shortcut switches (except for the sequencer run)
        _reset_sys_func_switches(SystemFuncType::MOD_MATRIX);
        _config_sys_func_switches(false);

        // Set the multi-function switches to SINGLE SELECT mode for the mod matrix sources
        _config_multifn_switches(_mod_matrix_src_names.size(), _selected_mod_matrix_src_index);

        // Pop/push the new state
        _pop_and_push_back_controls_state(_mod_matrix_states[_selected_mod_matrix_src_index]);

        // If this mod matrix source is not an LFO
        if (_mod_maxtrix_src_is_not_an_lfo(_selected_mod_matrix_src_index)) {
            // Push the LFO state
            utils::push_lfo_state(utils::LfoState(_mod_matrix_states[_selected_mod_matrix_src_index], true, false));

            // If we are showing LFO 2 push it
            if (utils::lfo_2_selected()) {
                // Push the mod matrix LFO 2 state
                _push_controls_state(_mod_matrix_lfo_states[_selected_mod_matrix_src_index]);
                utils::push_lfo_state(utils::LfoState(_mod_matrix_lfo_states[_selected_mod_matrix_src_index], true, true));
            }
        }
        else {
            // Are we showing mod matrix source LFO 1?
            if (_selected_mod_matrix_src_index == LFO_1_MOD_MAXTRIX_SRC_INDEX) {
                // If LFO 2 is showing, pop it
                if (utils::lfo_2_selected()) {
                    // Pop the LFO 2 states
                    auto lfo_state = utils::get_current_lfo_state();
                    while (lfo_state.lfo_2) {
                        utils::pop_lfo_state();
                        _pop_controls_state(lfo_state.state);
                        lfo_state = utils::get_current_lfo_state();
                    }

                    // Turn the LFO 1/2 select switch OFF
                    _set_sys_func_switch(SystemFuncType::LFO_SELECT, false);                    

                    // Now showing LFO 1
                    utils::set_lfo_2_selected(false);
                }              
            }
            else {
                // If LFO 2 is not showing, push it
                if (!utils::lfo_2_selected()) {
                    // Push the normal LFO 2 state (no sync)
                    utils::push_lfo_state(utils::LfoState(LFO_2_STATE, false, true));
                    _push_controls_state(LFO_2_STATE); 
                    if (utils::lfo_2_sync_rate()) {
                        utils::push_lfo_state(utils::LfoState(LFO_2_SYNC_RATE_STATE, false, true));
                        _push_controls_state(LFO_2_SYNC_RATE_STATE);
                    }

                    // Turn the LFO 1/2 select switch ON
                    _set_sys_func_switch(SystemFuncType::LFO_SELECT, true);

                    // Now showing LFO 2
                    utils::set_lfo_2_selected(true);                                        
                }
            }
        }

        // Is the EG2 Mod source NOT selected?
        if (_param_shown && !_is_mod_matrix_eg_2_src_param(_param_shown)) {
            // By default set the EG2 Level/Morph control to show the EG2 Level destination value
            _set_eg_2_level_dst_control(_get_mod_matrix_eg_2_level_param(), true);
        }     
    }
    else
    {
        // Reset the GUI state and show the home screen
        _stop_param_change_timer();
        _config_sys_func_switches(true);
        _reset_gui_state_and_show_home_screen();
    }
}

//----------------------------------------------------------------------------
// _process_manage_layer
//----------------------------------------------------------------------------
void GuiManager::_process_manage_layer(bool selected, Param *linked_param)
{
    // Selected functionality and the param has been specified?
    if (selected && linked_param) {
        // Process this linked param
        _stop_param_change_timer();

        // The linked param will become the root param - get the index
        // of this param in the param list
        auto index = _get_param_list_index(linked_param, linked_param);
        if (index >= 0) {
            // Reset the GUI state                  
            _reset_gui_state();
            _gui_state = GuiState::MANAGE_LAYERS;
            _showing_reset_layers_screen = false;

            // Make sure the other system function switches are reset and disable the
            // configurable system function switches
            // Soft button 2 must also be configured as EDIT mode
            _reset_sys_func_switches(SystemFuncType::LAYER);
            _config_sys_func_switches(false);
            _config_soft_button_2(true);

            // If the keyboard is not enabled, configure the multi-function switches
            if (!_kbd_enabled) {
                // Set the multi-function switches to SINGLE SELECT mode
                _config_multifn_switches(NUM_LAYERS, utils::get_current_layer_info().layer_num());
            }

            // Setup the param shown settings
            _param_shown_root = linked_param;
            _param_shown = linked_param;
            _param_shown_index = index;
            _manage_layers_state = ManageLayersState::SETUP_LAYERS;

            // Show the param
            _show_scope = false;
            _post_param_update();
        }
    }
    else
    {
        // Reset the GUI state and show the home screen
        _stop_param_change_timer();
        _config_sys_func_switches(true);        
        _reset_gui_state_and_show_home_screen();
    }
}

//----------------------------------------------------------------------------
// _process_manage_layers
//----------------------------------------------------------------------------
void GuiManager::_process_manage_layers(bool selected, bool save)
{
    // Selected functionality?
    if (selected)
    {
        // Show the select layers load/save screen
        _stop_param_change_timer();
        _reset_gui_state();
        _reset_multifn_switches();
        _manage_layers_state = save ? ManageLayersState::SAVE_LAYERS : ManageLayersState::LOAD_LAYERS;
        _selected_layers_num = utils::system_config()->get_layers_num();
        if (save) {
            // Show the save layers screen
            _save_edit_name.clear();
            _show_select_layers_save_screen();

            // We can only get here via the LAYER button
            // We just need to ensure the SAVE button is set and LOAD button is reset
            _set_sys_func_switch(SystemFuncType::SELECT_PATCH, false);
            _set_sys_func_switch(SystemFuncType::PATCH_SAVE, true);            
        }
        else {
            // Show the layers load screen
            _show_select_layers_load_screen();

            // We can only get here via the LAYER button
            // We just need to ensure the LOAD button is set and SAVE button is reset
            _set_sys_func_switch(SystemFuncType::SELECT_PATCH, true);
            _set_sys_func_switch(SystemFuncType::PATCH_SAVE, false);               
        }
    }
    else
    {
        // Reset the GUI state and show the home screen
        _stop_param_change_timer();
        _config_sys_func_switches(true);
        _reset_gui_state_and_show_home_screen();
    }
}

//----------------------------------------------------------------------------
// _process_manage_patch
//----------------------------------------------------------------------------
void GuiManager::_process_manage_patch(bool selected, bool save_patch)
{
    // Selected functionality?
    if (selected)
    {
        // Show the select patch load/save screen
        _stop_param_change_timer();
        _reset_gui_state();
        _gui_state = GuiState::MANAGE_PATCH;
        _manage_patch_state = save_patch ? ManagePatchState::SAVE_PATCH : ManagePatchState::LOAD_PATCH;
        auto id = utils::get_current_layer_info().get_patch_id();
        _selected_bank_num = id.bank_num;
        if (save_patch) {
            // Show the save patch screen
            _save_edit_name.clear();
            _show_select_patch_save_screen();

            // Make sure the other system function switches are reset
            _reset_sys_func_switches(SystemFuncType::PATCH_SAVE);            
        }
        else {
            // Show the patch load screen
            _show_select_patch_load_screen();

            // Make sure the other system function switches are reset
            _reset_sys_func_switches(SystemFuncType::SELECT_PATCH);            
        }

        // If the sequencer is running, stop it
        _start_stop_seq_run(false);

        // Configure the system function switches
        _config_sys_func_switches(false);
    }
    else
    {
        // Reset the GUI state and show the home screen
        _stop_param_change_timer();
        _config_sys_func_switches(true);
        _reset_gui_state_and_show_home_screen();
    }
}

//----------------------------------------------------------------------------
// _process_eg_2_level_mod_dst
//----------------------------------------------------------------------------
void GuiManager::_process_eg_2_level_mod_dst(float value)
{
    Param *param = nullptr;

    // Are we showing EG2 Level or Morph as a destination?
    (_eg_2_level_mod_state == Eg2LevelModState::EG_2_LEVEL) ?
        param = _get_mod_matrix_eg_2_level_param() :
        param = _get_mod_matrix_morph_param();

    // Process the param if valid
    if (param) {
        // If the value has changed
        if (param->get_value() != value) {
            // Set the param value
            param->set_value(value);

            // Show this param change
            auto pc = ParamChange(param, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(pc));
            _process_param_changed_event(pc);
        }      
    }
}

//----------------------------------------------------------------------------
// _process_arp
//----------------------------------------------------------------------------
void GuiManager::_process_arp(bool selected, Param *linked_param)
{
    // Has the linked param has been specified?
    if (linked_param) {    
        // Has the APR enable changed?
        // Note: Assumes the linked param is the ARP enable param
        bool arp_enabled = linked_param->get_value() ? true : false;
        if (selected != arp_enabled) {
            // Enable or disable the ARP
            linked_param->set_value((selected ? 1.0 : 0.0));
            auto param_change = ParamChange(linked_param, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));            
        }

        // Can we show this system function related param?
        if (_gui_state < GuiState::MOD_MATRIX_DST) {
            // Stop the param change timer
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto index = _get_param_list_index(linked_param, linked_param);
            if (index >= 0) {
                // Reset the GUI state                  
                _reset_gui_state();

                // Setup the param shown settings
                _param_shown_root = linked_param;
                _param_shown = linked_param;
                _param_shown_index = index;
                _gui_state = GuiState::PARAM_UPDATE;

                // Show the param normally
                _show_scope = false;
                _config_soft_button_2(true);
                _post_param_update();
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_seq_settings
//----------------------------------------------------------------------------
void GuiManager::_process_seq_settings(bool selected, Param *linked_param)
{
    // Can we show this system function related param?
    if (_gui_state != GuiState::MOD_MATRIX_DST) {
        // Selected functionality and the param has been specified?
        if (selected && linked_param) {
            // Process this linked param
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto index = _get_param_list_index(linked_param, linked_param);
            if (index >= 0) {
                // Reset the GUI state                  
                _reset_gui_state();
                _reset_sys_func_switches(SystemFuncType::SEQ_SETTINGS);
                _reset_multifn_switches();
                _config_sys_func_switches(true);

                // Setup the param shown settings
                _param_shown_root = linked_param;
                _param_shown = linked_param;
                _param_shown_index = index;
                _gui_state = GuiState::PARAM_UPDATE;

                // Show the param normally
                _show_scope = false;
                _config_soft_button_2(true);
                _post_param_update();
            }
        }
        else
        {
            // Reset the GUI state and show the home screen
            _stop_param_change_timer();
            _reset_gui_state_and_show_home_screen();
        }
    }
}

//----------------------------------------------------------------------------
// _process_seq_run
//----------------------------------------------------------------------------
void GuiManager::_process_seq_run(bool selected)
{
    // If we can start the sequencer running
    if (_gui_state <= GuiState::MANAGE_LAYERS) {
        // If the sequencer is recording, stop it
        _start_stop_seq_rec(false);

        // Start/stop running the sequencer
        _start_stop_seq_run(selected);

        // If we are starting the sequencer run and we are currently showing the number of steps, update
        // the value shown
        if (selected && (_param_shown == utils::get_param(NinaModule::SEQUENCER, SequencerParamId::NUM_STEPS_PARAM_ID))) {
            _post_param_update_value(false);
        }        
    }
}

//----------------------------------------------------------------------------
// _process_seq_rec
//----------------------------------------------------------------------------
void GuiManager::_process_seq_rec(bool selected)
{
    // If we can start the sequencer recording
    if (_gui_state != GuiState::MOD_MATRIX_DST) {
        // Reset the GUI state
        _config_sys_func_switches(true);
        _reset_gui_state_and_show_home_screen();
        
        // Make sure that if the keyboard is active it is disabled
        _enable_kbd(false);

        // If the sequencer is running, stop it
        _start_stop_seq_run(false);

        // Start/stop recording the sequencer
        _start_stop_seq_rec(selected);

        // If we have finished recording and we are currently showing the number of steps, update
        // the value shown
        if (!selected && (_param_shown == utils::get_param(NinaModule::SEQUENCER, SequencerParamId::NUM_STEPS_PARAM_ID))) {
            _post_param_update_value(false);
        }
    }
}

//----------------------------------------------------------------------------
// _process_kbd
//----------------------------------------------------------------------------
void GuiManager::_process_kbd(bool selected, Param *linked_param)
{
    // If the keyboard state is not changing
    if (selected == _kbd_enabled) {
        // If we get there then the KBD switch has been held down for over 1s, and we
        // should now send an All Notes Off param change
        auto param = utils::get_param_from_ref(utils::ParamRef::ALL_NOTES_OFF);
        if (param) {
            // Stop the Sequencer if it is running
            _start_stop_seq_run(false);
            
            // Set the param to 1.0 and send it to all layers
            param->set_value(1.0);
            auto param_change = ParamChange(param->get_path(), param->get_value(), module());
            for (uint i=0; i<NUM_LAYERS; i++) {
                param_change.layers_mask |= LayerInfo::GetLayerMaskBit(i);
            }
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));     
        }
    }
    else {
        // Process the keyboard settings system function
        _process_system_func_param(SystemFuncType::KBD, selected, linked_param);

        // Enable/disable keyboard processing if allowed
        if ((_gui_state <= GuiState::MANAGE_PATCH) && (_gui_state != GuiState::MOD_MATRIX_DST)) {
            _enable_kbd(selected);
        }
    }
}

//----------------------------------------------------------------------------
// _process_osc_coarse
//----------------------------------------------------------------------------
void GuiManager::_process_osc_coarse(bool selected, Param *linked_param)
{
    // Process the OSC coarse system function
    _process_system_func_param(SystemFuncType::OSC_COARSE, selected, linked_param);

    // If we are not in the Mod Matrix state, push/pop the OSC coarse state
    // The OSC coarse is allowed in all states except Mod Matrix
    if (_gui_state != GuiState::MOD_MATRIX_DST) {
        selected ? 
            _pop_and_push_back_controls_state(OSC_COARSE_STATE) :
            _pop_back_controls_state();
    }
}

//----------------------------------------------------------------------------
// _process_lfo_select
//----------------------------------------------------------------------------
void GuiManager::_process_lfo_select(bool selected)
{
    // If we are not in the mod matrix state but in a state the LFO 1/2 can be selected and
    // possibly param shown
    if ((_gui_state <= GuiState::MANAGE_PATCH) && (_gui_state != GuiState::MOD_MATRIX_DST)) {
        // Set the LFO 2 selected state
        utils::set_lfo_2_selected(selected);

        // Get the LFO rate param
        auto param = (selected ? utils::get_lfo_2_rate_param() : utils::get_lfo_1_rate_param());
        if (param) {
            // Show the param if we can
            if (_gui_state <= GuiState::PARAM_UPDATE) {
                // Get the param index
                auto index = _get_param_list_index(param, param);
                if (index >= 0) {          
                    // Show this param list
                    _param_shown_root = param;
                    _param_shown = param;
                    _param_shown_index = index;
                    _show_scope = true;
                    _post_param_update();
                    _config_soft_button_2(true);
                    _reset_sys_func_switches((selected ? SystemFuncType::LFO_SELECT : SystemFuncType::UNKNOWN));
                    _start_stop_seq_rec(false);
                    _editing_param = false;
                    _showing_param_shortcut = false;
                }
            }

            // If LFO 2 is selected
            if (selected) {
                // Push the normal LFO 2 state (no sync)
                utils::push_lfo_state(utils::LfoState(LFO_2_STATE, false, true));
                _push_controls_state(LFO_2_STATE); 
                if (utils::lfo_2_sync_rate()) {
                    utils::push_lfo_state(utils::LfoState(LFO_2_SYNC_RATE_STATE, false, true));
                    _push_controls_state(LFO_2_SYNC_RATE_STATE);
                }
            }
            else {
                // Pop all LFO 2 states
                auto lfo_state = utils::get_current_lfo_state();
                while (lfo_state.lfo_2) {
                    utils::pop_lfo_state();
                    _pop_controls_state(lfo_state.state);
                    lfo_state = utils::get_current_lfo_state();
                }
            }
        }
    }
    else if (_gui_state == GuiState::MOD_MATRIX_DST) {
        // Mod Matrix - get the LFO 1/2 Rate param
        auto param = utils::get_param_from_ref((selected ? utils::ParamRef::LFO_2_RATE : utils::ParamRef::LFO_1_RATE));
        if (param) {
            // Set the LFO 2 selected state
            utils::set_lfo_2_selected(selected);
            if (selected) {
                // If the mod matrix source is not LFO 2
                if (!_mod_maxtrix_src_is_lfo_2(_selected_mod_matrix_src_index)) {
                    // Get the LFO 2 state and push it
                    _push_controls_state(_mod_matrix_lfo_states[_selected_mod_matrix_src_index]);
                    utils::push_lfo_state(utils::LfoState(_mod_matrix_lfo_states[_selected_mod_matrix_src_index], true, true));
                }
                else {
                    // Pop the LFO 1 state - there will only be one
                    auto lfo_state = utils::get_current_lfo_state();
                    if (!lfo_state.lfo_2) {
                        utils::pop_lfo_state();
                        _pop_controls_state(lfo_state.state);
                        lfo_state = utils::get_current_lfo_state();
                    }
                }              
            }
            else {
                // If the mod matrix source is not LFO 2
                if (!_mod_maxtrix_src_is_lfo_2(_selected_mod_matrix_src_index)) {
                    // Pop the LFO 2 state  - there will only be one
                    auto lfo_state = utils::get_current_lfo_state();
                    if (lfo_state.lfo_2) {
                        utils::pop_lfo_state();
                        _pop_controls_state(lfo_state.state);
                        lfo_state = utils::get_current_lfo_state();
                    }
                }
                else {
                    // Get the LFO 1 state and push it
                    _push_controls_state(_mod_matrix_lfo_states[_selected_mod_matrix_src_index]);
                    utils::push_lfo_state(utils::LfoState(_mod_matrix_lfo_states[_selected_mod_matrix_src_index], true, false));              
                }
            }            
        }        
    }
    else {
        // In all other states just make sure the button does not change state
        if (selected) {
            // If LFO 2 is not selected make sure the switch remains OFF
            if (!utils::lfo_2_selected()) {
                _set_sys_func_switch(SystemFuncType::LFO_SELECT, false);
            }
        }
        else {
            // If LFO 2 is selected make sure the switch remains ON
            if (utils::lfo_2_selected()) {
                _set_sys_func_switch(SystemFuncType::LFO_SELECT, true);
            }            
        }       
    }
}

//----------------------------------------------------------------------------
// _process_lfo_shape
//----------------------------------------------------------------------------
void GuiManager::_process_lfo_shape(bool selected, Param *linked_param)
{
    // Can we show this system function related param?
    if (_gui_state != GuiState::MOD_MATRIX_DST) {
        // Selected functionality and the param has been specified?
        if (selected && linked_param) {
            // Process this linked param
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto index = _get_param_list_index(linked_param, linked_param);
            if (index >= 0) {
                // Reset the GUI state                  
                _reset_gui_state();
                _reset_sys_func_switches(SystemFuncType::LFO_SHAPE);
                _reset_multifn_switches();
                _config_sys_func_switches(true);

                // Setup the param shown settings
                _param_shown_root = linked_param;
                _param_shown = linked_param;
                _param_shown_index = index;
                _gui_state = GuiState::PARAM_UPDATE;

                // Indicate we are editing
                _editing_param = true;
                _showing_param_shortcut = true;               

                // Show the param as an enum list                   
                _post_enum_list_param_update();
            }
        }
        else
        {
            // Reset the GUI state and show the home screen
            _stop_param_change_timer();
            _reset_gui_state_and_show_home_screen();
        }
    }
}

//----------------------------------------------------------------------------
// _process_wt_select
//----------------------------------------------------------------------------
void GuiManager::_process_wt_select(bool selected, Param *linked_param)
{
    // Can we show this system function related param?
    if (_gui_state != GuiState::MOD_MATRIX_DST) {
        // Selected functionality and the param has been specified?
        if (selected && linked_param) {
            // Process this linked param
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto filename_param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::WT_NAME_PARAM_ID);
            auto index = _get_param_list_index(linked_param, linked_param);
            if (filename_param && (index >= 0)) {
                // Parse the wavetable folder and get the number of wavetables
                _filenames = _parse_wavetable_folder();

                // Set the number of positions for this param
                linked_param->set_multi_position_param_num_positions(_filenames.size());

                // Reset the GUI state
                _reset_gui_state();
                _reset_sys_func_switches(SystemFuncType::WT_SELECT);
                _reset_multifn_switches();
                _config_sys_func_switches(true);

                // Setup the param shown settings
                _param_shown_root = linked_param;
                _param_shown = linked_param;
                _filename_param = filename_param;
                _param_shown_index = index;
                _gui_state = GuiState::PARAM_UPDATE;

                // Indicate we are editing
                _editing_param = true;
                _showing_param_shortcut = true; 

                // Show the param as a file browser
                _post_file_browser_param_update();                       
            }
        }
        else
        {
            // Reset the GUI state and show the home screen
            _stop_param_change_timer();
            _reset_gui_state_and_show_home_screen();
        }
    }
}

//----------------------------------------------------------------------------
// _process_type
//----------------------------------------------------------------------------
void GuiManager::_process_type(bool selected, Param *linked_param)
{  
    // Can we show this system function related param?
    if (_gui_state != GuiState::MOD_MATRIX_DST) {
        // Selected functionality and the param has been specified?
        if (selected && linked_param) {
            // Process this linked param
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto index = _get_param_list_index(linked_param, linked_param);
            if (index >= 0) {
                // Reset the GUI state                  
                _reset_gui_state();
                _reset_sys_func_switches(SystemFuncType::NOISE_TYPE);
                _reset_multifn_switches();
                _config_sys_func_switches(true);

                // Setup the param shown settings
                _param_shown_root = linked_param;
                _param_shown = linked_param;
                _param_shown_index = index;
                _gui_state = GuiState::PARAM_UPDATE;

                // Indicate we are editing
                _editing_param = true;
                _showing_param_shortcut = true;               

                // Show the param as an enum list                   
                _post_enum_list_param_update();
            }
        }
        else
        {
            // Reset the GUI state and show the home screen
            _stop_param_change_timer();
            _reset_gui_state_and_show_home_screen();
        }
    }
}

//----------------------------------------------------------------------------
// _process_system_func_param
//----------------------------------------------------------------------------
void GuiManager::_process_system_func_param(SystemFuncType sys_func, bool selected, Param *param)
{
    // Can we show this system function related param?
    if (_gui_state < GuiState::MOD_MATRIX_DST) {
        // Selected functionality and the param has been specified?
        if (selected && param) {
            // Process this linked param
            _stop_param_change_timer();

            // This param will become the root param - get the index
            // of this param in the param list
            auto index = _get_param_list_index(param, param);
            if (index >= 0) {
                // Reset the GUI state                  
                _reset_gui_state(sys_func);
                _reset_sys_func_switches(sys_func);

                // Setup the param shown settings
                _param_shown_root = param;
                _param_shown = param;
                _param_shown_index = index;
                _gui_state = GuiState::PARAM_UPDATE;

                // Should we show this param as an enum list?
                if (_show_param_as_enum_list(param)) {
                    // Indicate we are editing
                    _editing_param = true;
                    _showing_param_shortcut = true;               

                    // Show the param as an enum list                   
                    _post_enum_list_param_update();
                }
                else {
                    // Show the param normally
                    _show_scope = true;
                    _config_soft_button_2(true);
                    _post_param_update();
                }
            }
        }
        else
        {
            // Reset the GUI state and show the home screen
            _stop_param_change_timer();
            _reset_gui_state_and_show_home_screen(sys_func);
        }
    }
}

//----------------------------------------------------------------------------
// _process_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_soft_button_1(bool selected)
{
    // Update the soft button state
    _post_soft_button_state_update(selected, SoftButton::SOFT_BUTTON_1);

    // Only process the button press on RELEASE
    if (!selected)
    {
        // Are we currently editing a name?
        if (_edit_name_state != EditNameState::NONE)
        {
            // Exit without renaming
            _stop_param_change_timer();
            _edit_name_state = EditNameState::NONE;
            _edit_name.clear();
            if (_gui_state == GuiState::MANAGE_LAYERS) {
                (_manage_layers_state == ManageLayersState::LOAD_LAYERS) ?
                    _show_select_layers_load_screen() :
                    _show_select_layers_save_screen();
                _save_layers_state = SaveLayersState::LAYERS_SELECT;
            }
            else {
                if (_select_patch_state == SelectPatchState::SELECT_BANK) {
                    _show_select_bank_screen();
                }
                else (_manage_patch_state == ManagePatchState::LOAD_PATCH) ?
                    _show_select_patch_load_screen() :
                    _show_select_patch_save_screen();
                _save_patch_state = SavePatchState::PATCH_SELECT;
            }              
        }
        else {
            // Parse the GUI state
            switch (_gui_state) {
                case GuiState::SYSTEM_MENU: {
                    // Parse the system menu state
                    switch (_system_menu_state) {
                        case SystemMenuState::SHOW_OPTIONS:
                            // Go back to the home screen
                            _selected_system_menu_item = 0;
                            _selected_bank_management_item = 0;
                            _selected_bank_archive = 0;
                            _selected_bank_dest = 0;
                            _selected_wt_management_item = 0;
                            _show_full_system_menu = false;
                            _show_param_list = true;
                            _show_home_screen();
                            break;

                        default:
                            // Show the system menu screen
                            _show_system_menu_screen();
                            break;
                    }
                    break;
                }

                case GuiState::MANAGE_PATCH: {
                    // Are we currently showing the patch list?
                    // If so, show the select bank screen
                    if (_select_patch_state == SelectPatchState::SELECT_PATCH) {
                        // Show the select bank screen
                        _show_select_bank_screen();
                    }
                    else {
                        // Otherwise reset the GUI state and show the home screen
                        _stop_param_change_timer();
                        _config_sys_func_switches(true);
                        _reset_gui_state_and_show_home_screen();
                    }
                    break;
                }

                case GuiState::PARAM_UPDATE: {
                    // Process the button press for a param update
                    _process_show_param_soft_button_1();
                    break;
                }

                case GuiState::MANAGE_LAYERS: {
                    // If we are showing/editing the layer params
                    if (_manage_layers_state == ManageLayersState::SETUP_LAYERS) {
                        // Are we showing the reset layers screen?
                        if (_showing_reset_layers_screen) {
                            // Go back to the layer config screen
                            _show_scope = false;
                            _clear_warning_screen();
                            _post_param_update();
                            _showing_reset_layers_screen = false;
                        }
                        else {
                            _process_show_param_soft_button_1();
                        }
                    }
                    else {
                        // If in layers config load/save, reset the GUI state
                        // and show the home screen
                        _stop_param_change_timer();
                        _config_sys_func_switches(true);            
                        _reset_gui_state_and_show_home_screen();                        
                    }
                    break;
                }

                case GuiState::HOME_SCREEN:
                case GuiState::SW_UPDATE:
                case GuiState::CALIBRATE:
                    // No action
                    break;

                case GuiState::BANK_MANAGMENT:
                    // Process the button press for Bank management
                    _process_bank_management_soft_button_1();
                    break;

                case GuiState::WAVETABLE_MANAGEMENT:
                    // If showing the management list
                    if (_wt_management_state == WtManagmentState::SHOW_LIST) {
                        // Reset the GUI state and show the home screen
                        _stop_param_change_timer();
                        _config_sys_func_switches(true);            
                        _reset_gui_state_and_show_home_screen();
                    }
                    // If the wavetable operation is complete or we are showing the wavetable
                    // prune confirm screen
                    else if ((_progress_state == ProgressState::FINISHED) || (_showing_wt_prune_confirm_screen)) {
                        // Go back to the Wavetable Management Menu
                        _clear_warning_screen();  
                        _show_sys_menu_wt_management_screen();
                    }
                    break;

                case GuiState::BACKUP: {
                    // If the backup is complete
                    if (_backup_state == BackupState::BACKUP_FINISHED) {
                        // Go back to the System Menu
                        _clear_warning_screen();               
                        _show_system_menu_screen();
                    }
                    break;
                }

                case GuiState::RUN_DIAG_SCRIPT: {
                    // Go back to the System Menu
                    _clear_warning_screen();               
                    _show_system_menu_screen();
                    break;
                }

                default: {
                    // Reset the GUI state and show the home screen
                    _stop_param_change_timer();
                    _config_sys_func_switches(true);            
                    _reset_gui_state_and_show_home_screen();
                    break;
                }
            }
        }
    }   
}

//----------------------------------------------------------------------------
// _process_show_param_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_show_param_soft_button_1()
{
    // If the param shown is valid and 
    if (_param_shown) {
        // If we are showing the edited param as an enum list
        if (_editing_param && _show_param_as_enum_list(_param_shown)) {
            // Exit editing and go back to the param list
            _post_param_update();
            _config_soft_button_2(true);
            _reset_param_shortcut_switches();     
            _editing_param = false;
            _showing_param_shortcut = false;
            _filename_param = nullptr;    
            _start_param_change_timer();
        }
        else {
            // Otherwise reset the GUI state and show the home screen
            _stop_param_change_timer();
            _config_sys_func_switches(true);
            _reset_gui_state_and_show_home_screen();            
        }
    }
}

//----------------------------------------------------------------------------
// _process_bank_management_soft_button_1
//----------------------------------------------------------------------------
void GuiManager::_process_bank_management_soft_button_1()
{
    // The action here depends on the current Bank Management state
    switch (_bank_management_state) 
    {
        case BankManagmentState::SHOW_LIST:
            // Reset the GUI state and show the home screen
            _stop_param_change_timer();
            _config_sys_func_switches(true);            
            _reset_gui_state_and_show_home_screen();
            break;

        case BankManagmentState::IMPORT:
        {
            // Parse the import state
            switch (_import_bank_state) {
                case ImportBankState::SELECT_ARCHIVE: 
                    // Go back to the Bank Management Menu
                    _import_bank_state = ImportBankState::NONE;
                    _selected_bank_archive = 0;
                    _show_sys_menu_bank_management_screen();
                    break;

                case ImportBankState::SELECT_DEST:
                    // Go back to the select archive state
                    _selected_bank_dest = 0;
                    _progress_state = ProgressState::NOT_STARTED;
                    _clear_warning_screen();
                    _show_sys_menu_select_bank_archive_screen();
                    break;

                case ImportBankState::IMPORT_METHOD:
                    if (_progress_state == ProgressState::NOT_STARTED) {
                        // Go back to the select destination bank screen
                        _clear_warning_screen();
                        _show_sys_menu_select_dest_bank_screen();
                    }
                    else {
                        // Go back to the select archive state
                        _selected_bank_dest = 0;
                        _progress_state = ProgressState::NOT_STARTED;
                        _clear_warning_screen();
                        _show_sys_menu_select_bank_archive_screen();
                    }
                    break;

                default:
                    // No action
                    break;
            }
            break;      
        }

        case BankManagmentState::EXPORT:
        {
            // Parse the export state
            switch (_export_bank_state) {
                case ExportBankState::SELECT_BANK:
                    // If the export has finished
                    if (_progress_state == ProgressState::FINISHED) {
                        // Show the select destination bank screen
                        _progress_state = ProgressState::NOT_STARTED;
                        _clear_warning_screen();
                        _show_sys_menu_select_dest_bank_screen();
                    }
                    else {
                        // Go back to the Bank Management Menu
                        _export_bank_state = ExportBankState::NONE;
                        _selected_bank_dest = 0;
                        _clear_warning_screen();
                        _show_sys_menu_bank_management_screen();
                    }
                    break;

                default:
                    // No action
                    break;
            }
            break;
        }

        case BankManagmentState::ADD:
            // Go back to the Bank Management Menu
            _clear_warning_screen();
            _show_sys_menu_bank_management_screen();
            break;

        case BankManagmentState::CLEAR:
        {
            // Parse the clear bank state
            switch (_clear_bank_state) {
                case ClearBankState::SELECT_BANK: 
                    // Go back to the Bank Management Menu
                    _clear_bank_state = ClearBankState::NONE;
                    _selected_bank_dest = 0;
                    _clear_warning_screen();
                    _show_sys_menu_bank_management_screen();
                    break;

                case ClearBankState::CONFIRM:
                    if (_progress_state == ProgressState::NOT_STARTED) {
                        // Go back to the select bank screen
                        _clear_warning_screen();
                        _show_sys_menu_select_dest_bank_screen();
                    }
                    else {
                        // Go back to the Bank Management Menu
                        _clear_bank_state = ClearBankState::NONE;
                        _selected_bank_dest = 0;
                        _clear_warning_screen();
                        _show_sys_menu_bank_management_screen();                            
                    }
                    break;

                default:
                    // No action
                    break;
            }
            break;
        }

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_soft_button_2(bool selected)
{
    // Update the soft button state
    _post_soft_button_state_update(selected, SoftButton::SOFT_BUTTON_2);
    
    // If we are showing a param
    if ((_gui_state == GuiState::PARAM_UPDATE) || 
        ((_gui_state == GuiState::MOD_MATRIX_DST) && (_params_list.size() > 0))) {
        // Process the button press for a param update
        _process_show_param_soft_button_2(selected);                                      
    }
    else
    {            
        // Parse the current system state
        switch (_gui_state)
        {
            case GuiState::MANAGE_PATCH: {
                // Process the Select Bank/Patch rename/edit switch press
                (_select_patch_state == SelectPatchState::SELECT_BANK) ?
                    _process_select_bank_soft_button_2(selected) :
                    _process_select_patch_soft_button_2(selected);
                break;
            }

            case GuiState::MANAGE_LAYERS: {
                // Process the rename/edit switch press
                if (_manage_layers_state == ManageLayersState::SETUP_LAYERS)  {
                    if (!_showing_reset_layers_screen) {
                        _process_show_param_soft_button_2(selected);
                    }
                }
                else {
                    _process_select_layers_soft_button_2(selected);
                }
                break;
            }

            case GuiState::HOME_SCREEN: {
                // Set the full system menu indicator
                _show_full_system_menu = selected;
                
                // Only process the button press on RELEASE
                if (!selected) {
                    // Update the Scope mode
                    if (_scope_mode == GuiScopeMode::SCOPE_MODE_OFF) {
                        _scope_mode = GuiScopeMode::SCOPE_MODE_OSC;
                    }
                    else if (_scope_mode == GuiScopeMode::SCOPE_MODE_OSC) {
                        _scope_mode = GuiScopeMode::SCOPE_MODE_XY;
                    }
                    else {
                        _scope_mode = GuiScopeMode::SCOPE_MODE_OFF;
                    }

                    // Update the home screen
                    _show_home_screen(true);
                }           
                break;
            }

            case GuiState::BANK_MANAGMENT: {
                // If we are importing a bank and selecting the import method
                if ((_bank_management_state == BankManagmentState::IMPORT) && (_import_bank_state == ImportBankState::IMPORT_METHOD) &&
                    (_progress_state == ProgressState::NOT_STARTED)) {
                    // Show the bank import screen - merge
                    _show_sys_menu_bank_import_screen(true);
                }
                break;
            }

            default:
                break;
        }                           
    }
}

//----------------------------------------------------------------------------
// _process_show_param_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_show_param_soft_button_2(bool pressed)
{
    // If showing a param via a shortcut, this button does nothing
    if (!_showing_param_shortcut) {
        // If the button is currently pressed (held down)
        if (pressed) {
            // If the param shown is valid
            if (_param_shown) {
                // Stop the param timeout completely
                _stop_param_change_timer();

                // Indicate we are now editing
                _editing_param = true;

                // Special case - if the currently selected param is Wavetable Select
                if (_param_shown == utils::get_param_from_ref(utils::ParamRef::WT_SELECT)) {
                    // This param will become the root param - get the index
                    // of this param in the param list
                    auto filename_param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::WT_NAME_PARAM_ID);
                    if (filename_param) {
                        // Parse the wavetable folder and get the number of wavetables
                        _filenames = _parse_wavetable_folder();

                        // Set the number of positions for this param
                        _param_shown->set_multi_position_param_num_positions(_filenames.size());

                        // Setup the param shown settings
                        _filename_param = filename_param;

                        // Show the param as a file browser
                        _post_file_browser_param_update();

                        // Turn the WT Shape switch ON
                        _set_sys_func_switch(SystemFuncType::WT_SELECT, true);
                    }
                }
                // Should we show this param as an enum list?
                else if (_show_param_as_enum_list(_param_shown))
                {
                    // Show the enum param
                    _post_enum_list_param_update(); 

                    // Special case - if the currently selected param is LFO Shape or Noise Type
                    if ((_param_shown == utils::get_param_from_ref(utils::ParamRef::LFO_SHAPE)) ||
                        (_param_shown == utils::get_param_from_ref(utils::ParamRef::LFO_2_SHAPE))) {
                        // Turn the LFO Shape switch ON
                        _set_sys_func_switch(SystemFuncType::LFO_SHAPE, true);                                       
                    }
                    else if (_param_shown == utils::get_param_from_ref(utils::ParamRef::NOISE_TYPE)) {
                        // Turn the Noise Type switch ON
                        _set_sys_func_switch(SystemFuncType::NOISE_TYPE, true);                                       
                    }
                }
                else
                {
                    // If this is an enum param (not shown as a list)
                    if (_param_shown->get_num_positions() > 0) {
                        // Setup the UI for this enum param
                        _config_data_knob(_param_shown->get_num_positions());
                    }
                    else {
                        // Setup the UI for this normal param
                        _config_data_knob(-1, _param_shown->get_normalised_value());
                    }
                }                      
            }
        }
        else {
            // Reset the data knob haptic mode and restart the param timeout
            _reset_param_shortcut_switches();
            _post_param_update();
            _config_data_knob(_params_list.size());
            _editing_param = false;
            _showing_param_shortcut = false;
            _filename_param = nullptr;
            _start_param_change_timer();
        }
    }
}

//----------------------------------------------------------------------------
// _process_select_layers_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_select_layers_soft_button_2(bool pressed)
{
    // Parse the edit (layers) name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {
            // Only process the button press on RELEASE
            if (!pressed)
            {
                // If we are in the load layers state
                if (_manage_layers_state == ManageLayersState::LOAD_LAYERS) {                            
                    // Enter the rename layers state
                    _edit_name = _get_edit_name_from_index(_selected_layers_index);
                    _stop_param_change_timer();
                    _show_edit_name_select_char_screen();
                    _config_soft_button_2(false);
                }
            }
            break;
        }

        default:
        {
            // If the EDIT button is pressed (held down)
            if (pressed) {
                // Show the Change Char screen
                _show_edit_name_change_char_screen();
            }
            else {
                // Get the changed character
                char new_char = _charset_index_to_char(_selected_list_char);

                // Character changed, show the Select Char screen
                if (_selected_char_index < _edit_name.size()) {
                    _edit_name.at(_selected_char_index) = new_char;
                }
                else {               
                    _edit_name.append(1, new_char);
                }
                _show_edit_name_select_char_screen();
            }
            break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_select_bank_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_select_bank_soft_button_2(bool pressed)
{
    // Parse the edit (bank) name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {
            // Only process the button press on RELEASE
            if (!pressed)
            {                   
                // Enter the rename bank state
                _edit_name = _get_edit_name_from_index(_selected_bank_index);
                _stop_param_change_timer();
                _show_edit_name_select_char_screen();
                _config_soft_button_2(false);
            }
            break;
        }

        default:
        {
            // If the EDIT button is pressed (held down)
            if (pressed) {
                // Show the Change Char screen
                _show_edit_name_change_char_screen();
            }
            else {
                // Get the changed character
                char new_char = _charset_index_to_char(_selected_list_char);

                // Character changed, show the Select Char screen
                if (_selected_char_index < _edit_name.size()) {
                    _edit_name.at(_selected_char_index) = new_char;
                }
                else {               
                    _edit_name.append(1, new_char);
                }
                _show_edit_name_select_char_screen();
            }
            break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_select_patch_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_process_select_patch_soft_button_2(bool pressed)
{
    // Parse the edit (patch) name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {
            // Only process the button press on RELEASE
            if (!pressed)
            {            
                // If we are in the load patch state and the selected patch index is valid
                if ((_manage_patch_state == ManagePatchState::LOAD_PATCH) && _selected_patch_index) {
                    // Show the edit name screen to allow a rename
                    _edit_name = _get_edit_name_from_index(_selected_patch_index);      
                    _stop_param_change_timer();
                    _show_edit_name_select_char_screen();
                    _config_soft_button_2(false);
                }
            }
            break;
        }

        default:
        {
            // If the EDIT button is pressed (held down)
            if (pressed) {
                // Show the Change Char screen
                _show_edit_name_change_char_screen();
            }
            else {
                // Get the changed character
                char new_char = _charset_index_to_char(_selected_list_char);

                // Character changed, show the Select Char screen
                if (_selected_char_index < _edit_name.size()) {
                    _edit_name.at(_selected_char_index) = new_char;
                }
                else {               
                    _edit_name.append(1, new_char);
                }
                _show_edit_name_select_char_screen();
            }
            break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_soft_button_3
//----------------------------------------------------------------------------
void GuiManager::_process_soft_button_3(bool selected)
{
    // Update the soft button state
    _post_soft_button_state_update(selected, SoftButton::SOFT_BUTTON_3);
    if (!selected)
    {    
        // Parse the current system state
        switch (_gui_state)
        {
            case GuiState::HOME_SCREEN:
                // Show the system smenu creen
                _show_system_menu_screen();
                break;

            case GuiState::PARAM_UPDATE:
                // Process the button press while showing a param list
                _process_show_param_soft_button_3();
                break;

            case GuiState::MANAGE_PATCH:
            {
                // Selecting a Bank?
                if (_select_patch_state == SelectPatchState::SELECT_BANK) {
                    // Process the Select Bank enter switch press
                    _process_select_bank_enter_switch();
                }
                else {
                    // Process the select Patch Load/Save
                    (_manage_patch_state == ManagePatchState::LOAD_PATCH) ?
                        _process_select_patch_load_enter_switch() :
                        _process_select_patch_save_enter_switch();
                }                                      
                break;
            }

            case GuiState::MANAGE_LAYERS:
            {
                // Process the select Layers Load/Save
                if (_manage_layers_state == ManageLayersState::LOAD_LAYERS) {
                    _process_select_layers_load_enter_switch();
                }
                else if (_manage_layers_state == ManageLayersState::SAVE_LAYERS) {
                    _process_select_layers_save_enter_switch();
                }
                else {
                    // Are we showing the reset layers screen?
                    if (!_showing_reset_layers_screen) {
                        // If we are not showing an edited param as an enum list
                        if (!_editing_param || !_show_param_as_enum_list(_param_shown)) {
                            // Show the confirm layers reset screen                   
                            _show_reset_layers_confirm_screen();
                        }
                    }
                    else {
                        // Reset the layer config
                        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(RESET_LAYERS, 0, GUI)));
                        auto msg = GuiMsg();
                        msg.type = GuiMsgType::SET_LAYER_STATUS;
                        _strcpy_to_gui_msg(msg.layer_status.status, "L1");
                        _post_gui_msg(msg);
                        _showing_reset_layers_screen = false;                 
                    }
                }
                break;             
            }

            case GuiState::MOD_MATRIX_DST:
                // Process the button press while showing a mod matrix list
                _process_mod_matrix_dst_soft_button_3();
                break;

            case GuiState::SYSTEM_MENU:
                // Process the System menu button press
                _process_system_menu_soft_button_3();
                break;

            case GuiState::SW_UPDATE:
                // If the software update is complete
                if (_sw_update_state == SwUpdateState::SW_UPDATE_FINISHED) {
                    // Process the SW Update button press
                    _process_sw_update_soft_button_3();                    
                }
                break;

            case GuiState::BANK_MANAGMENT:
                // Process the Bank Management button press
                _process_bank_management_soft_button_3();
                break;

            case GuiState::WAVETABLE_MANAGEMENT:
                // Process the Wavetable Management button press
                _process_wt_management_soft_button_3();
                break;

            case GuiState::BACKUP:
                // If the backup is complete
                if (_backup_state == BackupState::BACKUP_FINISHED) {
                    // Process the Backup button press
                    _process_backup_soft_button_3();                    
                }
                break;

            case GuiState::CALIBRATE:
                // If the calibrate is complete
                if (_calibrate_state == CalibrateState::CALIBRATE_FINISHED) {
                    // Process the Calibrate button press (same as SW update)
                    _process_sw_update_soft_button_3();                    
                }
                break;

            case GuiState::RUN_DIAG_SCRIPT:
                // If the script has not yet been run
                if (_progress_state == ProgressState::NOT_STARTED) {
                    // Run the diagnostic script
                    _show_sys_menu_run_diag_script_screen();
                }
                break;

            default:
                break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_show_param_soft_button_3
//----------------------------------------------------------------------------
void GuiManager::_process_show_param_soft_button_3()
{
    // If we are morphiong, save the morph to the current state
    if (_param_shown->param_list_name == PARAM_LIST_MORPH) {
        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SAVE_MORPH, 0, GUI)));
    }
}

//----------------------------------------------------------------------------
// _process_mod_matrix_dst_soft_button_3
//----------------------------------------------------------------------------
void GuiManager::_process_mod_matrix_dst_soft_button_3()
{
    // If the param shown is valid
    if (_param_shown && (_params_list.size() > 0)) {
        // Reset the mod matrix entry
        _param_shown->set_value(0.5);

        // Post a param change message
        auto param_change = ParamChange(_param_shown->get_path(), _param_shown->get_value(), module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
        _process_param_changed_mapped_params(_param_shown, _param_shown->get_value(), nullptr);

        // Update the list
        _show_param_list = false;
        _post_param_update();
    }
}

//----------------------------------------------------------------------------
// _process_system_menu_soft_button_3
//----------------------------------------------------------------------------
void GuiManager::_process_system_menu_soft_button_3()
{
    // Parse the system menu state
    switch (_system_menu_state)
    {
        case SystemMenuState::SHOW_OPTIONS:
        {
            auto selected_sys_menu_item = _selected_system_menu_item;

            // Adjust the selected item for full or normal system menu
            if (!_show_full_system_menu) {
                selected_sys_menu_item += SystemMenuOption::GLOBAL_SETTINGS;
            }

            // Action the selected item
            switch (selected_sys_menu_item) {
                case SystemMenuOption::CALIBRATE:
                    // Show the system menu calibrate screen
                    _show_sys_menu_calibrate_screen(CalMode::ALL);
                    break;

                case SystemMenuOption::CAL_VCA:
                    // Show the system menu Mix VCA calibrate screen
                    _show_sys_menu_calibrate_screen(CalMode::MIX_VCA);
                    break;

                case SystemMenuOption::CAL_FILTER:
                    // Show the system menu Filter calibrate screen
                    _show_sys_menu_calibrate_screen(CalMode::FILTER);
                    break;

                case SystemMenuOption::FACTORY_CALIBRATE:
                    // Show the system menu factory calibrate screen
                    _show_sys_menu_factory_calibrate_screen();
                    break;

                case SystemMenuOption::RUN_DIAG_SCRIPT:
                {
                    // If the MSD is mounted and contains the diagnostic script
                    if (_sw_manager->diag_script_present()) {
                        // Show the system menu run diagnostic script screen
                        _show_sys_menu_run_diag_script_confirm_screen();
                    }
                    break;
                }

                case SystemMenuOption::GLOBAL_SETTINGS:
                    // Show the global settings
                    _show_sys_menu_global_settings_screen();
                    break;

                case SystemMenuOption::BANK_MANAGMENT:
                    // Show the Bank Management options
                    _selected_bank_management_item = 0;
                    _selected_bank_archive = 0;
                    _selected_bank_dest = 0;
                    _import_bank_state = ImportBankState::NONE;
                    _export_bank_state = ExportBankState::NONE;
                    _clear_bank_state = ClearBankState::NONE;
                    _show_sys_menu_bank_management_screen();
                    break;

                case SystemMenuOption::WAVETABLE_MANAGEMENT:
                    // Show the Wavetable Management options
                    _show_sys_menu_wt_management_screen();
                    break;

                case SystemMenuOption::BACKUP:
                {
                    // If an MSD is mounted
                    if (_sw_manager->msd_mounted()) {
                        // Show the system menu backup screen
                        _show_sys_menu_backup_screen();      
                    }
                    break;
                }

                case SystemMenuOption::RESTORE_BACKUP:
                {
                    // If the MSD is mounted and contains at least one restore backup archive
                    if (_sw_manager->restore_backup_archives_present()) {
                        // Show the system menu restore screen
                        _show_sys_menu_restore_screen();                                        
                    }
                    break;                  
                }

                case SystemMenuOption::STORE_DEMO_MODE:
                {
                    // Toggle store demo-mode ON/OFF
                    utils::system_config()->set_demo_mode(!utils::system_config()->get_demo_mode());
                    _show_system_menu_screen();
                    utils::system_config()->get_demo_mode() ?
                        _start_demo_mode_timer() :
                        _stop_demo_mode_timer();
                    _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SAVE_DEMO_MODE, 0, GUI)));                    
                    break;
                }

                case SystemMenuOption::ABOUT:
                    // Show the about screen
                    _show_system_menu_about_screen();
                    break;                

                default:
                    // No processing
                    break;
            }
            break;
        }

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_sw_update_soft_button_3
//----------------------------------------------------------------------------
void GuiManager::_process_sw_update_soft_button_3()
{
    // Reboot Nina
    MSG("Rebooting Nina...");
    sync();
    reboot(RB_AUTOBOOT);
}

//----------------------------------------------------------------------------
// _process_bank_management_soft_button_3
//----------------------------------------------------------------------------
void GuiManager::_process_bank_management_soft_button_3()
{
    if (_bank_management_state == BankManagmentState::SHOW_LIST) {
        bool set_state = true;

        // Set the new Bank Management state
        auto state = static_cast<BankManagmentState>(_selected_bank_management_item + (uint)BankManagmentState::IMPORT);
        if (((state == BankManagmentState::IMPORT) && !_sw_manager->bank_archive_present()) ||
            ((state == BankManagmentState::EXPORT) && !_sw_manager->msd_mounted())) {
            set_state = false;
        }
        if (set_state) {
            _bank_management_state = state;
        } 
    }
    switch (_bank_management_state) 
    {
        case BankManagmentState::IMPORT:
        {
            // Parse the import state
            switch (_import_bank_state) {
                case ImportBankState::NONE: {
                    // If the import archive is present
                    if (_sw_manager->bank_archive_present()) {
                        // Show the bank import select bank archive screen
                        _show_sys_menu_select_bank_archive_screen();
                    }
                    break;
                }

                case ImportBankState::SELECT_ARCHIVE:
                    // Archive selected, show the select destination bank screen
                    _show_sys_menu_select_dest_bank_screen();
                    break;

                case ImportBankState::SELECT_DEST:
                    // Destination bank selected, show the import method screen
                    _show_sys_menu_bank_import_method_screen();
                    break;

                case ImportBankState::IMPORT_METHOD:
                    // If the export has finished
                    if (_progress_state == ProgressState::FINISHED) {
                        // Unmount the MSD and go back to the Bank Management Menu
                        _sw_manager->umount_msd();
                        _import_bank_state = ImportBankState::NONE;
                        _selected_bank_archive = 0;
                        _clear_warning_screen();
                        _show_sys_menu_bank_management_screen();                        
                    }
                    else if (_progress_state == ProgressState::NOT_STARTED) {
                        // Show the bank import screen - overwrite
                        _show_sys_menu_bank_import_screen(false);
                    }
                    break;

                default:
                    // No action
                    break;
            }
            break;             
        }

        case BankManagmentState::EXPORT:
        {
            // Parse the export state
            switch (_export_bank_state) {
                case ExportBankState::NONE: {
                    // If the MSD is mounted
                    if (_sw_manager->msd_mounted()) {
                        // Archive selected, show the select bank screen
                        _show_sys_menu_select_dest_bank_screen();
                    }
                    break;
                }

                case ExportBankState::SELECT_BANK:
                    // If the export has finished
                    if (_progress_state == ProgressState::FINISHED) {
                        // Unmount the MSD and go back to the Bank Management Menu
                        _sw_manager->umount_msd();
                        _export_bank_state = ExportBankState::NONE;
                        _selected_bank_dest = 0;
                        _clear_warning_screen();
                        _show_sys_menu_bank_management_screen();
                    }
                    else {
                        // Show the bank export scteen
                        _show_sys_menu_bank_export_screen();
                    }
                    break;

                default:
                    // No action
                    break;
            }          
            break;             
        }

        case BankManagmentState::ADD:
            // Show the add bank screen
            if (_progress_state == ProgressState::NOT_STARTED) {
                _show_sys_menu_bank_add_screen();
            }   
            break;

        case BankManagmentState::CLEAR:
            // Parse the clear state
            switch (_clear_bank_state) {
                case ClearBankState::NONE: {
                    // Show the select bank screen
                    _show_sys_menu_select_dest_bank_screen();
                    break;
                }

                case ClearBankState::SELECT_BANK:
                    // Show the clear bank confirm screen
                    _show_sys_menu_bank_clear_confirm_screen();
                    break;

                case ClearBankState::CONFIRM:
                    // Show the clean bank screen
                    _show_sys_menu_bank_clear_screen();
                    break;

                default:
                    // No action
                    break;
            }          
            break;

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_wt_management_soft_button_3
//----------------------------------------------------------------------------
void GuiManager::_process_wt_management_soft_button_3()
{
    // Process the selected wavetable management action
    uint selected_item = _selected_wt_management_item + (uint)WtManagmentState::IMPORT;
    switch (selected_item) 
    {
        case (uint)WtManagmentState::IMPORT:
        {
            // If the import archive is present
            if ((_progress_state == ProgressState::NOT_STARTED) && _sw_manager->wt_archive_present()) {
                // Show the wavetable import screen
                _show_sys_menu_wt_import_screen();
            }
            else if (_progress_state == ProgressState::FINISHED) {
                // Unmount the MSD
                _sw_manager->umount_msd();
                _clear_warning_screen();
                _show_sys_menu_wt_management_screen();
            }
            break;             
        }

        case (uint)WtManagmentState::EXPORT:
        {
            // If the MSD is mounted
            if ((_progress_state == ProgressState::NOT_STARTED) && _sw_manager->msd_mounted()) {
                // Show the wavetable export screen
                _show_sys_menu_wt_export_screen();
            }
            else if (_progress_state == ProgressState::FINISHED) {
                // Unmount the MSD
                _sw_manager->umount_msd();
                _clear_warning_screen();
                _show_sys_menu_wt_management_screen();
            }            
            break;             
        }

        case (uint)WtManagmentState::PRUNE:
            // Show the wavetable prune screen
            if (_progress_state == ProgressState::NOT_STARTED) {
                (_showing_wt_prune_confirm_screen) ?
                    _show_sys_menu_wt_prune_screen() :
                    _show_sys_menu_wt_prune_confirm_screen();
            }
            break;

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_backup_soft_button_3
//----------------------------------------------------------------------------
void GuiManager::_process_backup_soft_button_3()
{
    // Unmount the USB drive
    _sw_manager->umount_msd();

    // Return to the system menu screen
    _clear_warning_screen();
    _show_system_menu_screen();
}

//----------------------------------------------------------------------------
// _process_select_layers_load_enter_switch
//----------------------------------------------------------------------------
void GuiManager::_process_select_layers_load_enter_switch()
{
    // Parse the edit name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {
            // Load the specified layers immediately - get the layer number to load
            auto layers_item = _list_item_from_index(_selected_layers_index);
            if (layers_item.first > 0) {
                // Load this layers config
                _selected_layers_num = layers_item.first;
                _reload_presets_from_select_patch_load++;
                _layers_load = true;
                _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::LOAD_LAYERS, _selected_layers_num, GUI)));                                  
            }
            break;
        }

        default:
            // Process the edit layers name exit
            _process_edit_layers_name_exit();
            break;
    }        
}

//----------------------------------------------------------------------------
// _process_select_layers_save_enter_switch
//----------------------------------------------------------------------------
void GuiManager::_process_select_layers_save_enter_switch()
{
    // Layers selected for saving?
    if (_save_layers_state == SaveLayersState::LAYERS_SELECT) {
        // Show the edit name screen to allow a rename
        _edit_name =_save_edit_name;  
        _stop_param_change_timer();
        _show_edit_name_select_char_screen();
        _config_soft_button_2(false);
        _save_layers_state = SaveLayersState::LAYERS_SAVE;
    }
    else {
        // Save the layers - firstly rename if required
        _process_edit_layers_name_exit();

        // Now save the layers config
        auto selected_layers = _list_item_from_index(_selected_layers_index);
        _selected_layers_num = selected_layers.first;           
        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SAVE_LAYERS, _selected_layers_num, GUI)));

        // Show a confirmations creen
        _show_conf_screen(_format_filename(selected_layers.second.c_str()).c_str(), "LAYERS SAVED");

        // Show the default screen
        _stop_param_change_timer();
        _config_sys_func_switches(true);
        _reset_gui_state_and_show_home_screen();        
    }
}

//----------------------------------------------------------------------------
// _process_select_bank_enter_switch
//----------------------------------------------------------------------------
void GuiManager::_process_select_bank_enter_switch()
{
    // Parse the edit name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {
            // Get the selected bank
            _stop_param_change_timer();
            auto selected_bank = _list_item_from_index(_selected_bank_index);
            _selected_bank_num = selected_bank.first;
            _selected_bank_folder_name = selected_bank.second;
            if (_manage_patch_state == ManagePatchState::LOAD_PATCH)
            {
                // Show the Select Patch Load screen
                _show_select_patch_load_screen();
            }
            else
            {
                // Show the Select Patch Save screen
                _show_select_patch_save_screen();
            }        
            break;
        }

        default:
            // Process the edit bank name exit
            _process_edit_bank_name_exit();
            break;
    }
}

//----------------------------------------------------------------------------
// _process_select_patch_load_enter_switch
//----------------------------------------------------------------------------
void GuiManager::_process_select_patch_load_enter_switch()
{
    // Parse the edit name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {
            auto id = PatchId();

            // Patch index zero is a special case used to initialise the patch
            if (_selected_patch_index) {
                // Load the specified patch immediately - get the patch number to load
                auto patch_item = _list_item_from_index(_selected_patch_index);
                if (patch_item.first > 0) {
                    // Load this patch
                    _selected_patch_num = patch_item.first;
                    id.bank_num = _selected_bank_num;
                    id.patch_num = _selected_patch_num;
                    _reload_presets_from_select_patch_load++;
                    auto sys_func = (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ? 
                                        SystemFuncType::LOAD_PATCH :
                                        SystemFuncType::LOAD_PATCH_STATE_B;
                    _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(sys_func, id, GUI)));
                }
            }
            else {
                // The user has selected to initialise the patch
                // Update the patch load screen - this re-selects the current patch
                _show_select_patch_load_screen();
                _set_sys_func_switch(SystemFuncType::TOGGLE_PATCH_STATE, false);

                // Load the init patch, overwriting the current patch values
                _reload_presets_from_select_patch_load++;
                _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::INIT_PATCH, 0, GUI)));
            }
            break;
        }

        default:
            // Process the edit patch name exit
            _process_edit_patch_name_exit();
            break;
    }        
}

//----------------------------------------------------------------------------
// _process_select_patch_save_enter_switch
//----------------------------------------------------------------------------
void GuiManager::_process_select_patch_save_enter_switch()
{
    // Patch selected for saving?
    if (_save_patch_state == SavePatchState::PATCH_SELECT) {
        // Show the edit name screen to allow a rename
        _edit_name =_save_edit_name;  
        _stop_param_change_timer();
        _show_edit_name_select_char_screen();
        _config_soft_button_2(false);
        _save_patch_state = SavePatchState::PATCH_SAVE;
    }
    else {
        // Save the patch - firstly rename if required
        _process_edit_patch_name_exit();

        // Now save the patch
        auto id = PatchId();
        auto selected_patch = _list_item_from_index(_selected_patch_index);
        _selected_patch_num = selected_patch.first;
        id.bank_num = _selected_bank_num;
        id.patch_num = _selected_patch_num;
        utils::get_current_layer_info().set_patch_id(id);
        utils::get_current_layer_info().set_patch_modified(false);             
        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SAVE_PATCH, id, GUI)));

        // Show a confirmations creen
        _show_conf_screen(_format_filename(selected_patch.second.c_str()).c_str(), "PRESET SAVED");

        // Show the default screen
        _stop_param_change_timer();
        _config_sys_func_switches(true);
        _reset_gui_state_and_show_home_screen();        
    }
}

//----------------------------------------------------------------------------
// _process_edit_layers_name_exit
//----------------------------------------------------------------------------
void GuiManager::_process_edit_layers_name_exit()
{
    // Was the patch name changed?
    if (_edit_name.size() > 0) 
    {
        // Right trim all characters after the cursor
        _edit_name = _edit_name.substr(0, _selected_char_index + 1);

        // Also right trim any whitespace
        auto end = _edit_name.find_last_not_of(" ");
        if (end != std::string::npos) {
            _edit_name = _edit_name.substr(0, end + 1);
        }

        // Append the prefix to the edited name
        auto num = _list_item_from_index(_selected_layers_index).first;
        _edit_name = _list_items[num].substr(0,4) + _edit_name;

        // Has the name actually changed?
        if (std::strcmp(_edit_name.c_str(), _list_items[num].c_str()) != 0)
        {
            std::string org_filename;
            std::string new_filename;

            // Create the original and new patch filenames (full path)
            org_filename = NINA_LAYERS_DIR + _list_items[num] + ".json";
            new_filename = NINA_LAYERS_DIR + _edit_name + ".json";
            org_filename = std::regex_replace(org_filename, std::regex{" "}, "_");
            new_filename = std::regex_replace(new_filename, std::regex{" "}, "_");

            // Does the original file exist? It may not if we have loaded the default layer
            if (std::filesystem::exists(org_filename)) {            
                // Rename it....
                int ret = ::rename(org_filename.c_str(), new_filename.c_str());
                if (ret == 0) {
                    _list_items[num] = _edit_name;
                }
                else {
                    MSG("Error renaming layers: " << org_filename << " to: " << new_filename);
                    NINA_LOG_ERROR(module(), "Error renaming Layers file: {} to {}", org_filename, new_filename);
                }
            }
            else {
                // The original file does not exist, probably because we have loaded the default layer
                // Just create the new file as an empty file
                std::ofstream(new_filename).flush();
                _list_items[num] = _edit_name;           
            }
        }
    }

    // Reset the edit name state, and show the select patch load/save screen
    _edit_name_state = EditNameState::NONE;
    _edit_name.clear();
    _stop_param_change_timer();   
    if (_manage_layers_state == ManageLayersState::LOAD_LAYERS) {
        _show_select_layers_load_screen();
    }
}

//----------------------------------------------------------------------------
// _process_edit_bank_name_exit
//----------------------------------------------------------------------------
void GuiManager::_process_edit_bank_name_exit()
{
    // Was the bank name changed?
    if (_edit_name.size() > 0) {
        // Right trim all characters after the cursor
        _edit_name = _edit_name.substr(0, _selected_char_index + 1);

        // Also right trim any whitespace
        auto end = _edit_name.find_last_not_of(" ");
        if (end != std::string::npos) {
            _edit_name = _edit_name.substr(0, end + 1);
        }

        // Append the prefix to the edited name
        auto num = _list_item_from_index(_selected_bank_index).first;
        _edit_name = _list_items[num].substr(0,4) + _edit_name;

        // Has the name actually changed?
        if (std::strcmp(_edit_name.c_str(), _list_items[num].c_str()) != 0)
        {
            std::string org_folder;
            std::string new_folder;

            // Create the original and new bank folder names (full path)
            org_folder = NINA_PATCHES_DIR + _list_items[num];
            new_folder = NINA_PATCHES_DIR + _edit_name;
            org_folder = std::regex_replace(org_folder, std::regex{" "}, "_");
            new_folder = std::regex_replace(new_folder, std::regex{" "}, "_");
            
            // Rename it....
            int ret = ::rename(org_folder.c_str(), new_folder.c_str());
            if (ret != 0) {
                MSG("Error renaming bank: " << org_folder << " to: " << new_folder);
                NINA_LOG_ERROR(module(), "Error renaming Bank folder: {} to {}", org_folder, new_folder);
            }
        }
    }

    // Reset the edit name state, and show the Select Bank screen
    _edit_name_state = EditNameState::NONE;
    _edit_name.clear();
    _stop_param_change_timer();  
    _show_select_bank_screen();
}

//----------------------------------------------------------------------------
// _process_edit_patch_name_exit
//----------------------------------------------------------------------------
void GuiManager::_process_edit_patch_name_exit()
{
    // Was the patch name changed?
    if (_edit_name.size() > 0) 
    {
        // Right trim all characters after the cursor
        _edit_name = _edit_name.substr(0, _selected_char_index + 1);

        // Also right trim any whitespace
        auto end = _edit_name.find_last_not_of(" ");
        if (end != std::string::npos) {
            _edit_name = _edit_name.substr(0, end + 1);
        }

        // Save the edit name so we can use it as the Layer filename tag
        auto filename_tag = _edit_name;

        // Append the prefix to the edited name
        auto num = _list_item_from_index(_selected_patch_index).first;
        _edit_name = _list_items[num].substr(0,4) + _edit_name;

        // Has the name actually changed?
        if (std::strcmp(_edit_name.c_str(), _list_items[num].c_str()) != 0)
        {
            std::string patch_folder;
            std::string org_filename;
            std::string new_filename;
            bool renamed = true;

            // Create the original and new patch filenames (full path)
            patch_folder = NINA_PATCHES_DIR + _selected_bank_folder_name + "/";
            org_filename = patch_folder + _list_items[num] + ".json";
            new_filename = patch_folder + _edit_name + ".json";
            org_filename = std::regex_replace(org_filename, std::regex{" "}, "_");
            new_filename = std::regex_replace(new_filename, std::regex{" "}, "_");

            // Does the original file exist? It may not if we have loaded the default patch
            if (std::filesystem::exists(org_filename)) {
                // It exists, so rename it....
                int ret = ::rename(org_filename.c_str(), new_filename.c_str());
                if (ret == 0) {
                    _list_items[num] = _edit_name;
                }
                else {
                    MSG("Error renaming patch: " << org_filename << " to: " << new_filename);
                    NINA_LOG_ERROR(module(), "Error renaming Patch file: {} to {}", org_filename, new_filename);
                    renamed = false;
                }
            }
            else {
                // The original file does not exist, probably because we have loaded the default patch
                // Just create the new file as an empty file
                std::ofstream(new_filename).flush();
                _list_items[num] = _edit_name;
            }

            // Was the rename successful?
            if (renamed) {
                // Get the Current Layer param so we can update any of the filename tags
                auto param = utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER);
                if (param) {   
                    auto patch_id = utils::get_current_layer_info().get_patch_id();

                    // We need to check if any Layers are using this patch, and if so set the new
                    // filename tag for that layer
                    for (uint i=0; i<NUM_LAYERS; i++) {
                        // Is this Layer using this patch?
                        if (utils::get_layer_info(i).get_patch_id() == patch_id) {
                            // Update the filename tag
                            param->value_tags.at(i) = filename_tag;
                        }
                    }
                }
            }
        }
    }

    // Reset the edit name state, and show the select patch load/save screen
    _edit_name_state = EditNameState::NONE;
    _edit_name.clear();
    _stop_param_change_timer();    
    if (_manage_patch_state == ManagePatchState::LOAD_PATCH) {
        _show_select_patch_load_screen();
    }
}

//----------------------------------------------------------------------------
// _process_manage_mod_matrix_multifn_switch
//----------------------------------------------------------------------------
void GuiManager::_process_manage_mod_matrix_multifn_switch(uint switch_index)
{
    // Has the index changed?
    if (switch_index != (uint)_selected_mod_matrix_src_index) {
        auto prev_index = _selected_mod_matrix_src_index;

        // Update the selected mod matrix index
        _selected_mod_matrix_src_index = switch_index;
        _eg_2_level_mod_state = Eg2LevelModState::EG_2_LEVEL;
        _show_select_mod_matrix_dst_screen();

        // Is EG 2 mod source NOT selected?
        if (_param_shown && !_is_mod_matrix_eg_2_src_param(_param_shown)) {
            // By default set the EG2 Level/Morph knob to show the EG2 Level destination value
            // Note: No need to actually set the knob position, this is done when the new state is pushed
            _set_eg_2_level_dst_control(_get_mod_matrix_eg_2_level_param(), false);
        }

        // If the previous mod matrix source is not LFO 2
        auto lfo_state = utils::get_current_lfo_state();
        if (!_mod_maxtrix_src_is_lfo_2(prev_index)) {
            // Check if there is a mod matrix LFO 2 state we need to pop first
            if (lfo_state.mod_matrix && lfo_state.lfo_2) {
                // Pop the mod matrix LFO state
                _pop_controls_state(lfo_state.state);
                utils::pop_lfo_state();
            }
        }
        else {
            // Check if there is a mod matrix LFO 1 state we need to pop first
            if (lfo_state.mod_matrix && !lfo_state.lfo_2) {
                // Pop the mod matrix LFO state
                _pop_controls_state(lfo_state.state);
                utils::pop_lfo_state();
            }            
        }

        // If there is also a normal mod matrix state, pop that from the LFO states
        lfo_state = utils::get_current_lfo_state();
        if (lfo_state.mod_matrix) {
            utils::pop_lfo_state();
        }

        // Pop/push the new state
        _pop_and_push_back_controls_state(_mod_matrix_states[_selected_mod_matrix_src_index]);

        // If this mod matrix source has an LFO state
        if (_mod_maxtrix_src_is_not_an_lfo(_selected_mod_matrix_src_index)) {
            // Push the LFO state
             utils::push_lfo_state(utils::LfoState(_mod_matrix_states[_selected_mod_matrix_src_index], true, false));
    
            // If we are showing LFO 2 push it
            if (utils::lfo_2_selected()) {
                // Push the mod matrix LFO 2 state
                _push_controls_state(_mod_matrix_lfo_states[_selected_mod_matrix_src_index]);
                utils::push_lfo_state(utils::LfoState(_mod_matrix_lfo_states[_selected_mod_matrix_src_index], true, true));
            }
        }
        else {
            // Are we now showing mod matrix source LFO 1?
            if (_selected_mod_matrix_src_index == LFO_1_MOD_MAXTRIX_SRC_INDEX) {
                // If LFO 2 is showing, or the previous source was LFO 2, pop it
                if (utils::lfo_2_selected() || (prev_index == LFO_2_MOD_MAXTRIX_SRC_INDEX)) {                
                    // Pop the LFO 2 states
                    auto lfo_state = utils::get_current_lfo_state();
                    while (lfo_state.lfo_2) {
                        utils::pop_lfo_state();
                        _pop_controls_state(lfo_state.state);
                        lfo_state = utils::get_current_lfo_state();
                    }

                    // Turn the LFO 1/2 select switch OFF
                    _set_sys_func_switch(SystemFuncType::LFO_SELECT, false);                    

                    // Now showing LFO 1
                    utils::set_lfo_2_selected(false);
                }            
            }
            else {
                // If LFO 2 is not showing, or the previous source was LFO 1, push it
                if (!utils::lfo_2_selected() || (prev_index == LFO_1_MOD_MAXTRIX_SRC_INDEX)) {                 
                    // Push the normal LFO 2 state (no sync)
                    utils::push_lfo_state(utils::LfoState(LFO_2_STATE, false, true));
                    _push_controls_state(LFO_2_STATE); 
                    if (utils::lfo_2_sync_rate()) {
                        utils::push_lfo_state(utils::LfoState(LFO_2_SYNC_RATE_STATE, false, true));
                        _push_controls_state(LFO_2_SYNC_RATE_STATE);
                    }

                    // Turn the LFO 1/2 select switch ON
                    _set_sys_func_switch(SystemFuncType::LFO_SELECT, true);

                    // Now showing LFO 2
                    utils::set_lfo_2_selected(true);
                }                                      
            }
        }

        // Send an event to indicate the mod matrix index/num has changed
        _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(SystemFuncType::SET_MOD_SRC_NUM, _selected_mod_matrix_src_index, GUI)));      
    }   
}

//----------------------------------------------------------------------------
// _process_manage_layer_multifn_switch
//----------------------------------------------------------------------------
void GuiManager::_process_manage_layer_multifn_switch(uint switch_index)
{
    // Has the layer changed?
    if (switch_index != utils::get_current_layer_info().layer_num()) {
        // Get the current layer param and update it
        auto param = utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER);
        if (param) {
            param->set_value_from_position(switch_index);

            // Post a param change message
            auto param_change = ParamChange(param->get_path(), param->get_value(), module());
            param_change.display = false;
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            _process_param_changed_mapped_params(param, param->get_value(), nullptr); 

            // Load the new Layer
            auto layer = switch_index;
            _reload_presets_from_select_patch_load++;
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(CURRENT_LAYER, layer, GUI)));
            _set_sys_func_switch(SystemFuncType::TOGGLE_PATCH_STATE, 
                                    ((utils::get_layer_info(layer).get_patch_state() == PatchState::STATE_A) ? false : true));

            // Update the current layer status
            auto msg = GuiMsg();
            msg.type = GuiMsgType::SET_LAYER_STATUS;
            auto str = "L" + std::to_string(layer + 1);
            _strcpy_to_gui_msg(msg.layer_status.status, str.c_str());
            _post_gui_msg(msg);

            // Because we have loaded a new layer, we need to process this value update as a full
            // layer param update (updates the list)
            // We also make sure we leave edit mode
            _param_shown = _param_shown_root;
            _param_shown_index = 0;
            _editing_param = false;
            _config_soft_button_2(true);
            _post_layer_param_update(layer);               
        }
        return;
    }
}

//----------------------------------------------------------------------------
// _process_select_bank_multifn_switch
//----------------------------------------------------------------------------
void GuiManager::_process_select_bank_multifn_switch(uint switch_index)
{
    // Set the new selected bank index
    _selected_bank_index = switch_index;
        
    // Update the selected list item
    _post_update_selected_list_item(_selected_bank_index);
}

//----------------------------------------------------------------------------
// _process_select_patch_multifn_switch
//----------------------------------------------------------------------------
void GuiManager::_process_select_patch_multifn_switch(uint switch_index)
{
    // Get the new selected patch index 
    _selected_patch_index = switch_index;

    // Are we loading a patch?
    if (_manage_patch_state == ManagePatchState::LOAD_PATCH)
    {
        auto id = PatchId();

        // Load the specified patch immediately - get the patch number to load
        // Note: Increment the index as the first patch shown is the "init patch"
        _selected_patch_index++;
        auto patch_item = _list_item_from_index(_selected_patch_index);
        if (patch_item.first > 0) {
            // Load this patch
            _selected_patch_num = patch_item.first;
            id.bank_num = _selected_bank_num;
            id.patch_num = _selected_patch_num;
            _reload_presets_from_select_patch_load++;
            auto sys_func = (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ? 
                                SystemFuncType::LOAD_PATCH :
                                SystemFuncType::LOAD_PATCH_STATE_B;
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(sys_func, id, GUI)));           
        }
    }

    // Update the selected list item
    _post_update_selected_list_item(_selected_patch_index);
}

//----------------------------------------------------------------------------
// _process_shown_param_update_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_shown_param_update_data_knob(KnobParam &data_knob)
{
    // Restart the param change timer
    _start_param_change_timer();

    // Are we editing a param?
    if (!_editing_param) {
        // Get the knob position value
        auto value = data_knob.get_position_value(_param_shown_index);
        if (value != (uint)_param_shown_index) 
        {
            // Set the new selected param
            _param_shown_index = value;

            // Get the list param to show
            if ((uint)_param_shown_index < _params_list.size()) {
                auto param = _params_list[_param_shown_index];
                if (param) {
                    // Set the param shown
                    _param_shown = param;

                    // Are we in the Mod Matrix state?
                    if (_gui_state == GuiState::MOD_MATRIX_DST) {
                        // Post a param update
                        _post_param_update();

                        // If this is not the EG2 Mod source
                        if (!_is_mod_matrix_eg_2_src_param(_param_shown)) {
                            Param *eg_2_level_param = nullptr;

                            // Is Morph as a destination currently selected?
                            if (_is_mod_matrix_morph_dst_param(_param_shown)) {
                                eg_2_level_param = _param_shown;
                                _eg_2_level_mod_state = Eg2LevelModState::MORPH;
                            }
                            // Or were we showing Morph?
                            else if (_eg_2_level_mod_state == Eg2LevelModState::MORPH) {
                                eg_2_level_param = _get_mod_matrix_eg_2_level_param();
                                _eg_2_level_mod_state = Eg2LevelModState::EG_2_LEVEL;
                            }

                            // Do we need to update the EG2 Level dst control?
                            if (eg_2_level_param) {
                                // Set the EG2 LVL knob to show the destination value
                                _set_eg_2_level_dst_control(eg_2_level_param, true);
                            }
                        }
                    }
                    else {
                        // Post a param value update
                        _post_param_update_value(true);
                    }
                }
            }
        }
    }
    else {
        // If the shown param is valid
        if (_param_shown)
        {
            // If we are showing the edited param as an enum list
            if (_show_param_as_enum_list(_param_shown)) {
                uint num_pos = 0;
                uint pos_value = 0;

                // Get the enum number of positions and current value
                if (_param_shown->display_switch) {
                    num_pos = 2;
                    pos_value = (_param_shown->get_value() == 0.0) ? 0 : 1;
                }
                else {
                    num_pos = _param_shown->get_num_positions();
                    pos_value = _param_shown->get_position_value();
                }

                // If the number of positions valid (should always be)
                if (num_pos > 0)
                {
                    // Get the knob position value and only process if its changed
                    auto value = data_knob.get_position_value(pos_value);
                    if (value != pos_value) {
                        // Is this the special case of the System Colour param?
                        if ((_param_shown->type == ParamType::COMMON_PARAM) && (_param_shown->param_id == CommonParamId::SYSTEM_COLOUR)) {
                            // Get the system colour from the display string selected
                            auto system_colour = utils::get_system_colour_from_name(_param_shown->display_strings[value]);
                            if (system_colour) {
                                // Send a set system colour GUI message
                                auto msg = GuiMsg();
                                msg.type = GuiMsgType::SET_SYSTEM_COLOUR;
                                _strcpy_to_gui_msg(msg.set_system_colour.colour, system_colour->colour.c_str());
                                _post_gui_msg(msg);

                                // Post a list update
                                _post_enum_list_param_update_value(value);                                

                                // Post a system function to indicate it has changed
                                auto sys_func = SystemFunc();
                                sys_func.type = SystemFuncType::SYSTEM_COLOUR_SET;
                                sys_func.str_value = system_colour->colour;
                                sys_func.from_module = NinaModule::GUI;
                                _event_router->post_system_func_event(new SystemFuncEvent(sys_func));                                
                            }
                        }
                        else {
                            // Post a list update
                            _post_enum_list_param_update_value(value);

                            // Post a param change message
                            auto param_change = ParamChange(_param_shown->get_path(), _param_shown->get_value(), module());
                            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                            _process_param_changed_mapped_params(_param_shown, _param_shown->get_value(), nullptr);
                        }                       
                    }
                }                        
            }
            // If we are showing the edited param as an enum (but not as an enum list)
            else if (_param_shown->get_num_positions() > 0) {
                // Get the knob position value and only process if its changed
                uint pos_value = _param_shown->get_position_value();
                auto value = data_knob.get_position_value(pos_value);
                if (value != pos_value) {
                    // Set the new param value and show it
                    _param_shown->set_value_from_position(value);
                    _post_param_update_value(false);

                     // Send the param change
                    auto param_change = ParamChange(_param_shown->get_path(), _param_shown->get_value(), module());
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                    _process_param_changed_mapped_params(_param_shown, _param_shown->get_value(), nullptr);                          
                }
            }
            else
            {
                // Normal param, set the value
                _param_shown->set_value_from_normalised_float(data_knob.get_value());

                // Are we in the Mod Matrix state?
                if (_gui_state == GuiState::MOD_MATRIX_DST) {
                    // If this is not the EG2 Mod source
                    if (!_is_mod_matrix_eg_2_src_param(_param_shown)) {
                        // Is EG2 Level or Morph as a destination currently selected?
                        if (_is_mod_matrix_eg_2_level_dst_param(_param_shown) || _is_mod_matrix_morph_dst_param(_param_shown)) {
                            // Set the EG2 LVL knob to show the Morph destination value
                            _set_eg_2_level_dst_control(_param_shown, true);
                        }
                    }
                }

                // Update the shown value
                _post_param_update_value(false);

                // Send the param change
                auto param_change = ParamChange(_param_shown->get_path(), _param_shown->get_value(), module());
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                _process_param_changed_mapped_params(_param_shown, _param_shown->get_value(), nullptr);
            }
        }
    }
}

//----------------------------------------------------------------------------
// _process_select_layers_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_select_layers_data_knob(KnobParam &data_knob)
{
    // Parse the edit name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {    
            auto value = data_knob.get_position_value(_selected_layers_index);
            if (value != (uint)_selected_layers_index) {
                // Set the new selected layers index
                _selected_layers_index = value;

                // Update the selected list item
                _post_update_selected_list_item(_selected_layers_index);                      
            }
            break;
        }

        case EditNameState::SELECT_CHAR:
        {
            // Get the knob position value
            auto value = data_knob.get_position_value(_selected_char_index);
            if ((value != _selected_char_index) && (value <= _edit_name.size()))
            {
                // Set the new selected character index
                _selected_char_index = value;

                // Update the edit name to show the selected character
                auto msg = GuiMsg();
                msg.type = GuiMsgType::EDIT_NAME_SELECT_CHAR;
                msg.edit_name_select_char.selected_char = _selected_char_index;
                _post_gui_msg(msg);                
            }
            break;         
        }

        case EditNameState::CHANGE_CHAR:
        {
            // Get the knob position value
            auto value = data_knob.get_position_value(_selected_list_char);
            if (value != _selected_list_char)
            {
                // Set the new selected list character index
                _selected_list_char = value;

                // Update the list to show the selected character
                auto msg = GuiMsg();
                msg.type = GuiMsgType::EDIT_NAME_CHANGE_CHAR;
                msg.edit_name_change_char.change_char = _selected_list_char;
                _post_gui_msg(msg);
            }
            break;
        }
    }        
}

//----------------------------------------------------------------------------
// _process_select_bank_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_select_bank_data_knob(KnobParam &data_knob)
{
    // Parse the edit name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {
            // Get the knob position value
            auto value = data_knob.get_position_value(_selected_bank_index);
            if (value != (uint)_selected_bank_index) 
            {
                // Set the new selected bank index
                _selected_bank_index = value;

                // If the keyboard is disabled, select the multi-function key
                if (!_kbd_enabled) {
                    _select_multifn_switch(_selected_bank_index);
                }

                // Update the selected list item
                _post_update_selected_list_item(_selected_bank_index);
            }
            break;
        }

        case EditNameState::SELECT_CHAR:
        {
            // Get the knob position value
            auto value = data_knob.get_position_value(_selected_char_index);
            if ((value != _selected_char_index) && (value <= _edit_name.size()))
            {
                // Set the new selected character index
                _selected_char_index = value;

                // Update the edit name to show the selected character
                auto msg = GuiMsg();
                msg.type = GuiMsgType::EDIT_NAME_SELECT_CHAR;
                msg.edit_name_select_char.selected_char = _selected_char_index;
                _post_gui_msg(msg);                
            }
            break;         
        }

        case EditNameState::CHANGE_CHAR:
        {
            // Get the knob position value
            auto value = data_knob.get_position_value(_selected_list_char);
            if (value != _selected_list_char)
            {
                // Set the new selected list character index
                _selected_list_char = value;

                // Update the list to show the selected character
                auto msg = GuiMsg();
                msg.type = GuiMsgType::EDIT_NAME_CHANGE_CHAR;
                msg.edit_name_change_char.change_char = _selected_list_char;
                _post_gui_msg(msg);
            }
            break;
        }   
    }
}

//----------------------------------------------------------------------------
// _process_select_patch_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_select_patch_data_knob(KnobParam &data_knob)
{
    // Parse the edit name state
    switch (_edit_name_state)
    {
        case EditNameState::NONE:
        {
            // Has the selection changed?
            auto value = data_knob.get_position_value(_selected_patch_index);
            if (value != (uint)_selected_patch_index) {
                // Set the new selected patch index
                auto prev_index = _selected_patch_index;
                _selected_patch_index = value;

                // If the keyboard is disabled, select the multi-function key
                if (!_kbd_enabled) {
                    (_manage_patch_state == ManagePatchState::LOAD_PATCH) ?
                        _select_multifn_switch(_selected_patch_index - 1) :
                        _select_multifn_switch(_selected_patch_index);
                }

                // Update the selected list item
                _post_update_selected_list_item(_selected_patch_index);  

                // If we are in the LOAD PATCH state
                if (_manage_patch_state == ManagePatchState::LOAD_PATCH) {
                    // If the index moved from zero
                    if (prev_index == 0) {
                        // Show the standard soft buttons for a patch
                        auto msg = GuiMsg();
                        msg.type = GuiMsgType::SET_SOFT_BUTTONS;
                        _strcpy_to_gui_msg(msg.soft_buttons.button1, "BANK");
                        _strcpy_to_gui_msg(msg.soft_buttons.button2, "RENAME");
                        if (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) {
                            _strcpy_to_gui_msg(msg.soft_buttons.button3, "LOAD");
                        }
                        else {
                            _strcpy_to_gui_msg(msg.soft_buttons.button3, "LOAD B");
                        }
                        _post_gui_msg(msg);
                    }
                    // If the index moved to zero
                    else if (_selected_patch_index == 0) {
                        // Show the init patch soft buttons
                        auto msg = GuiMsg();
                        msg.type = GuiMsgType::SET_SOFT_BUTTONS;
                        _strcpy_to_gui_msg(msg.soft_buttons.button1, "BANK");
                        _strcpy_to_gui_msg(msg.soft_buttons.button2, "----");
                        _strcpy_to_gui_msg(msg.soft_buttons.button3, "INIT");
                        _post_gui_msg(msg);
                    }
                }                   
            }
            break;
        }

        case EditNameState::SELECT_CHAR:
        {
            // Get the knob position value
            auto value = data_knob.get_position_value(_selected_char_index);
            if ((value != _selected_char_index) && (value <= _edit_name.size()))
            {
                // Set the new selected character index
                _selected_char_index = value;

                // Update the edit name to show the selected character
                auto msg = GuiMsg();
                msg.type = GuiMsgType::EDIT_NAME_SELECT_CHAR;
                msg.edit_name_select_char.selected_char = _selected_char_index;
                _post_gui_msg(msg);                
            }
            break;         
        }

        case EditNameState::CHANGE_CHAR:
        {
            // Get the knob position value
            auto value = data_knob.get_position_value(_selected_list_char);
            if (value != _selected_list_char)
            {
                // Set the new selected list character index
                _selected_list_char = value;

                // Update the list to show the selected character
                auto msg = GuiMsg();
                msg.type = GuiMsgType::EDIT_NAME_CHANGE_CHAR;
                msg.edit_name_change_char.change_char = _selected_list_char;
                _post_gui_msg(msg);
            }
            break;
        }
    }        
}

//----------------------------------------------------------------------------
// _process_system_menu_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_system_menu_data_knob(KnobParam &data_knob)
{
    // If showing the system menu
    if (_system_menu_state == SystemMenuState::SHOW_OPTIONS) {
        // Get the data knob position
        auto value = data_knob.get_position_value(_selected_system_menu_item);
        if (value != (uint)_selected_system_menu_item) {
            // Set the new menu item
            _selected_system_menu_item = value;

            // Update the selected list item
            _post_update_selected_list_item(_selected_system_menu_item);
        }
    }  
}

//----------------------------------------------------------------------------
// _process_bank_management_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_bank_management_data_knob(KnobParam &data_knob)
{
    // Parse the Bank Management state
    switch (_bank_management_state) {
        case BankManagmentState::SHOW_LIST:
        {
            // Get the data knob position
            auto value = data_knob.get_position_value(_selected_bank_management_item);
            if (value != (uint)_selected_bank_management_item) {
                // Set the new menu item
                _selected_bank_management_item = value;

                // Update the selected list item
                _post_update_selected_list_item(_selected_bank_management_item);
            }
            break;
        }

        case BankManagmentState::IMPORT:
        {
            // Parse the import state
            switch (_import_bank_state) {
                case ImportBankState::SELECT_ARCHIVE:
                {
                    // Get the data knob position
                    auto value = data_knob.get_position_value(_selected_bank_archive);
                    if (value != (uint)_selected_bank_archive) {
                        // Set the new menu item
                        _selected_bank_archive = value;
                        _selected_bank_archive_name = _list_items[_selected_bank_archive];

                        // Update the selected list item
                        _post_update_selected_list_item(_selected_bank_archive);
                    }
                    break;                    
                }

                case ImportBankState::SELECT_DEST:
                {
                    // Get the data knob position
                    auto value = data_knob.get_position_value(_selected_bank_dest);
                    if (value != (uint)_selected_bank_dest) {
                        // Set the new menu item
                        _selected_bank_dest = value;
                        _selected_bank_dest_name = _list_item_from_index(_selected_bank_dest).second;

                        // Update the selected list item
                        _post_update_selected_list_item(_selected_bank_dest);
                    }
                    break;                    
                }

                default:
                    // No action
                    break;
            }
            break;
        }

        case BankManagmentState::EXPORT:
        {
            // Parse the export state
            switch (_export_bank_state) {
                case ExportBankState::SELECT_BANK:
                {
                    // Get the data knob position
                    auto value = data_knob.get_position_value(_selected_bank_dest);
                    if (value != (uint)_selected_bank_dest) {
                        // Set the new menu item
                        _selected_bank_dest = value;
                        _selected_bank_dest_name = _list_item_from_index(_selected_bank_dest).second;

                        // Update the selected list item
                        _post_update_selected_list_item(_selected_bank_dest);
                    }
                    break;                    
                }

                default:
                    // No action
                    break;
            }
            break;
        }

        case BankManagmentState::CLEAR:
        {
            // Parse the clear state
            switch (_clear_bank_state) {
                case ClearBankState::SELECT_BANK:
                {
                    // Get the data knob position
                    auto value = data_knob.get_position_value(_selected_bank_dest);
                    if (value != (uint)_selected_bank_dest) {
                        // Set the new menu item
                        _selected_bank_dest = value;
                        _selected_bank_dest_name = _list_item_from_index(_selected_bank_dest).second;

                        // Update the selected list item
                        _post_update_selected_list_item(_selected_bank_dest);
                    }
                    break;                    
                }

                default:
                    // No action
                    break;
            }
            break;
        }

        default:
            // No action
            break;
    }
}

//----------------------------------------------------------------------------
// _process_wt_management_data_knob
//----------------------------------------------------------------------------
void GuiManager::_process_wt_management_data_knob(KnobParam &data_knob)
{
    // Get the data knob position
    auto value = data_knob.get_position_value(_selected_wt_management_item);
    if (value != (uint)_selected_wt_management_item) {
        // Set the new menu item
        _selected_wt_management_item = value;

        // Update the selected list item
        _post_update_selected_list_item(_selected_wt_management_item);
    }
}

//----------------------------------------------------------------------------
// _process_param_changed_timeout
//----------------------------------------------------------------------------
void GuiManager::_process_param_changed_timeout()
{
    // After timing-out, reset the UI state and show the default screen
    _reset_gui_state_and_show_home_screen();
}

//----------------------------------------------------------------------------
// _process_demo_mode_timeout
//----------------------------------------------------------------------------
void GuiManager::_process_demo_mode_timeout()
{
    // Don't do anything if we are in maintenance mode
    if (!utils::maintenance_mode()) {
        // Wait for n timeouts before entering demo mode
        if (_start_demo_mode_count == 0) {
            auto id = PatchId();
            auto msg = GuiMsg();
            std::string bank_folder_path;
            std::string bank_folder_name;
            std::string patch_name;

            // Are we entering demo mode?
            if (!_demo_mode) {
                // Entering demo mode, reset the UI state and show the default screen (unless we
                // are in the system menu)
                _config_sys_func_switches(true);
                if (_gui_state != GuiState::SYSTEM_MENU) {
                    _reset_gui_state_and_show_home_screen();
                }

                // Reset the selected bank and patch
                _selected_bank_num = 1;
                _selected_patch_num = 0;
                _demo_mode = true;
            }

            // Increment the selected patch number (wrap-around at 16)
            if (_selected_patch_num < 16) {
                _selected_patch_num++;
            }
            else {
                _selected_patch_num = 1;
            }      

            // Load the Bank 1 patches in succession
            id.bank_num = _selected_bank_num;
            id.patch_num = _selected_patch_num;
            _reload_presets_from_select_patch_load++;
            auto sys_func = (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) ? 
                                SystemFuncType::LOAD_PATCH :
                                SystemFuncType::LOAD_PATCH_STATE_B;
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(sys_func, id, GUI)));

            // If not in the system menu
            if (_gui_state != GuiState::SYSTEM_MENU) {
                // Update the patch name shown
                if (_open_bank_folder(id.bank_num, bank_folder_path, bank_folder_name))
                {
                    // Get the patch filename
                    if (_get_patch_filename(id.patch_num, bank_folder_path, patch_name)) {
                        patch_name = _format_filename(patch_name.c_str());
                        msg.type = GuiMsgType::SHOW_HOME_SCREEN;
                        msg.home_screen.patch_modified = utils::get_current_layer_info().get_patch_modified();
                        _strcpy_to_gui_msg(msg.home_screen.patch_name , patch_name.c_str());
                        _post_gui_msg(msg);
                        msg.type = GuiMsgType::SET_SOFT_BUTTONS;
                        std::strcpy(msg.soft_buttons.button1, "----");
                        std::strcpy(msg.soft_buttons.button2, "----");
                        std::strcpy(msg.soft_buttons.button3, "SYSTEM");
                        _post_gui_msg(msg);                             
                    }
                }
            }
        }
        else {
            _start_demo_mode_count--;
        }
    }
}

//----------------------------------------------------------------------------
// _show_home_screen
//----------------------------------------------------------------------------
void GuiManager::_show_home_screen(bool update)
{
    auto msg = GuiMsg();
    std::string bank_folder_path;
    std::string bank_folder_name;
    std::string patch_name;

    // If this is not a home screen update
    if (!update) {
        // Set a default patch name
        patch_name = "NO PATCH LOADED";

        // Get the current patch index
        _edit_name_state = EditNameState::NONE;
        _edit_name.clear();
        auto id = utils::get_current_layer_info().get_patch_id();

        // Open the specified bank folder
        _selected_bank_num = -1;
        _selected_patch_num = -1;
        _selected_bank_index = -1;
        _selected_patch_index = -1;    
        if (_open_bank_folder(id.bank_num, bank_folder_path, bank_folder_name))
        {
            // Get the patch filename
            _selected_bank_num = id.bank_num;
            if (_get_patch_filename(id.patch_num, bank_folder_path, patch_name)) {
                _selected_patch_num = id.patch_num;
                patch_name = _format_filename(patch_name.c_str());         
            }
        }
    }

    // Show the main area as the default
    msg.type = GuiMsgType::SHOW_HOME_SCREEN;
    if (utils::get_current_layer_info().get_num_voices() == 0) {
        msg.home_screen.patch_modified = false;
        _strcpy_to_gui_msg(msg.home_screen.patch_name, "LAYER DISABLED");
    }
    else {
        msg.home_screen.patch_modified = utils::get_current_layer_info().get_patch_modified();
        _strcpy_to_gui_msg(msg.home_screen.patch_name , patch_name.c_str());
    }
    msg.home_screen.scope_mode = _scope_mode;
    _post_gui_msg(msg);

    // Show the soft buttons
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    if (_scope_mode == GuiScopeMode::SCOPE_MODE_OFF) {
        std::strcpy(msg.soft_buttons.button2, "SCOPE OSC");
    }
    else if (_scope_mode == GuiScopeMode::SCOPE_MODE_OSC) {
        std::strcpy(msg.soft_buttons.button2, "SCOPE XY");
    }
    else {
        std::strcpy(msg.soft_buttons.button2, "SCOPE OFF");
    }
    std::strcpy(msg.soft_buttons.button3, "SYSTEM");
    _post_gui_msg(msg);

    // If this is not an update
    if (!update) {
        // Set the GUI state
        _gui_state = GuiState::HOME_SCREEN;

        // Reset the data knob and system function and multi-funtion switches
        _config_data_knob();
        _reset_sys_func_switches(SystemFuncType::UNKNOWN);
        _reset_multifn_switches();
    }
}

//----------------------------------------------------------------------------
// _show_system_menu_screen
//----------------------------------------------------------------------------
void GuiManager::_show_system_menu_screen()
{
    auto msg = GuiMsg();
    auto first_item_index = static_cast<int>(SystemMenuOption::CALIBRATE);

    // Adjust the selected item for full or normal system menu
    if (!_show_full_system_menu) {
        first_item_index += SystemMenuOption::GLOBAL_SETTINGS;
    }

    // Has calibration already been performed? If so we show a slightly different menu
    const char **menu_options = _system_menu_options_not_cal;
    if (std::filesystem::exists(NINA_CALIBRATION_FILE(VOICE_0_CALIBRATION_FILE))) {
        menu_options = _system_menu_options_cal;
    }

    // Setup the system list
    uint index = 0;
    _list_items.clear();
    for (uint i=first_item_index; i<=SystemMenuOption::ABOUT; i++) {
        // If this entry is for store demo mode, add the demo mode status to the menu item
        if (i == SystemMenuOption::STORE_DEMO_MODE) {
            _list_items[index++] = std::string(menu_options[i]) + (utils::system_config()->get_demo_mode() ? "ON" : "OFF");
        }
        else {
            _list_items[index++] = menu_options[i];
        }
    }

    // Set the left status
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "SYSTEM");
    _post_gui_msg(msg);

    // Show the system list
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.num_items = _list_items.size();
    msg.list_items.selected_item = _selected_system_menu_item;
    msg.list_items.process_enabled_state = true;
    auto list_itr = _list_items.begin();
    for (uint i=0; i<_list_items.size(); i++) {
        _strcpy_to_gui_msg(msg.list_items.list_items[i], (*list_itr).second.c_str());
        if (((i + first_item_index) == SystemMenuOption::BACKUP) && !_sw_manager->msd_mounted()) {
            msg.list_items.list_item_enabled[i] = false;
        }
        else if (((i + first_item_index) == SystemMenuOption::RESTORE_BACKUP) && !_sw_manager->restore_backup_archives_present()) {
            msg.list_items.list_item_enabled[i] = false;
        }
        else if (((i + first_item_index) == SystemMenuOption::RUN_DIAG_SCRIPT) && !_sw_manager->diag_script_present()) {
            msg.list_items.list_item_enabled[i] = false;
        }
        else {
            msg.list_items.list_item_enabled[i] = true;
        }
        list_itr++;
    }
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    _strcpy_to_gui_msg(msg.soft_buttons.button1, "EXIT");
    _strcpy_to_gui_msg(msg.soft_buttons.button2, "----");
    _strcpy_to_gui_msg(msg.soft_buttons.button3, "ENTER");
    _post_gui_msg(msg);

    // Set the GUI state
    _gui_state = GuiState::SYSTEM_MENU;
    _system_menu_state = SystemMenuState::SHOW_OPTIONS;
    _progress_state = ProgressState::NOT_STARTED;

    // Reset the system function and multi-funtion switches
    _reset_sys_func_switches(SystemFuncType::UNKNOWN);
    _start_stop_seq_rec(false);
    _reset_multifn_switches();

    // Set the data knob to the list selector state
    _num_list_items = _list_items.size();
    _config_data_knob(_list_items.size());

    // Create a thread to poll for MSD insertion events
    if (!_msd_event_thread) {
        _run_msd_event_thread = true;
        _msd_event_thread = new std::thread(_process_msd_event, this);
    }
}

//----------------------------------------------------------------------------
// _show_select_layers_load_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_layers_load_screen()
{
    auto msg = GuiMsg();

    // Parse the layers folder
    _list_items = _parse_layers_folder();
    if (_list_items.size() > 0)
    {
        uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;          

        // Is a layers file currently selected?
        _selected_layers_index= -1;
        if (_selected_layers_num > 0) {
            // Get the index into the layers items
            _selected_layers_index = _index_from_list_items(_selected_layers_num);
        }
        if (_selected_layers_index == -1) {
            _selected_layers_index = 0;
        }

        // Set the left status = select the layers
        msg.type = GuiMsgType::SET_LEFT_STATUS;
        _strcpy_to_gui_msg(msg.left_status.status, "LAYERS");
        _post_gui_msg(msg);

        // Show the list of patches to choose from
        msg.type = GuiMsgType::SHOW_LIST_ITEMS;
        msg.list_items.num_items = list_size;
        msg.list_items.selected_item = _selected_layers_index;
        msg.list_items.process_enabled_state = false;
        auto list_itr = _list_items.begin();
        for (uint i=0; i<list_size; i++) {
            _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_filename((*list_itr).second.c_str()).c_str());
            list_itr++;
        }
        _post_gui_msg(msg);

        // Set the soft buttons text
        msg.type = GuiMsgType::SET_SOFT_BUTTONS;
        _strcpy_to_gui_msg(msg.soft_buttons.button1, "EXIT");
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "RENAME");
        _strcpy_to_gui_msg(msg.soft_buttons.button3, "LOAD");
        _post_gui_msg(msg);
    
        // Set the data knob to the select patch state
        _num_list_items = list_size;
        _config_data_knob(_num_list_items);
        _config_soft_button_2(false);
    }
}

//----------------------------------------------------------------------------
// _show_select_layers_save_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_layers_save_screen()
{
    auto msg = GuiMsg();

    // Parse the layers folder
    _list_items = _parse_layers_folder();
    if (_list_items.size() > 0)
    {
        uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;          

        // Is a layers file currently selected?
        _selected_layers_index= -1;
        if (_selected_layers_num > 0) {
            // Get the index into the layers items
            _selected_layers_index = _index_from_list_items(_selected_layers_num);
        }
        if (_selected_layers_index == -1) {
            _selected_layers_index = 0;
        }

        // Set the left status = select the layers
        msg.type = GuiMsgType::SET_LEFT_STATUS;
        _strcpy_to_gui_msg(msg.left_status.status, "LAYERS");
        _post_gui_msg(msg);

        // Show the list of patches to choose from
        msg.type = GuiMsgType::SHOW_LIST_ITEMS;
        msg.list_items.num_items = list_size;
        msg.list_items.selected_item = _selected_layers_index;
        msg.list_items.process_enabled_state = false;
        auto list_itr = _list_items.begin();
        for (uint i=0; i<list_size; i++) {
            _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_filename((*list_itr).second.c_str()).c_str());
            list_itr++;
        }
        _post_gui_msg(msg);

        // Set the soft buttons text
        msg.type = GuiMsgType::SET_SOFT_BUTTONS;
        _strcpy_to_gui_msg(msg.soft_buttons.button1, "EXIT");
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "----");
        _strcpy_to_gui_msg(msg.soft_buttons.button3, "SAVE");
        _post_gui_msg(msg);

        // Set the save edit name if not already set
        // This is always shown regardless of the patch selected to save over
        if (_save_edit_name.size() == 0) {
            _save_edit_name = _get_edit_name_from_index(_selected_layers_index);
        }

        // Set the data knob to the select patch state
        _num_list_items = list_size;
        _config_data_knob(_num_list_items);
        _config_soft_button_2(false);
    }
}

//----------------------------------------------------------------------------
// _show_select_bank_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_bank_screen()
{
    auto msg = GuiMsg();

    // Parse the patches folder containing the banks
    _list_items = _parse_patches_folder();
    if (_list_items.size() > 0)
    {
        uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;

        // Is the currently selected bank number valid?
        if ((_selected_bank_index == -1) && (_selected_bank_num > 0)) {
            // Get the index into the bank items 
            _selected_bank_index = _index_from_list_items(_selected_bank_num);
        }
        if (_selected_bank_index == -1) {
            _selected_bank_index = 0;
        }

        // Set the left status = select the bank
        msg.type = GuiMsgType::SET_LEFT_STATUS;
        _strcpy_to_gui_msg(msg.left_status.status, "BANKS");
        _post_gui_msg(msg);

        // Show the list of banks to choose from
        msg.type = GuiMsgType::SHOW_LIST_ITEMS;
        msg.list_items.num_items = list_size;
        msg.list_items.selected_item = _selected_bank_index;
        msg.list_items.process_enabled_state = false;
        auto list_itr = _list_items.begin();
        for (uint i=0; i<list_size; i++) {
            _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_folder_name((*list_itr).second.c_str()).c_str());
            list_itr++;
        }
        _post_gui_msg(msg);

        // Set the soft buttons text
        msg.type = GuiMsgType::SET_SOFT_BUTTONS;
        _strcpy_to_gui_msg(msg.soft_buttons.button1, "EXIT");
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "RENAME");
        _strcpy_to_gui_msg(msg.soft_buttons.button3, "ENTER");
        _post_gui_msg(msg);

        // If the keyboard is not enabled, configure the multi-function switches
        if (!_kbd_enabled) {
            // Set the multi-function switches to SINGLE SELECT mode
            _config_multifn_switches(NUM_MULTIFN_SWITCHES, _selected_bank_index);
        }

        // Set the data knob to the list selector state
        _num_list_items = list_size;
        _config_data_knob(list_size);
        _config_soft_button_2(false);

        // Set the select patch state
        _select_patch_state = SelectPatchState::SELECT_BANK;
    }
}

//----------------------------------------------------------------------------
// _show_select_patch_load_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_patch_load_screen()
{
    std::string bank_folder_path;
    std::string bank_folder_name;
    std::string patch_name = "";
    auto msg = GuiMsg();

    // Is a valid bank currently selected?
    if (_selected_bank_num == -1) {
        // No - so show the select bank screen instead
        _show_select_bank_screen();
    }
    else {
        // Try to open the specified bank folder
        bool res = _open_bank_folder(_selected_bank_num, bank_folder_path, bank_folder_name);
        if (!res) {
            // We can't open the bank folder - probably as it doesn't exist
            // In this case just default to bank 001 and try again
            _selected_bank_num = 1;
            res = _open_bank_folder(_selected_bank_num, bank_folder_path, bank_folder_name);
        }
        if (res)
        {
            // Save the selected bank folder
            _selected_bank_folder_name = bank_folder_name;

            // Get the patches - adding the init patch to the start of the list
            _list_items = _parse_bank_folder(bank_folder_path);
            _list_items[0] = INIT_PATCH_LIST_TEXT;
            uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;            

            // Is a patch currently selected?
            _selected_patch_index = -1;
            if (_selected_patch_num > 0) {
                auto id = utils::get_current_layer_info().get_patch_id();

                // Is the selected bank the bank currently being used?
                if (id.bank_num == (uint)_selected_bank_num)
                {
                    // Get the index into the patch items
                    _selected_patch_index = _index_from_list_items(_selected_patch_num);
                }
            }
            if (_selected_patch_index == -1) {
                // Select the first patch (not the init patch)
                _selected_patch_index = 1;
            }

            // Set the left status = select the patch
            msg.type = GuiMsgType::SET_LEFT_STATUS;
            auto bank_name = _format_folder_name(bank_folder_name.c_str());
            _strcpy_to_gui_msg(msg.left_status.status, bank_name.c_str());
            _post_gui_msg(msg);

            // Show the list of patches to choose from
            msg.type = GuiMsgType::SHOW_LIST_ITEMS;
            msg.list_items.num_items = list_size;
            msg.list_items.selected_item = _selected_patch_index;
            msg.list_items.process_enabled_state = false;
            auto list_itr = _list_items.begin();
            for (uint i=0; i<list_size; i++) {
                _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_filename((*list_itr).second.c_str()).c_str());
                list_itr++;
            }
            _post_gui_msg(msg);

            // Set the soft buttons text
            msg.type = GuiMsgType::SET_SOFT_BUTTONS;
            _strcpy_to_gui_msg(msg.soft_buttons.button1, "BANK");
            _strcpy_to_gui_msg(msg.soft_buttons.button2, "RENAME");
            if (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) {
                _strcpy_to_gui_msg(msg.soft_buttons.button3, "LOAD");
            }
            else {
                _strcpy_to_gui_msg(msg.soft_buttons.button3, "LOAD B");
            }
            _post_gui_msg(msg);

            // If the keyboard is not enabled, configure the multi-function switches
            if (!_kbd_enabled) {
                // Set the multi-function switches to SINGLE SELECT mode
                _config_multifn_switches(((list_size < NUM_MULTIFN_SWITCHES) ? list_size : NUM_MULTIFN_SWITCHES), (_selected_patch_index - 1));
            }

            // Set the data knob to the select patch state
            _num_list_items = list_size;
            _config_data_knob(_num_list_items);
            _config_soft_button_2(false);

            // Set the select patch state
            _select_patch_state = SelectPatchState::SELECT_PATCH;
        }
    }   
}

//----------------------------------------------------------------------------
// _show_select_patch_save_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_patch_save_screen()
{
    std::string bank_folder_path;
    std::string bank_folder_name;
    std::string patch_name = "";
    auto msg = GuiMsg();

    // Is a valid bank currently selected?
    if (_selected_bank_num == -1) {
        // No - so show the select bank screen instead
        _show_select_bank_screen();
    }
    else {
        // Try to open the specified bank folder
        bool res = _open_bank_folder(_selected_bank_num, bank_folder_path, bank_folder_name);
        if (!res) {
            // We can't open the bank folder - probably as it doesn't exist
            // In this case just default to bank 001 and try again
            _selected_bank_num = 1;
            res = _open_bank_folder(_selected_bank_num, bank_folder_path, bank_folder_name);
        }
        if (res)
        {
            // Save the selected bank folder
            _selected_bank_folder_name = bank_folder_name;
            
            // Get the patches
            _list_items = _parse_bank_folder(bank_folder_path);
            uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;            

            // Is a patch currently selected?
            _selected_patch_index = -1;
            if (_selected_patch_num > 0) {
                auto id = utils::get_current_layer_info().get_patch_id();

                // Is the selected bank the bank currently being used?
                if (id.bank_num == (uint)_selected_bank_num)
                {
                    // Get the index into the patch items
                    _selected_patch_index = _index_from_list_items(_selected_patch_num);
                }
            }
            if (_selected_patch_index == -1) {
                _selected_patch_index = 0;
            }

            // Set the left status = save the patch
            msg.type = GuiMsgType::SET_LEFT_STATUS;
            auto bank_name = _format_folder_name(bank_folder_name.c_str());
            _strcpy_to_gui_msg(msg.left_status.status, bank_name.c_str());
            _post_gui_msg(msg);

            // Show the list of patches to choose from
            msg.type = GuiMsgType::SHOW_LIST_ITEMS;
            msg.list_items.num_items = list_size;
            msg.list_items.selected_item = _selected_patch_index;
            msg.list_items.process_enabled_state = false;
            auto list_itr = _list_items.begin();
            for (uint i=0; i<list_size; i++) {
                _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_filename((*list_itr).second.c_str()).c_str());
                list_itr++;
            }
            _post_gui_msg(msg);

            // Set the soft buttons text
            msg.type = GuiMsgType::SET_SOFT_BUTTONS;
            _strcpy_to_gui_msg(msg.soft_buttons.button1, "BANK");
            _strcpy_to_gui_msg(msg.soft_buttons.button2, "----");
            _strcpy_to_gui_msg(msg.soft_buttons.button3, "SAVE");
            _post_gui_msg(msg);

            // Set the save edit name if not already set
            // This is always shown regardless of the patch selected to save over
            if (_save_edit_name.size() == 0) {
                _save_edit_name = _get_edit_name_from_index(_selected_patch_index);
            }

            // If the keyboard is not enabled, configure the multi-function switches
            if (!_kbd_enabled) {
                // Set the multi-function switches to SINGLE SELECT mode
                _config_multifn_switches(((list_size < NUM_MULTIFN_SWITCHES) ? list_size : NUM_MULTIFN_SWITCHES), _selected_patch_index);
            }

            // Set the data knob to the select patch state
            _num_list_items = list_size;
            _config_data_knob(_num_list_items);
            _config_soft_button_2(false);

            // Set the select patch state
            _select_patch_state = SelectPatchState::SELECT_PATCH;
        }        
    }  
}

//----------------------------------------------------------------------------
// _show_select_mod_matrix_dst_screen
//----------------------------------------------------------------------------
void GuiManager::_show_select_mod_matrix_dst_screen()
{
    // Get the first mod matrix source dest entry
    std::string param_path = _mod_matrix_param_path(_selected_mod_matrix_src_index, 0);
    auto param = utils::get_param(param_path)->param_list[0];
    if (param) {
        // Show the mod matrix destination parameters
        _stop_param_change_timer();

        // This param will become the root param - get the index
        // of this param in the param list
        auto index = _get_param_list_index(param, param);
        if (index >= 0) {
            // Setup the param shown settings
            _param_shown_root = param;
            _param_shown = param;
            _param_shown_index = index;
            _show_param_list = true;
            _new_mod_matrix_param_list = true;
            _params_list.clear();

            // Show the param normally
            _post_param_update();              
        }
    }
}

//----------------------------------------------------------------------------
// _show_edit_name_select_char_screen
//----------------------------------------------------------------------------
void GuiManager::_show_edit_name_select_char_screen()
{
    // Disable use of the multi-function switches
    _reset_multifn_switches();
    
    // Show the name we are currently editing
    auto msg = GuiMsg();
    msg.type = GuiMsgType::EDIT_NAME;
    std::memset(msg.edit_name.name, 0, sizeof(msg.edit_name.name));
    _strcpy_to_gui_msg(msg.edit_name.name, _edit_name.c_str());
    _post_gui_msg(msg);

    // Are we entering the rename state?
    if (_edit_name_state == EditNameState::NONE) 
    {
        // Yes - set the left status to indicate this
        msg.type = GuiMsgType::SET_LEFT_STATUS;
        if (_gui_state == GuiState::MANAGE_LAYERS) {
            if (_manage_layers_state == ManageLayersState::LOAD_LAYERS) {
                _strcpy_to_gui_msg(msg.left_status.status, "RENAME LAYER SETUP");
            }
            else {
                _strcpy_to_gui_msg(msg.left_status.status, "SAVE LAYER SETUP");
            }            
        }
        else if (_select_patch_state == SelectPatchState::SELECT_PATCH) {
            if (_manage_patch_state == ManagePatchState::LOAD_PATCH) {
                _strcpy_to_gui_msg(msg.left_status.status, "RENAME PRESET");
            }
            else {
                _strcpy_to_gui_msg(msg.left_status.status, "SAVE PRESET");
            }
        }
        else {
            _strcpy_to_gui_msg(msg.left_status.status, "RENAME BANK");
        }
        _post_gui_msg(msg);

        // Reset the selected character/list character indexes
        _selected_char_index = (_edit_name.size() < EDIT_NAME_STR_LEN) ? _edit_name.size() : (EDIT_NAME_STR_LEN - 1);
        _selected_list_char = 0;
    }

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    _strcpy_to_gui_msg(msg.soft_buttons.button1, "BACK");
    _strcpy_to_gui_msg(msg.soft_buttons.button2, "EDIT");
    if ((_select_patch_state == SelectPatchState::SELECT_PATCH) && 
        (_manage_patch_state == ManagePatchState::SAVE_PATCH)) {
        _strcpy_to_gui_msg(msg.soft_buttons.button3, "SAVE");
    }
    else {
        _strcpy_to_gui_msg(msg.soft_buttons.button3, "ENTER");
    }
    _post_gui_msg(msg);

    // Enter the edit name - select character state
    _edit_name_state = EditNameState::SELECT_CHAR;

    // Configure the data knob
    _config_data_knob(FILENAME_MAX_SIZE);
}

//----------------------------------------------------------------------------
// _show_edit_name_change_char_screen
//----------------------------------------------------------------------------
void GuiManager::_show_edit_name_change_char_screen()
{
    // Disable use of the multi-function switches
    _reset_multifn_switches();

    // Show the selected char
    if (_selected_char_index < _edit_name.size()) {
        // If not a valid char to show, show the default
        _selected_list_char = _edit_name[_selected_char_index];
        if (!_char_is_charset_valid(_selected_list_char)) {
            _selected_list_char = DEFAULT_CHARSET_CHAR;
        }
    }
    else {
        // Show the default char for new characters
        _selected_list_char = DEFAULT_CHARSET_CHAR;
    }

    // Get the index into the character ser
    _selected_list_char = _char_to_charset_index(_selected_list_char);

    // Show the change character control
    auto msg = GuiMsg();
    msg.type = GuiMsgType::EDIT_NAME_CHANGE_CHAR;
    msg.edit_name_change_char.change_char = _selected_list_char;                  
    _post_gui_msg(msg);

    // Enter the edit name - change character state
    _edit_name_state = EditNameState::CHANGE_CHAR;

    // Configure the data knob
    _config_data_knob(NUM_CHARSET_CHARS);
}

//----------------------------------------------------------------------------
// _show_system_menu_about_screen
//----------------------------------------------------------------------------
void GuiManager::_show_system_menu_about_screen()
{
   auto msg = GuiMsg();

    // Set the left status
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "ABOUT");
    _post_gui_msg(msg);

    // Show the list of versions
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.num_items = 7;
    msg.list_items.selected_item = -1;
    msg.list_items.process_enabled_state = false;

    // Get the System Version
    uint item = 0;
    char sw_ver[_UTSNAME_VERSION_LENGTH*2];
    std::string sys_ver = "Unknown";
    std::ifstream sys_ver_file("/etc/sw_version");
    std::getline(sys_ver_file, sys_ver);

    // Get the OS Version
    struct utsname  os_ver;
    ::uname(&os_ver);

    // Show the System and OS Versions
    std::sprintf(sw_ver, "SYSTEM: %s (%s)", sys_ver.c_str(), os_ver.release); 
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver); 

    // UI Version
    std::sprintf(sw_ver, "UI: %d.%d.%d-%c%c%c%c%c%c%c", NINA_UI_MAJOR_VERSION, NINA_UI_MINOR_VERSION, NINA_UI_PATCH_VERSION, 
                                                        NINA_UI_GIT_COMMIT_HASH[0], NINA_UI_GIT_COMMIT_HASH[1], NINA_UI_GIT_COMMIT_HASH[2],
                                                        NINA_UI_GIT_COMMIT_HASH[3], NINA_UI_GIT_COMMIT_HASH[4], NINA_UI_GIT_COMMIT_HASH[5],
                                                        NINA_UI_GIT_COMMIT_HASH[6]);
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver);

    // GUI Version
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], "GUI_VER"); // Placeholder, GUI replaces with its version

    // Sushi Version
    auto sushi_ver = static_cast<DawManager *>(utils::get_manager(NinaModule::DAW))->get_sushi_version();
    std::sprintf(sw_ver, "VST HOST: %s-%s", sushi_ver.version.c_str(), sushi_ver.commit_hash.substr(0,7).c_str());  
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver);

    // VST Version
    std::string vst_ver = "Unknown";
    std::ifstream vst_ver_file(VST_CONTENTS_DIR + std::string("version.txt"));
    std::getline(vst_ver_file, vst_ver);
    std::sprintf(sw_ver, "VST: %s", vst_ver.c_str());  
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver);

    // Device ID (RPi Serial Number)
    std::string ser_num = "";
    std::ifstream ser_num_file("/sys/firmware/devicetree/base/serial-number");
    std::getline(ser_num_file, ser_num);
    std::sprintf(sw_ver, "DEVICE ID: %s", ser_num.substr((ser_num.size() - TRUNC_SERIAL_NUM_SIZE), TRUNC_SERIAL_NUM_SIZE).c_str());
    _strcpy_to_gui_msg(msg.list_items.list_items[item++], sw_ver);   

    // Hostname
    std::string hostname = "Unknown";
    std::ifstream hostname_file("/etc/hostname");
    std::getline(hostname_file, hostname);
    std::sprintf(sw_ver, "HOSTNAME: %s", hostname.c_str());  
    _strcpy_to_gui_msg(msg.list_items.list_items[item], sw_ver);
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    _strcpy_to_gui_msg(msg.soft_buttons.button1, "BACK");
    _strcpy_to_gui_msg(msg.soft_buttons.button2, "----");
    _strcpy_to_gui_msg(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    
    // Set the system menu state
    _system_menu_state = SystemMenuState::OPTION_ACTIONED;
}

//----------------------------------------------------------------------------
// _show_start_sw_update_screen
//----------------------------------------------------------------------------
void GuiManager::_show_start_sw_update_screen(std::string sw_version)
{
    // Show the starting software update screen
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "SYSTEM");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    auto str = "Updating Software to v" + sw_version;
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::SW_UPDATE;
}

//----------------------------------------------------------------------------
// _show_finish_sw_update_screen
//----------------------------------------------------------------------------
void GuiManager::_show_finish_sw_update_screen(std::string sw_version, bool result)
{
    // Show the finishing software update screen
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = "Update to v" + sw_version + (result ? " Done" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Reboot NINA?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "OK");
    _post_gui_msg(msg);
    _gui_state = GuiState::SW_UPDATE;
    _sw_update_state = SwUpdateState::SW_UPDATE_FINISHED;
}

//----------------------------------------------------------------------------
// _show_start_auto_calibrate_screen
//----------------------------------------------------------------------------
void GuiManager::_show_start_auto_calibrate_screen()
{
    // Show the start auto calibrate screen
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "SYSTEM");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Running calibration");
    std::strcpy(msg.warning_screen.line_2, "Please wait (long process)");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_STARTED;
}

//----------------------------------------------------------------------------
// _show_finish_auto_calibrate_screen
//----------------------------------------------------------------------------
void GuiManager::_show_finish_auto_calibrate_screen(bool result)
{
    // Show the finish software update screen
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Calibration") + (result ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Reboot NINA?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "OK");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_calibrate_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_calibrate_screen(CalMode mode)
{
    std::string run_cal_str1;
    std::string run_cal_str2;
    std::string cal_type_str;

    // Set the calibration strings
    switch (mode) {
        case CalMode::FILTER:
            run_cal_str1 = "Running Filter calibration";
            run_cal_str2 = "Please wait";
            cal_type_str = "Filter calibration";
            break;

        case CalMode::MIX_VCA:
            run_cal_str1 = "Running Mix VCA calibration";
            run_cal_str2 = "Please wait (long process)";
            cal_type_str = "Mix VCA calibration";
            break;

        default:
            run_cal_str1 = "Running calibration";
            run_cal_str2 = "Please wait (long process)";
            cal_type_str = "Calibration";        
            break;
    }

    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "SYSTEM");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, run_cal_str1.c_str());
    std::strcpy(msg.warning_screen.line_2, run_cal_str2.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_STARTED; 

    // Run the calibration script
    auto ret = _sw_manager->run_calibration_script(mode);
    MSG(cal_type_str + " script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), cal_type_str + " script: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), cal_type_str + " script: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = cal_type_str + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Reboot NINA?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "OK");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_factory_calibrate_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_factory_calibrate_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "SYSTEM");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Running factory calibration");
    std::strcpy(msg.warning_screen.line_2, "Please wait (long process)");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_STARTED; 

    // Run the factory calibration script
    auto ret = _sw_manager->run_factory_calibration_script();
    MSG("Factory calibration script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Factory calibration script: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Factory calibration script: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Factory calibration") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Reboot NINA?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "OK");
    _post_gui_msg(msg);
    _gui_state = GuiState::CALIBRATE;
    _calibrate_state = CalibrateState::CALIBRATE_FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_run_diag_script_confirm_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_run_diag_script_confirm_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "DIAGNOSTICS");
    _post_gui_msg(msg);

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    std::strcpy(msg.warning_screen.line_1, "Are you sure this script");
    std::strcpy(msg.warning_screen.line_2, "is from a trusted source?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "YES");
    _post_gui_msg(msg);
    _gui_state = GuiState::RUN_DIAG_SCRIPT;
}

//----------------------------------------------------------------------------
// _show_sys_menu_run_diag_script_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_run_diag_script_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "DIAGNOSTICS");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Running diagnostic script");
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);

    // Run the diagnositics script
    auto ret = _sw_manager->run_diag_script();
    MSG("Diagnostic script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Diagnostic script: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Diagnostic script: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Diagnostic script") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_bank_management_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_bank_management_screen()
{
    uint item = 0;

    // Set the lefr status
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "BANKS");
    _post_gui_msg(msg);

    // Set the list of Wavetable Management options
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.process_enabled_state = true;
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Import Bank");
    msg.list_items.list_item_enabled[item++] = _sw_manager->bank_archive_present();
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Export Bank");
    msg.list_items.list_item_enabled[item++] = _sw_manager->msd_mounted();
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Add Bank");
     msg.list_items.list_item_enabled[item++] = true;    
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Clear Bank");
     msg.list_items.list_item_enabled[item++] = true;
    msg.list_items.num_items = item;
    msg.list_items.selected_item = _selected_bank_management_item;    
    _post_gui_msg(msg);

    // Set the soft buttons
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "EXIT");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "ENTER");
    _post_gui_msg(msg);
    _gui_state = GuiState::BANK_MANAGMENT;
    _bank_management_state = BankManagmentState::SHOW_LIST;
    _progress_state = ProgressState::NOT_STARTED;

    // Configure the data knob
    _num_list_items = item;
    _config_data_knob(_num_list_items);    
}

//----------------------------------------------------------------------------
// _show_sys_menu_select_bank_archive_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_select_bank_archive_screen()
{
    uint item = 0;

    // Set the left status
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "SELECT IMPORT ARCHIVE");
    _post_gui_msg(msg);

    // Get a list of the available bank archives and show them
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.process_enabled_state = false;
    _list_items.clear();    
    auto banks = _sw_manager->get_bank_archives();
    for (auto itr=banks.begin(); itr<banks.end(); itr++) {
        _list_items[item] = *itr;
        _strcpy_to_gui_msg(msg.list_items.list_items[item++], itr->c_str());
    }
    msg.list_items.num_items = item;
    msg.list_items.selected_item = _selected_bank_archive;
    _selected_bank_archive_name = _list_items[_selected_bank_archive];
    _post_gui_msg(msg);

    // Set the soft buttons
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "NEXT");
    _post_gui_msg(msg);
    _bank_management_state = BankManagmentState::IMPORT;
    _import_bank_state = ImportBankState::SELECT_ARCHIVE;

    // Configure the data knob
    _num_list_items = item;
    _config_data_knob(_num_list_items);      
}

//----------------------------------------------------------------------------
// _show_sys_menu_select_dest_bank_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_select_dest_bank_screen()
{
    // Set the left status
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    if (_bank_management_state == BankManagmentState::IMPORT) {
        _strcpy_to_gui_msg(msg.left_status.status, "SELECT DEST BANK");
    }
    else {
        _strcpy_to_gui_msg(msg.left_status.status, "SELECT BANK");
    }
    _post_gui_msg(msg);

    // Parse the patches folder containing the banks
    _list_items = _parse_patches_folder();
    if (_list_items.size() > 0)
    {
        uint list_size = (_list_items.size() < LIST_MAX_LEN) ? _list_items.size() : LIST_MAX_LEN;

        // Show the list of banks to choose from
        msg.type = GuiMsgType::SHOW_LIST_ITEMS;
        msg.list_items.num_items = list_size;
        msg.list_items.selected_item = _selected_bank_dest;
        msg.list_items.process_enabled_state = false;
        auto list_itr = _list_items.begin();
        for (uint i=0; i<list_size; i++) {
            _strcpy_to_gui_msg(msg.list_items.list_items[i], _format_folder_name((*list_itr).second.c_str()).c_str());
            list_itr++;
        }
        _post_gui_msg(msg);

        // Set the soft buttons
        msg.type = GuiMsgType::SET_SOFT_BUTTONS;
        std::strcpy(msg.soft_buttons.button1, "BACK");
        std::strcpy(msg.soft_buttons.button2, "----");
        if (_bank_management_state == BankManagmentState::IMPORT) {
            std::strcpy(msg.soft_buttons.button3, "NEXT");
            _import_bank_state = ImportBankState::SELECT_DEST;
        }
        else if (_bank_management_state == BankManagmentState::EXPORT) {
            std::strcpy(msg.soft_buttons.button3, "EXPORT");
            _export_bank_state = ExportBankState::SELECT_BANK;
        }
        else {
            std::strcpy(msg.soft_buttons.button3, "CLEAR");
            _clear_bank_state = ClearBankState::SELECT_BANK;
        }
        _post_gui_msg(msg);
        _selected_bank_dest_name = _list_item_from_index(_selected_bank_dest).second;

        // Configure the data knob
        _num_list_items = list_size;
        _config_data_knob(_num_list_items);
    }    
}

//----------------------------------------------------------------------------
// _show_sys_menu_bank_import_method_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_bank_import_method_screen()
{
    // Set the left status
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "IMPORT BANK");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    std::strcpy(msg.warning_screen.line_1, "Merge or overwrite existing");
    std::strcpy(msg.warning_screen.line_2, "patch files?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "MERGE");
    std::strcpy(msg.soft_buttons.button3, "OVERWITE");
    _post_gui_msg(msg);
    _import_bank_state = ImportBankState::IMPORT_METHOD;
}

//----------------------------------------------------------------------------
// _show_sys_menu_bank_import_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_bank_import_screen(bool merge)
{
    std::string str1;
    std::string str2;
    int ret = 0;

    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "IMPORT BANK");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    str1 = std::string("Importing bank ") + (merge ? "(merge)" : "(overwrite)");
    std::strcpy(msg.warning_screen.line_1, str1.c_str());
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::NOT_STARTED;

    // If we are merging, run the bank import check merge script first
    if (merge) {
        // Run the bank import merge check script
        ret = _sw_manager->run_bank_import_merge_check_script(_selected_bank_archive_name.c_str(), _selected_bank_dest_name.c_str());
        MSG("Bank import merge check: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
        if (ret == 0) {
            NINA_LOG_INFO(module(), "Bank import merge check: COMPLETE");
        }
        else {
            // Merge check failed
            str1 = "Merge cannot be performed,";
            str2 = "not enough free patches";
            _progress_state = ProgressState::FAILED;            
            NINA_LOG_ERROR(module(), "Bank import merge check: FAILED");
        }
    }

    // If all is well, run the bank import script
    if (ret == 0) {
        // Run the bank import script
        ret = _sw_manager->run_bank_import_script(_selected_bank_archive_name.c_str(), _selected_bank_dest_name.c_str(), merge);
        MSG("Bank import: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
        if (ret == 0) {
            NINA_LOG_INFO(module(), "Bank import: COMPLETE");
        }
        else {
            NINA_LOG_ERROR(module(), "Bank import: FAILED");
        }
        str1 = std::string("Bank import") + ((ret == 0) ? " Complete" : " FAILED");
        str2 = "Eject USB Drive?";
        _progress_state = ProgressState::FINISHED;
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    std::strcpy(msg.warning_screen.line_1, str1.c_str());
    std::strcpy(msg.warning_screen.line_2, str2.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    if (_progress_state == ProgressState::FINISHED) {
        std::strcpy(msg.soft_buttons.button3, "EJECT");
    }
    else {
        std::strcpy(msg.soft_buttons.button3, "----");
    }
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _show_sys_menu_bank_export_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_bank_export_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "EXPORT BANK");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Exporting bank");
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::NOT_STARTED;

    // Run the bank export script
    auto ret = _sw_manager->run_bank_export_script(_selected_bank_dest_name.c_str());
    MSG("Bank export: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Bank export: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Bank export: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Bank export") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "EJECT");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_bank_add_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_bank_add_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "ADD BANK");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Adding Bank");
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::NOT_STARTED;

    // Run the add bank scrept script
    auto ret = _sw_manager->run_bank_add_script();
    MSG("Add Bank: " << ((ret > 0) ? "COMPLETE" : "FAILED"));
    if (ret > 0) {
        NINA_LOG_INFO(module(), "Add Bank: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Add Bank: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str1 = std::string("Add Bank") + ((ret > 0) ? " Complete" : " FAILED");
    auto str2 = "New Bank " + std::to_string(ret) + " added";
    std::strcpy(msg.warning_screen.line_1, str1.c_str());
    std::strcpy(msg.warning_screen.line_2, str2.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_bank_clear_confirm_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_bank_clear_confirm_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "CLEAR BANK");
    _post_gui_msg(msg);

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Confirm clear bank");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, _selected_bank_dest_name.c_str());
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "YES");
    _post_gui_msg(msg);
    _clear_bank_state = ClearBankState::CONFIRM;
}

//----------------------------------------------------------------------------
// _show_sys_menu_bank_clear_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_bank_clear_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "CLEAR BANK");
    _post_gui_msg(msg);

    // Run the bank clear script script
    auto ret = _sw_manager->run_bank_clear_script(_selected_bank_dest_name.c_str());
    MSG("Clear Bank: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Clear Bank: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Clear Bank: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Clear Bank") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_wt_management_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_wt_management_screen()
{
    uint item = 0;

    // Set the lefr status
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "WAVETABLES");
    _post_gui_msg(msg);

    // Set the list of Wavetable Management options
    msg.type = GuiMsgType::SHOW_LIST_ITEMS;
    msg.list_items.process_enabled_state = true;
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Import Wavetables");
    msg.list_items.list_item_enabled[item++] = _sw_manager->wt_archive_present();
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Export Wavetables");
    msg.list_items.list_item_enabled[item++] = _sw_manager->msd_mounted();  
    _strcpy_to_gui_msg(msg.list_items.list_items[item], "Delete unused Wavetables");
     msg.list_items.list_item_enabled[item++] = true;
    msg.list_items.num_items = item;
    msg.list_items.selected_item = _selected_wt_management_item;    
    _post_gui_msg(msg);

    // Set the soft buttons
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "EXIT");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "ENTER");
    _post_gui_msg(msg);
    _gui_state = GuiState::WAVETABLE_MANAGEMENT;
    _wt_management_state = WtManagmentState::SHOW_LIST;
    _progress_state = ProgressState::NOT_STARTED;
    _showing_wt_prune_confirm_screen = false;

    // Configure the data knob
    _num_list_items = item;
    _config_data_knob(_num_list_items);    
}

//----------------------------------------------------------------------------
// _show_sys_menu_wt_import_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_wt_import_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "IMPORT");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Importing wavetables");
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _wt_management_state = WtManagmentState::IMPORT;
    _progress_state = ProgressState::NOT_STARTED;

    // Run the wavetable import script
    std::string str;
    auto ret = _sw_manager->run_wt_import_script();
    MSG("Wavetable import: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        // Wavetable import was successful
        str = "Wavetable import Complete";
        NINA_LOG_INFO(module(), "Wavetable import: COMPLETE");
    }
    else if (ret == WT_NUM_EXCEEDED_ERROR_CODE) {
        // Tried to import too many wavetables
        str = "FAILED: Exceeded max 127 files";
        NINA_LOG_ERROR(module(), "Wavetable import: FAILED, exceeded maximum 127 wavetables");
    }
    else {
        // The import failed - most likely due to one or more of the wavetables
        // being invalid
        // Was there an invalid wavetable?
        std::string invalid_wt = "";
        std::ifstream wt_error_file(WT_ERROR_FILENAME);
        std::getline(wt_error_file, invalid_wt);
        if (!invalid_wt.empty()) {
            // One or more wavetables were invalid
            str = "FAILED: " + invalid_wt;
            NINA_LOG_ERROR(module(), "Wavetable import: FAILED, invalid wavetable '{}'",  invalid_wt);
        }
        else {
            // Generic error
            str = "Wavetable import FAILED";
            NINA_LOG_ERROR(module(), "Wavetable import: FAILED");
        }
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    _strcpy_to_gui_msg(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "OK");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_wt_export_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_wt_export_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "EXPORT");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Exporting wavetables");
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _wt_management_state = WtManagmentState::EXPORT;
    _progress_state = ProgressState::NOT_STARTED;

    // Run the wavetable export script
    auto ret = _sw_manager->run_wt_export_script();
    MSG("Wavetable export: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Wavetable export: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Wavetable export: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Wavetable export") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "OK");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_wt_prune_confirm_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_wt_prune_confirm_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "WAVETABLES");
    _post_gui_msg(msg);

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    std::strcpy(msg.warning_screen.line_1, "Delete all unused");
    std::strcpy(msg.warning_screen.line_2, "wavetables?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "YES");
    _post_gui_msg(msg);
    _wt_management_state = WtManagmentState::PRUNE;
    _progress_state = ProgressState::NOT_STARTED;    
    _showing_wt_prune_confirm_screen = true;
}

//----------------------------------------------------------------------------
// _show_sys_menu_wt_prune_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_wt_prune_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "WAVETABLES");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Deleting unused wavetables");
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);

    // Run the wavetable prune script
    auto ret = _sw_manager->run_wt_prune_script();
    MSG("Delete unused Wavetables: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Delete unused Wavetables: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Delete unused Wavetables: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    std::strcpy(msg.warning_screen.line_1, "Delete unused wavetables");
    std::strcpy(msg.warning_screen.line_2, ((ret == 0) ? "Complete" : "FAILED"));
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _progress_state = ProgressState::FINISHED;
    _showing_wt_prune_confirm_screen = false;
}

//----------------------------------------------------------------------------
// _show_sys_menu_backup_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_backup_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "SYSTEM");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Running backup script");
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::BACKUP;
    _backup_state = BackupState::BACKUP_STARTED; 

    // Run the backup script
    auto ret = _sw_manager->run_backup_script();
    MSG("Backup script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Backup script: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Backup script: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Backup script") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "OK");
    _post_gui_msg(msg);
    _gui_state = GuiState::BACKUP;
    _backup_state = BackupState::BACKUP_FINISHED;
}

//----------------------------------------------------------------------------
// _show_sys_menu_restore_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_restore_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "SYSTEM");
    _post_gui_msg(msg);            
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = true;
    std::strcpy(msg.warning_screen.line_1, "Running restore backup script");
    std::strcpy(msg.warning_screen.line_2, "Please wait");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "----");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);
    _gui_state = GuiState::BACKUP;
    _backup_state = BackupState::BACKUP_STARTED; 

    // Run the restore backup script
    auto ret = _sw_manager->run_restore_backup_script();
    MSG("Restore backup script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Restore backup script: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Restore backup script: FAILED");
    }

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    auto str = std::string("Restore backup script") + ((ret == 0) ? " Complete" : " FAILED");
    std::strcpy(msg.warning_screen.line_1, str.c_str());
    std::strcpy(msg.warning_screen.line_2, "Eject USB Drive?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "OK");
    _post_gui_msg(msg);
    _gui_state = GuiState::BACKUP;
    _backup_state = BackupState::BACKUP_FINISHED; 
}

//----------------------------------------------------------------------------
// _show_sys_menu_global_settings_screen
//----------------------------------------------------------------------------
void GuiManager::_show_sys_menu_global_settings_screen()
{
    // Get the first global settings param
    _stop_param_change_timer();
    auto param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::MIDI_CLK_IN_PARAM_ID);
    if (param) {
        // The param will become the root param - get the index
        // of this param in the param list
        auto index = _get_param_list_index(param, param);
        if (index >= 0) {
            // Reset the GUI state                  
            _reset_gui_state();
            _gui_state = GuiState::PARAM_UPDATE;

            // Soft button 2 must be configured as EDIT mode
            _config_soft_button_2(true);

            // Setup the param shown settings
            _param_shown_root = param;
            _param_shown = param;
            _param_shown_index = index;

            // Show the param
            _show_scope = false;
            _post_param_update();
        }
    }
}

//----------------------------------------------------------------------------
// _show_reset_layers_confirm_screen
//----------------------------------------------------------------------------
void GuiManager::_show_reset_layers_confirm_screen()
{
    // Show a confirmation popup
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_LEFT_STATUS;
    _strcpy_to_gui_msg(msg.left_status.status, "RESET LAYERS");
    _post_gui_msg(msg);

    // Show a confirmation popup
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = true;
    msg.warning_screen.show_hourglass = false;
    std::strcpy(msg.warning_screen.line_1, "Reset Layers back to");
    std::strcpy(msg.warning_screen.line_2, "one Layer default?");
    _post_gui_msg(msg);
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    std::strcpy(msg.soft_buttons.button1, "BACK");
    std::strcpy(msg.soft_buttons.button2, "----");
    std::strcpy(msg.soft_buttons.button3, "YES");
    _post_gui_msg(msg);
    _showing_reset_layers_screen = true;
}

//----------------------------------------------------------------------------
// _show_conf_screen
//----------------------------------------------------------------------------
void GuiManager::_show_conf_screen(const char *line1, const char *line2)
{
    // Show a confirmations creen
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_CONF_SCREEN;
    std::strcpy(msg.conf_screen.line_1, line1);
    std::strcpy(msg.conf_screen.line_2, line2);
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _set_tempo_status
//----------------------------------------------------------------------------
void GuiManager::_set_tempo_status(uint tempo)
{
    // Update the Tempo Status
    auto msg = GuiMsg(GuiMsgType::SET_TEMPO_STATUS);
    _strcpy_to_gui_msg(msg.tempo_status.tempo_value, _to_string(tempo, 3).c_str());
    _post_gui_msg(msg); 
}

//----------------------------------------------------------------------------
// _mod_matrix_param_path
//----------------------------------------------------------------------------
std::string GuiManager::_mod_matrix_param_path(uint _src_index, uint _dst_index)
{
    // Create the Mod Matrix param path
    return std::regex_replace("/daw/main/ninavst/Mod_" + _mod_matrix_src_names[_src_index] + 
                              ":" + _mod_matrix_dst_names[_dst_index],
                              std::regex{" "}, "_");
}

//----------------------------------------------------------------------------
// _mod_maxtrix_src_is_not_an_lfo
//----------------------------------------------------------------------------
bool GuiManager::_mod_maxtrix_src_is_not_an_lfo(uint src_index)
{
    return (src_index != LFO_1_MOD_MAXTRIX_SRC_INDEX) && (src_index != LFO_2_MOD_MAXTRIX_SRC_INDEX);
}

//----------------------------------------------------------------------------
// _mod_maxtrix_src_is_lfo_2
//----------------------------------------------------------------------------
bool GuiManager::_mod_maxtrix_src_is_lfo_2(uint src_index)
{
    return src_index == LFO_2_MOD_MAXTRIX_SRC_INDEX;
}

//----------------------------------------------------------------------------
// _post_param_update
//----------------------------------------------------------------------------
void GuiManager::_post_param_update()
{
    // Parse the GUI state
    switch (_gui_state) {
        case GuiState::HOME_SCREEN:
        case GuiState::PARAM_UPDATE: {
            // Post the param update for a normal param
            _post_normal_param_update();
            break;
        }

        case GuiState::MOD_MATRIX_DST: {
            // Post the param update for mod matrix
            _post_mod_matrix_param_update();
            break;
        }

        case GuiState::MANAGE_LAYERS: {
            // Post the param update for the current layer
            _post_layer_param_update(utils::get_current_layer_info().layer_num());
            break;            
        }

        default: {
            break;
        } 
    }     
}

//----------------------------------------------------------------------------
// _post_normal_param_update
//----------------------------------------------------------------------------
void GuiManager::_post_normal_param_update()
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_PARAM_UPDATE;

    // Create the param list
    _params_list.clear();
    _params_list = _param_shown->get_param_list();
    uint num_items = _params_list.size();
    msg.param_update.num_items = num_items;
    msg.param_update.selected_item = _param_shown_index;
    msg.param_update.show_scope = _show_scope;
    uint i = 0;
    for (auto p : _params_list) {
        _strcpy_to_gui_msg(msg.param_update.list_items[i], p->get_name());
        msg.param_update.list_item_separator[i] = p->separator;
        msg.param_update.list_item_enabled[i++] = true;
    }

    // Set the parameter name
    _strcpy_to_gui_msg(msg.param_update.name, (_param_shown->param_list_display_name + ":").c_str());

    // Set the parameter value
    // Check for the special case of Wavetable Select - in this case we show the actual filename, not
    // the value of the Wavetable Select float
    if (_param_shown == utils::get_param_from_ref(utils::ParamRef::WT_SELECT)) {
        // Show the actual filename
        auto filename_param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::WT_NAME_PARAM_ID);
        if (filename_param) {        
            _strcpy_to_gui_msg(msg.param_update.display_string, filename_param->get_str_value().c_str());
        }
        else {
           _strcpy_to_gui_msg(msg.param_update.display_string, "Unknown file"); 
        }
        std::memset(msg.param_update.value_string, 0, sizeof(msg.param_update.value_string));       
    }
    else {
        // Get the value as a string to show
        auto str = _param_shown->get_value_as_string();
        if (str.second) {
            _strcpy_to_gui_msg(msg.param_update.value_string, str.first.c_str());
            std::memset(msg.param_update.display_string, 0, sizeof(msg.param_update.display_string));
        }
        else {
            _strcpy_to_gui_msg(msg.param_update.display_string, str.first.c_str());
            std::memset(msg.param_update.value_string, 0, sizeof(msg.param_update.value_string));
        }
    }

    // If there is a value tag, show it
    if (_param_shown->get_value_tag().size() > 0) {
        _strcpy_to_gui_msg(msg.param_update.value_tag, _param_shown->get_value_tag().c_str());
    }
    else {
        std::memset(msg.param_update.value_tag, 0, sizeof(msg.param_update.value_tag));
    }     
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    _strcpy_to_gui_msg(msg.soft_buttons.button1, "EXIT");
    _strcpy_to_gui_msg(msg.soft_buttons.button2, "EDIT");
    if (_param_shown->param_list_name == PARAM_LIST_MORPH) {
        if (utils::get_current_layer_info().get_patch_state() == PatchState::STATE_A) {
            _strcpy_to_gui_msg(msg.soft_buttons.button3, "STORE TO A");
        }
        else {
            _strcpy_to_gui_msg(msg.soft_buttons.button3, "STORE TO B");
        }
    }
    else {
        _strcpy_to_gui_msg(msg.soft_buttons.button3, "----");
    }
    _post_gui_msg(msg);

    // Configure the data knob
    _config_data_knob(num_items);

    // If we are in the Home GUI state, set it to param update
    if (_gui_state == GuiState::HOME_SCREEN)
        _gui_state = GuiState::PARAM_UPDATE;
}

//----------------------------------------------------------------------------
// _post_mod_matrix_param_update
//----------------------------------------------------------------------------
void GuiManager::_post_mod_matrix_param_update()
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_PARAM_UPDATE;

    // Go through the param list for this mod matrix source
    uint num_items = 0;
    msg.param_update.selected_item = 0;
    msg.param_update.show_scope = true;
    bool found = false;
    auto params_list_cpy = _params_list;
    _params_list.clear();
    for (auto p : _param_shown->param_list) {
        // If we are showing a new list
        if (_new_mod_matrix_param_list) {
            // If either:
            // - The value of the mod matrix entry is active, or
            // - We should always show this parameter
            // Then add it to the display list
            if (p->always_show || _mod_matrix_param_active(p)) {
                // Add it to the display list
                // Note: Always show the currently selected param as enabled
                _strcpy_to_gui_msg(msg.param_update.list_items[num_items], p->get_name());
                msg.param_update.list_item_enabled[num_items] = (num_items == 0) ? true : _mod_matrix_param_active(p);
                _params_list.push_back(p);

                // If this param is the param to show
                if (p == _param_shown) {
                    // Indicate it is found
                    found = true;
                }
                num_items++;
            }
        }
        else {
            // Not showing a new list, so we need to process the previous list first
            bool shown = false;
            for (const Param *sp : params_list_cpy) {
                // If the param in the previous list
                if (p == sp) {
                    // Indicate this param is shown and add it to the display list
                    // Note: Always show the currently selected param as enabled
                    shown = true;
                    _strcpy_to_gui_msg(msg.param_update.list_items[num_items], p->get_name());
                    msg.param_update.list_item_enabled[num_items] = (p == _param_shown) ? true : _mod_matrix_param_active(p);
                    _params_list.push_back(p); 
                                        
                    // If this param is the param to show
                    if (p == _param_shown) {
                        // Indicate it is found and set the selected item
                        found = true;
                        msg.param_update.selected_item = num_items;
                    }
                    num_items++;
                    break;
                }
            }
            // If this param did not already exist in the previous list
            if (!shown) {
                // If this is the param to show
                if (p == _param_shown) {
                    // Add it to the display list
                    // Note: Always show the currently selected param as enabled              
                    _strcpy_to_gui_msg(msg.param_update.list_items[num_items], p->get_name());
                    msg.param_update.list_item_enabled[num_items] = true;
                    _params_list.push_back(p);                    

                    // Indicate it is found and set the selected item                       
                    found = true;
                    msg.param_update.selected_item = num_items;
                    num_items++;
                }
            }

            // Is this the Morph destination param and should it now be shown?
            if (_is_mod_matrix_morph_dst_param(p) && !shown && _is_mod_matrix_eg_2_level_dst_param(_param_shown)) {
                // Add it to the display list                 
                _strcpy_to_gui_msg(msg.param_update.list_items[num_items], p->get_name());
                msg.param_update.list_item_enabled[num_items] = _mod_matrix_param_active(p);
                _params_list.push_back(p);
                num_items++;
            }
        }         
    }
    _new_mod_matrix_param_list = false;

    // If the param to show was not found but there are items in the list,
    // then just default to the first item in the list
    if (!found && num_items) {
        _param_shown = _params_list.front();
    }
    msg.param_update.num_items = num_items;
    _param_shown_index =  msg.param_update.selected_item;

    // Set the parameter name
    _strcpy_to_gui_msg(msg.param_update.name, (_param_shown->param_list_display_name + ":").c_str());

    // Set the parameter value  
    auto str = _param_shown->get_value_as_string();
    if (str.second) {
        _strcpy_to_gui_msg(msg.param_update.value_string, str.first.c_str());
        std::memset(msg.param_update.display_string, 0, sizeof(msg.param_update.display_string));
    }
    else {
        _strcpy_to_gui_msg(msg.param_update.display_string, str.first.c_str());
        std::memset(msg.param_update.value_string, 0, sizeof(msg.param_update.value_string));
    }
    
    // If there is a value tag, show it
    if (_param_shown->get_value_tag().size() > 0) {
        _strcpy_to_gui_msg(msg.param_update.value_tag, _param_shown->get_value_tag().c_str());
    }
    else {
        std::memset(msg.param_update.value_tag, 0, sizeof(msg.param_update.value_tag));
    }     
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    _strcpy_to_gui_msg(msg.soft_buttons.button1, "EXIT");
    if (_params_list.size() > 0) {
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "EDIT");
        _strcpy_to_gui_msg(msg.soft_buttons.button3, "RESET");
    }
    else {
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "----");
        _strcpy_to_gui_msg(msg.soft_buttons.button3, "----");
    }
    _post_gui_msg(msg);

    // Configure the data knob
    if (_params_list.size() > 0) {
        _config_data_knob( num_items);
        _config_soft_button_2(true);
    }
    else {
        _config_data_knob();
        _config_soft_button_2(false);
    } 
}

//----------------------------------------------------------------------------
// _post_layer_param_update
//----------------------------------------------------------------------------
void GuiManager::_post_layer_param_update(uint layer)
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SHOW_PARAM_UPDATE;

    // Get the Layer number of voices
    uint num_voices = utils::get_layer_info(layer).get_num_voices();

    // Create the param list
    // Note: The first two items in the list are assumed to be:
    // - The current layer
    // - Layer number of voices
    // If the number of voices in the layer is zero, all other params are disabled
    _params_list.clear();
    _params_list = _param_shown->get_param_list();
    uint num_items = _params_list.size();
    msg.param_update.num_items = num_items;
    msg.param_update.selected_item = _param_shown_index;
    msg.param_update.show_scope = false;
    uint i = 0;
    for (auto p : _params_list) {
        _strcpy_to_gui_msg(msg.param_update.list_items[i], p->get_name());
        msg.param_update.list_item_separator[i] = p->separator;
        if ((i > 1) && (num_voices == 0)) {
            msg.param_update.list_item_enabled[i++] = false;
        }
        else {
            msg.param_update.list_item_enabled[i++] = true;
        }
    }

    // Set the parameter name
    _strcpy_to_gui_msg(msg.param_update.name, (_param_shown->param_list_display_name + ":").c_str());

    // Set the parameter value
    auto str = _param_shown->get_value_as_string();
    if (str.second) {
        _strcpy_to_gui_msg(msg.param_update.value_string, str.first.c_str());
        std::memset(msg.param_update.display_string, 0, sizeof(msg.param_update.display_string));
    }
    else {
        _strcpy_to_gui_msg(msg.param_update.display_string, str.first.c_str());
        std::memset(msg.param_update.value_string, 0, sizeof(msg.param_update.value_string));
    }

    // If there is a value tag, show it
    if (_param_shown->get_value_tag().size() > 0) {
        // We need to check for the special case of Current Layer
        // If the param shown is the current layer, and the number of voices is zero, indicate this in the value tag
        if ((_param_shown == utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER)) && (num_voices == 0)) {
            _strcpy_to_gui_msg(msg.param_update.value_tag, "0 Voices");
        }
        else {
            _strcpy_to_gui_msg(msg.param_update.value_tag, _param_shown->get_value_tag().c_str());                
        }
    }
    else {
        std::memset(msg.param_update.value_tag, 0, sizeof(msg.param_update.value_tag));
    }     
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    _strcpy_to_gui_msg(msg.soft_buttons.button1, "EXIT");
    _strcpy_to_gui_msg(msg.soft_buttons.button2, "EDIT");
    _strcpy_to_gui_msg(msg.soft_buttons.button3, "RESET");
    _post_gui_msg(msg);

    // Configure the data knob
    _config_data_knob(num_items);     
}

//----------------------------------------------------------------------------
// _post_param_value_update
//----------------------------------------------------------------------------
void GuiManager::_post_param_update_value(bool select_list_item)
{
    // Parse the GUI state
    switch (_gui_state) {
        case GuiState::HOME_SCREEN:
        case GuiState::PARAM_UPDATE: {
            // Post the param update value for a normal param
            _post_normal_param_update_value(select_list_item);
            break;
        }

        case GuiState::MOD_MATRIX_DST: {
            // Post the param update value for mod matrix
            _post_mod_matrix_param_update_value(select_list_item);
            break;
        }

        case GuiState::MANAGE_LAYERS: {
            // Post the param update value for the current layer
            _post_layer_param_update_value(select_list_item);
            break;            
        }

        default: {
            break;
        } 
    }
}

//----------------------------------------------------------------------------
// _post_normal_param_update_value
//----------------------------------------------------------------------------
void GuiManager::_post_normal_param_update_value(bool select_list_item)
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::PARAM_UPDATE_VALUE;

    // Set the parameter value
    // Check for the special case of Wavetable Select - in this case we show the actual filename, not
    // the value of the Wavetable Select float
    if (_param_shown == utils::get_param_from_ref(utils::ParamRef::WT_SELECT)) {
        // Show the actual filename
        auto filename_param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::WT_NAME_PARAM_ID);
        if (filename_param) {        
            _strcpy_to_gui_msg(msg.param_update_value.display_string, filename_param->get_str_value().c_str());
        }
        else {
           _strcpy_to_gui_msg(msg.param_update_value.display_string, "Unknown file"); 
        }
        std::memset(msg.param_update_value.value_string, 0, sizeof(msg.param_update_value.value_string));       
    }
    else {
        // Get the value as a string to show
        auto str = _param_shown->get_value_as_string();
        if (str.second) {
            _strcpy_to_gui_msg(msg.param_update_value.value_string, str.first.c_str());
            std::memset(msg.param_update_value.display_string, 0, sizeof(msg.param_update_value.display_string));
        }
        else {
            _strcpy_to_gui_msg(msg.param_update_value.display_string, str.first.c_str());
            std::memset(msg.param_update_value.value_string, 0, sizeof(msg.param_update_value.value_string));
        }
    }

    // If there is a value tag, show it
    if (_param_shown->get_value_tag().size() > 0) {
        _strcpy_to_gui_msg(msg.param_update_value.value_tag, _param_shown->get_value_tag().c_str());
    }
    else {
        std::memset(msg.param_update_value.value_tag, 0, sizeof(msg.param_update_value.value_tag));
    }

    // Select the list item if needed (-1 means leave the current selection as is)
    if (select_list_item)
        msg.param_update_value.selected_item = _param_shown_index;
    else
        msg.param_update_value.selected_item = -1;
    _post_gui_msg(msg);    
}

//----------------------------------------------------------------------------
// _post_mod_matrix_param_update_value
//----------------------------------------------------------------------------
void GuiManager::_post_mod_matrix_param_update_value(bool select_list_item)
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::PARAM_UPDATE_VALUE;

    // Set the parameter value
    auto str = _param_shown->get_value_as_string();
    if (str.second) {
        _strcpy_to_gui_msg(msg.param_update_value.value_string, str.first.c_str());
        std::memset(msg.param_update_value.display_string, 0, sizeof(msg.param_update_value.display_string));
    }
    else {
        _strcpy_to_gui_msg(msg.param_update_value.display_string, str.first.c_str());
        std::memset(msg.param_update_value.value_string, 0, sizeof(msg.param_update_value.value_string));
    }

    // If there is a value tag, show it
    if (_param_shown->get_value_tag().size() > 0) {
        _strcpy_to_gui_msg(msg.param_update_value.value_tag, _param_shown->get_value_tag().c_str());
    }
    else {
        std::memset(msg.param_update_value.value_tag, 0, sizeof(msg.param_update_value.value_tag));
    }

    // Select the list item if needed (-1 means leave the current selection as is)
    if (select_list_item)
        msg.param_update_value.selected_item = _param_shown_index;
    else
        msg.param_update_value.selected_item = -1;
    _post_gui_msg(msg);    
}

//----------------------------------------------------------------------------
// _post_layer_param_update_value
//----------------------------------------------------------------------------
void GuiManager::_post_layer_param_update_value(bool select_list_item)
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::PARAM_UPDATE_VALUE;

    // We need special handling for the Current Layer param
    if (_param_shown == utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER)) {
        // Has the layer changed?
        if ((uint)_param_shown->get_position_value() != utils::get_current_layer_info().layer_num()) {
            // Load the new Layer
            auto layer = _param_shown->get_position_value();
            _reload_presets_from_select_patch_load++;
            _event_router->post_system_func_event(new SystemFuncEvent(SystemFunc(CURRENT_LAYER, layer, GUI)));
            _set_sys_func_switch(SystemFuncType::TOGGLE_PATCH_STATE, 
                                 ((utils::get_layer_info(layer).get_patch_state() == PatchState::STATE_A) ? false : true));

            // Update the current layer status
            auto msg = GuiMsg();
            msg.type = GuiMsgType::SET_LAYER_STATUS;
            auto str = "L" + std::to_string(layer + 1);
            _strcpy_to_gui_msg(msg.layer_status.status, str.c_str());
            _post_gui_msg(msg);

            // Because we have loaded a new layer, we need to process this value update as a full
            // layer param update (updates the list)
            _post_layer_param_update(layer);

            // If the keyboard is not enabled, select the multi-function switches
            if (!_kbd_enabled) {
                // Select the multi-function switch
                _select_multifn_switch(layer);
            }
            return;
        }
    }
    else if (_param_shown == utils::get_param_from_ref(utils::ParamRef::LAYER_NUM_VOICES)) {
        // We also need special handling if the Layer number of voices parameter is changed
        // Set the new number of voices, and process this as a full layer param update (updates
        // the list)
        // This ensures that if the number of voices is either set to 0 or from 0, the enabled/disabled
        // params in the list are shown correctly
        utils::get_current_layer_info().set_num_voices(_param_shown->get_position_value());
        _post_param_update();
        return;
    }
    else if (_param_shown == utils::get_param_from_ref(utils::ParamRef::MIDI_CHANNEL_FILTER)) {
        // We also need special handling if the Layer MIDI Channel Filter parameter is changed
        utils::get_current_layer_info().set_midi_channel_filter(_param_shown->get_position_value());
    }

    // Set the parameter value
    auto str = _param_shown->get_value_as_string();
    if (str.second) {
        _strcpy_to_gui_msg(msg.param_update_value.value_string, str.first.c_str());
        std::memset(msg.param_update_value.display_string, 0, sizeof(msg.param_update_value.display_string));
    }
    else {
        _strcpy_to_gui_msg(msg.param_update_value.display_string, str.first.c_str());
        std::memset(msg.param_update_value.value_string, 0, sizeof(msg.param_update_value.value_string));
    }

    // If there is a value tag, show it
    if (_param_shown->get_value_tag().size() > 0) {
        // We need to check for the special case of Current Layer
        // If the param shown is the current layer, and the number of voices is zero, indicate this in the value tag
        uint num_voices = utils::get_current_layer_info().get_num_voices();
        if ((_param_shown == utils::get_param_from_ref(utils::ParamRef::CURRENT_LAYER)) && (num_voices == 0)) {
            _strcpy_to_gui_msg(msg.param_update_value.value_tag, "0 Voices");
        }
        else {
            _strcpy_to_gui_msg(msg.param_update_value.value_tag, _param_shown->get_value_tag().c_str());                
        }
    }
    else {
        std::memset(msg.param_update_value.value_tag, 0, sizeof(msg.param_update_value.value_tag));
    }     

    // Select the list item if needed (-1 means leave the current selection as is)
    if (select_list_item)
        msg.param_update_value.selected_item = _param_shown_index;
    else
        msg.param_update_value.selected_item = -1;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _post_enum_list_param_update
//----------------------------------------------------------------------------
void GuiManager::_post_enum_list_param_update()
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::ENUM_PARAM_UPDATE;
    msg.enum_param_update.wt_list = false;

    // Get the number of positions and current value for this enum list param
    uint num_pos = 0;
    uint pos_value = 0;    
    if (_param_shown->display_switch) {
        num_pos = 2;
        pos_value = (_param_shown->get_value() == 0.0) ? 0 : 1;
    }
    else {
        num_pos = _param_shown->get_num_positions();
        pos_value = _param_shown->get_position_value();
    }

    // Set the parameter name
    auto param_name = std::string(_param_shown->get_name());
    _string_toupper(param_name);
    auto str = _param_shown->param_list_display_name + ":" + param_name;
    _strcpy_to_gui_msg(msg.param_update.name, str.c_str());

    // Set the param enum list and selected item
    msg.enum_param_update.num_items = num_pos;
    msg.enum_param_update.selected_item = pos_value;
    for (uint i=0; i<num_pos; i++) {
        _strcpy_to_gui_msg(msg.enum_param_update.list_items[i], _param_shown->get_position_as_string(i).c_str());
    }
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    _strcpy_to_gui_msg(msg.soft_buttons.button1, "BACK");
    (_showing_param_shortcut) ?
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "----") :
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "EDIT");
    _strcpy_to_gui_msg(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);

    // Configure the data knob
    _config_data_knob(num_pos);
}

//----------------------------------------------------------------------------
// _post_enum_list_param_update_value
//----------------------------------------------------------------------------
void GuiManager::_post_enum_list_param_update_value(uint value)
{
    bool wt_list = false;

    // Update the parameter value
    if (_param_shown->get_num_positions() > 0) {
        _param_shown->set_value_from_position(value);
        if (_filename_param) {
            _filename_param->set_str_value(_filenames[value]);
            auto param_change = ParamChange(_filename_param->get_path(), _filename_param->get_value(), module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            wt_list = true;   
        }

        // Special case - if we are changing the LFO 1 Tempo Sync
        if (_param_shown == utils::get_lfo_1_tempo_sync_param()) {
            // Is tempo sync on?
            if (utils::lfo_1_sync_rate()) {
                _push_controls_state(LFO_1_SYNC_RATE_STATE);
                utils::push_lfo_state(utils::LfoState(LFO_1_SYNC_RATE_STATE, false, false));
            }
            else {
                _pop_controls_state(LFO_1_SYNC_RATE_STATE);
                utils::pop_lfo_state();
            }
        }
        // Special case - if we are changing the LFO 2 Tempo Sync
        else if (_param_shown == utils::get_lfo_2_tempo_sync_param()) {
            // Is tempo sync on?
            if (utils::lfo_2_sync_rate()) {
                _push_controls_state(LFO_2_SYNC_RATE_STATE);
                utils::push_lfo_state(utils::LfoState(LFO_2_SYNC_RATE_STATE, false, true));
            }
            else {
                _pop_controls_state(LFO_2_SYNC_RATE_STATE);
                utils::pop_lfo_state();
            }
        }
        else if (_param_shown == utils::get_param_from_ref(utils::ParamRef::MPE_MODE)) {
            // We also need special handling if the Layer MPE Mode parameter is changed
            utils::get_current_layer_info().set_mpe_mode(utils::get_mpe_mode(_param_shown->get_value()));

            // Update the MPE zone channel params
            auto params_changed = utils::config_mpe_zone_channel_params();
            auto lower_zone_num_channels_param = std::get<0>(params_changed);
            auto upper_zone_num_channels_param = std::get<1>(params_changed);

            // Did the Lower and/or Upper Zone Num Channels change?
            // If so, send a param change event
            if (lower_zone_num_channels_param) {
                auto param_change = ParamChange(lower_zone_num_channels_param->get_path(), lower_zone_num_channels_param->get_value(), module());
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            }
            if (upper_zone_num_channels_param) {
                auto param_change = ParamChange(upper_zone_num_channels_param->get_path(), upper_zone_num_channels_param->get_value(), module());
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            }            
        }
    }
    else {
        // Assume its a switch param
        _param_shown->set_value((value == 0) ? 0.0 : 1.0);

        // Is this the ARP Enable param?
        if (_param_shown == utils::get_param(NinaModule::ARPEGGIATOR, ArpeggiatorParamId::ARP_ENABLE_PARAM_ID)) {
            // Set/clear the ARP LED
            _set_sys_func_switch(SystemFuncType::ARP, (value == 0 ? false : true));
        }
    }
    
    // Update the selected enum param value from the list
    auto msg = GuiMsg();
    msg.type = GuiMsgType::ENUM_PARAM_UPDATE_VALUE;
    msg.list_select_item.selected_item = value;
    msg.list_select_item.wt_list = wt_list;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _post_file_browser_param_update
//----------------------------------------------------------------------------
void GuiManager::_post_file_browser_param_update()
{
    // Set the message type
    auto msg = GuiMsg();
    msg.type = GuiMsgType::ENUM_PARAM_UPDATE;
    msg.enum_param_update.wt_list = true;

    // Set the parameter name
    uint selected_item = 0;
    uint num_items = _filenames.size();
    auto param_name = std::string(_param_shown->get_name());
    _string_toupper(param_name);
    auto str = _param_shown->param_list_display_name + ":" + param_name;
    _strcpy_to_gui_msg(msg.param_update.name, str.c_str());

    // Set the param enum list and selected item
    for (uint i=0; i<num_items; i++) {
        _strcpy_to_gui_msg(msg.enum_param_update.list_items[i], _filenames[i].c_str());
        if (_filenames[i] == _filename_param->get_str_value()) {
            selected_item = i;
        }
    }
    msg.enum_param_update.num_items = num_items;
    msg.enum_param_update.selected_item = selected_item;
    _post_gui_msg(msg);

    // Set the soft buttons text
    msg.type = GuiMsgType::SET_SOFT_BUTTONS;
    _strcpy_to_gui_msg(msg.soft_buttons.button1, "BACK");
    (_showing_param_shortcut) ?
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "----") :
        _strcpy_to_gui_msg(msg.soft_buttons.button2, "EDIT");
    _strcpy_to_gui_msg(msg.soft_buttons.button3, "----");
    _post_gui_msg(msg);

    // Configure the data knob
    _config_data_knob(num_items);

    // If the selected item has changed
    if (selected_item != (uint)_param_shown->get_position_value()) {
        // Update the param and post a param change message
        _param_shown->set_value_from_position(selected_item);                 
        auto param_change = ParamChange(_param_shown->get_path(), _param_shown->get_value(), module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
        _process_param_changed_mapped_params(_param_shown, _param_shown->get_value(), nullptr);        
        
    }
}

//----------------------------------------------------------------------------
// _post_soft_button_state_update
//----------------------------------------------------------------------------
void GuiManager::_post_soft_button_state_update(uint state, SoftButton soft_button)
{
    // Update the soft button state
    auto msg = GuiMsg();
    msg.type = GuiMsgType::SET_SOFT_BUTTONS_STATE;
    msg.soft_buttons_state.state_button1 = (soft_button == SoftButton::SOFT_BUTTON_1 ? state : -1);
    msg.soft_buttons_state.state_button2 = (soft_button == SoftButton::SOFT_BUTTON_2 ? state : -1);
    msg.soft_buttons_state.state_button3 = (soft_button == SoftButton::SOFT_BUTTON_3 ? state : -1);
    _post_gui_msg(msg); 
}

//----------------------------------------------------------------------------
// _clear_warning_screen
//----------------------------------------------------------------------------
void GuiManager::_clear_warning_screen()
{
    // Post a clear warning screen message
    auto msg = GuiMsg();           
    msg.type = GuiMsgType::SHOW_WARNING_SCREEN;
    msg.warning_screen.show = false;
    _post_gui_msg(msg);    
}

//----------------------------------------------------------------------------
// _post_update_selected_list_item
//----------------------------------------------------------------------------
void GuiManager::_post_update_selected_list_item(uint selected_item)
{
    // Update the selected list item
    auto msg = GuiMsg();
    msg.type = GuiMsgType::LIST_SELECT_ITEM;
    msg.list_select_item.selected_item = selected_item;
    _post_gui_msg(msg);
}

//----------------------------------------------------------------------------
// _post_gui_msg
//----------------------------------------------------------------------------
void GuiManager::_post_gui_msg(const GuiMsg &msg)
{
    // If the GUI Message Queue is valid
    if (_gui_mq_desc != (mqd_t)-1)
    {
        // Write the message to the GUI Message Queue
        if (::mq_send(_gui_mq_desc, (char *)&msg, sizeof(msg), 0) == -1)
        {
            // An error occured
            MSG("ERROR: Sending GUI Message: " << errno);
        }
    }
}

//----------------------------------------------------------------------------
// _gui_send_callback
//----------------------------------------------------------------------------
void GuiManager::_gui_send_callback()
{
	// Get the GUI mutex
	std::lock_guard<std::mutex> guard(_gui_mutex);

    // Param change available?
    if (_param_change_available && _param_shown)
    {
        // Are we showing a new param?
        if (_show_param_list) {
            // Post a new param update
            _post_param_update();
            _show_param_list = false;
        }
        else {
            // Show the param update
            _post_param_update_value(true);
        }
        _param_change_available = false;
    }    
}

//----------------------------------------------------------------------------
// _process_param_changed_mapped_params
//----------------------------------------------------------------------------
void GuiManager::_process_param_changed_mapped_params(const Param *param, float value, const Param *skip_param)
{
    // Get the mapped params
    auto mapped_params = param->get_mapped_params();
    for (Param *mp : mapped_params)
    {
        // Because this function is recursive, we need to skip the param that
        // caused any recursion, so it is not processed twice
        if (skip_param && (mp == skip_param))
            continue;

        // Is this a System Func param?
        if (mp->type == ParamType::SYSTEM_FUNC)
        {
            // Is the parent param a physical control?
            if (param && param->physical_control_param) {
                auto sfc_param = static_cast<const SurfaceControlParam *>(param);

                // Is the mapped parameter a multi-position param?
                if (mp->multi_position_param)
                {
                    // Are we changing a switch control value?
                    if (sfc_param->type() == SurfaceControlType::SWITCH)
                    {
                        // For a multi-position param mapped to a system function, we pass
                        // the value in the system function event as the integer position
                        value = param->position;              
                    }
                }

                // Send the System Function event
                auto system_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->get_system_func_type(), 
                                              value,
                                              mp->get_linked_param(),
                                              module());
                system_func.sfc_control_type = sfc_param->type();
                if (system_func.type == SystemFuncType::MULTIFN_SWITCH) 
                {
                    // Calculate the value from the switch number
                    system_func.value = param->param_id - utils::system_config()->get_first_multifn_switch_num();
                }
                _event_router->post_system_func_event(new SystemFuncEvent(system_func));
            }
            else {
                // If this is a position param, get the position value
                if (param->num_positions) {
                    value = param->get_position_value();
                }

                // Create the system function event
                auto system_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->get_system_func_type(), value, mp->get_linked_param(), module());

                // Send the system function event
                _event_router->post_system_func_event(new SystemFuncEvent(system_func));
            }

            // Note: We don't recurse system function params as they are a system action to be performed
        }
        else
        {
            // Only process if something has actually changed
            if (mp->get_value() != value)
            {
                // Update the param value
                mp->set_value_from_param(*param);

                // Send the param change event - only show the first param we can actually display
                // on the GUI
                auto param_change = ParamChange(mp, module());
                param_change.display = false;
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));             
            }

            // We need to recurse each mapped param and process it
            _process_param_changed_mapped_params(mp, value, param);           
        }
    }
}

//----------------------------------------------------------------------------
// _config_data_knob
//----------------------------------------------------------------------------
void GuiManager::_config_data_knob(int num_selectable_positions, float pos)
{
    // Get the data knob param
    auto param = utils::get_data_knob_param();
    if (param)
    {
        std::string haptic_mode = "360_deg";

        // Number of positions specified?
        if (num_selectable_positions > 0) {
            param->set_position_param(NUM_MULTIFN_SWITCHES, num_selectable_positions);
            haptic_mode = "16_step_endless";
        }
        else if (pos >= 0.0)
            param->set_relative_position_param(pos);
        else
            param->reset_param();

        // Set the haptic mode
        auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::SET_CONTROL_HAPTIC_MODE, NinaModule::GUI);
        sfc_func.control_path = param->get_path();
        sfc_func.control_haptic_mode = haptic_mode;
        _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));            
    }
}

//----------------------------------------------------------------------------
// _config_soft_button_2
//----------------------------------------------------------------------------
void GuiManager::_config_soft_button_2(bool as_edit_button)
{
    // Turn the switch OFF and configure the haptic mode
    _set_sys_func_switch(SystemFuncType::SOFT_BUTTON2, false);
    _post_soft_button_state_update(false, SoftButton::SOFT_BUTTON_2);
    _config_switch(SystemFuncType::SOFT_BUTTON2, (as_edit_button ? "latch_push" : "push"));
}

//----------------------------------------------------------------------------
// _config_sys_func_switches
//----------------------------------------------------------------------------
void GuiManager::_config_sys_func_switches(bool enable)
{
    auto haptic_mode = ((_gui_state == GuiState::MOD_MATRIX_DST) && !enable) ? "push" : "toggle";

    // Note: This function only processes the system function switches
    // that are configurable. All other system function switches are
    // left untouched

    // Enable/disable the configurable system function switches
    // The Sequencer cannot be run in the LOAD/SAVE state, and REC/SETTINGS are disabled in the mod
    // matrix state
    _config_switch(SystemFuncType::SEQ_RUN, ((_gui_state == GuiState::MANAGE_PATCH) && !enable) ? "push" : "toggle");
    _config_switch(SystemFuncType::SEQ_REC, (((_gui_state == GuiState::MOD_MATRIX_DST) && !enable)  ? "push" : "toggle_led_pulse"));
    _config_switch(SystemFuncType::SEQ_SETTINGS, haptic_mode);

    // The Keyboard, OSC Coarse, WT Select, LFO Shape, and Noise Type must also be
    // disabled in the mod matrix state
    _config_switch(SystemFuncType::KBD, ((_gui_state == GuiState::MOD_MATRIX_DST) && !enable) ? "push" : "toggle_hold");
    _config_switch(SystemFuncType::OSC_COARSE, haptic_mode);
    _config_switch(SystemFuncType::WT_SELECT, haptic_mode);
    _config_switch(SystemFuncType::LFO_SHAPE, haptic_mode);
    _config_switch(SystemFuncType::NOISE_TYPE, haptic_mode);
}

//----------------------------------------------------------------------------
// _config_switch
//----------------------------------------------------------------------------
void GuiManager::_config_switch(SystemFuncType system_func_type, std::string haptic_mode)
{
    auto param = utils::get_param(SystemFuncParam::ParamPath(system_func_type).c_str());
    if (param) {    
        auto mapped_params = param->get_mapped_params();
        for (Param *mp : mapped_params) {
            // Set the switch haptic modeWT_Select
            if (mp->physical_control_param) {
                auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::SET_CONTROL_HAPTIC_MODE, NinaModule::GUI);
                sfc_func.control_path = mp->get_path();
                sfc_func.control_haptic_mode = haptic_mode;
                _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
            }
        }
    }
}

//----------------------------------------------------------------------------
// _reset_sys_func_switches
//----------------------------------------------------------------------------
void GuiManager::_reset_sys_func_switches(SystemFuncType except_system_func_type)
{
    if (except_system_func_type != SystemFuncType::SELECT_PATCH)
        _set_sys_func_switch(SystemFuncType::SELECT_PATCH, false);
    if (except_system_func_type != SystemFuncType::PATCH_SAVE)
         _set_sys_func_switch(SystemFuncType::PATCH_SAVE, false);         
    if (except_system_func_type != SystemFuncType::MOD_MATRIX)    
        _set_sys_func_switch(SystemFuncType::MOD_MATRIX, false);
    if (except_system_func_type != SystemFuncType::LAYER)    
        _set_sys_func_switch(SystemFuncType::LAYER, false);
    if (except_system_func_type != SystemFuncType::SEQ_SETTINGS)    
        _set_sys_func_switch(SystemFuncType::SEQ_SETTINGS, false);
    if (except_system_func_type != SystemFuncType::OSC_COARSE) {  
        // Only reset the OSC Coarse system function if processing Mod Matrix
        if (except_system_func_type == SystemFuncType::MOD_MATRIX) {
            _set_sys_func_switch(SystemFuncType::OSC_COARSE, false);
            _pop_back_controls_state();
        }
    }
    if (except_system_func_type != SystemFuncType::WT_SELECT)
        _set_sys_func_switch(SystemFuncType::WT_SELECT, false);
    if (except_system_func_type != SystemFuncType::LFO_SHAPE)
        _set_sys_func_switch(SystemFuncType::LFO_SHAPE, false);
    if (except_system_func_type != SystemFuncType::NOISE_TYPE)
        _set_sys_func_switch(SystemFuncType::NOISE_TYPE, false);
    if (except_system_func_type != SystemFuncType::UNKNOWN)
        _set_sys_func_switch(except_system_func_type, true);            
}

//----------------------------------------------------------------------------
// _reset_param_shortcut_switches
//----------------------------------------------------------------------------
void GuiManager::_reset_param_shortcut_switches()
{
    // If not in the Mod Matrix state
    if (_gui_state != GuiState::MOD_MATRIX_DST) {
        // Reset all of the param short cut switches
        _set_sys_func_switch(SystemFuncType::WT_SELECT, false);
        _set_sys_func_switch(SystemFuncType::LFO_SHAPE, false);
        _set_sys_func_switch(SystemFuncType::NOISE_TYPE, false);
    }
}

//----------------------------------------------------------------------------
// _set_sys_func_switch
//----------------------------------------------------------------------------
void GuiManager::_set_sys_func_switch(SystemFuncType system_func_type, bool set)
{
    // Get the switch associated with the system function and reset it
    auto param = utils::get_param(SystemFuncParam::ParamPath(system_func_type).c_str());
    if (param) {
        auto mapped_params = param->get_mapped_params();
        for (Param *mp : mapped_params)
            _set_switch(mp->get_path(), set);
    }
}

//----------------------------------------------------------------------------
// _set_switch
//----------------------------------------------------------------------------
void GuiManager::_set_switch(std::string path, bool set)
{
    // Reset the specified switch
    auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::SET_SWITCH_VALUE, NinaModule::GUI);
    sfc_func.control_path = path;
    sfc_func.set_switch = set;
    _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));   
}

//----------------------------------------------------------------------------
// _reset_multifn_switches
//----------------------------------------------------------------------------
void GuiManager::_reset_multifn_switches(bool force)
{
    // If the multi-function switches are not in keyboard mode, or we should force a reset, reset them
    if ((utils::get_multifn_switches_mode() != MultifnSwitchesMode::KEYBOARD) || force) {
        // Reset the multi-function switches to their default state
        utils::set_multifn_switches_mode(MultifnSwitchesMode::NONE);
        utils::set_num_active_multifn_switches(0);
        auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::RESET_MULTIFN_SWITCHES, NinaModule::GUI);
        _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
    }   
}

//----------------------------------------------------------------------------
// _config_multifn_switches
//----------------------------------------------------------------------------
void GuiManager::_config_multifn_switches(uint num_active, int selected, MultifnSwitchesMode mode)
{
    // Reset the multi-function switches to their default state
    auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::RESET_MULTIFN_SWITCHES, NinaModule::GUI);
    _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));    
    utils::set_multifn_switches_mode(mode);
    utils::set_num_active_multifn_switches(num_active);
    if (selected != -1) {
        _select_multifn_switch(selected);
    }
}

//----------------------------------------------------------------------------
// _select_multifn_switch
//----------------------------------------------------------------------------
void GuiManager::_select_multifn_switch(uint index)
{
    // If the index is within range
    if (index < NUM_MULTIFN_SWITCHES) {
        // Select the specified multi-function key
        auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::SET_MULTIFN_SWITCH, NinaModule::GUI);
        sfc_func.control_path = SwitchParam::MultifnSwitchParamPath(index);
        _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
    }
    else {
        // Clear all the multi-function keys
        auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::RESET_MULTIFN_SWITCHES, NinaModule::GUI);
        _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));         
    }    
}

//----------------------------------------------------------------------------
// _pop_and_push_back_controls_state
//----------------------------------------------------------------------------
void GuiManager::_pop_and_push_back_controls_state(std::string state)
{
    // If there are any pushed control states
    if (_pushed_control_states.size() > 0)
    {
        // Pop the most recent controls state and push the passed state
        auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::PUSH_POP_CONTROLS_STATE, NinaModule::GUI);
        sfc_func.pop_controls_state = _pushed_control_states.back();
        sfc_func.push_controls_state = state;
        sfc_func.process_physical_control = true;
        _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
        _pushed_control_states.pop_back();
        _pushed_control_states.push_back(state);
    }
    else
    {
        // Push the new controls state
        _push_back_controls_state(state);
    }
}

//----------------------------------------------------------------------------
// _push_back_controls_state
//----------------------------------------------------------------------------
void GuiManager::_push_back_controls_state(std::string state)
{
    // Push the passed controls state
    auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::PUSH_POP_CONTROLS_STATE, NinaModule::GUI);
    sfc_func.push_controls_state = state;
    sfc_func.pop_controls_state = "";
    _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
    _pushed_control_states.push_back(state); 
}

//----------------------------------------------------------------------------
// _pop_back_controls_state
//----------------------------------------------------------------------------
void GuiManager::_pop_back_controls_state()
{
    // If there are any pushed control states
    if (_pushed_control_states.size() > 0)
    {
        // Pop the most recent controls state
        auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::PUSH_POP_CONTROLS_STATE, NinaModule::GUI);
        sfc_func.pop_controls_state = _pushed_control_states.back();
        sfc_func.push_controls_state = "";
        sfc_func.process_physical_control = _pushed_control_states.size() == 1;
        _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
        _pushed_control_states.pop_back();
    }
}

//----------------------------------------------------------------------------
// _push_controls_state
//----------------------------------------------------------------------------
void GuiManager::_push_controls_state(std::string state)
{
    // Push the passed controls state
    auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::PUSH_POP_CONTROLS_STATE, NinaModule::GUI);
    sfc_func.push_controls_state = state;
    sfc_func.pop_controls_state = "";
    _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
}

//----------------------------------------------------------------------------
// _pop_controls_state
//----------------------------------------------------------------------------
void GuiManager::_pop_controls_state(std::string state)
{
    // Pop the most recent controls state
    auto sfc_func = SurfaceControlFunc(SurfaceControlFuncType::PUSH_POP_CONTROLS_STATE, NinaModule::GUI);
    sfc_func.pop_controls_state = state;
    sfc_func.push_controls_state = "";
    _event_router->post_sfc_func_event(new SurfaceControlFuncEvent(sfc_func));
}

//----------------------------------------------------------------------------
// _parse_layers_folder
//----------------------------------------------------------------------------
std::map<uint, std::string> GuiManager::_parse_layers_folder()
{
    std::map<uint, std::string> filenames;
    struct dirent **dirent = nullptr;
    int num_files;

    // Scan the layers folder
    num_files = ::scandir(NINA_LAYERS_DIR, &dirent, 0, ::versionsort);
    if (num_files > 0) {
        // Process each file in the folder
        for (uint i=0; i<(uint)num_files; i++) {
            // Is this a normal file?
            if (dirent[i]->d_type == DT_REG)
            {
                // Get the layer index from the filename
                // Note: If the filename format is invalid, atoi will return 0 - which is ok
                // as this is an invalid patch index
                uint index = std::atoi(dirent[i]->d_name);

                // Are the first two characters the patch number?
                if ((index > 0) && (dirent[i]->d_name[3] == '_'))
                {
                    // Has this layers file already been found?
                    // We ignore any duplicated patches with the same index
                    if (filenames[index].empty()) {                    
                        // Add the layers name
                        auto name = std::string(dirent[i]->d_name);
                        filenames[index] = name.substr(0, (name.size() - (sizeof(".json") - 1)));
                    }
                }
            }
            ::free(dirent[i]);
        }
    }
    else if ((num_files == -1) && (errno == ENOENT)) {
        // Layers folder does not exist - this is a critical error
        MSG("The layers folder does not exist: " << NINA_LAYERS_DIR);
        NINA_LOG_CRITICAL(module(), "The layers folder does not exist: {}", NINA_LAYERS_DIR);
    }

    // We now need to make sure that the maximum number of layer configs is always shown
    // Any missing layers are shown as the default in the list
    for (uint i=1; i<=NUM_LAYER_CONFIG_FILES; i++) {
        // Does this layer config exist?
        if (filenames[i].empty()) {
            // Set the default layers filename           
            filenames[i] = utils::get_default_layers_filename(i, false);
        }
    }    
    if (dirent) {
        ::free(dirent);
    }    
    return filenames;
}

//----------------------------------------------------------------------------
// _parse_patches_folder
//----------------------------------------------------------------------------
std::map<uint, std::string> GuiManager::_parse_patches_folder()
{
    std::map<uint, std::string> folder_names;
    struct dirent **dirent = nullptr;
    int num_files;

    // Scan the patches folder
    num_files = ::scandir(NINA_PATCHES_DIR, &dirent, 0, ::versionsort);
    if (num_files > 0) {
        // Process each directory in the folder
        for (uint i=0; i<(uint)num_files; i++) {
            // Is this a directory?
            if (dirent[i]->d_type == DT_DIR)
            {
                // Get the bank index from the folder name
                // Note: If the folder name format is invalid, atoi will return 0 - which is ok
                // as this is an invalid bank index                
                uint index = std::atoi(dirent[i]->d_name);

                // Are the first four characters the bank index?
                if ((index > 0) && (dirent[i]->d_name[3] == '_'))
                {
                    // Has this folder index already been found?
                    // We ignore any duplicated folders with the same index
                    if (folder_names[index].empty()) {
                        // Add the bank name
                        folder_names[index] = dirent[i]->d_name;
                    }
                }
            }
            ::free(dirent[i]);
        }
    }
    else if ((num_files == -1) && (errno == ENOENT)) {
        // Patches folder does not exist - this is a critical error
        MSG("The patches folder does not exist: " << NINA_PATCHES_DIR);
        NINA_LOG_CRITICAL(module(), "The patches folder does not exist: {}", NINA_PATCHES_DIR);
    }
    if (dirent) {
        ::free(dirent);
    }    
    return folder_names;
}

//----------------------------------------------------------------------------
// _parse_bank_folder
//----------------------------------------------------------------------------
std::map<uint, std::string> GuiManager::_parse_bank_folder(const std::string bank_folder_path)
{
    std::map<uint, std::string> filenames;
    struct dirent **dirent = nullptr;
    int num_files;

    // Scan the patches bank folder
    num_files = ::scandir(bank_folder_path.c_str(), &dirent, 0, ::versionsort);
    if (num_files > 0) {
        // Process each file in the folder
        for (uint i=0; i<(uint)num_files; i++) {
            // Is this a normal file?
            if (dirent[i]->d_type == DT_REG)
            {
                // Get the patch index from the filename
                // Note: If the filename format is invalid, atoi will return 0 - which is ok
                // as this is an invalid patch index
                uint index = std::atoi(dirent[i]->d_name);

                // Are the first two characters the patch number?
                if ((index > 0) && (dirent[i]->d_name[3] == '_'))
                {
                    // Has this patch already been found?
                    // We ignore any duplicated patches with the same index
                    if (filenames[index].empty()) {                    
                        // Add the patch name
                        auto name = std::string(dirent[i]->d_name);
                        filenames[index] = name.substr(0, (name.size() - (sizeof(".json") - 1)));
                    }
                }
            }
            ::free(dirent[i]);
        }
    }
    else if ((num_files == -1) && (errno == ENOENT)) {
        // The bank folder folder does not exist - show and log the error
        MSG("The patches bank folder does not exist: " << bank_folder_path);
        NINA_LOG_ERROR(module(), "The patches bank folder does not exist: {}", bank_folder_path);
    }

    // We now need to make sure that the maximum number of patches is always shown
    // Any missing patches are shown as BLANK (the default) in the list
    for (uint i=1; i<=NUM_BANK_PATCH_FILES; i++) {
        // Does this patch exist?
        if (filenames[i].empty()) {
            // Set the default patch filename            
            filenames[i] = utils::get_default_patch_filename(i, false);
        }
    }
    if (dirent) {
        ::free(dirent);
    }    
    return filenames;
}

//----------------------------------------------------------------------------
// _parse_wavetable_folder
//----------------------------------------------------------------------------
std::vector<std::string> GuiManager::_parse_wavetable_folder()
{
    std::vector<std::string> folder_names;
    struct dirent **dirent = nullptr;
    int num_files;

    // Scan the Nina wavetables folder
    num_files = ::scandir(NINA_WAVETABLES_DIR, &dirent, 0, ::versionsort);
    if (num_files > 0) {
        // Process each file in the folder
        for (uint i=0; i<(uint)num_files; i++) {
            // If we've not found the max number of wavetables yet and this a normal file
            if ((folder_names.size() < MAX_NUM_WAVETABLE_FILES) && (dirent[i]->d_type == DT_REG))
            {
                // If it has a WAV file extension
                auto name = std::string(dirent[i]->d_name);
                if (name.substr((name.size() - (sizeof(".wav") - 1))) == ".wav") {
                    // Add the filename
                    folder_names.push_back(name.substr(0, (name.size() - (sizeof(".wav") - 1))));
                }
            }
            ::free(dirent[i]);
        }
    }
    else if ((num_files == -1) && (errno == ENOENT)) {
        // Wavetables folder does not exist - this is a critical error
        MSG("The wavetables folder does not exist: " << NINA_WAVETABLES_DIR);
        NINA_LOG_CRITICAL(module(), "The wavetables folder does not exist: {}", NINA_WAVETABLES_DIR);
    }
    if (dirent) {
        ::free(dirent);
    }
    return folder_names;
}

//----------------------------------------------------------------------------
// _open_bank_folder
//----------------------------------------------------------------------------
bool GuiManager::_open_bank_folder(uint bank_index, std::string& full_path, std::string& folder_name)
{
    char bank_file_prefix[5];
    bool ret = false;

    // Format the bank and patch file prefixes
    std::sprintf(bank_file_prefix, "%03d_", bank_index);

    // Open the patches folder
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
                // Is this the specified bank folder?
                if (std::strncmp(dirent->d_name, bank_file_prefix, (sizeof(bank_file_prefix)-1)) == 0)
                {
                    // Bank folder found
                    full_path = NINA_PATCHES_DIR + std::string(dirent->d_name);
                    folder_name = dirent->d_name;
                    ret = true;
                    break;
                }
            }
        }
        ::closedir(dir);
    }
    else
    {
        // Patches folder does not exist - this is a critical error
        MSG("The patches folder does not exist: " << NINA_PATCHES_DIR);
        NINA_LOG_CRITICAL(module(), "The patches folder does not exist: {}", NINA_PATCHES_DIR);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _get_patch_filename
//----------------------------------------------------------------------------
bool GuiManager::_get_patch_filename(uint patch_index, std::string bank_folder_path, std::string& filename)
{
    bool ret = false;

    // Open the bank folder
    auto dir = ::opendir(bank_folder_path.c_str());
    if (dir != nullptr)
    {
        struct dirent *dirent;

        // Parse each directory, looking for the bank folder
        while ((dirent = ::readdir(dir)) != nullptr)
        {
            // Is this a normal file?
            if (dirent->d_type == DT_REG)
            {
                // Get the patch number from the filename
                // Note: If the filename format is invalid, atoi will return 0 - which is ok
                // as this is an invalid patch index
                uint num = std::atoi(dirent->d_name);

                // Are the first two characters the patch number?
                if ((num == patch_index) && (dirent->d_name[3] == '_'))
                {
                    // Set the patch name
                    auto name = std::string(dirent->d_name);
                    filename = name.substr(0, (name.size() - (sizeof(".json") - 1)));
                    ret = true;
                    break;
                }
            }
        }
        ::closedir(dir);

        // Patch found?
        if (!ret)
        {
            // The patch does not exist - in which case we can safely assume the default is loaded           
            filename = utils::get_default_patch_filename(patch_index, false);
            ret = true;
        }        
    }
    else
    {
        // The bank folder folder does not exist - show and log the error
        MSG("The patches bank folder does not exist: " << bank_folder_path);
        NINA_LOG_ERROR(module(), "The patche bank folder does not exist: {}", bank_folder_path);
    }
    return ret;    
}

//----------------------------------------------------------------------------
// _format_folder_name
//----------------------------------------------------------------------------
std::string GuiManager::_format_folder_name(const char *folder)
{
    // Return the formatted folder name
    auto name = std::string(folder);
    uint index = (name[0] == '0') ? 1 : 0;                        
    name = name.substr(index, (name.size() - index));
    _string_toupper(name);
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// _format_filename
//----------------------------------------------------------------------------
std::string GuiManager::_format_filename(const char *filename)
{
    // Return the formatted filename
    auto name = std::string(filename);
    uint index = (name[0] == '0') ? 1 : 0;                        
    name = name.substr(index, (name.size() - index));
    _string_toupper(name);
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// _get_edit_name_from_index
//----------------------------------------------------------------------------
std::string GuiManager::_get_edit_name_from_index(uint index)
{
    std::string name = "";
    auto num = _list_item_from_index(index).first;
    name = _list_items[num].substr(4, _list_items[num].size() - 4);
    if (name.size() > EDIT_NAME_STR_LEN) {
        name.resize(EDIT_NAME_STR_LEN);
    }
    return std::regex_replace(name, std::regex{"_"}, " ");
}

//----------------------------------------------------------------------------
// _index_from_list_items
//----------------------------------------------------------------------------
int GuiManager::_index_from_list_items(uint key)
{
    int index = 0;

    // Parse the list until the key is found
    for (const auto& item : _list_items) {
        if (item.first == key) {
            return index;
        }
        index++;
    }
    return -1;
}

//----------------------------------------------------------------------------
// _list_item_from_index
//----------------------------------------------------------------------------
std::pair<uint, std::string> GuiManager::_list_item_from_index(uint index)
{
    // Parse the list index times
    auto itr = _list_items.begin();
    while (index--) {
        itr++;
    }
    return *itr; 
}

//----------------------------------------------------------------------------
// _get_root_param_list_index
//----------------------------------------------------------------------------
int GuiManager::_get_root_param_list_index(const Param *param)
{
    return _get_param_list_index(_param_shown_root, param);
}

//----------------------------------------------------------------------------
// _get_param_list_index
//----------------------------------------------------------------------------
int GuiManager::_get_param_list_index(const Param *root_param, const Param *param)
{
    int ret = -1;

    // If the root param has actually been specified
    if (root_param) {
        // Check if the param in the root param list
        uint index = 0;
        for (const Param *p : root_param->get_param_list()) {
            if (p->get_path() == param->get_path()) {
                ret = index;
                break;
            }
            index++;
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _start_param_change_timer
//----------------------------------------------------------------------------
void GuiManager::_start_param_change_timer() 
{
    // No action for now - the timer is disabled
}

//----------------------------------------------------------------------------
// _stop_param_change_timer
//----------------------------------------------------------------------------
void GuiManager::_stop_param_change_timer() 
{
    // The timer is disabled, but stop it anyway just in case
    _param_changed_timer->stop();
}

//----------------------------------------------------------------------------
// _start_demo_mode_timer
//----------------------------------------------------------------------------
void GuiManager::_start_demo_mode_timer() 
{
    // If demo mode enabled and we are not in maintenance mode
    if (utils::system_config()->get_demo_mode() && !utils::maintenance_mode()) {
        // Stop and start the demo mode timer
        _demo_mode_timer->stop();
        if (utils::system_config()->get_demo_mode_timeout()) {
            _start_demo_mode_count = (utils::system_config()->get_demo_mode_timeout() * 1000000) / DEMO_MODE_TIMEOUT_SHORT;
            _demo_mode = false;
            _demo_mode_timer->start(DEMO_MODE_TIMEOUT_SHORT, std::bind(&GuiManager::_process_demo_mode_timeout, this));
        }
    }
}

//----------------------------------------------------------------------------
// _stop_demo_mode_timer
//----------------------------------------------------------------------------
void GuiManager::_stop_demo_mode_timer()
{
    // Stop the demo mode timer
    if (_demo_mode_timer->is_running()) {
        _demo_mode_timer->stop();
    }
}

//----------------------------------------------------------------------------
// _reset_gui_state_and_show_home_screen
//----------------------------------------------------------------------------
void GuiManager::_reset_gui_state_and_show_home_screen(SystemFuncType sys_func) 
{
    // Reset the GUI state and show the home screen
    _reset_gui_state(sys_func);
    _show_home_screen();
}

//----------------------------------------------------------------------------
// _reset_gui_state
//----------------------------------------------------------------------------
void GuiManager::_reset_gui_state(SystemFuncType sys_func) 
{
	// Get the GUI mutex
	std::lock_guard<std::mutex> guard(_gui_mutex);
    
    // Reset the relevant GUI variables
    _manage_patch_state = ManagePatchState::LOAD_PATCH;
    _select_patch_state = SelectPatchState::SELECT_PATCH;
    _save_patch_state = SavePatchState::PATCH_SELECT;
    _manage_layers_state = ManageLayersState::SETUP_LAYERS;
    _save_layers_state = SaveLayersState::LAYERS_SELECT;
    _edit_name_state = EditNameState::NONE;
    _sw_update_state = SwUpdateState::SW_UPDATE_STARTED;
    _bank_management_state = BankManagmentState::SHOW_LIST;
    _import_bank_state = ImportBankState::NONE; 
    _export_bank_state = ExportBankState::NONE;
    _clear_bank_state = ClearBankState::NONE;
    _wt_management_state = WtManagmentState::SHOW_LIST;
    _progress_state = ProgressState::NOT_STARTED;
    _param_change_available = false;
    _show_param_list = false;
    _param_shown_root = nullptr;
    _param_shown = nullptr;
    _filename_param = nullptr;
    _param_shown_index = -1;
    _editing_param = false;
    _showing_param_shortcut = false;
    _params_list.clear();    
    _num_list_items = 0;
    _list_items.clear();
    _edit_name.clear();
    _save_edit_name.clear();
    _selected_system_menu_item = 0;
    _selected_bank_management_item = 0;
    _selected_bank_archive = 0;
    _selected_bank_archive_name = "";
    _selected_bank_dest = 0;
    _selected_bank_dest_name = "";
    _selected_wt_management_item = 0;
    _show_full_system_menu = false;
    _run_msd_event_thread = false;
    _msd_event_thread = nullptr;
    _new_mod_matrix_param_list = false;
    _showing_reset_layers_screen = false;
    _showing_wt_prune_confirm_screen = false;
    _layers_load = false;
    _eg_2_level_mod_state = Eg2LevelModState::EG_2_LEVEL;
    _show_scope = true;
    _config_soft_button_2(false);

    // Reset the LFO state
    _reset_lfo_state();

    // If the sequencer is recording, stop it - unless the system function is KBD, in which
    // case we leave it in the record state
    if (sys_func != SystemFuncType::KBD) {
        _start_stop_seq_rec(false);
    }
}

//----------------------------------------------------------------------------
// _reset_lfo_state
//----------------------------------------------------------------------------
void GuiManager::_reset_lfo_state()
{
    // If we were processing mod matrix entries
    if (_selected_mod_matrix_src_index != -1) {
        // If the mod matrix source was not LFO 2
        auto lfo_state = utils::get_current_lfo_state();
        if (!_mod_maxtrix_src_is_lfo_2(_selected_mod_matrix_src_index)) {
            // Check if there is a mod matrix LFO 2 state we need to pop first
            if (lfo_state.mod_matrix && lfo_state.lfo_2) {
                // Pop the mod matrix LFO state
                _pop_controls_state(lfo_state.state);
                utils::pop_lfo_state();
            }
        }
        else {
            // Check if there is a mod matrix LFO 1 state we need to pop first
            if (lfo_state.mod_matrix && !lfo_state.lfo_2) {
                // Pop the mod matrix LFO state
                _pop_controls_state(lfo_state.state);
                utils::pop_lfo_state();
            }            
        }

        // Pop the mod matrix state (and LFO state if there is one)
        lfo_state = utils::get_current_lfo_state();
        _pop_back_controls_state();    
        if (lfo_state.mod_matrix) {
            utils::pop_lfo_state();
        }
        _selected_mod_matrix_src_index = -1;
    }

    // We now need to make sure the LFO is in the correct state
    auto lfo_state = utils::get_current_lfo_state();
    if (!utils::lfo_2_selected() && lfo_state.lfo_2) {
        // This means that we were showing LFO 2 but should now show LFO 1
        // Pop the LFO states
        while (lfo_state.lfo_2) {
            _pop_controls_state(lfo_state.state);
            utils::pop_lfo_state();
            lfo_state = utils::get_current_lfo_state();
        }
    }
    else if (utils::lfo_2_selected() && (!lfo_state.lfo_2)) {
        // This means that we were showing LFO 1 but should now show LFO 2
        _push_controls_state(LFO_2_STATE);
        utils::push_lfo_state(utils::LfoState(LFO_2_STATE, false, true));
        if (utils::lfo_2_sync_rate()) {
            utils::push_lfo_state(utils::LfoState(LFO_2_SYNC_RATE_STATE, false, true));
            _push_controls_state(LFO_2_SYNC_RATE_STATE);
        }   
    }
}

//----------------------------------------------------------------------------
// _can_show_param
//----------------------------------------------------------------------------
bool GuiManager::_can_show_param() 
{
    // We can show a param on the screen IF:
    // - We are in the home screen state OR
    // - We are in the param update state OR
    // - We are in the manage matrix state AND
    // - We are not showing the WT browser
    return (_gui_state <= GuiState::MOD_MATRIX_DST) && (_filename_param == nullptr);
}

//----------------------------------------------------------------------------
// _show_param_as_enum_list
//----------------------------------------------------------------------------
bool GuiManager::_show_param_as_enum_list(const Param *param)
{
    // If the param has a number of positions, and is not a switch
    // or purely numeric, then show this param as a list
    return (param->get_num_positions() || param->display_switch) && !param->numeric_enum_param;
}

//----------------------------------------------------------------------------
// _set_eg_2_level_dst_control
//----------------------------------------------------------------------------
void GuiManager::_set_eg_2_level_dst_control(const Param *dst_param, bool set_knob_pos)
{
    // Get the EG2 Level mod  dest system func param
    auto sys_func_param = utils::get_sys_func_param(SystemFuncType::EG_2_LEVEL_MOD_DST);
    if (dst_param && sys_func_param) {
        auto value = dst_param->get_value();

        // Set the system func command value
        sys_func_param->set_value(value);

        // Get the knob param (in the specified mod matrix state)
        auto knob_param = utils::get_param(KnobParam::ParamPath(utils::get_morph_knob_num()), _mod_matrix_states[_selected_mod_matrix_src_index]);
        if (knob_param) {
            // Update the knob param value
            knob_param->set_value(value);

            // Should we send a param change to set the knob position?
            if (set_knob_pos) {
                auto param_change = ParamChange(knob_param->get_path(), value, module());
                param_change.display = false;
                _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            }
        }
    }
}

//----------------------------------------------------------------------------
// _get_mod_matrix_eg_2_level_param
//----------------------------------------------------------------------------
Param *GuiManager::_get_mod_matrix_eg_2_level_param()
{
    // Note: Assume EG2 Level dest is always the second last in the param list
    return _param_shown->param_list[_param_shown->param_list.size() - 2];
}

//----------------------------------------------------------------------------
// _get_mod_matrix_morph_param
//----------------------------------------------------------------------------
Param *GuiManager::_get_mod_matrix_morph_param()
{
    // Note: Assume Morph dest is always the last in the param list
    return _param_shown->param_list[_param_shown->param_list.size() - 1];    
}

//----------------------------------------------------------------------------
// _is_mod_matrix_eg_2_src_param
//----------------------------------------------------------------------------
bool GuiManager::_is_mod_matrix_eg_2_src_param(const Param *param)
{
    // If this param is a Mod Matrix param with EG2 as its src
    return param->mod_matrix_param && (param->mod_src_name == MOD_MATRIX_EG_2_SRC_NAME);
}

//----------------------------------------------------------------------------
// _is_mod_matrix_eg_2_level_dst_param
//----------------------------------------------------------------------------
bool GuiManager::_is_mod_matrix_eg_2_level_dst_param(const Param *param)
{
    // Return if the param is the Mod Matrix EG2 Level dest
    return param == _get_mod_matrix_eg_2_level_param();
}

//----------------------------------------------------------------------------
// _is_mod_matrix_morph_dst_param
//----------------------------------------------------------------------------
bool GuiManager::_is_mod_matrix_morph_dst_param(const Param *param)
{
    // Return if the param is the Mod Matrix Morph dest
    return param == _get_mod_matrix_morph_param();
}

//----------------------------------------------------------------------------
// _mod_matrix_param_active
//----------------------------------------------------------------------------
bool GuiManager::_mod_matrix_param_active(const Param *param)
{
    // Return if this Mod Matrix param is active or not
    return (param->get_value() < 0.495) || (param->get_value() > 0.505);
}

//----------------------------------------------------------------------------
// _start_stop_seq_run
//----------------------------------------------------------------------------
void GuiManager::_start_stop_seq_run(bool start)
{
    // If the sequencer running state is changing
    if ((start && (_seq_ui_state != SeqUiState::SEQ_RUNNING)) || (!start && (_seq_ui_state == SeqUiState::SEQ_RUNNING))) {
        auto param = utils::get_param(NinaModule::SEQUENCER, SequencerParamId::RUN_PARAM_ID);
        if (param) {
            // Start/stop sequencer running
            param->set_value(start ? 1.0 : 0.0);
            auto param_change = ParamChange(param, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            if (!start) {
                _set_sys_func_switch(SystemFuncType::SEQ_RUN, false);
                if (utils::get_multifn_switches_mode() == MultifnSwitchesMode::NONE) {
                    _reset_multifn_switches();
                }
            }
            _seq_ui_state = start ? SeqUiState::SEQ_RUNNING : SeqUiState::SEQ_IDLE;
        }
    }
}

//----------------------------------------------------------------------------
// _start_stop_seq_rec
//----------------------------------------------------------------------------
void GuiManager::_start_stop_seq_rec(bool start)
{
    // If the sequencer recording state is changing
    if ((start && (_seq_ui_state != SeqUiState::SEQ_RECORDING)) || (!start && (_seq_ui_state == SeqUiState::SEQ_RECORDING))) {
        auto param = utils::get_param(NinaModule::SEQUENCER, SequencerParamId::REC_PARAM_ID);
        if (param) {
            // Start/stop sequencer recording
            param->set_value(start ? 1.0 : 0.0);
            auto param_change = ParamChange(param, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
            if (!start) {
                _set_sys_func_switch(SystemFuncType::SEQ_REC, false);
                if (utils::get_multifn_switches_mode() == MultifnSwitchesMode::NONE) {
                    _reset_multifn_switches();
                }
            }
            _seq_ui_state = start ? SeqUiState::SEQ_RECORDING : SeqUiState::SEQ_IDLE;
        }
    }
}

//----------------------------------------------------------------------------
// _enable_kbd
//----------------------------------------------------------------------------
void GuiManager::_enable_kbd(bool enable)
{
    // If the keyboard state is chaning
    if ((enable && !_kbd_enabled) || (!enable && _kbd_enabled)) {
        auto param = utils::get_param(NinaModule::KEYBOARD, KeyboardParamId::ENABLE_PARAM_ID);
        if (param) {
            // Configure the multi-function switches
            // Note: Do this BEFORE sending the enable param change
            if (enable) {
                // Configure the multi-function keys
                _config_multifn_switches(NUM_MULTIFN_SWITCHES, -1, MultifnSwitchesMode::KEYBOARD);
            }
            else {
                // Make sure the KBD switch is off
                _set_sys_func_switch(SystemFuncType::KBD, false);

                // Reset the multi-function keys
                if (utils::get_multifn_switches_mode() == MultifnSwitchesMode::KEYBOARD) {
                    if (_gui_state == GuiState::MANAGE_PATCH) {
                        // Set the multi-function switches to SINGLE SELECT mode
                        _config_multifn_switches(((_num_list_items < NUM_MULTIFN_SWITCHES) ? _num_list_items : NUM_MULTIFN_SWITCHES), 
                                                 ((_select_patch_state== SelectPatchState::SELECT_BANK) ? _selected_bank_index : 
                                                                                                          ((_manage_patch_state == ManagePatchState::LOAD_PATCH) ?
                                                                                                                (_selected_patch_index - 1) :
                                                                                                                _selected_patch_index)));
                    }
                    else if ((_gui_state == GuiState::MANAGE_LAYERS) && (_manage_layers_state == ManageLayersState::SETUP_LAYERS)) {
                        // Set the multi-function switches to SINGLE SELECT mode
                        _config_multifn_switches(NUM_LAYERS, utils::get_current_layer_info().layer_num());                       
                    }
                    else {
                        _reset_multifn_switches(true);
                    }
                }
            }

            // Enable/disable the keyboard
            param->set_value(enable ? 1.0 : 0.0);
            auto param_change = ParamChange(param, module());
            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));            
            _kbd_enabled = enable;
        }
    }
}

//----------------------------------------------------------------------------
// _strcpy_to_gui_msg
//----------------------------------------------------------------------------
inline void GuiManager::_strcpy_to_gui_msg(char *dest, const char *src)
{
    // Copy the passed string to the GUI message destination - truncating if necessary
    // Note: The max size is decreased by 1 to account for the NULL terminator
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    std::strncpy(dest, src, (STD_STR_LEN-1));
#pragma GCC diagnostic pop    
}

//----------------------------------------------------------------------------
// _char_is_charset_valid
//----------------------------------------------------------------------------
bool GuiManager::_char_is_charset_valid(char c)
{
    // Check if the char is valid for the character set
    return (c == ' ') || (c == '-') ||
           ((c >= '0') && (c <= '9')) ||
           ((c >= 'A') && (c <= 'Z'));
}

//----------------------------------------------------------------------------
// _char_to_charset_index
//----------------------------------------------------------------------------
uint GuiManager::_char_to_charset_index(char c)
{
    // Return the chararcter set index - assumes the passed char is valid for
    // the character set
    if (c == ' ')
        return 0;
    else if (c == '-')
        return NUM_CHARSET_CHARS - 1;
    else if (c >= 'A')
        return c - 'A' + 1;
    return c - '0' + (1 + 26);
}

//----------------------------------------------------------------------------
// _charset_index_to_char
//----------------------------------------------------------------------------
char GuiManager::_charset_index_to_char(uint index)
{
    // Return the chararcter set char from the passed index - assumes the
    // passed index is valid for the character set
    if (index == 0) {
        return ' ';
    }
    else if (index == (NUM_CHARSET_CHARS - 1)) {
        return '-';
    }
    else if (index < (1 + 26)) {
        return (char)(index - 1 + 'A');
    }
    else {
        return (char)(index - (1 + 26) + '0');
    }
}

//----------------------------------------------------------------------------
// _string_toupper
//----------------------------------------------------------------------------
void GuiManager::_string_toupper(std::string& str)
{
    // Transform the string to uppercase
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);  
}

//----------------------------------------------------------------------------
// _to_string
//----------------------------------------------------------------------------
std::string GuiManager::_to_string(int val, int width)
{
    // Convert the value to a string
    auto val_str = std::to_string(val);

    // If no width is specified just return the value as a string, otherwise
    // left-pad the string with zeros as required
    if (width == -1) {
        return val_str;
    }
    return std::string(width - std::min(width, (int)val_str.size()), '0') + val_str;
}

//----------------------------------------------------------------------------
// _process_msd_event
//----------------------------------------------------------------------------
static void *_process_msd_event(void* data)
{
    auto mgr = static_cast<GuiManager*>(data);
    mgr->process_msd_event();

    // To suppress warnings
    return nullptr;
}
