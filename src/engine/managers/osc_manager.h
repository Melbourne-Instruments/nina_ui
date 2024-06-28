/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2022 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  osc_manager.h
 * @brief OSC Manager class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _OSC_MANAGER_H
#define _OSC_MANAGER_H

#include "lo/lo.h"
#include "lo/lo_cpp.h"
#include "base_manager.h"
#include "event.h"
#include "event_router.h"
#include "param.h"
#include "timer.h"

// OSC Bundle structure
struct OscBundle
{
	OscBundle() 
	{
		bundle = lo_bundle_new(LO_TT_IMMEDIATE);
		send_count = 0;
        id = 0;
	}
    lo_bundle bundle;
    unsigned int send_count;
    unsigned int id;
};

// OSC Param change structure
struct OscParamChange
{
	OscParamChange(std::string path, float value) 
	{
		this->path = path;
		this->value = value;
	}    
    std::string path;
    float value;
};

// OSC Manager class
class OscManager: public BaseManager
{
public:
    // Constructor
    OscManager(EventRouter *event_router);

    // Destructor
    ~OscManager();

    // Public functions
    bool start();
    void process();
    void process_event(const BaseEvent *event);
    void osc_control_receive_value(std::string control_path, float value);

private:
    // Private variables
    lo_server_thread _osc_server;
    lo_address _send_addr;
    uint _send_count;
    EventListener *_sfc_listener;
    EventListener *_fm_reload_presets_listener;
    EventListener *_fm_param_changed_listener;
    Timer *_send_timer;
    Timer *_param_change_timer;
    std::mutex _osc_mutex;
    std::vector<OscBundle> _send_bundles;
    std::vector<OscParamChange> _param_changes;

    // Private functions
    bool _initialise_osc_server();
    void _process_param_change_event(const ParamChange &param_change);
    void _process_param_changed_mapped_params(const Param *param, float value, const Param *skip_param, bool displayed);
    void _params_changed_timeout();
    void _process_presets();
    void _osc_send_callback();
};

#endif  // _OSC_MANAGER_H
