/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  analog_input_control_manager.cpp
 * @brief Analog Input Control Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include <cstring>
 #include <sys/ioctl.h>
#include <linux/gpio.h>
#include "analog_input_control_manager.h"
#include "utils.h"
#include "logger.h"

// Constants
constexpr char GPIO_DEV_NAME[]       = "/dev/gpiochip0";
constexpr uint COMBO_TRS_DETECT_GPIO = 26;
constexpr auto GPIO_POLL_TIMEOUT_MS  = 500;

// Static functions
static void *_process_gpio_event(void* data);

//----------------------------------------------------------------------------
// AnalogInputControlManager
//----------------------------------------------------------------------------
AnalogInputControlManager::AnalogInputControlManager(EventRouter *event_router) : 
    BaseManager(NinaModule::ANALOG_INPUT_CONTROL, "AnalogInputControlManager", event_router)
{
    // Initialise class data
    _analog_input_control = new AnalogInputControl();
    _gpio_chip_handle = -1;
    _gpio_event_thread = 0;
    _run_gpio_event_thread = true;    
}

//----------------------------------------------------------------------------
// ~AnalogInputControlManager
//----------------------------------------------------------------------------
AnalogInputControlManager::~AnalogInputControlManager()
{
    // Clean up allocated data
    if (_analog_input_control)
        delete _analog_input_control;
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool AnalogInputControlManager::start()
{
    // Open the Analog Input control
    int res = _analog_input_control->open();
    if (res < 0)
    {
        // Could not open the Analog Input control, show an error
        MSG("ERROR: Could not open the Analog Input Control");
        NINA_LOG_CRITICAL(module(), "Could not open the Analog Input Control: {}", res);
    }

    // Open the GPIO chip (read-only)
    int handle = ::open(GPIO_DEV_NAME, O_RDONLY);
    if (handle > 0)
    {
        struct gpiohandle_request gpio_req;
        struct gpiohandle_data gpio_data;

        // GPIO chip successfully opened
        _gpio_chip_handle = handle;

        // Read the state of the Combo TRS Detect GPIO line, and set the input to LINE 4
        // if detected (default is MIC)
        gpio_req.lineoffsets[0] = COMBO_TRS_DETECT_GPIO;
        gpio_req.flags = GPIOHANDLE_REQUEST_INPUT;
        gpio_req.lines = 1;
        res = ::ioctl(_gpio_chip_handle, GPIO_GET_LINEHANDLE_IOCTL, &gpio_req);
        if (res == 0)
        {
            // Now read the GPIO line value
            res = ::ioctl(gpio_req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &gpio_data);
            if (res == 0)
            {
                // If the Combo TRS Detect GPIO line is a 0, then this is the default
                // state for MIC IN
                // If the value is a 1, a jack is inserted and we can change the combo
                // input for the line
                if (gpio_data.values[0] == 1)
                {
                    // Change to LINE 4 input
                    _analog_input_control->set_combo_channel_input(ComboChannelInput::LINE_4_IN);
                }
            }
            ::close(gpio_req.fd);
        }
    }
    else
    {
        // Could not open the GPIO chip, show an error
        MSG("ERROR: Could not open the GPIO chip for Analog Input Control");
        NINA_LOG_CRITICAL(module(), "Could not open the GPIO chip for Analog Input Control: {}", res);
    }

    // Create a normal thread to listen for GPIO events
    _gpio_event_thread = new std::thread(_process_gpio_event, this);    

    // Call the base manager
    return BaseManager::start();		
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void AnalogInputControlManager::stop()
{
    // Call the base manager
    BaseManager::stop();

    // GPIO event task running?
    if (_gpio_event_thread != 0)
    {
        // Stop the GPIO event task
        _run_gpio_event_thread = false;
		if (_gpio_event_thread->joinable())
			_gpio_event_thread->join(); 
        _gpio_event_thread = 0;       
    }

    // Close the Analog Input control
    int res = _analog_input_control->close();
    if (res < 0)
    {
        // Error closing the Analog Input Control, show the error
        DEBUG_BASEMGR_MSG("Could not close the Analog Input Control, close has failed: " << res);
    }

    // GPIO chip open?
    if (_gpio_chip_handle != -1)
    {
        // Close the GPIO chip
        res = ::close(_gpio_chip_handle);
        if (res < 0)
        {
            // Error closing the GPIO chip for Analog Input Control, show the error
            DEBUG_BASEMGR_MSG("Could not close the GPIO chip for Analog Input Control, close has failed: " << res);
        }        
    }
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void AnalogInputControlManager::process()
{
    // Note: This class currently has no listeners. Add here when needed

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_gpio_event
//----------------------------------------------------------------------------
void AnalogInputControlManager::process_gpio_event()
{
    struct pollfd pfd;
    struct gpioevent_request event_req;
    struct gpioevent_data event_data;
    int ret;

    // Request an event for the Combo TRS Detect GPIO line (falling and rising edge)
    event_req.lineoffset = COMBO_TRS_DETECT_GPIO;
    event_req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
    ret = ::ioctl(_gpio_chip_handle, GPIO_GET_LINEEVENT_IOCTL, &event_req);
    if (ret == 0)
    {
        // Set the GPIO poll descriptor
        pfd.fd = event_req.fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        // Do forever (until the thread is exited)
        while (_run_gpio_event_thread)
        {
            // Wait for a GPIO event, or a timeout
            if (poll(&pfd, 1, GPIO_POLL_TIMEOUT_MS) > 0)
            {
                // GPIO event occurred?
                if ((pfd.revents & POLLIN) == POLLIN)
                {
                    // Read the event
                    ret = ::read(event_req.fd, &event_data, sizeof(event_data));
                    if (ret == sizeof(event_data))
                    {
                        // If the event is a *rising* edge, then a jack has been
                        // inserted
                        // If the event is a *falling* edge, then the jack has been
                        // removed
                        if (event_data.id == GPIOEVENT_EVENT_RISING_EDGE)
                        {
                            // Jack inserted - set the combo input to LINE 4
                            _analog_input_control->set_combo_channel_input(ComboChannelInput::LINE_4_IN);
                        }
                        else
                        {
                            // Jack removed - set the combo input to MIC IN
                            _analog_input_control->set_combo_channel_input(ComboChannelInput::MIC_IN);
                        }
                    }
                }
            }
        }
    }
    else
    {
        // Event request failed
        DEBUG_BASEMGR_MSG("Could not request the GPIO event: " << errno);
    }
}

//----------------------------------------------------------------------------
// _process_gpio_event
//----------------------------------------------------------------------------
static void *_process_gpio_event(void* data)
{
    auto mgr = static_cast<AnalogInputControlManager*>(data);
    mgr->process_gpio_event();

    // To suppress warnings
    return nullptr;
}
