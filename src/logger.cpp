/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2022 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  logger.cpp
 * @brief Logger implementation.
 *-----------------------------------------------------------------------------
 */
#include "common.h"
#include "logger.h"
#include "version.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/async.h"

// Constants
constexpr char LOGGER_NAME[]       = "Nina";
constexpr char LOGGER_FILENAME[]   = "/udata/nina/nina.log";
constexpr int LOGGER_MAX_FILE_SIZE = 1048576 * 10;          // 10MB

// Static variables
std::shared_ptr<spdlog::logger> Logger::_logger{nullptr};

//----------------------------------------------------------------------------
// Start
//----------------------------------------------------------------------------
void Logger::Start() 
{
    // Setup the spd file logger
    spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] %v");
    spdlog::set_level(spdlog::level::info);
    _logger = spdlog::rotating_logger_mt<spdlog::async_factory>(LOGGER_NAME,
                                                                LOGGER_FILENAME,
                                                                LOGGER_MAX_FILE_SIZE,
                                                                1);

    // Show the start log header
    _logger->flush_on(spdlog::level::err);
    _logger->info("--------------------");
    _logger->info("Nina UI Log: Started");
    _logger->info("--------------------");    
#ifdef NINA_UI_BETA_RELEASE    
    _logger->info("Version: {}.{}.{}-beta ({})", NINA_UI_MAJOR_VERSION, NINA_UI_MINOR_VERSION, NINA_UI_PATCH_VERSION, NINA_UI_GIT_COMMIT_HASH);
#else
    _logger->info("Version: {}.{}.{} ({})", NINA_UI_MAJOR_VERSION, NINA_UI_MINOR_VERSION, NINA_UI_PATCH_VERSION, NINA_UI_GIT_COMMIT_HASH);
#endif
}

//----------------------------------------------------------------------------
// Stop
//----------------------------------------------------------------------------
void Logger::Stop() 
{
    // Show the stop log header
    _logger->info("--------------------");
    _logger->info("Nina UI Log: Stopped");
    _logger->info("--------------------");
    _logger->flush();
}

//----------------------------------------------------------------------------
// Flush
//----------------------------------------------------------------------------
void Logger::Flush() 
{
    // Flush the log
    _logger->flush();
}

//----------------------------------------------------------------------------
// module_name
//----------------------------------------------------------------------------
std::string Logger::module_name(NinaModule module)
{
    // Parse the module
    switch(module)
    {
        case NinaModule::ARPEGGIATOR:
            return "[arp]";

        case NinaModule::DAW:
            return "[daw]";

        case NinaModule::FILE_MANAGER:
            return "[fmg]";

        case NinaModule::MIDI_DEVICE:
            return "[midi]";

        case NinaModule::OSC:
            return "[osc]";
        
        case NinaModule::SEQUENCER:
            return "[seq]";

        case NinaModule::SURFACE_CONTROL:
            return "[sfc]";

        case NinaModule::ANALOG_INPUT_CONTROL:
            return "[aic]";

        case NinaModule::SOFTWARE:
            return "[sw]";       

        case NinaModule::KEYBOARD:
            return "[kbd]"; 

        case NinaModule::ANY:
        default:
            return "[main]";            
    }
}
