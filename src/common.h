/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  common.h
 * @brief Common definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h> 
#include <chrono>

//--------------
// Build options
//--------------

// Define to record I2C interface stats, shown when the app exits
// Default: Not defined
//#define SURFACE_HW_I2C_INTERFACE_STATS          1

// Define to log motor controller calibration results
// Default: Not defined
//#define SURFACE_HW_LOG_MC_CAL_RESULTS           1

// Define to change the Surface Manager hardware processing
// loop duration to be 1s - typically used for testing this loop
// Default: Not defined
//#define TESTING_SURFACE_HARDWARE                1

// Define to check the layers load by reading back (some) of the patch
// data from the DAW
// Default: Not defined
//#define CHECK_LAYERS_LOAD                       1

// Define to include maintaining the patch history changes for the
// current layer
// Default: Not defined
//#define INCLUDE_PATCH_HISTORY                   1

// Define to not build in Xenomai RT support
// Default: Defined
#define NO_XENOMAI                              1

// MACRO to show a string on the console
#define MSG(str) do { std::cout << str << std::endl; } while( false )

// MACRO to show a debug string on the console
#ifndef NDEBUG
#define DEBUG_MSG(str) MSG(str)
#else
#define DEBUG_MSG(str) do { } while ( false )
#endif

// MACRO to get the full path of a Nina file
#define NINA_ROOT_FILE_PATH(filename)    (NINA_ROOT_DIR + std::string(filename))
#define NINA_UDATA_FILE_PATH(filename)   (NINA_UDATA_DIR + std::string(filename))
#define NINA_PATCH_FILE_PATH(filename)   (NINA_PATCHES_DIR + std::string(filename))
#define NINA_SCRIPTS_FILE_PATH(filename) (NINA_SCRIPTS_DIR + std::string(filename))
#define NINA_CALIBRATION_FILE(filename)  (NINA_CALIBRATION_DIR + std::string(filename))

// Constants
constexpr int NUM_PHYSICAL_SWITCHES = 37;
constexpr int NUM_PHYSICAL_KNOBS    = 32;
constexpr int NUM_MULTIFN_SWITCHES  = 16;
constexpr int DEFAULT_LAYERS_NUM = 1;
constexpr int DEFAULT_BANK_NUM  = 1;
constexpr int DEFAULT_PATCH_NUM = 1;
constexpr int DEFAULT_MOD_SRC_NUM = 1;
constexpr uint NUM_LAYER_CONFIG_FILES = 127;
constexpr uint NUM_BANKS = 127;
constexpr uint NUM_BANK_PATCH_FILES = 127;
constexpr uint MAX_NUM_WAVETABLE_FILES = 127;
constexpr uint NUM_LAYERS = 4;
constexpr uint NUM_VOICES = 12;
constexpr uint MAX_NUM_MPE_CHANNELS = 15;
constexpr uint NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT = 24;
constexpr int MS_PER_MINUTE = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(1)).count();
constexpr int US_PER_MINUTE = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::minutes(1)).count();
constexpr char NINA_ROOT_DIR[] = "/home/root/nina/";
constexpr char NINA_UDATA_DIR[] = "/udata/nina/";
constexpr char NINA_LAYERS_DIR[] = "/udata/nina/presets/layers/";
constexpr char NINA_PATCHES_DIR[] = "/udata/nina/presets/patches/";
constexpr char NINA_SCRIPTS_DIR[] = "/home/root/nina/scripts/";
constexpr char NINA_WAVETABLES_DIR[] = "/udata/nina/wavetables";
constexpr char NINA_CALIBRATION_DIR[] = "/udata/nina/calibration/";
constexpr char FIRMWARE_DIR[] = "/home/root/firmware";
constexpr char VST_CONTENTS_DIR[] = "/home/root/nina/synthia_vst.vst3/Contents/";
constexpr uint32_t KNOB_HW_VALUE_MAX_VALUE            = 32767;               // 2^15            
constexpr float FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR = 32767.0f;            // 2^15 - 1
constexpr float KNOB_HW_VALUE_TO_FLOAT_SCALING_FACTOR = 3.051850948e-05f;    // 1.0 / (2^15 - 1)

// Nina module
enum NinaModule : int
{
    ANY = 0,
    DAW,
    MIDI_DEVICE,
    SEQUENCER,
    ARPEGGIATOR,
    KEYBOARD,
    OSC,
    FILE_MANAGER,
    SURFACE_CONTROL,
    ANALOG_INPUT_CONTROL,
    GUI,
    SOFTWARE
};

// Multi-function switches mode
enum class MultifnSwitchesMode {
    NONE,
    KEYBOARD,
    SEQ_REC,
    SINGLE_SELECT,
    MULTI_SELECT,
};

// Morph mode
enum MorphMode {
    DANCE,
    DJ,
    NUM_MODES
};

// MIDI Echo Filter
enum MidiEchoFilter
{
    NO_FILTER,
    ECHO_FILTER,
    FILTER_ALL,
    NUM_ECHO_FILTERS
};

// Options for running calibration script
enum CalMode
{
    ALL = 0,
    FILTER = 1,
    MIX_VCA = 2
};

// MPE Modes
enum MpeMode
{
    OFF = 0,
    LOWER_ZONE,
    UPPER_ZONE,
    NUM_MPE_MODES
};

// Effects Type
enum FxType
{
    CHORUS = 0,
    DELAY,
    REVERB,
    NUM_FX_TYPES
};

// Patch ID
struct PatchId
{
    uint bank_num;
    uint patch_num;

    PatchId() {
        bank_num = DEFAULT_BANK_NUM;
        patch_num = DEFAULT_PATCH_NUM;        
    }

    inline bool operator==(const PatchId& rhs) const { 
        return (bank_num == rhs.bank_num) && (patch_num == rhs.patch_num);
    }
};

#endif  // _COMMON_H
