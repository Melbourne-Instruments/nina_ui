/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  analog_input_control_manager.h
 * @brief Analog Input Control Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _ANALOG_INPUT_CONTROL_MANAGER_H
#define _ANALOG_INPUT_CONTROL_MANAGER_H

#include <thread>
#include "base_manager.h"
#include "event_router.h"
#include "event.h"
#include "timer.h"
#include "analog_input_control.h"

// Analog Input Control Manager class
class AnalogInputControlManager: public BaseManager
{
public:
    // Constructor
    AnalogInputControlManager(EventRouter *event_router);

    // Destructor
    ~AnalogInputControlManager();

    // Public functions
    bool start();
    void stop();
    void process();
    void process_gpio_event();

private:
    // Private variables
    AnalogInputControl *_analog_input_control;
    int _gpio_chip_handle;
    std::thread *_gpio_event_thread;
    bool _run_gpio_event_thread;

    // Private functions
    

};

#endif  // _ANALOG_INPUT_CONTROL_MANAGER_H
