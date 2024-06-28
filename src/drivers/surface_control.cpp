/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  surface_control.cpp
 * @brief Surface Control driver implementation.
 *-----------------------------------------------------------------------------
 */

#include <iostream>
#include <stdint.h>
#include <cstring>
#include <thread>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifndef NO_XENOMAI
#include <rtdm/rtdm.h>
#else
#include <fcntl.h>
#include <errno.h>
#endif
#include "surface_control.h"
#include "utils.h"
#include "logger.h"
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

// Surface Control types
constexpr char KNOB_TYPE_STRING[] = "knob";
constexpr char SWITCH_TYPE_STRING[] = "switch";
constexpr uint16_t HAPTIC_KNOB_MIN_WIDTH  = 30;
constexpr uint16_t HAPTIC_KNOB_MAX_WIDTH  = 330;
constexpr int HAPTIC_KNOB_MAX_NUM_INDENTS = 32;
#ifdef SURFACE_HW_LOG_MC_CAL_RESULTS
constexpr char NINA_LOGS_DIR[] = "/udata/nina/logs/";
#endif

// Common registers for bootloader and controllers
enum CommonRegMap : int
{
    CONFIG_DEVICE = 0,
    CHECK_FIRMWARE
};

// Bootloader specific register map
enum BootloaderRegMap : int
{
    START_FIRMWARE = 2,
    START_PROGRAMMING,
    WRITE_FIRMWARE,
    STATUS
};

// Bootloader status
enum BootloaderStatus : int {
    IDLE = 0,
    BUSY,
    PARAM_SIZE_ERROR,
    FIRMWARE_OK,
    INVALID_FIRMWARE,
    INVALID_ADDRESS,
    READY_FOR_DATA,
    CHECKSUM_ERROR,
    PROGRAMMING_COMPLETE
};

// Motor Controller specific register map
enum MotorControllerRegMap : int
{
    SAMPLER_BUFFER_READ = 0x0A,
    ENCODER_A_OFFSET = 0x0B,
    ENCODER_A_GAIN = 0x0C,
    ENCODER_B_OFFSET = 0x0D,
    ENCODER_B_GAIN = 0x0E,
    ENCODER_DATUM_THRESHOLD = 0x0F,    
    MOTION_HAPTIC_CONFIG = 0x21,
    MOTION_MODE_FIND_DATUM = 0x25,
    MOTION_MODE_POSITION = 0x29,
    MOTION_MODE_HAPTIC = 0x2A,
    MOTOR_STATUS = 0x2B,
    REBOOT = 0x2F,
    CAL_ENC_PARAMS = 0x33
};

// Panel Controller specific register map
enum PanelControllerRegMap : int
{
    SWITCH_STATE = 0,
    LED_STATE
};

// Audio Control device name
#define SURFACE_HW_I2C_BUS_NUM  "6"
#define AUDIO_CONTROL_DEV_NAME  "/dev/i2c-" SURFACE_HW_I2C_BUS_NUM

// Array sizes
constexpr int NUM_SWITCH_BYTES = 5;
constexpr int NUM_LED_BYTES    = NUM_SWITCH_BYTES;
constexpr int NUM_BITS_IN_BYTE = 8;

// I2C slave addresses
constexpr uint8_t MOTOR_CONTROLLER_DEFAULT_I2C_SLAVE_ADDR = 8;
constexpr uint8_t MOTOR_CONTROLLER_BASE_I2C_SLAVE_ADDR    = 50;
constexpr uint8_t PANEL_CONTROLLER_I2C_SLAVE_ADDR         = 100;

// Bootloader constants
constexpr uint8_t CONFIG_DEVICE_SCL_LOOP_OUT = 0x80;
constexpr uint8_t FIRMWARE_TYPE_BOOTLOADER   = 0x80;

// Motor Control constants
constexpr int FIRMWARE_VER_SIZE              = 16;
constexpr int CAL_ENCODER_PARAMS_FAILED      = 0x00;
constexpr int CAL_ENCODER_PARAMS_OK          = 0x01;
constexpr int CALIBRATION_STATUS_DATUM_FOUND = 0x01;
constexpr int HAPTIC_CONFIG_MIN_NUM_BYTES    = 9;
constexpr int MOTOR_STATUS_RESP_NUM_WORDS    = 2;

// I2C retry counts
constexpr uint I2C_READ_RETRY_COUNT         = 5;
constexpr uint I2C_ROBUST_WRITE_RETRY_COUNT = 5;
constexpr uint I2C_WRITE_RETRY_COUNT        = 5;


//----------------------------------------------------------------------------
// ControlTypeFromString
//----------------------------------------------------------------------------
SurfaceControlType SurfaceControl::ControlTypeFromString(const char *type) 
{ 
    // Return the Surface Control type from the string
    if (std::strcmp(type, KNOB_TYPE_STRING) == 0)
        return SurfaceControlType::KNOB;
    else if (std::strcmp(type, SWITCH_TYPE_STRING) == 0)
        return SurfaceControlType::SWITCH;			
    return SurfaceControlType::UNKNOWN;
}

//----------------------------------------------------------------------------
// SurfaceControl
//----------------------------------------------------------------------------
SurfaceControl::SurfaceControl()
{
    // Initialise private data
    _dev_handle = -1;
    _panel_controller_active = false;
    std::memset(_motor_controller_active, false, sizeof(_motor_controller_active));
    std::memset(_motor_controller_knob_state_requested, false, sizeof(_motor_controller_knob_state_requested));
    std::memset(_motor_controller_haptic_set, false, sizeof(_motor_controller_haptic_set));
    _led_states = new uint8_t[NUM_LED_BYTES];
    std::memset(_led_states, 0, sizeof(uint8_t[NUM_LED_BYTES]));
    _selected_controller_addr = -1;
#ifdef SURFACE_HW_I2C_INTERFACE_STATS
    num_i2c_writes = 0;
    num_i2c_write_nacks = 0;
    num_i2c_write_timeouts = 0;
    num_i2c_write_errors = 0;
    num_i2c_reads = 0;
    num_i2c_read_nacks = 0;
    num_i2c_read_timeouts = 0;
    num_i2c_read_errors = 0;
#endif

#ifdef SURFACE_HW_LOG_MC_CAL_RESULTS
    // Does the logs folder not exist?
    if (::opendir(NINA_LOGS_DIR) == nullptr)
    {
        // Create it
        ::mkdir(NINA_LOGS_DIR, (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH));
    }
    _motor_status_file.open(NINA_LOGS_DIR + std::string("motor_status.log"));
#endif
}

//----------------------------------------------------------------------------
// ~SurfaceControl
//----------------------------------------------------------------------------
SurfaceControl::~SurfaceControl()
{
#ifdef SURFACE_HW_LOG_MC_CAL_RESULTS
    _motor_status_file.close();
#endif

    // Is the Surface Control device still open?
    if (_dev_handle > 0)
    {
        // Close it
        close();
    }

    // Clean up any allocated memory
    if (_led_states)
        delete [] _led_states;     
}

//----------------------------------------------------------------------------
// open
//----------------------------------------------------------------------------
int SurfaceControl::open()
{
    int handle;

    // Return an error if already open
    if (_dev_handle > 0)
    {
        // The driver is already open
        return -EBUSY;
    }

    // Open the I2C bus
    handle = ::open(AUDIO_CONTROL_DEV_NAME, O_RDWR);
    if (handle < 0)
    {
        // An error occurred opening the I2C device
        return handle;
    }

    // Save the device handle
    _dev_handle = handle;

    // Initialise the Panel and Motor Controllers
    _init_controllers();

    // Indicate the controllers were initialised OK, and show the number of knobs
    // initialised
    uint num_motor_controllers = 0;
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active?
        if (_motor_controller_active[i])
            num_motor_controllers++;
    }
    MSG("Panel Controller: " << (_panel_controller_active ? "OK": "FAILED"));
    MSG("Motor Controllers (" << num_motor_controllers << "): " << (num_motor_controllers ? "OK": "FAILED"));
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Panel Controller: {}", (_panel_controller_active ? "OK": "FAILED"));
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Motor Controllers ({}): {}", num_motor_controllers, (num_motor_controllers ? "OK": "FAILED"));

    // If less than the expected Motor Controllers, show a warning
    if (num_motor_controllers < NUM_PHYSICAL_KNOBS)
    {
        MSG("WARNING: Less Motor Controllers found than expected (" << NUM_PHYSICAL_KNOBS << ")");
        NINA_LOG_WARNING(NinaModule::SURFACE_CONTROL, "Less Motor Controllers found than expected ({})", NUM_PHYSICAL_KNOBS);
    }
    NINA_LOG_FLUSH();
    return 0;
}

//----------------------------------------------------------------------------
// close
//----------------------------------------------------------------------------
int SurfaceControl::close()
{
    // Before we close the surface control device, disable haptics for all knobs
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active?
        if (_motor_controller_active[i])
        {        
            // Disable haptic mode (ignore the return value)
            // Note: The default haptic mode class has disabled haptics
            auto haptic_mode = HapticMode();
            (void)_motor_controller_set_haptic_mode(i, haptic_mode);
        }
    }    


#ifdef SURFACE_HW_I2C_INTERFACE_STATS
    // Log the I2C stats
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Number of I2C writes: {}", num_i2c_writes);
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Number of I2C write NACKS: {}", num_i2c_write_nacks);
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Number of I2C write timeouts: {}", num_i2c_write_timeouts);
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Number of I2C write errors: {}", num_i2c_write_errors);
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Number of I2C reads: {}", num_i2c_reads);
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Number of I2C read NACKS: {}", num_i2c_read_nacks);
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Number of I2C read timeouts: {}", num_i2c_read_timeouts);
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Number of I2C read errors: {}", num_i2c_read_errors);
#endif
    
    // If the device is open
    if (_dev_handle > 0)
    {
        // Close it
        int res = ::close(_dev_handle);
        if (res < 0)
        {
            return res;
        }
        _dev_handle = -1;
        _selected_controller_addr = -1;       
    }
    return 0;
}

//----------------------------------------------------------------------------
// lock
//----------------------------------------------------------------------------
void SurfaceControl::lock()
{
    // Lock the controller mutex
    _controller_mutex.lock();
}

//----------------------------------------------------------------------------
// unlock
//----------------------------------------------------------------------------
void SurfaceControl::unlock()
{
    // Unlock the controller mutex
    _controller_mutex.unlock();
}

//----------------------------------------------------------------------------
// knob_is_active
//----------------------------------------------------------------------------
bool SurfaceControl::knob_is_active(uint num)
{
    // If knob number is valid
    if (num < NUM_PHYSICAL_KNOBS)
    {
        // Return the active state for this knob
        return _motor_controller_active[num];
    }
    return false;
}

//----------------------------------------------------------------------------
// request_knob_states
//----------------------------------------------------------------------------
int SurfaceControl::request_knob_states()
{
    int ret;

    // NOTE: Controller Mutex must be locked before calling this function

    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Request the state of each knob
    std::memset(_motor_controller_knob_state_requested, false, sizeof(_motor_controller_knob_state_requested));
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active?
        if (_motor_controller_active[i])
        {
            // Request the knob state
            ret = _motor_controller_request_knob_state(i);
            if (ret == 0)
            {
                // Request knob state requested OK
                _motor_controller_knob_state_requested[i] = true;
            }
            else
            {
                // Request knob state FAILED
                // Note: Ignore the error as it is possible from time to time
                // that the knob is not responsive due to it being busy with
                // other motion tasks                
                //DEBUG_MSG("Request knob state: FAILED: " << ret);            
            }
        }
    }
    return 0;     
}

//----------------------------------------------------------------------------
// read_knob_states
//----------------------------------------------------------------------------
int SurfaceControl::read_knob_states(KnobState *states)
{
    KnobState state = {};
    int ret;

    // NOTE: Controller Mutex must be locked before calling this function

    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Read the state of each knob
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active and has the knob state been requested?
        if (_motor_controller_active[i] && _motor_controller_knob_state_requested[i])
        {        
            // Read the knob state
            ret = _motor_controller_read_knob_state(i, &state);
            if (ret == 0)
            {
                // Return the knob state
                *states++ = state;
            }
            else
            {
                // Read knob state FAILED
                // Note: Ignore the error as it is possible from time to time
                // that the knob is not responsive due to it being busy with
                // other motion tasks
                //DEBUG_MSG("Read knob state: FAILED: " << ret);
            }
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// read_switch_states
//----------------------------------------------------------------------------
int SurfaceControl::read_switch_states(bool *states)
{
    uint8_t read_switch_states[NUM_SWITCH_BYTES];
    uint8_t switch_states[NUM_SWITCH_BYTES];
    uint8_t *switch_states_ptr = switch_states;
    int ret;

    // NOTE: Controller Mutex must be locked before calling this function

    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Is the Panel Controller active?
    if (_panel_controller_active)
    {
        // Read the Switch States position
        ret = _panel_controller_read_switch_states(read_switch_states);
        if (ret < 0)
        {
            // Read switch states failed
            //DEBUG_MSG("Read Panel Controller switch states: FAILED: " << ret);
            return ret;
        }

        // Byte swap this array
        switch_states[0] = read_switch_states[4];
        switch_states[1] = read_switch_states[3];
        switch_states[2] = read_switch_states[2];
        switch_states[3] = read_switch_states[1];
        switch_states[4] = read_switch_states[0];

        // Return the switch values as booleans
        int bit_pos = 0;
        uint8_t states_byte = *switch_states_ptr;
        for (uint i=0; i<NUM_PHYSICAL_SWITCHES; i++)
        {
            // Set the switch state as a bool
            *states++ = (states_byte & (1 << bit_pos)) == 0 ? false : true;
            bit_pos++;

            // Have we parsed all bits in this byte?
            if (bit_pos >= NUM_BITS_IN_BYTE)
            {
                // Move to the next states byte, and reset the bit position
                states_byte = *(++switch_states_ptr);
                bit_pos = 0;

            }
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_switch_led_state
//----------------------------------------------------------------------------
int SurfaceControl::set_switch_led_state(unsigned int switch_num, bool led_on)
{
    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Check the switch number is valid
    if (switch_num >= NUM_PHYSICAL_SWITCHES)
    {
        // Parameter is invalid
        return -EINVAL;
    }

    // Get the controller mutex
    std::lock_guard<std::mutex> lock(_controller_mutex);
    
    // Cache the LED state, we only commit it to hardware with the specific
    // commit function
    // This is done for efficiency to avoid multiple, sequential writes to the
    // hardware
    // Firstly get the array and bit position to set
    int array_pos = switch_num >> 3;
    int bit_pos = switch_num % NUM_BITS_IN_BYTE;

        // Check the array and bit positions are valid
    if ((array_pos > NUM_LED_BYTES) || (bit_pos >= NUM_BITS_IN_BYTE))
    {
        // This shouldn't really happen, but return an error anyway
        return -ENXIO;
    }

    // Check if we should set or clear the LED bit
    if (led_on)
    {
        // Set the LED bit
        _led_states[array_pos] |= (1 << bit_pos);
    }
    else
    {
        // Clear the LED bit
        _led_states[array_pos] &= ~(1 << bit_pos);
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_all_switch_led_states
//----------------------------------------------------------------------------
void SurfaceControl::set_all_switch_led_states(bool leds_on)
{
    // Is the Panel Controller active?
    if (_panel_controller_active)
    {    
        // Get the controller mutex
        std::lock_guard<std::mutex> lock(_controller_mutex);

        // Switching all LEDs on?
        if (leds_on)
        {
            // Set all bits ON
            for (uint i=0; i<NUM_LED_BYTES; i++)
                _led_states[i] = 0xFF;
        }
        else
        {
            // Set all bits OFF
            for (uint i=0; i<NUM_LED_BYTES; i++)
                _led_states[i] = 0x00;        
        }
    }
}

//----------------------------------------------------------------------------
// commit_led_states
//----------------------------------------------------------------------------
int SurfaceControl::commit_led_states()
{
    uint8_t led_states[NUM_LED_BYTES];
    int ret;

    // Is the Panel Controller active?
    if (_panel_controller_active)
    {
        // Get the controller mutex
        std::lock_guard<std::mutex> lock(_controller_mutex);

        // Byte swap the LEDs array
        led_states[0] = _led_states[4];
        led_states[1] = _led_states[3];
        led_states[2] = _led_states[2];
        led_states[3] = _led_states[1];
        led_states[4] = _led_states[0];

        // Set the LED States
        ret = _panel_controller_set_led_states(led_states);
        if (ret < 0)
        {
            // Set the LED States failed
            //DEBUG_MSG("Set Panel Controller LED states: FAILED");
            return ret;
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_knob_haptic_mode
//----------------------------------------------------------------------------
int SurfaceControl::set_knob_haptic_mode(unsigned int knob_num, const HapticMode& haptic_mode)
{
    // Is this Motor Controller active?
    if (_motor_controller_active[knob_num])
    {
        // Get the controller mutex
        std::lock_guard<std::mutex> lock(_controller_mutex);

        // Set the haptic mode (if any)
        int ret = _motor_controller_set_haptic_mode(knob_num, haptic_mode);
        if (ret < 0)
        {
            // Set Motor Controller haptic mode failed
            DEBUG_MSG("Motor Controller " << knob_num << " set haptic mode: FAILED");
            return ret;
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// set_knob_position
//----------------------------------------------------------------------------
int SurfaceControl::set_knob_position(unsigned int knob_num, uint16_t position, bool robust)
{
    // Return an error if not open
    if (_dev_handle == -1)
    {
        // Operation not permitted unless open first
        return -EPERM;
    }

    // Check the Motor Controller is active
    if (_motor_controller_active[knob_num])
    {
        // Get the controller mutex
        std::lock_guard<std::mutex> lock(_controller_mutex);

        // Set the knob position
        int ret = _motor_controller_set_position(knob_num, position, robust);
        if (ret < 0)
        {
            // Set Motor Controller position failed
            DEBUG_MSG("Set Motor Controller position: FAILED");
            return ret;
        }
    }
    return 0;   
}

//----------------------------------------------------------------------------
// reinit
//----------------------------------------------------------------------------
void SurfaceControl::reinit()
{
    // Initialise the Panel and Motor Controllers
    _panel_controller_active = false;
    std::memset(_motor_controller_active, false, sizeof(_motor_controller_active));
    std::memset(_motor_controller_knob_state_requested, false, sizeof(_motor_controller_knob_state_requested));
    std::memset(_motor_controller_haptic_set, false, sizeof(_motor_controller_haptic_set));
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++) {
        _motor_controller_haptic_mode[i] = "";
    } 
    _init_controllers();

    // Indicate the controllers were initialised OK, and show the number of knobs
    // initialised
    uint num_motor_controllers = 0;
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Is this Motor Controller active?
        if (_motor_controller_active[i])
            num_motor_controllers++;
    }
    MSG("Panel Controller: " << (_panel_controller_active ? "OK": "FAILED"));
    MSG("Motor Controllers (" << num_motor_controllers << "): " << (num_motor_controllers ? "OK": "FAILED"));
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Panel Controller: {}", (_panel_controller_active ? "OK": "FAILED"));
    NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Motor Controllers ({}): {}", num_motor_controllers, (num_motor_controllers ? "OK": "FAILED"));

    // If less than the expected Motor Controllers, show a warning
    if (num_motor_controllers < NUM_PHYSICAL_KNOBS)
    {
        MSG("WARNING: Less Motor Controllers found than expected (" << NUM_PHYSICAL_KNOBS << ")");
        NINA_LOG_WARNING(NinaModule::SURFACE_CONTROL, "Less Motor Controllers found than expected ({})", NUM_PHYSICAL_KNOBS);
    }
    NINA_LOG_FLUSH();
}

//----------------------------------------------------------------------------
// _init_controllers
//----------------------------------------------------------------------------
void SurfaceControl::_init_controllers()
{
    uint num_motor_controllers_found = 0;
    bool motor_controllers_started[NUM_PHYSICAL_KNOBS] = { 0 };
    bool motor_controllers_enc_params_calibration_requested[NUM_PHYSICAL_KNOBS] = { 0 };
    bool motor_controllers_enc_params_calibrated[NUM_PHYSICAL_KNOBS] = { 0 };
    bool motor_controllers_find_datum_requested[NUM_PHYSICAL_KNOBS] = { 0 };
    bool motor_controllers_find_datum_calibrated[NUM_PHYSICAL_KNOBS] = { 0 };
    int ret;

    // We need to firstly check if each Motor Controller has had its
    // address set, and if so reboot that motor controller
    // If the address is not set, or after the reboot, we then set the 
    // Motor Controller address
    for (uint i=0; i<NUM_PHYSICAL_KNOBS; i++)
    {
        // Request the Motor Controller status
        ret = _motor_controller_request_status(i);
        if (ret == 0)
        {
            // The request succeeded, so the Motor Controller address has been set
            // Reboot the the Motor Controller            
            ret = _motor_controller_reboot(i);
            if (ret < 0)
            {
                // Reboot Motor Controller failed
                NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Reboot Motor Controller {} address: FAILED", i);
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Set the Motor Controller address
        ret = _motor_controller_set_addr(i);
        if (ret < 0)
        {
            // Set Motor Controller address failed - if this fails
            // then assume there are no more motors available
            // This should never happen if all Motor Controllers are installed
            // and working correctly
            NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Set Motor Controller {} address: FAILED", i);
            break;
        }

        // Increment the number of Motor Controllers found
        num_motor_controllers_found++;
    }

    // Log the firmware version for each Motor Controller found
    for (uint i=0; i<num_motor_controllers_found; i++)
    {
        // Get the firmware version for this motor
        uint8_t firmware_ver[FIRMWARE_VER_SIZE];
        ret = _motor_controller_get_firmware_ver(i, firmware_ver);
        if (ret == 0) {
            // Log the firmware version
            char fw_ver_str[(FIRMWARE_VER_SIZE*2)+1];
            std::sprintf(fw_ver_str, "%02X%02X%02X%02X%02X%02X%02X%02X%c%c%c%c%c%c%c%c",
                         firmware_ver[0], firmware_ver[1], firmware_ver[2], firmware_ver[3],
                         firmware_ver[4], firmware_ver[5], firmware_ver[6], firmware_ver[7],
                         (char)firmware_ver[8], (char)firmware_ver[9], (char)firmware_ver[10], (char)firmware_ver[11],
                         (char)firmware_ver[12], (char)firmware_ver[13], (char)firmware_ver[14], (char)firmware_ver[15]);
            NINA_LOG_INFO(NinaModule::SURFACE_CONTROL, "Motor Controller {} FW Ver: {}", i, fw_ver_str);
        }
        else {
            NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Get Motor Controller {} FW Ver: FAILED", i); 
        }
    }

    // Start the Panel Controller
    // We do this regardless of it's state (bootloader or started), as if
    // it is already started the start request will simply be ignored
    ret = _start_panel_controller();
    if (ret == 0)
    {
        // Panel Controller active
        _panel_controller_active = true;
    }
    else
    {      
        // Start Panel Controller failed - this should never fail
        NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Start Panel Controller: FAILED");
    }

    // Go through and start each Motor Controller
    // Like the Panel Controller, we do this regardless of it's state (bootloader 
    // or started), as if it is already started the start request will simply 
    // be ignored    
    for (uint i=0; i<num_motor_controllers_found; i++)
    {
        // Start the Motor Controller
        ret = _start_motor_controller(i);
        if (ret ==0)
        {
            // Motor Controller started
            motor_controllers_started[i] = true;
        }
        else
        {
            // Could not start the Motor Controller - this should never fail
            NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Start Motor Controller {}: FAILED", i);
        }
    }

    // If any motor controllers were found
    if (num_motor_controllers_found) 
    {
        // Once the motor controllers have been started, we need to request each
        // motor to calibrate its encoder params
        // Note: If the motor controller calibrate encoder params succeeds, but the find datum
        // fails, we retry the entire calibration process for that motor
        uint cal_retry_count = 3;
        while (cal_retry_count--) 
        {
            // Loop and find motors that need to perform encoder params calibration
            for (uint i=0; i<num_motor_controllers_found; i++)
            {
                // Has this Motor Controller been started and not calibrated?
                if (motor_controllers_started[i] && !motor_controllers_find_datum_calibrated[i])
                {
                    // If this motor controller has already performed encoder params calibration, try again
                    if (motor_controllers_enc_params_calibration_requested[i])
                    {
                        // Reset the calibration status
                        motor_controllers_enc_params_calibration_requested[i] = false;
                        motor_controllers_enc_params_calibrated[i] = false;
                    }

                    // Request the Motor Controllor to calibrate its encoder params
                    ret = _motor_controller_request_cal_enc_params(i);
                    if (ret == 0)
                    {
                        // Motor Controller calibrate encoder params requested
                        motor_controllers_enc_params_calibration_requested[i] = true;                
                    }
                    else
                    {
                        // Motor Controller could not accept the calibrate encoder params request - this should never fail
                        NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Request Motor Controller {} calibrate encoder params: FAILED", i);
                    }

                    // Wait 50ms before kicking off the next motor
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            // Loop checking each Motor Controller calibrate encoder params status, waiting for the
            // status to indicate the calibration is complete
            // Try for a maximum of 20 times before returning an error
            uint wait_count = 20;
            while (wait_count)
            {
                // Read and check the calibrate encoder params status for each Motor Controller 
                // NOT calibrated
                for (uint i=0; i<num_motor_controllers_found; i++)
                {
                    // If calibration requested and not calibrated?
                    if (motor_controllers_enc_params_calibration_requested[i] && !motor_controllers_enc_params_calibrated[i])
                    {
                        uint8_t cal_enc_params_status = 0xFF;

                        // Wait for the motor to complete calibration of its encoder params
                        if (_motor_controller_check_cal_enc_params_status(i, &cal_enc_params_status) == 0)
                        {
                            // Has this Motor Controller calibrated its encoder params?
                            if (cal_enc_params_status == CAL_ENCODER_PARAMS_OK)
                            {
                                // Yes, indicate it has now calibrated its encoder params
                                motor_controllers_enc_params_calibrated[i] = true;                            
                            }
                            else if (cal_enc_params_status == CAL_ENCODER_PARAMS_FAILED)
                            {
                                // This is an unexpected response, log an error and stop trying for this
                                // motor
                                motor_controllers_enc_params_calibration_requested[i] = false;
                                NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Calibrate Motor {} Controller encoder params returned ZERO", i);
                            }
                        }
                    }
                }

                // Have all Motor Controllers calibrated their encoder params              
                if (std::memcmp(motor_controllers_enc_params_calibration_requested, 
                                motor_controllers_enc_params_calibrated, 
                                sizeof(motor_controllers_enc_params_calibrated)) == 0)
                {
                    // Yep - break from this loop, encoder params calibration is complete
                    break;
                }

                // Waiting for one or more Motor Controllers to become calibrated
                // Decrement the wait count, and sleep for 50ms before checking again
                wait_count--;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));  
            }

            // Log any Motor Controllers that could not be calibrate their encoder params
            bool find_datums = false;
            for (uint i=0; i<num_motor_controllers_found; i++)
            {
                // Did any motors request encoder params calibration?
                if (motor_controllers_enc_params_calibration_requested[i])
                {
                    // Yes - did they succeed?
                    if (!motor_controllers_enc_params_calibrated[i])
                        NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Calibrate Motor {} Controller encoder params: FAILED", i);
                    else
                        find_datums = true;
                }
            }

#ifdef SURFACE_HW_LOG_MC_CAL_RESULTS
            // Get the timestamp
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream datetime;
            datetime << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%X");

            // Log each motor's results
            for (uint i=0; i<num_motor_controllers_found; i++)
            {
                // Log the calibration results
                _log_mc_calibration_results(i, motor_controllers_enc_params_calibrated[i], datetime.str());
            }
#endif

            // Are there any motors that require datum find?
            if (find_datums)
            {
                // Once the motor controllers have had their encoder params calibrated, we need to request each
                // motor to find it's datum point
                // Loop requesting each motor to do this
                for (uint i=0; i<num_motor_controllers_found; i++)
                {
                    // Has this Motor Controller had its encoder params calibrated and not found datum yet?
                    if (motor_controllers_enc_params_calibrated[i] && !motor_controllers_find_datum_calibrated[i])
                    {
                        // Request the Motor Controllor to find it's datum point
                        motor_controllers_find_datum_requested[i] = false;
                        ret = _motor_controller_request_find_datum(i);
                        if (ret == 0)
                        {
                            // Motor Controller calibration requested
                            motor_controllers_find_datum_requested[i] = true;                
                        }
                        else
                        {
                            // Motor Controller could not request it's datum - this should never fail
                            NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Request Motor Controller {} datum: FAILED", i);
                        }

                        // Wait 50ms before kicking off the next motor
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }

                // Loop checking each Motor Controller datum status, waiting for the
                // status to indicate the datum has been found
                // Try for a maximum of 20 times before returning an error
                wait_count = 20;
                while (wait_count)
                {
                    // Read and check the datum status for each Motor Controller 
                    // NOT calibrated
                    for (uint i=0; i<num_motor_controllers_found; i++)
                    {
                        // If calibration requested and not calibrated?
                        if (motor_controllers_find_datum_requested[i] && !motor_controllers_find_datum_calibrated[i])
                        {            
                            uint8_t datum_status = 0;

                            // Read the datum status
                            // If the read fails ignore it as it may be busy still finding
                            // the datum
                            if (_motor_controller_read_find_datum_status(i, &datum_status) == 0)
                            {
                                // Has this Motor Controller found the datum OK?
                                if (datum_status == CALIBRATION_STATUS_DATUM_FOUND)
                                {
                                    // Yes, indicate it is now calibrated and active
                                    motor_controllers_find_datum_calibrated[i] = true;
                                    _motor_controller_active[i] = true;
                                }
                            }
                        }
                    }

                    // Have all Motor Controllers tried to find datum?
                    if (std::memcmp(motor_controllers_find_datum_requested, 
                                    motor_controllers_find_datum_calibrated, 
                                    sizeof(motor_controllers_find_datum_calibrated)) == 0)
                    {
                        // Yep - break from this loop, calibration is complete
                        break;
                    }

                    // Waiting for one or more Motor Controllers to find datum
                    // Decrement the wait count, and sleep for 50ms before checking again
                    wait_count--;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                // Log any Motor Controllers that could find datum
                auto motors_ok = true;
                for (uint i=0; i<num_motor_controllers_found; i++)
                {
                    // Not calibrated?
                    if (motor_controllers_find_datum_requested[i] && !motor_controllers_find_datum_calibrated[i])
                    {
                        motors_ok = false;
                        NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, "Calibrate Motor {} Controller datum: FAILED", i);
                    }
                }
                if (motors_ok)
                    break;
            }
            else
            {
                // No motors require find datum
                break;
            }
        }
    }
}

//----------------------------------------------------------------------------
// _motor_controller_request_status
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_request_status(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = CommonRegMap::CONFIG_DEVICE;

        // Request the controller slave status - robust write
        ret = _i2c_robust_write(&cmd, sizeof(cmd), true, (_selected_controller_addr + CONFIG_DEVICE_SCL_LOOP_OUT));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_reboot
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_reboot(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        // Reboot the controller
        uint8_t cmd = MotorControllerRegMap::REBOOT;
        ret = _i2c_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_set_addr
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_set_addr(uint8_t mc_num)
{
    // Select the default Motor Controller
    int ret = _select_motor_controller_default();
    if (ret  == 0)
    {
        uint8_t cmd[] = { CommonRegMap::CONFIG_DEVICE, 
                          (uint8_t)(MOTOR_CONTROLLER_BASE_I2C_SLAVE_ADDR + mc_num + CONFIG_DEVICE_SCL_LOOP_OUT) };

        // Configure the I2C slave address - robust write
        ret = _i2c_robust_write(cmd, sizeof(cmd), false);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_get_firmware_ver
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_get_firmware_ver(uint8_t mc_num, uint8_t *ver)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = CommonRegMap::CHECK_FIRMWARE;

        // Request the controller firmware version
        ret = _i2c_write(&cmd, sizeof(cmd));
        if (ret == 0)
        {
            // Read the firmware version
            ret = _i2c_read(ver, FIRMWARE_VER_SIZE);
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _start_panel_controller
//----------------------------------------------------------------------------
int SurfaceControl::_start_panel_controller()
{
    // Select the Panel Controller
    int ret = _select_panel_controller();
    if (ret == 0)
    {
        // Start the Panel Controller
        ret = _start_controller();
    }
    return ret;
}

//----------------------------------------------------------------------------
// _start_motor_controller
//----------------------------------------------------------------------------
int SurfaceControl::_start_motor_controller(uint8_t mc_num)
{
    // Select the Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        // Start the Motor Controller
        ret = _start_controller();
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_request_cal_enc_params
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_request_cal_enc_params(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = MotorControllerRegMap::CAL_ENC_PARAMS;
        
        // Request the Motor Controllor to calibrate its encoder params - robust write
        // with no readback, as this is done later using the command below
        ret = _i2c_robust_write(&cmd, sizeof(cmd), false);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_check_cal_enc_params_status
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_check_cal_enc_params_status(uint8_t mc_num, uint8_t *status)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        uint8_t resp;

        // Read the Motor Controller calibrate encoder params status
        ret = _i2c_read(&resp, sizeof(resp));
        if (ret == 0)
        {
            // Return the datum status
            *status = resp;
        }        
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_request_find_datum
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_request_find_datum(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = MotorControllerRegMap::MOTION_MODE_FIND_DATUM;
        
        // Request the Motor Controllor to find it's datum point - robust write
        // with no readback, as this is done later using the command below
        ret = _i2c_robust_write(&cmd, sizeof(cmd), false);
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_read_find_datum_status
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_read_find_datum_status(uint8_t mc_num, uint8_t *status)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        uint8_t resp;

        // Read the Motor Controller datum status
        ret = _i2c_read(&resp, sizeof(resp));
        if (ret == 0)
        {
            // Return the datum status
            *status = resp;
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_set_haptic_mode
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_set_haptic_mode(uint8_t mc_num, const HapticMode& haptic_mode)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        // Are we actually changing the haptic mode?
        if (haptic_mode.name != _motor_controller_haptic_mode[mc_num])
        {        
            // If knob haptics are switched on
            if (haptic_mode.knob_haptics_on())
            {
                auto knob_width = haptic_mode.knob_width;
                uint8_t config_cmd[HAPTIC_CONFIG_MIN_NUM_BYTES + (HAPTIC_KNOB_MAX_NUM_INDENTS * sizeof(uint16_t))] = {};
                uint8_t config_cmd_len = HAPTIC_CONFIG_MIN_NUM_BYTES;
                uint8_t mode_cmd[] = { MotorControllerRegMap::MOTION_MODE_HAPTIC, 0x01};                      
                uint8_t *cmd_ptr = &config_cmd[sizeof(uint8_t)];
                uint8_t detent_strength = 0x00;            
                uint16_t start_pos;
                uint16_t width;
                uint8_t num_indents = 0;

                // Calculate the number of indents to set in hardware
                if (haptic_mode.knob_indents.size()) {
                    for (uint i=0; i<haptic_mode.knob_indents.size(); i++)
                    {
                        // If this indent is active in hardware
                        if (haptic_mode.knob_indents[i].first) {
                            num_indents++;
                        }
                    }
                }               

                // Setup the config command byte
                config_cmd[0] = MotorControllerRegMap::MOTION_HAPTIC_CONFIG;

                // Has the width benn specified?
                if (knob_width < 360)
                {
                    // The width must be within the specified haptic range
                    // Clip to these values                
                    if (knob_width < HAPTIC_KNOB_MIN_WIDTH)
                        knob_width = HAPTIC_KNOB_MIN_WIDTH;
                    else if (knob_width > HAPTIC_KNOB_MAX_WIDTH)
                        knob_width = HAPTIC_KNOB_MAX_WIDTH;
                }

                // Have detents been specified?
                if (haptic_mode.knob_num_detents)
                {
                    // Set the detent strength
                    detent_strength = haptic_mode.knob_detent_strength;
                }

                // Has the knob start pos been specified?
                if (haptic_mode.knob_start_pos != -1)
                {
                    // Make sure the start pos and width do not exceed the knob limits
                    if ((haptic_mode.knob_start_pos + knob_width) > 360)
                    {
                        // Truncate the knob width
                        knob_width = 360 - haptic_mode.knob_start_pos;
                    }

                    // Calculate the start position
                    start_pos = (haptic_mode.knob_start_pos / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
                }
                else
                {
                    // Calculate the start position
                    start_pos = (((360.0f - knob_width) / 2) / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;
                }

                // Calculate the width
                width = (knob_width / 360.0f) * FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR;

                // Clip the number of indents if needed
                if (num_indents > HAPTIC_KNOB_MAX_NUM_INDENTS)
                    num_indents = HAPTIC_KNOB_MAX_NUM_INDENTS;

                // Setup the command data
                *cmd_ptr++ = (uint8_t)haptic_mode.knob_friction;        // Friction
                *cmd_ptr++ = (uint8_t)haptic_mode.knob_num_detents;     // Number of detents
                *cmd_ptr++ = detent_strength;                           // Detent strength
                *cmd_ptr++ = (uint8_t)(start_pos & 0xFF);               // Start pos (LSB)
                *cmd_ptr++ = (uint8_t)(start_pos >> 8);                 // Start pos (MSB)
                *cmd_ptr++ = (uint8_t)(width & 0xFF);                   // Width (LSB)
                *cmd_ptr++ = (uint8_t)(width >> 8);                     // Width (MSB)
                *cmd_ptr++ = num_indents;                               // Number of indents

                // Copy the indents, if any
                if (num_indents)
                {
                    // Process each indent
                    for (uint i=0; i<num_indents; i++)
                    {
                        // If this indent is active in hardware
                        if (haptic_mode.knob_indents[i].first) {
                            // Set the indent in the command data
                            *cmd_ptr++ = (uint8_t)(haptic_mode.knob_indents[i].second & 0xFF);
                            *cmd_ptr++ = (uint8_t)(haptic_mode.knob_indents[i].second >> 8);
                            config_cmd_len += sizeof(uint16_t);
                        }
                    }
                }

                // Set the haptic config - robust write
                ret = _i2c_robust_write(config_cmd, config_cmd_len, true, config_cmd[sizeof(uint8_t)]);

                // Was the haptic config successfully set?
                if (!_motor_controller_haptic_set[mc_num] && ret == 0)
                {
                    // Once the config has been set, enable haptic mode in the Motor Controller
                    // Use a robust write
                    ret = _i2c_robust_write(mode_cmd, sizeof(mode_cmd), true, mode_cmd[sizeof(uint8_t)]);
                    _motor_controller_haptic_set[mc_num] = true;
                }

                // Was the haptic mode correctly set and enabled if required?
                if (ret == 0)
                {
                    // Save the haptic details
                    _motor_controller_haptic_mode[mc_num] = haptic_mode.name;
                    _motor_controller_haptic_set[mc_num] = true;
                }
            }
            else
            {
                // Disable the haptic mode
                uint8_t mode_cmd[] = { MotorControllerRegMap::MOTION_MODE_HAPTIC, 0x00};
                ret = _i2c_robust_write(mode_cmd, sizeof(mode_cmd), true, mode_cmd[sizeof(uint8_t)]);

                // Was the haptic mode correctly disabled?
                if (ret == 0)
                {
                    // Save the haptic details
                    _motor_controller_haptic_mode[mc_num] = haptic_mode.name;
                    _motor_controller_haptic_set[mc_num] = false;
                }
            }
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_request_knob_state
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_request_knob_state(uint8_t mc_num)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        uint8_t cmd = MotorControllerRegMap::MOTOR_STATUS;

        // Request the knob state
        ret = _i2c_write(&cmd, sizeof(cmd));        
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_read_knob_state
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_read_knob_state(uint8_t mc_num, KnobState *state)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {
        uint16_t resp[MOTOR_STATUS_RESP_NUM_WORDS];

        // Read the knob status
        ret = _i2c_read(resp, sizeof(resp));
        if (ret == 0)
        {
#ifdef SURFACE_HW_LOG_MC_CAL_RESULTS            
            // Log if the status is non-zero
            if (resp[1])
                _motor_status_file << "Motor Status: " << std::hex << resp[1] <<  '\n';
#endif

            // Make sure the response is valid
            if (resp[0] > FLOAT_TO_KNOB_HW_VALUE_SCALING_FACTOR)
            {
                // The response is not valid
                return -EIO;
            }

            // Return the position and state
            state->position = resp[0];
            state->state = resp[1] & (KnobState::STATE_MOVING_TO_TARGET+KnobState::STATE_TAP_DETECTED);
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _motor_controller_set_position
//----------------------------------------------------------------------------
int SurfaceControl::_motor_controller_set_position(uint8_t mc_num, uint16_t position, bool robust)
{
    // Select the specific Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    {    
        uint8_t cmd[] = { MotorControllerRegMap::MOTION_MODE_POSITION, 0, 0};

        // Set the motor position
        *(uint16_t *)&cmd[sizeof(uint8_t)] = position;

        // Write the command - robust write if requested
        if (robust)
            ret = _i2c_robust_write(&cmd, sizeof(cmd), true, cmd[sizeof(uint8_t)]);
        else
            ret = _i2c_write(&cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _panel_controller_read_switch_states
//----------------------------------------------------------------------------
int SurfaceControl::_panel_controller_read_switch_states(uint8_t *switch_states)
{
    // Select the Panel Controller
    int ret = _select_panel_controller();
    if (ret == 0)
    {    
        uint8_t cmd = PanelControllerRegMap::SWITCH_STATE;

        // Write the command
        ret = _i2c_write(&cmd, sizeof(cmd));
        if (ret == 0)
        {
            // Read the switch states
            ret = _i2c_read(switch_states, NUM_SWITCH_BYTES);
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _panel_controller_set_led_states
//----------------------------------------------------------------------------
int SurfaceControl::_panel_controller_set_led_states(uint8_t *led_states)
{
    // Select the Panel Controller
    int ret = _select_panel_controller();
    if (ret == 0)
    {    
        uint8_t cmd[] = { PanelControllerRegMap::LED_STATE, 0, 0, 0, 0, 0};

        // Copy the LED states to set
        memcpy(&cmd[sizeof(uint8_t)], led_states, NUM_LED_BYTES);

        // Write the command
        ret = _i2c_write(cmd, sizeof(cmd));
    }
    return ret;
}

//----------------------------------------------------------------------------
// _select_motor_controller_default
//----------------------------------------------------------------------------
int SurfaceControl::_select_motor_controller_default()
{
    // Set the selected controller address
    _selected_controller_addr = MOTOR_CONTROLLER_DEFAULT_I2C_SLAVE_ADDR;

    // Select the default Motor Controller slave
    return _i2c_select_slave(_selected_controller_addr);
}

//----------------------------------------------------------------------------
// _select_motor_controller
//----------------------------------------------------------------------------
int SurfaceControl::_select_motor_controller(uint8_t num)
{
    // Set the selected controller address
    _selected_controller_addr = MOTOR_CONTROLLER_BASE_I2C_SLAVE_ADDR + num;

    // Select the specific Motor Controller I2C slave
    return _i2c_select_slave(_selected_controller_addr);
}

//----------------------------------------------------------------------------
// _select_panel_controller
//----------------------------------------------------------------------------
int SurfaceControl::_select_panel_controller()
{
    // Set the selected controller address
    _selected_controller_addr = PANEL_CONTROLLER_I2C_SLAVE_ADDR;
    
    // Select the Panel Controller I2C slave
    return _i2c_select_slave(PANEL_CONTROLLER_I2C_SLAVE_ADDR);
}

//----------------------------------------------------------------------------
// _controller_read_status
//----------------------------------------------------------------------------
ControllerStatus SurfaceControl::_controller_read_status()
{
    uint8_t resp;

    // Has the selected controller address been set?
    // If not return immediately with an error
    if (_selected_controller_addr == -1)
    {
        // No controller selected
        return ControllerStatus::STATUS_ERROR;
    }

    // Read the controller config device response
    int ret = _i2c_read(&resp, sizeof(resp));
    if (ret)
    {
        // Error reading the data
        return ControllerStatus::STATUS_ERROR;
    }

    // If the controller has started running its firmware, the returned status will
    // be the (controller address | SCL Loop Out bit)
    if (resp == (uint8_t)(_selected_controller_addr + CONFIG_DEVICE_SCL_LOOP_OUT))
    {
        // Firmware active
        return ControllerStatus::FIRMWARE_ACTIVE;
    }
    // If the returned status is the bootloader error "param size error", then
    // the bootloader is active
    else if (resp == BootloaderStatus::PARAM_SIZE_ERROR)
    {
        // Bootloader active
        return ControllerStatus::BOOTLOADER_ACTIVE;
    }

    // If we get here the status is unknown
    // This should never happen, so indicate an error
    return ControllerStatus::STATUS_ERROR;
}

//----------------------------------------------------------------------------
// _start_controller
//----------------------------------------------------------------------------
int SurfaceControl::_start_controller()
{
    uint8_t cmd = BootloaderRegMap::START_FIRMWARE;

    // Start the Motor/Panel Controller - robust write
    return _i2c_robust_write(&cmd, sizeof(cmd), true, 0);
}

//------------------------
// Low-level I2C functions
//------------------------

//----------------------------------------------------------------------------
// _i2c_select_slave
//----------------------------------------------------------------------------
int SurfaceControl::_i2c_select_slave(uint8_t addr)
{
    // Select the requested I2C slave
    int ret = ioctl(_dev_handle, I2C_SLAVE, addr);
    if (ret < 0)
    {
        // Select save failed
        return -errno;
    }
    return 0;
}

//----------------------------------------------------------------------------
// _i2c_read
//----------------------------------------------------------------------------
int SurfaceControl::_i2c_read(void *buf, size_t buf_len) 
{
    uint retry_count = I2C_READ_RETRY_COUNT;
    int ret;

    // Perform the I2C reads with retries
    while (retry_count--)
    {
        // Read the required bytes from the I2C slave
        ret = read(_dev_handle, buf, buf_len);
        if (ret >= 0)
        {
            // Were the required number of bytes read?
            if ((size_t)ret == buf_len)
            {
                // Read was successful
                ret = 0;
#ifdef SURFACE_HW_I2C_INTERFACE_STATS
                num_i2c_reads++;
#endif                
            }
            else
            {
                // The number of required bytes were not returned, this
                // is treated as a read error
                ret = -EIO;
#ifdef SURFACE_HW_I2C_INTERFACE_STATS
                num_i2c_read_errors++;
#endif                  
            }
            break;
        }
        else
        {
            // Set the return value to -errno in case we need to return
            ret = -errno;

            // Did a timeout occur?
            if (errno == ETIMEDOUT)
            {
                // If a timeout occurred then there is no I2C contact
                // with the device, and we can stop trying the read
#ifdef SURFACE_HW_I2C_INTERFACE_STATS
                num_i2c_read_timeouts++;
#endif                
                break;
            }

#ifdef SURFACE_HW_I2C_INTERFACE_STATS
            // Was a NACK received from the slave?
            // This is received if the slave is not ready yet to return the data
            if (errno == EREMOTEIO)
            {
                // Increment the NACK count
                num_i2c_read_nacks++;         
            }
            else
            {
                // Increment the error count
                num_i2c_read_errors++;
            }
#endif           
        }
    }
    return ret;
}

//----------------------------------------------------------------------------
// _i2c_robust_write
//----------------------------------------------------------------------------
int SurfaceControl::_i2c_robust_write(const void *buf, size_t buf_len, bool readback, uint8_t readback_value) 
{
    uint8_t resp;
    int ret;

    // A robust write tries the write for a maximum of X times with a 1ms delay
    // between each attempt
    // Note this does not include the Y retry for the standard I2C write
    uint retry_count = I2C_ROBUST_WRITE_RETRY_COUNT;
    while (retry_count--)
    {
        // Write the command
        ret = _i2c_write(buf, buf_len);

        // Was the write successful?
        if (ret == 0)
        {
            // The write succeeded, do we now need to perform
            // a read-back to ensure it was processed?
            if (readback)
            {
                // Perform a read-back, with retries
                uint readback_retry_count = I2C_ROBUST_WRITE_RETRY_COUNT;
                while (readback_retry_count--)
                {
                    // The Motor Controller always returns data as part of its protocol
                    // For efficiency, just check the first byte returned is as expected
                    ret = _i2c_read(&resp, sizeof(resp));
                    if ((ret == 0) && (resp == readback_value))
                    {
                        // Read-back succeeded
                        break;
                    }

                    // Sleep for 1ms before trying again
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                // If we could not read-back the expected byte of data
                if ((ret == 0) && (resp != readback_value))
                {
                    // Indicate an error
                    ret = -EIO;
                }
            }
            
            // Did the read-back (if performed) succeed?
            if (ret == 0)
            {
                // Robust write succeeded
                break;
            }
            
        }
        // Did a timeout occur? If so it means that there is no communications
        // with the controller, most likely because it does not exist or has failed
        else if (ret == -ETIMEDOUT)
        {
            // Stop attempting the write
            break;
        }

        // Sleep for 1ms before trying again (if possible)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // If the robust write failed, add an entry to the Nina log
    if (ret != 0)
    {
        // Log the error
        NINA_LOG_ERROR(NinaModule::SURFACE_CONTROL, 
                         "I2C robust write failed, address {}, command: {:02d}", _selected_controller_addr, (int)*(uint8_t *)buf); 
    }  
    return ret;
}

//----------------------------------------------------------------------------
// _i2c_write
//----------------------------------------------------------------------------
int SurfaceControl::_i2c_write(const void *buf, size_t buf_len) 
{
    uint retry_count = I2C_WRITE_RETRY_COUNT;
    int ret;

    // Perform the I2C writes with retries
    while (retry_count--)
    {
        // Write the bytes to the I2C device
        ret = write(_dev_handle, buf, buf_len);
        if (ret >= 0)
        {
            // Were the required number of bytes written?
            if ((size_t)ret == buf_len)
            {
                // Write was successful
                ret = 0;
#ifdef SURFACE_HW_I2C_INTERFACE_STATS
                num_i2c_writes++;
#endif                
            }
            else
            {
                // The number of required bytes were not written, this
                // is treated as a write error
                ret = -EIO;
#ifdef SURFACE_HW_I2C_INTERFACE_STATS
                num_i2c_write_errors++;
#endif                 
            }
            break;
        }
        else
        {
            // Set the return value to -errno in case we need to return
            ret = -errno;

            // Did a timeout occur?
            if (errno == ETIMEDOUT)
            {
                // If a timeout occurred then there is no I2C contact
                // with the device, and we can stop trying the write
#ifdef SURFACE_HW_I2C_INTERFACE_STATS
                num_i2c_write_timeouts++;
#endif                
                break;
            }

#ifdef SURFACE_HW_I2C_INTERFACE_STATS
            // Was a NACK received from the slave?
            // This is received if the slave is not ready yet to accept the
            // data (should never happen if the slave is connected)
            if (errno == EREMOTEIO)
            {
                // Increment the NACK count
                num_i2c_write_nacks++;         
            }
            else
            {
                // Increment the error count
                num_i2c_write_errors++;
            }
#endif
        }
    }
    return ret; 
}

#ifdef SURFACE_HW_LOG_MC_CAL_RESULTS
//----------------------------------------------------------------------------
// _log_mc_calibration_results
//----------------------------------------------------------------------------
void SurfaceControl::_log_mc_calibration_results(uint8_t mc_num, bool mc_ok, std::string datetime)
{
    std::ofstream log_file;

    // Open the log file
    // NOTE: Do not use std::endl to add a newline when writing to the file, as this is *very* slow;
    // it flushes the buffer to disk which is not needed. Use '\n' instead
    log_file.open(NINA_LOGS_DIR + std::string("mc_") + std::to_string(mc_num+1) + "_cal_results_" + datetime + ".log");

    // Motor info
    log_file << "Calibration Results for Motor Controler " << std::to_string(mc_num+1) <<  '\n';
    log_file << "Calibration: " << (mc_ok ? "OK":"FAILED") << '\n' << '\n';

    // Select the Motor Controller
    int ret = _select_motor_controller(mc_num);
    if (ret == 0)
    { 
        uint8_t cmd = MotorControllerRegMap::ENCODER_A_OFFSET;

        // Get the Encoder A Offset/Gain
        ret = _i2c_write(&cmd, sizeof(cmd));
        if (ret == 0)
        {
            int16_t resp;
            ret = _i2c_read(&resp, sizeof(resp));
            if (ret == 0)
                log_file << "Encoder A Offset: " << resp << '\n';
        }
        cmd = MotorControllerRegMap::ENCODER_A_GAIN;
        ret = _i2c_write(&cmd, sizeof(cmd));
        if (ret == 0)
        {
            int16_t resp;
            ret = _i2c_read(&resp, sizeof(resp));
            if (ret == 0)
                log_file << "Encoder A Gain: " << resp << '\n';
        }

        // Get the Encoder B Offset/Gain
        cmd = MotorControllerRegMap::ENCODER_B_OFFSET;
        ret = _i2c_write(&cmd, sizeof(cmd));
        if (ret == 0)
        {
            int16_t resp;
            ret = _i2c_read(&resp, sizeof(resp));
            if (ret == 0)
                log_file << "Encoder B Offset: " << resp << '\n';
        }
        cmd = MotorControllerRegMap::ENCODER_B_GAIN;
        ret = _i2c_write(&cmd, sizeof(cmd));
        if (ret == 0)
        {
            int16_t resp;
            ret = _i2c_read(&resp, sizeof(resp));
            if (ret == 0)
                log_file << "Encoder B Gain: " << resp << '\n';
        }

        // Get the Encoder Datum Threshold
        cmd = MotorControllerRegMap::ENCODER_DATUM_THRESHOLD;
        ret = _i2c_write(&cmd, sizeof(cmd));
        if (ret == 0)
        {
            int16_t resp;
            ret = _i2c_read(&resp, sizeof(resp));
            if (ret == 0)
                log_file << "Encoder Datum Threshold: " << resp << '\n';
        }

        // Log encoder samples if the motor was not calibrated
        if (!mc_ok)
        {
            // Log all encoder samples
            cmd = MotorControllerRegMap::SAMPLER_BUFFER_READ;
            ret = _i2c_write(&cmd, sizeof(cmd));
            if (ret == 0)
            {
                int16_t resp[2*4096];
                std::memset(resp, 0, sizeof(resp));
                ret = _i2c_read(&resp, sizeof(resp));
                if (ret == 0)
                {
                    // Log all Encoder A/B samples
                    log_file << "Raw Encoder Samples:" << '\n';
                    log_file << "Encoder A   Encoder B" << '\n';
                    for (int i=0; i<4096/2; i++)
                        log_file << std::setw(12) << std::left << resp[1 + (i*2)] << resp[i*2] << '\n';
                }
            }
        }              
    }
    log_file.close();
}
#endif
