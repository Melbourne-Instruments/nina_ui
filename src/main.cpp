/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  main.cpp
 * @brief Main entry point to the Nina UI.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <csignal>
#include <unistd.h>
#include "event_router.h"
#include "daw_manager.h"
#include "midi_device_manager.h"
#include "sequencer_manager.h"
#include "arpeggiator_manager.h"
#include "keyboard_manager.h"
#include "osc_manager.h"
#include "file_manager.h"
#include "surface_control_manager.h"
#include "analog_input_control_manager.h"
#include "gui_manager.h"
#include "sw_manager.h"
#include "utils.h"
#include "system_func.h"
#include "logger.h"
#include "version.h"

// Constants
constexpr char PID_FILENAME[] = "/var/run/nina_app.pid";

// Global variables
bool exit_flag = false;
bool exit_condition() {return exit_flag;}
std::condition_variable exit_notifier;

// Local functions
void _print_nina_ui_info();
bool _check_pid();
void _sigint_handler([[maybe_unused]] int sig);

//----------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------
int main(void)
{
    // Setup the exit signal handler (e.g. ctrl-c, kill)
    signal(SIGINT, _sigint_handler);
    signal(SIGTERM, _sigint_handler);

    // Ignore broken pipe signals, handle in the app instead
    signal(SIGPIPE, SIG_IGN);

    // Show the app info
    _print_nina_ui_info();

    // Check the PID and don't run the app if its already running
    if (_check_pid())
    {
        // Generate the session UUID
        // This just needs to be done once on startup
        utils::generate_session_uuid();
        
        // Start the logger
        Logger::Start();

        // Create the Event Router
        auto event_router = std::make_unique<EventRouter>();

        // Create the manager threads
        // Note 1: During creation, any associated params are registered
        // Note 2: The file manager must be created first so that it can create the params
        // blacklist (if any)
        auto file_manager = std::make_unique<FileManager>(event_router.get());
        auto surface_control_manager = std::make_unique<SurfaceControlManager>(event_router.get());
        auto analog_input_control_manager = std::make_unique<AnalogInputControlManager>(event_router.get());
        auto gui_manager = std::make_unique<GuiManager>(event_router.get());
        auto daw_manager = std::make_unique<DawManager>(event_router.get());
        auto midi_device_manager = std::make_unique<MidiDeviceManager>(event_router.get());
        auto sequencer_manager = std::make_unique<SequencerManager>(event_router.get());
        auto arpeggiator_manager = std::make_unique<ArpeggiatorManager>(event_router.get());
        auto keyboard_manager = std::make_unique<KeyboardManager>(event_router.get());
        auto osc_manager = std::make_unique<OscManager>(event_router.get());
        auto sw_manager = std::make_unique<SwManager>(event_router.get());

        // Register the DAW, MIDI, SEQ, ARP, and SOFTWARE managers
        // We do this so that they can be used for direct access when needed
        utils::register_manager(NinaModule::DAW, daw_manager.get());
        utils::register_manager(NinaModule::MIDI_DEVICE, midi_device_manager.get());
        utils::register_manager(NinaModule::SEQUENCER, sequencer_manager.get());
        utils::register_manager(NinaModule::ARPEGGIATOR, arpeggiator_manager.get());
        utils::register_manager(NinaModule::SOFTWARE, sw_manager.get());

        // Register the Common params
        utils::register_common_params();
        
        // Register the System Function params
        SystemFunc::RegisterParams();

        // Start the managers
        // Note: The file manager must be started first so that it can initialise the
        // params from the preset files, and map from the map file
        if (file_manager->start())
        {
            // Start the GUI manager, this is always used no matter the mode
            gui_manager->start();

            // Start the software manager - if this module finds a software
            // update, it will put the app into maintenance mode
            sw_manager->start();
            if (!utils::maintenance_mode())
            {
                // Start the other managers
                analog_input_control_manager->start();
                midi_device_manager->start();
                daw_manager->start();
                sequencer_manager->start();
                arpeggiator_manager->start();
                keyboard_manager->start();
                osc_manager->start();
                surface_control_manager->start();
                
                // Wait forever for an exit signal
                std::mutex m;
                std::unique_lock<std::mutex> lock(m);
                exit_notifier.wait(lock, exit_condition);

                // Clean up the managers
                analog_input_control_manager->stop();
                file_manager->stop();
                gui_manager->stop();
                daw_manager->stop();
                midi_device_manager->stop();
                sequencer_manager->stop();
                arpeggiator_manager->stop();
                keyboard_manager->start();
                osc_manager->stop();
                sw_manager->stop();
                surface_control_manager->stop();
            }
            else
            {
                // Maintence mode - a software update is available and in progress
                // Start the minimum managers to process the software update
                analog_input_control_manager->start();
                surface_control_manager->start();

                // Wait forever for an exit signal
                std::mutex m;
                std::unique_lock<std::mutex> lock(m);
                exit_notifier.wait(lock, exit_condition);

                // Clean up the managers
                file_manager->stop();
                gui_manager->stop();
                surface_control_manager->stop();
                analog_input_control_manager->stop();
                sw_manager->stop();     
            }
        }          
        else
        {
            // The file manager is a critical component to start the UI
            MSG("\nNINA UI could not be started");
        }

        // Stop the logger
        Logger::Stop();
    }

    // NINA UI has exited
    MSG("NINA UI exited");
    return 0;
}

//----------------------------------------------------------------------------
// _print_nina_ui_info
//----------------------------------------------------------------------------
void _print_nina_ui_info()
{
    MSG("NINA UI - Copyright (c) 2023-2024 Melbourne Instruments, Australia");
#ifdef NINA_UI_BETA_RELEASE    
    MSG("Version " << NINA_UI_MAJOR_VERSION << 
        "." << NINA_UI_MINOR_VERSION << 
        "." << NINA_UI_PATCH_VERSION <<
        "-beta " <<
        "(" << NINA_UI_GIT_COMMIT_HASH << ")");    
#else
    MSG("Version " << NINA_UI_MAJOR_VERSION << 
        "." << NINA_UI_MINOR_VERSION << 
        "." << NINA_UI_PATCH_VERSION << 
        " (" << NINA_UI_GIT_COMMIT_HASH << ")");
#endif
#ifdef NO_XENOMAI
    MSG("App build with NO_XENOMAI");
#endif
    MSG("");
}

//----------------------------------------------------------------------------
// _check_pid
//----------------------------------------------------------------------------
bool _check_pid()
{
    FILE *fp;

    // Get the program PID
    auto pid = ::getpid();

    // Try to open the PID file
    fp = std::fopen(PID_FILENAME, "r");
    if (fp != nullptr)
    {
        // The file exits, read the specified PID
        pid_t current_pid;
        if (std::fread(&current_pid, sizeof(pid), 1, fp) == 1)
        {
            // Check if a program is running with that PID
            if (::kill(current_pid, 0) == 0)
            {
                // The NINA UI app is already running
                MSG("App is already running");
                return false;
            }
        }
        std::fclose(fp);

        // Open the PID file for writing
        fp = fopen(PID_FILENAME, "w");
    }
    else
    {
        // File doesn't exist, create it
        fp = fopen(PID_FILENAME, "a");
    }

    // File open to write the PID ok?
    if (fp != nullptr)
    {
        // Write the PID
        std::fwrite(&pid, sizeof(pid), 1, fp);
        std::fclose(fp);
    }
    return true;
}

//----------------------------------------------------------------------------
// _sigint_handler
//----------------------------------------------------------------------------
void _sigint_handler([[maybe_unused]] int sig)
{
    // Signal to exit the app
    exit_flag = true;
    exit_notifier.notify_one();
}
