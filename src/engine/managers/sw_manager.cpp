/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  sw_manager.cpp
 * @brief Software Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <dirent.h>
#include <filesystem>
#include <glob.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <blkid/blkid.h>
#include "sw_manager.h"
#include "common.h"
#include "logger.h"
#include "utils.h"

// MACRO to get the full path of an MSD file
#define MSD_FILE_PATH(filename)     (MSD_MOUNT_DIR + std::string(filename))

// Constants
constexpr char CALIBRATE_SCRIPT[]                    = "calibrate.sh";
constexpr char FACTORY_CALIBRATE_SCRIPT[]            = "factory_calibrate.sh";
constexpr char DIAG_SCRIPT[]                         = "nina_diag.sh";
constexpr char BANK_IMPORT_MERGE_CHECK_SCRIPT[]      = "bank_import_merge_check.sh";
constexpr char BANK_IMPORT_MERGE_SCRIPT[]            = "bank_import_merge.sh";
constexpr char BANK_IMPORT_OVERWRITE_SCRIPT[]        = "bank_import_overwrite.sh";
constexpr char BANK_EXPORT_SCRIPT[]                  = "bank_export.sh";
constexpr char BANK_ADD_SCRIPT[]                     = "bank_add.sh";
constexpr char BANK_CLEAR_SCRIPT[]                   = "bank_clear.sh";
constexpr char WT_IMPORT_SCRIPT[]                    = "wt_import.sh";
constexpr char WT_EXPORT_SCRIPT[]                    = "wt_export.sh";
constexpr char WT_PRUNE_SCRIPT[]                     = "wt_prune.sh";
constexpr char BACKUP_SCRIPT[]                       = "backup.sh";
constexpr char RESTORE_BACKUP_SCRIPT[]               = "restore_backup.sh";
constexpr char WT_ARCHIVE_FILE[]                     = "nina_wavetables.zip";
constexpr char RESTORE_BACKUP_PATCHES_ARCHIVE_FILE[] = "patches_backup.zip";
constexpr char RESTORE_BACKUP_WT_ARCHIVE_FILE[]      = "wavetables_backup.zip";
constexpr char MIX_VCA_CAL_DONE_FILE[]               = "1_1_mix_cal.txt";
constexpr char BANK_ARCHIVE_FILE_PATTERN[]           = "[nN][iI][nN][aA]_[bB][aA][nN][kK]_?*.zip";
constexpr char MSD_MOUNT_DIR[]                       = "/media/";
constexpr char MSD_FILESYSTEM_TYPE[]                 = "vfat";
constexpr uint MSD_POLL_TIMEOUT                      = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(1)).count();
constexpr char FAT32_PART_TYPE[]                     = "VFAT";
constexpr char EFI_PART_TYPE_GUID[]                  = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B";

// Static functions
static void *_process_sw_update(void* data);
static void *_process_mix_vca_cal(void* data);
static void *_process_msd_event(void* data);
static int _msd_device_handler(sd_device_monitor *m, sd_device *device, void *userdata);

//----------------------------------------------------------------------------
// SwManager
//----------------------------------------------------------------------------
SwManager::SwManager(EventRouter *event_router) : 
    BaseManager(NinaModule::SOFTWARE, "SwManager", event_router)
{
    // Initialise class data
    _sw_update_thread = nullptr;
    _mix_vca_cal_thread = nullptr;
    _msd_event_thread = nullptr;
    _run_msd_event_thread = true;
    _msd_mounted = false;
}

//----------------------------------------------------------------------------
// ~SwManager
//----------------------------------------------------------------------------
SwManager::~SwManager()
{
    // Unmount any mounted drives
    // Note /boot is unmounted just in case an swupdate process is interrupted
    // with a signal like ctrl-c
    // If it is not mounted then the function does nothing
    umount_msd();
    ::umount("/boot");
}

//----------------------------------------------------------------------------
// run_calibration_script
//----------------------------------------------------------------------------
int SwManager::run_calibration_script(CalMode mode)
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(CALIBRATE_SCRIPT).c_str(), &statbuf) == 0) {
        // Script exists, run it
        std::string command = NINA_SCRIPTS_FILE_PATH(CALIBRATE_SCRIPT);
        switch (mode)
        {
            case CalMode::FILTER:
                command += " filter";;
                break;

            case CalMode::MIX_VCA:
                command += " mix-vca";
                break;

            default:
                break;
        }
        return _spawn_bash_script(command.c_str());
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(CALIBRATE_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(CALIBRATE_SCRIPT));
        return -1;
    }
}

//----------------------------------------------------------------------------
// run_factory_calibration_script
//----------------------------------------------------------------------------
int SwManager::run_factory_calibration_script()
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(FACTORY_CALIBRATE_SCRIPT).c_str(), &statbuf) == 0) {
        // Script exists, run it
        return _spawn_bash_script(NINA_SCRIPTS_FILE_PATH(FACTORY_CALIBRATE_SCRIPT).c_str());
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(FACTORY_CALIBRATE_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(FACTORY_CALIBRATE_SCRIPT));
        return -1;
    }
}

//----------------------------------------------------------------------------
// diag_script_present
//----------------------------------------------------------------------------
bool SwManager::diag_script_present()
{
    return _msd_mounted && std::filesystem::exists(MSD_FILE_PATH(DIAG_SCRIPT));
}

//----------------------------------------------------------------------------
// run_diag_script
//----------------------------------------------------------------------------
int SwManager::run_diag_script()
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat(MSD_FILE_PATH(DIAG_SCRIPT).c_str(), &statbuf) == 0) {
        // Script exists, run it
        auto cmd_line = MSD_FILE_PATH(DIAG_SCRIPT) + " > " + MSD_FILE_PATH("nina_diag_log.txt");
        return _spawn_bash_script(cmd_line.c_str());
    }
    else {
        // Log the error
        MSG("The script does not exist: " << MSD_FILE_PATH(DIAG_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", MSD_FILE_PATH(DIAG_SCRIPT));
        return -1;
    }
}

//----------------------------------------------------------------------------
// bank_archive_present
//----------------------------------------------------------------------------
bool SwManager::bank_archive_present()
{
    // If the MSD is mounted
    if (_msd_mounted) {
        // Check for any bank archives that are present (using good old glob)
        glob_t glob_res;
        std::memset(&glob_res, 0, sizeof(glob_res)); 
        if (::glob(MSD_FILE_PATH(BANK_ARCHIVE_FILE_PATTERN).c_str(), 0, NULL, &glob_res) == 0) {
            ::globfree(&glob_res);
            return true;
        }           
    }
    return false;
}

//----------------------------------------------------------------------------
// get_bank_archives
//----------------------------------------------------------------------------
std::vector<std::string> SwManager::get_bank_archives()
{
    std::vector<std::string> banks;

    // Get the bank archives present (using good old glob)
    glob_t glob_res;
    std::memset(&glob_res, 0, sizeof(glob_res)); 
    if (::glob(MSD_FILE_PATH(BANK_ARCHIVE_FILE_PATTERN).c_str(), 0, NULL, &glob_res) == 0) {
        // Process each archive
        for (uint i=0; i<glob_res.gl_pathc; i++) {
            // Add the archive name without the mount point path
            std::string filename = glob_res.gl_pathv[i];
            filename = filename.substr((sizeof(MSD_MOUNT_DIR) - 1), (filename.size() - (sizeof(MSD_MOUNT_DIR) - 1)));
            banks.push_back(filename);
        }
        ::globfree(&glob_res);
    }
    return banks; 
}

//----------------------------------------------------------------------------
// run_bank_import_merge_check_script
//----------------------------------------------------------------------------
int SwManager::run_bank_import_merge_check_script(const char *archive, const char *dest_bank)
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(BANK_IMPORT_MERGE_CHECK_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the bank import merge check script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(BANK_IMPORT_MERGE_CHECK_SCRIPT) + " " + archive + " " + dest_bank + " > /tmp/bank_import_merge_check_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(BANK_IMPORT_MERGE_CHECK_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(BANK_IMPORT_MERGE_CHECK_SCRIPT));
        return -1;
    }    
}

//----------------------------------------------------------------------------
// run_bank_import_script
//----------------------------------------------------------------------------
int SwManager::run_bank_import_script(const char *archive, const char *dest_bank, bool merge)
{
    auto script = (merge ? NINA_SCRIPTS_FILE_PATH(BANK_IMPORT_MERGE_SCRIPT) : NINA_SCRIPTS_FILE_PATH(BANK_IMPORT_OVERWRITE_SCRIPT));

    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (script.c_str(), &statbuf) == 0) {
        // Run the bank import script
        auto cmd_line = script + " " + archive + " " + dest_bank + " > /tmp/bank_import_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << script);
        NINA_LOG_ERROR(module(), "The script does not exist: {}", script);
        return -1;
    }  
}

//----------------------------------------------------------------------------
// run_bank_export_script
//----------------------------------------------------------------------------
int SwManager::run_bank_export_script(const char *bank)
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(BANK_EXPORT_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the bank export script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(BANK_EXPORT_SCRIPT) + " " + bank + " > /tmp/bank_export_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(BANK_EXPORT_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(BANK_EXPORT_SCRIPT));
        return -1;
    }     
}

//----------------------------------------------------------------------------
// run_bank_add_script
//----------------------------------------------------------------------------
int SwManager::run_bank_add_script()
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(BANK_ADD_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the bank add script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(BANK_ADD_SCRIPT) + " > /tmp/bank_add_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(BANK_ADD_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(BANK_ADD_SCRIPT));
        return -1;
    }    
}

//----------------------------------------------------------------------------
// run_bank_clear_script
//----------------------------------------------------------------------------
int SwManager::run_bank_clear_script(const char *bank)
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(BANK_CLEAR_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the bank clear script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(BANK_CLEAR_SCRIPT) + " " + bank + " > /tmp/bank_clear_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(BANK_CLEAR_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(BANK_CLEAR_SCRIPT));
        return -1;
    } 
}

//----------------------------------------------------------------------------
// wt_archive_present
//----------------------------------------------------------------------------
bool SwManager::wt_archive_present()
{
    return _msd_mounted && std::filesystem::exists(MSD_FILE_PATH(WT_ARCHIVE_FILE));
}

//----------------------------------------------------------------------------
// run_wt_import_script
//----------------------------------------------------------------------------
int SwManager::run_wt_import_script()
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(WT_IMPORT_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the wavetable import script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(WT_IMPORT_SCRIPT) + " > /tmp/wt_import_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(WT_IMPORT_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(WT_IMPORT_SCRIPT));
        return -1;
    }
}

//----------------------------------------------------------------------------
// run_wt_export_script
//----------------------------------------------------------------------------
int SwManager::run_wt_export_script()
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(WT_EXPORT_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the wavetable export script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(WT_EXPORT_SCRIPT) + " > /tmp/wt_export_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(WT_EXPORT_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(WT_EXPORT_SCRIPT));
        return -1;
    }    
}

//----------------------------------------------------------------------------
// run_wt_prune_script
//----------------------------------------------------------------------------
int SwManager::run_wt_prune_script()
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(WT_PRUNE_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the wavetable prune script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(WT_PRUNE_SCRIPT) + " > /tmp/wt_prune_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(WT_PRUNE_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(WT_PRUNE_SCRIPT));
        return -1;
    }  
}

//----------------------------------------------------------------------------
// run_backup_script
//----------------------------------------------------------------------------
bool SwManager::restore_backup_archives_present()
{
    // If the MSD is mounted
    if (_msd_mounted) {
        // Get the pi serial number and create the calibration archive filename
        char cal_archive_filename[50];
        std::memset(cal_archive_filename, 0, sizeof(cal_archive_filename));
        std::string serial_num = "unknown";
        std::ifstream serial_num_file("/sys/firmware/devicetree/base/serial-number");
        std::getline(serial_num_file, serial_num);
        std::sprintf(cal_archive_filename, "calibration_%s.zip", serial_num.c_str());

        // Check if any of the restore backup files are present
        if (std::filesystem::exists(MSD_FILE_PATH(RESTORE_BACKUP_PATCHES_ARCHIVE_FILE)) ||
            std::filesystem::exists(MSD_FILE_PATH(RESTORE_BACKUP_WT_ARCHIVE_FILE)) ||
            std::filesystem::exists(MSD_FILE_PATH(cal_archive_filename))) {
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// run_backup_script
//----------------------------------------------------------------------------
int SwManager::run_backup_script()
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(BACKUP_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the backup script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(BACKUP_SCRIPT) + " > /tmp/backup_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(BACKUP_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(BACKUP_SCRIPT));
        return -1;
    }
}

//----------------------------------------------------------------------------
// run_restore_backup_script
//----------------------------------------------------------------------------
int SwManager::run_restore_backup_script()
{
    // Firstly check if the script exists
    struct stat statbuf;   
    if (::stat (NINA_SCRIPTS_FILE_PATH(RESTORE_BACKUP_SCRIPT).c_str(), &statbuf) == 0) {
        // Run the retore backup script
        auto cmd_line = NINA_SCRIPTS_FILE_PATH(RESTORE_BACKUP_SCRIPT) + " > /tmp/restore_backup_log.txt";
        return _spawn_bash_script(cmd_line.c_str());       
    }
    else {
        // Log the error
        MSG("The script does not exist: " << NINA_SCRIPTS_FILE_PATH(RESTORE_BACKUP_SCRIPT));
        NINA_LOG_ERROR(module(), "The script does not exist: {}", NINA_SCRIPTS_FILE_PATH(RESTORE_BACKUP_SCRIPT));
        return -1;
    }
}

//----------------------------------------------------------------------------
// msd_mounted
//----------------------------------------------------------------------------
bool SwManager::msd_mounted()
{
    return _msd_mounted;
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool SwManager::start()
{
    struct sd_device_enumerator* enumerator;

    // Firstly check for an MSD alreaady plugged in
    // Setup the device enumerator
    ::sd_device_enumerator_new(&enumerator);
    ::sd_device_enumerator_add_match_subsystem(enumerator, "block", true);
    ::sd_device_enumerator_add_match_sysname(enumerator, "sd*");

    // Check each device (if any)
    auto msd = ::sd_device_enumerator_get_device_first(enumerator);
    while (msd) {
        const char *dev_type;

        // Only process partition devices
        ::sd_device_get_devtype(msd, &dev_type);
        if (std::strcmp(dev_type, "partition") == 0) {
            // Check if this partition is valid to use
            const char *dev_name = is_partition_valid(msd);
            if (dev_name) {
                // Mount the partition
                mount_msd(dev_name);
                if (_msd_mounted) {
                    // Spawn a bash script to get the latest SWU update on the media, if any
                    // This creates a text file in /tmp that can be parsed
                    if (_spawn_bash_script("get-latest-swu /media > /tmp/swu_version") == 0) {
                        // Get the update software version, if any)
                        _update_sw_ver = "";
                        _update_sw_file = "";
                        _update_partition = "";
                        std::ifstream update_sw_ver_file("/tmp/swu_version");
                        std::getline (update_sw_ver_file, _update_sw_ver);
                        std::getline (update_sw_ver_file, _update_sw_file);
                        std::getline (update_sw_ver_file, _update_partition);
                        if ((_update_sw_ver.size() > 0) && (_update_sw_file.size() > 0) && (_update_partition.size() > 0)) {
                            // Software update found - remove the temporary file
                            ::remove("/tmp/swu_version");

                            // Enter maintenance mode
                            utils::set_maintenance_mode();

                            // Create a thread to perform the SW update
                            _sw_update_thread = new std::thread(_process_sw_update, this); 
                        }                  
                    }
                    break;                                
                }
            }
        }
        msd = ::sd_device_enumerator_get_device_next(enumerator);
    }
    ::sd_device_enumerator_unref(enumerator);

    // Create a normal thread to listen for MSD events
    _msd_event_thread = new std::thread(_process_msd_event, this);

    // If we are not performing a software update, check if VCA Mix calibration needs to be done
    if (_sw_update_thread == nullptr) {
        // Does the Mix VCA calibration file not exist?
        if (!std::filesystem::exists(NINA_CALIBRATION_FILE(MIX_VCA_CAL_DONE_FILE))) {
            // Enter maintenance mode
            utils::set_maintenance_mode();

            // Create a thread to perform Mix VCA calibration
            _mix_vca_cal_thread = new std::thread(_process_mix_vca_cal, this);                                        
        }        
    }

    // Call the base manager
    return BaseManager::start();		
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void SwManager::stop()
{
    // Call the base manager
    BaseManager::stop();

    // SW update task running?
    if (_sw_update_thread != 0)
    {
        // Stop the SW update task
		if (_sw_update_thread->joinable())
			_sw_update_thread->join(); 
        _sw_update_thread = 0;       
    }

    // Mix VCA calibration task running?
    if (_mix_vca_cal_thread != 0)
    {
        // Stop the Mix VCA calibration task
		if (_mix_vca_cal_thread->joinable())
			_mix_vca_cal_thread->join(); 
        _mix_vca_cal_thread = 0;       
    }

    // MSD event task running?
    if (_msd_event_thread != 0)
    {
        // Stop the MSD event task
        _run_msd_event_thread = false;
		if (_msd_event_thread->joinable())
			_msd_event_thread->join(); 
        _run_msd_event_thread = 0;       
    }
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void SwManager::process()
{
    // Note: This class currently has no listeners. Add here when needed

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_sw_update
//----------------------------------------------------------------------------
void SwManager::process_sw_update()
{
    // Send an event to the GUI manager to show an in progress screen
    auto system_func = SystemFunc();
    system_func.type = SystemFuncType::START_SW_UPDATE;
    system_func.from_module = NinaModule::SOFTWARE;
    system_func.str_value = _update_sw_ver;
    _event_router->post_system_func_event(new SystemFuncEvent(system_func));

    // Start the software update process automatically
    MSG("Updating Software to: v" << _update_sw_ver << ", " << _update_sw_file);
    NINA_LOG_INFO(module(), "Updating Software to: v{}, {}", _update_sw_ver, _update_sw_file);
    auto cmd_line = "swupdate -v -P 'mount /dev/mmcblk0p1 /boot' -p 'umount /boot' -e stable," + _update_partition + " -i " + _update_sw_file + " >> /tmp/swupdate_log.txt";
    auto ret = _spawn_bash_script(cmd_line.c_str());

    // Remove the SWU file from the MSD
    ::remove(_update_sw_file.c_str());

    // Send an event to the GUI manager to show the software update
    // is complete
    MSG("Software update: " << ((ret == 0) ? "SUCCEEDED" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Software Update: SUCCEEDED");
    }
    else {
        NINA_LOG_ERROR(module(), "Software Update: FAILED");
    }
    system_func = SystemFunc();
    system_func.type = SystemFuncType::FINISH_SW_UPDATE;
    system_func.from_module = NinaModule::SOFTWARE;
    system_func.str_value = _update_sw_ver;
    system_func.result = (ret == 0);
    _event_router->post_system_func_event(new SystemFuncEvent(system_func));
}

//----------------------------------------------------------------------------
// process_mix_vca_cal
//----------------------------------------------------------------------------
void SwManager::process_mix_vca_cal()
{
    // Delay so that the GUI manager is ready to receive messages
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send an event to the GUI manager to show an in progress screen
    auto system_func = SystemFunc();
    system_func.type = SystemFuncType::START_CALIBRATION;
    system_func.from_module = NinaModule::SOFTWARE;
    _event_router->post_system_func_event(new SystemFuncEvent(system_func));

    // Start the Mix VCA calibration manually
    auto ret = run_calibration_script(CalMode::MIX_VCA);

    // Send an event to the GUI manager to show the Mix VCA calibration
    // is complete
    MSG("Mix VCA calibration script: " << ((ret == 0) ? "COMPLETE" : "FAILED"));
    if (ret == 0) {
        NINA_LOG_INFO(module(), "Mix VCA calibration script: COMPLETE");
    }
    else {
        NINA_LOG_ERROR(module(), "Mix VCA calibration script: FAILED");
    }
    system_func = SystemFunc();
    system_func.type = SystemFuncType::FINISH_CALIBRATION;
    system_func.from_module = NinaModule::SOFTWARE;
    system_func.result = (ret == 0);
    _event_router->post_system_func_event(new SystemFuncEvent(system_func));
}

//----------------------------------------------------------------------------
// process_msd_event
//----------------------------------------------------------------------------
void SwManager::process_msd_event()
{
    sd_device_monitor *monitor = nullptr;
    sd_event *event = nullptr;
    int res;

    // Setup the MSD event monitor
    res = ::sd_event_new(&event);
    if (res < 0) {
        // Event create failed
        DEBUG_BASEMGR_MSG("Could not create the MSD event: " << errno);
        return;
    }
    res = ::sd_device_monitor_new(&monitor);
    if (res < 0) {
        // Monitor create failed
        DEBUG_BASEMGR_MSG("Could not create the MSD monitor: " << errno);
        return;
    }
    res = ::sd_device_monitor_filter_add_match_subsystem_devtype(monitor, "block", "partition");
    if (res < 0) {
        // Add monitor filter failed
        DEBUG_BASEMGR_MSG("Could not add the MSD monitor filter: " << errno);
        return;
    }
    res = ::sd_device_monitor_attach_event(monitor, event);
    if (res < 0) {
        // Attach monitor event failed
        DEBUG_BASEMGR_MSG("Could not attach the MSD monitor event: " << errno);
        return;
    }
    res = ::sd_device_monitor_start(monitor, _msd_device_handler, this);
    if (res < 0) {
        // Monitor start failed
        DEBUG_BASEMGR_MSG("Could not start the MSD monitor: " << errno);
        return;
    }

    // Run the monitor event thread
    while (_run_msd_event_thread) {
        ::sd_event_run(event, MSD_POLL_TIMEOUT);
    }
    ::sd_device_monitor_unref(monitor);
    ::sd_event_unref(event);
}

//----------------------------------------------------------------------------
// is_partition_valid
//----------------------------------------------------------------------------
const char *SwManager::is_partition_valid(sd_device *device)
{
    const char *dev_name = nullptr;
    const char *part_type = nullptr;
    const char *part_type_guid = nullptr;

    // Get the device name
    ::sd_device_get_devname(device, &dev_name);

    // Get the partition type
    auto pr = ::blkid_new_probe_from_filename(dev_name);
    if (pr) {
        ::blkid_probe_enable_partitions(pr, 1);
        ::blkid_probe_set_partitions_flags(pr, BLKID_PARTS_ENTRY_DETAILS);
        ::blkid_do_safeprobe(pr);
        ::blkid_probe_lookup_value(pr, "TYPE", &part_type, NULL);
        if (part_type) {
            // Check this is a FAT32 partition
            std::string str(part_type);
            std::transform(str.begin(), str.end(), str.begin(), ::toupper);
            if (str == FAT32_PART_TYPE) {
                // We also need to make sure this is NOT an EFI partition
                // Get the partition type GUID
                ::blkid_probe_lookup_value(pr, "PART_ENTRY_TYPE", &part_type_guid, NULL);
                if (part_type_guid) {
                    std::string str(part_type_guid);
                    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
                    if (str == EFI_PART_TYPE_GUID) {
                        // It's an EFI partition so just return here with a NULL pointer
                        // to indicate it's not a valid partition
                        ::blkid_free_probe(pr);
                        return nullptr;
                    }             
                }

                // The partition is valid for mounting
                ::blkid_free_probe(pr);
                return dev_name;
            }
        }
        ::blkid_free_probe(pr);
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// mount_msd
//----------------------------------------------------------------------------
void SwManager::mount_msd(const char *dev_name)
{
    // If an MSD is not already mounted
    if (!_msd_mounted) {
        // Mount the passed device
        int ret = ::mount(dev_name, MSD_MOUNT_DIR, MSD_FILESYSTEM_TYPE, 0, "");
        if ((ret == 0) || (errno == EBUSY)) {
            // Drive mounted
            _msd_mounted = true;
            MSG("Mass storage device mounted: " << dev_name);
        }
        else {
            DEBUG_BASEMGR_MSG("Mass storage device mount error: " << errno);
        }
    }
    else {
        // An MSD is already mounted
        DEBUG_BASEMGR_MSG("Could not mount mass storage device " << dev_name << ", another is already mounted");        
    }
}

//----------------------------------------------------------------------------
// umount_msd
//----------------------------------------------------------------------------
void SwManager::umount_msd()
{
    // Unmount any mounted drive
    if (_msd_mounted) {
        ::umount(MSD_MOUNT_DIR);
        _msd_mounted = false;
        MSG("Mass storage device unmounted");
    }
}

//----------------------------------------------------------------------------
// _spawn_bash_script
//----------------------------------------------------------------------------
int SwManager::_spawn_bash_script(const char *cmd_line)
{
    // Fork this process
    pid_t pid = fork();
    if (pid == 0) {
        // Spawn the bash script
        const char *argv[] = { "bash", "-c", cmd_line, NULL };
        ::execve("/bin/bash", const_cast<char* const *>(argv), NULL);
        _exit(1);
    }
    else
    {
        // Wait for the bash script to complete
        int status;
        ::waitpid(pid, &status, 0);

        // If the script exited normally, return the exit status
        if (WIFEXITED(status)) {
            status = WEXITSTATUS(status);
        }
        return status;
    }
}

//----------------------------------------------------------------------------
// _spawn_python_script
//----------------------------------------------------------------------------
int SwManager::_spawn_python_script(const char *cmd_line)
{
    pid_t pid = fork();
    if(pid == 0)
    {
        const char *argv[] = { "python3", cmd_line, NULL };
        ::execve("/usr/bin/python3", const_cast<char* const *>(argv), NULL);
        _exit(1);
    }
    else
    {
        int status;
        ::waitpid(pid, &status, 0);
        return status;
    }
}

//----------------------------------------------------------------------------
// _process_sw_update
//----------------------------------------------------------------------------
static void *_process_sw_update(void* data)
{
    auto mgr = static_cast<SwManager*>(data);
    mgr->process_sw_update();

    // To suppress warnings
    return nullptr;
}

//----------------------------------------------------------------------------
// _process_mix_vca_cal
//----------------------------------------------------------------------------
static void *_process_mix_vca_cal(void* data)
{
    auto mgr = static_cast<SwManager*>(data);
    mgr->process_mix_vca_cal();

    // To suppress warnings
    return nullptr;
}

//----------------------------------------------------------------------------
// _process_msd_event
//----------------------------------------------------------------------------
static void *_process_msd_event(void* data)
{
    auto mgr = static_cast<SwManager*>(data);
    mgr->process_msd_event();

    // To suppress warnings
    return nullptr;
}

//----------------------------------------------------------------------------
// _msd_device_handler
//----------------------------------------------------------------------------
static int _msd_device_handler(sd_device_monitor *monitor, sd_device *device, void *userdata)
{
    auto mgr = static_cast<SwManager*>(userdata);
    const char *action;

    // Get the action performed on this device
    (void)monitor;
    if (::sd_device_get_property_value(device, "ACTION", &action) == 0) {
        // Device added?
        if (std::strcmp(action, "add") == 0) {
            // Check if this partition is valid to use
            const char *dev_name = mgr->is_partition_valid(device);
            if (dev_name) {
                // Mount this device
                mgr->mount_msd(dev_name);
            }
        }
        // Device removed?
        else if (std::strcmp(action, "remove") == 0) {
            // Unmount the device
            mgr->umount_msd();
        }
    }   
    return 0;
}
