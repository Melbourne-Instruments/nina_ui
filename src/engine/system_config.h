/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  system_config.h
 * @brief System Config definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _SYSTEM_CONFIG_H
#define _SYSTEM_CONFIG_H

#include <mutex>
#include "param.h"
#include "common.h"

// System Config struct
class SystemConfig
{
public:
    // Constructor    
    SystemConfig();

    // Destructor
    ~SystemConfig();

    // Public functions
    uint get_layers_num();
    void set_layers_num(uint num);
    uint get_first_multifn_switch_num();
    uint get_mod_src_num();
    uint get_patch_modified_threshold();
    bool get_demo_mode();
    uint get_demo_mode_timeout();
    std::string get_system_colour();
    void set_first_multifn_switch_num(uint num);
    void set_mod_src_num(uint num);
    void set_patch_modified_threshold(uint threshold);
    void set_demo_mode(bool enable);
    void set_demo_mode_timeout(uint timeout);
    void set_system_colour(std::string colour);
    void init_osc_config(const char *host_ip, const char *incoming_port, const char *outgoing_port);
    void set_osc_send_count(uint send_count);
    const char *osc_host_ip();
    const char *osc_incoming_port();
    const char *osc_outgoing_port();
    uint osc_send_count();

private:
    // Private variables
    uint _layers_num;
    uint _first_multifn_switch_num;
    uint _mod_src_num;
    uint _patch_modified_threshold;
    bool _demo_mode;
    uint _demo_mode_timeout;
    std::string _system_colour;
    const char *_osc_host_ip;
    const char *_osc_incoming_port;
    const char *_osc_outgoing_port;
    uint _osc_send_count;
    std::mutex _mutex;
};

#endif  // _SYSTEM_CONFIG_H
