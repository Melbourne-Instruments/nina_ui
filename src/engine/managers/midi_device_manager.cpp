/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2024 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  midi_device_manager.cpp
 * @brief MIDI Device Manager implementation.
 *-----------------------------------------------------------------------------
 */
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <cmath>
#include <sys/ioctl.h>
#include <termios.h>
#include "daw_manager.h"
#include "sequencer_manager.h"
#include "arpeggiator_manager.h"
#include "utils.h"

// Constants
#define SERIAL_MIDI_AMA_PORT_NUM              "1"
#define SERIAL_MIDI_DEV_NAME                  "/dev/ttyAMA" SERIAL_MIDI_AMA_PORT_NUM
constexpr char CC_PARAM_NAME[]                = "cc/0/";
constexpr char PITCH_BEND_PARAM_NAME[]        = "pitch_bend/0";
constexpr char CHANPRESS_PARAM_NAME[]         = "chanpress/0";
constexpr uint MIDI_CC_MIN_VALUE              = 0;
constexpr uint MIDI_CC_MAX_VALUE              = 127;
constexpr int MIDI_PITCH_BEND_MIN_VALUE       = -8192;
constexpr int MIDI_PITCH_BEND_MAX_VALUE       = 8191;
constexpr uint MIDI_CHANPRESS_MIN_VALUE       = 0;
constexpr uint MIDI_CHANPRESS_MAX_VALUE       = 127;
constexpr auto MIDI_POLL_TIMEOUT_MS           = 200;
constexpr int SERIAL_MIDI_ENCODING_BUF_SIZE   = 300;
constexpr uint MIDI_DEVICE_POLL_SLEEP_US      = 1*1000*1000;
constexpr uint SYSTEM_CLIENT_ID               = 0;
constexpr uint MIDI_THROUGH_CLIENT_ID         = 14;
constexpr char SUSHI_CLIENT_NAME[]            = "Sushi";
constexpr char GADGET_CLIENT_NAME[]           = "f_midi";
constexpr uint NUM_MIDI_CLOCK_PULSES_PER_BEAT = 24;
constexpr int MIDI_CC_ECHO_TIMEOUT            = 300;
constexpr uint MPE_Y_PARAM_MIDI_CC_CHANNEL    = 74;
constexpr uint MAX_DECODED_MIDI_EVENT_SIZE    = 12;
constexpr uint MIDI_EVENT_QUEUE_POLL_SEC      = 0;
constexpr uint MIDI_EVENT_QUEUE_POLL_NSEC     = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(20)).count();
constexpr uint MIDI_EVENT_QUEUE_RESERVE_SIZE  = 200;
constexpr uint MAX_TEMPO_DURATION             = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(60000/5)).count();

// Static functions
static void *_process_midi_devices(void* data);
static void *_process_midi_event(void* data);
static void *_process_midi_queue_event(void* data);

//----------------------------------------------------------------------------
// IsMidiPitchBendParamPath
//----------------------------------------------------------------------------
bool MidiDeviceManager::IsMidiCcParamPath(std::string param_name)
{
    // Check if this param is a MIDI CC param
    auto path = Param::ParamPath(NinaModule::MIDI_DEVICE, CC_PARAM_NAME);
    if (param_name.compare(0, path.length(), path) == 0)
    {
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// IsMidiPitchBendParamPath
//----------------------------------------------------------------------------
bool MidiDeviceManager::IsMidiPitchBendParamPath(std::string param_name)
{
    // Check if this param is a MIDI Pitch Bend param
    auto path = Param::ParamPath(NinaModule::MIDI_DEVICE, PITCH_BEND_PARAM_NAME);
    if (param_name.compare(0, path.length(), path) == 0)
    {
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// IsMidiChanpressParamPath
//----------------------------------------------------------------------------
bool MidiDeviceManager::IsMidiChanpressParamPath(std::string param_name)
{
    // Check if this param is a MIDI Chanpress param
    auto path = Param::ParamPath(NinaModule::MIDI_DEVICE, CHANPRESS_PARAM_NAME);
    if (param_name.compare(0, path.length(), path) == 0)
    {
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// MidiDeviceManager
//----------------------------------------------------------------------------
MidiDeviceManager::MidiDeviceManager(EventRouter *event_router) : 
    BaseManager(NinaModule::MIDI_DEVICE, "MidiDeviceManager", event_router)
{
    // Initialise class data
    _sfc_listener = 0;
    _osc_listener = 0;
    _fm_listener = 0;
    _seq_handle = 0;
    _seq_client = -1;
    _seq_port = -1;
    _serial_snd_midi_event = 0;
    _serial_midi_port_handle = 0;
    _midi_devices_thread = 0;
    _run_midi_devices_thread = true;    
    _midi_event_thread = 0;
    _run_midi_event_thread = true;
    _midi_event_queue_thread = 0;
    _run_midi_event_queue_thread = true;
    std::memset(_bank_select_index, -1, sizeof(_bank_select_index));
    _tempo_timer = new Timer(TimerType::PERIODIC);
    _midi_clock_count = 0;
    _tempo_param = 0;
    _midi_clk_in_param = 0;
    _midi_echo_filter_param = 0;
    _mpe_x_param = nullptr;
    _mpe_y_param = nullptr;
    _mpe_z_param = nullptr;
    _pitch_bend_param = nullptr;
    _chanpress_param = nullptr;
    _midi_event_queue_a.reserve(MIDI_EVENT_QUEUE_RESERVE_SIZE);
    _midi_event_queue_b.reserve(MIDI_EVENT_QUEUE_RESERVE_SIZE);
    _push_midi_event_queue = &_midi_event_queue_a;
    _pop_midi_event_queue = &_midi_event_queue_b;
    _start_time = std::chrono::high_resolution_clock::now();
}

//----------------------------------------------------------------------------
// ~MidiDeviceManager
//----------------------------------------------------------------------------
MidiDeviceManager::~MidiDeviceManager()
{
    // Stop the tempo timer task
    if (_tempo_timer) {
        _tempo_timer->stop();
        delete _tempo_timer;
        _tempo_timer = 0;
    }
    
    // Delete the listeners
    if (_sfc_listener)
        delete _sfc_listener;
    if (_osc_listener)
        delete _osc_listener;
    if (_fm_listener)
        delete _fm_listener;
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
bool MidiDeviceManager::start()
{
    // Open the sequencer MIDI interface
    MidiDeviceManager::_open_seq_midi();
    if (!_seq_handle)
    {
        // Error opening the sequencer MIDI interface, show an error
        DEBUG_BASEMGR_MSG("Open sequencer MIDI interface failed");    
    }

    // Open the serial MIDI interface
    MidiDeviceManager::_open_serial_midi();
    if (!_serial_midi_port_handle)
    {
        // Error opening the serial MIDI interface, show an error
        DEBUG_BASEMGR_MSG("Open serial MIDI interface failed");    
    }

    // If no MIDI interface can be opened, return false
    if (!_seq_handle && !_serial_midi_port_handle)
        return false;

    // Create a normal thread to poll for MIDI devices
    if (_seq_handle)
        _midi_devices_thread = new std::thread(_process_midi_devices, this);

    // Create a thread to listen for MIDI events
    _midi_event_thread = new std::thread(_process_midi_event, this);
	//struct sched_param param;
	//param.sched_priority = sched_get_priority_min(SCHED_FIFO);
	//::pthread_setschedparam(_midi_event_thread->native_handle(), SCHED_FIFO, &param);

    // Create a normal thread to listen for MIDI queue events
    _midi_event_queue_thread = new std::thread(_process_midi_queue_event, this);

    // Get the various params used in MIDI processing
    // We do this here for efficiency - we only retrieve them once
    _tempo_param = utils::get_param(TempoBpmParam::ParamPath());
    _midi_clk_in_param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::MIDI_CLK_IN_PARAM_ID);
    _midi_echo_filter_param = utils::get_param(ParamType::COMMON_PARAM, CommonParamId::MIDI_ECHO_FILTER_PARAM_ID);
    _mpe_x_param = utils::get_param_from_ref(utils::ParamRef::MPE_X);
    _mpe_y_param = utils::get_param_from_ref(utils::ParamRef::MPE_Y);
    _mpe_z_param = utils::get_param_from_ref(utils::ParamRef::MPE_Z);
    _pitch_bend_param = utils::get_param(Param::ParamPath(this, PITCH_BEND_PARAM_NAME).c_str());
    _chanpress_param = utils::get_param(Param::ParamPath(this, CHANPRESS_PARAM_NAME).c_str());

    // Start the Tempo timer thread
    _tempo_timer->start((US_PER_MINUTE / (_tempo_param->get_value() * NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT)), std::bind(&MidiDeviceManager::_tempo_timer_callback, this));

    // Call the base manager
    return BaseManager::start();		
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void MidiDeviceManager::stop()
{
    // Call the base manager
    BaseManager::stop();

    // MIDI devices task running?
    if (_midi_devices_thread != 0)
    {
        // Stop the MIDI devices task
        _run_midi_devices_thread = false;
		if (_midi_devices_thread->joinable())
			_midi_devices_thread->join(); 
        _midi_devices_thread = 0;       
    }

    // MIDI event queue task running?
    if (_midi_event_queue_thread != 0)
    {
        // Stop the MIDI queue event task
        _run_midi_event_queue_thread = false;
		if (_midi_event_queue_thread->joinable())
			_midi_event_queue_thread->join(); 
        _midi_event_queue_thread = 0;       
    }

    // MIDI event task running?
    if (_midi_event_thread != 0)
    {
        // Stop the MIDI event task
        _run_midi_event_thread = false;
		if (_midi_event_thread->joinable())
			_midi_event_thread->join(); 
        _midi_event_thread = 0;       
    }

    // Close the sequencer MIDI interface
    _close_seq_midi();

    // Close the serial MIDI interface
    _close_serial_midi();
}

//----------------------------------------------------------------------------
// process
//----------------------------------------------------------------------------
void MidiDeviceManager::process()
{
    // Create and add the listeners
    _sfc_listener = new EventListener(NinaModule::SURFACE_CONTROL, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_sfc_listener);
    _osc_listener = new EventListener(NinaModule::OSC, EventType::PARAM_CHANGED, this);
    _event_router->register_event_listener(_osc_listener);
    _fm_listener = new EventListener(NinaModule::FILE_MANAGER, EventType::RELOAD_PRESETS, this);
    _event_router->register_event_listener(_fm_listener);	

    // Process all events
    BaseManager::process();
}

//----------------------------------------------------------------------------
// process_event
//----------------------------------------------------------------------------
void MidiDeviceManager::process_event(const BaseEvent * event)
{
    // Process the event depending on the type
    switch (event->type())
    {
        case EventType::RELOAD_PRESETS:
            // Process the reloading of presets
            _process_reload_presets();
            break;
 
        case EventType::PARAM_CHANGED:
            // Process the param changed event
            _process_param_changed_event(static_cast<const ParamChangedEvent *>(event)->param_change());
            break;

		default:
            // Event unknown, we can ignore it
            break;
	}
}

//----------------------------------------------------------------------------
// _process_reload_presets
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_reload_presets()
{
    // Update the tempo timer
    _tempo_timer->change_interval((US_PER_MINUTE / (_tempo_param->get_value() * NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT))); 
}

//----------------------------------------------------------------------------
// _process_param_changed_event
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_param_changed_event(const ParamChange &param_change)
{
    // If this is a MIDI param change
    Param *param = utils::get_param(param_change.path.c_str());
    if (param && (param->module == NinaModule::MIDI_DEVICE))
    {
        // Process the MIDI param change
        _process_midi_param_changed(param);
    }
    // If this is a tempo BPM param change
    else if(param == _tempo_param) {
        // Update the tempo timer
        _tempo_timer->change_interval((US_PER_MINUTE / (_tempo_param->get_value() * NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT)));            
    }    
}

//----------------------------------------------------------------------------
// _process_midi_param_changed
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_midi_param_changed(Param *param)
{
    // Get the change state string
    int pos1 = param->get_path().find(CC_PARAM_NAME);
    if (pos1 != -1)
    {
        // Start position of the param number        
        pos1 += std::strlen(CC_PARAM_NAME);

        // Get the param number string and make sure its valid
        auto str = param->get_path().substr(pos1, (param->get_path().size() - pos1));
        if (str.size() > 0) {
            // Get the channel number to send the MIDI out on
            auto channel = utils::get_current_layer_info().get_midi_channel_filter();
            if (channel) {
                channel--;
            }

            // Get the param number
            int cc_param = std::atoi(str.c_str());

            // Create the sound sequence controller event
            snd_seq_event_t ev;
            snd_seq_ev_clear(&ev);
            ev.type = SND_SEQ_EVENT_CONTROLLER;
            ev.data.control.channel = channel;
            ev.data.control.param = cc_param;
            ev.data.control.value = std::round((param->get_value() * (MIDI_CC_MAX_VALUE - MIDI_CC_MIN_VALUE)) + MIDI_CC_MIN_VALUE);

            // If the MIDI echo filter is on, then we log the msg
            if (_get_midi_echo_filter() == MidiEchoFilter::ECHO_FILTER)
            { 
                // Log the MIDI CC message and elapsed time
                auto end = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - _start_time);
                _cc_prev_messages.push_back({ev, elapsed.count()});
            }

            // Send the event to all subscribers of this port
            snd_seq_ev_set_direct(&ev);
            snd_seq_ev_set_subs(&ev);
            snd_seq_event_output(_seq_handle, &ev);
            snd_seq_drain_output(_seq_handle);

            // Any received MIDI event should also be outout on the serial MIDI interface
            uint8_t serial_buf[MAX_DECODED_MIDI_EVENT_SIZE];
            std::memset(serial_buf, 0, sizeof(serial_buf));

            // Decode the input MIDI event into serial MIDI data
            int res = snd_midi_event_decode(_serial_snd_midi_event, serial_buf, sizeof(serial_buf), &ev);
            if (res > 0)
            {
                // Write the bytes to the serial MIDI port
                // Ignore the return value for now
                std::ignore = ::write(_serial_midi_port_handle, &serial_buf, res);

                // Reset the event decoder
                // This needs to be done after each event is processed (not sure why, but no
                // big deal as it has no performance impact)
                snd_midi_event_reset_decode(_serial_snd_midi_event);                            
            }              
        }
    }
}

//----------------------------------------------------------------------------
// process_midi_devices
//----------------------------------------------------------------------------
void MidiDeviceManager::process_midi_devices()
{
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t *pinfo;

    // Do forever (until the thread is exited)
    while (_run_midi_devices_thread)
    {
        // Allocate and set the client/port info structures
        snd_seq_client_info_malloc(&cinfo);
        snd_seq_port_info_malloc(&pinfo);
        snd_seq_client_info_set_client(cinfo, -1);

        // Query and process all clients
        while (snd_seq_query_next_client(_seq_handle, cinfo) >= 0) 
        {
            uint client_id = snd_seq_client_info_get_client(cinfo);

            // Always ignore the System, MIDI Through, Sushi and Nina clients
            if ((client_id != SYSTEM_CLIENT_ID) && (client_id != MIDI_THROUGH_CLIENT_ID) &&
                (client_id != (uint)_seq_client) && 
                (std::strcmp(snd_seq_client_info_get_name(cinfo), SUSHI_CLIENT_NAME) != 0))
            {
                // Got a client, query all ports
                snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
                snd_seq_port_info_set_port(pinfo, -1);                
                while (snd_seq_query_next_port(_seq_handle, pinfo) >= 0)
                {
                    snd_seq_addr_t src_addr;
                    snd_seq_addr_t dst_addr;

                    // Set the source and destination address
                    src_addr.client = snd_seq_client_info_get_client(cinfo);
                    src_addr.port = snd_seq_port_info_get_port(pinfo);                        
                    dst_addr.client = _seq_client;
                    dst_addr.port = _seq_port;                    
                
                    // Can this port support a write subscription?
                    uint seq_port_cap = snd_seq_port_info_get_capability(pinfo);
                    if (seq_port_cap & SND_SEQ_PORT_CAP_SUBS_WRITE)
                    {
                        // We can and should be subscribed to this client
                        // Firstly check if we are already subscribed
                        bool subscribed = false;
                        snd_seq_query_subscribe_t *query;

                        // Setup the query
                        snd_seq_query_subscribe_malloc(&query);
                        snd_seq_query_subscribe_set_root(query, &dst_addr);
                        snd_seq_query_subscribe_set_type(query, SND_SEQ_QUERY_SUBS_WRITE);
                        snd_seq_query_subscribe_set_index(query, 0);

                        // Go through all subscribers
                        while (snd_seq_query_port_subscribers(_seq_handle, query) >= 0)
                        {
                            // Check if we are already subscribed
                            auto addr = snd_seq_query_subscribe_get_addr(query);
                            if ((addr->client == src_addr.client) && (addr->port == src_addr.port))
                            {
                                // Already subscribed
                                subscribed = true;
                                break;
                            }
                            snd_seq_query_subscribe_set_index(query, snd_seq_query_subscribe_get_index(query) + 1);
                        }
                        snd_seq_query_subscribe_free(query);

                        // Do we need to subscribe to this port?
                        if (!subscribed)
                        {
                            // Connect FROM this port
                            if (snd_seq_connect_from(_seq_handle, _seq_port, src_addr.client, src_addr.port) == 0)
                            {
                                // Successful connection
                                MSG("Connected from MIDI device: " << snd_seq_client_info_get_name(cinfo));
                            }
                            else
                            {
                                // Connection failed
                                //MSG("Connection from MIDI device FAILED: " << snd_seq_client_info_get_client(cinfo));
                            }
                        }                    
                    }

                    // Can this port support a read subscription AND is this the gadget port?
                    if ((seq_port_cap & SND_SEQ_PORT_CAP_SUBS_READ) &&
                        (std::strcmp(snd_seq_client_info_get_name(cinfo), GADGET_CLIENT_NAME) == 0))
                    {
                        // We can and should be subscribed to this client
                        // Firstly check if we are already subscribed
                        bool subscribed = false;
                        snd_seq_query_subscribe_t *query;

                        // Setup the query
                        snd_seq_query_subscribe_malloc(&query);
                        snd_seq_query_subscribe_set_root(query, &dst_addr);
                        snd_seq_query_subscribe_set_type(query, SND_SEQ_QUERY_SUBS_READ);
                        snd_seq_query_subscribe_set_index(query, 0);

                        // Go through all subscribers
                        while (snd_seq_query_port_subscribers(_seq_handle, query) >= 0)
                        {
                            // Check if we are already subscribed
                            auto addr = snd_seq_query_subscribe_get_addr(query);
                            if ((addr->client == src_addr.client) && (addr->port == src_addr.port))
                            {
                                // Already subscribed
                                subscribed = true;
                                break;
                            }
                            snd_seq_query_subscribe_set_index(query, snd_seq_query_subscribe_get_index(query) + 1);
                        }
                        snd_seq_query_subscribe_free(query);

                        // Do we need to subscribe to this port?
                        if (!subscribed)
                        {
                            // Connect TO this port
                            if (snd_seq_connect_to(_seq_handle, _seq_port, src_addr.client, src_addr.port) == 0)
                            {
                                // Successful connection
                                MSG("Connected to MIDI device: " << snd_seq_client_info_get_name(cinfo));
                            }
                            else
                            {
                                // Connection failed
                                //MSG("Connection to MIDI device FAILED: " << snd_seq_client_info_get_client(cinfo));
                            }
                        }                    
                    }                    
                }
            }
        }
        snd_seq_client_info_free(cinfo);
        snd_seq_port_info_free(pinfo);

        // Sleep before checking all connections again
        usleep(MIDI_DEVICE_POLL_SLEEP_US);
    }
}

//----------------------------------------------------------------------------
// process_midi_event
//----------------------------------------------------------------------------
void MidiDeviceManager::process_midi_event()
{
    int seq_npfd = 0;
    int serial_npfd = 0;
    int npfd = 0;
    std::vector<snd_seq_event_t> seq_midi_events;
    std::vector<snd_seq_event_t> serial_midi_events;
    seq_midi_events.reserve(MIDI_EVENT_QUEUE_RESERVE_SIZE);
    serial_midi_events.reserve(MIDI_EVENT_QUEUE_RESERVE_SIZE);

    // Was the sequencer MIDI interface opened?
    if (_seq_handle)
    {
        // Get the number of sequencer MIDI poll descriptors, checking for POLLIN
        seq_npfd = snd_seq_poll_descriptors_count(_seq_handle, POLLIN);
    }

    // Was the serial MIDI interface opened? We need to add a POLLIN
    // descriptor for that port if so
    if (_serial_midi_port_handle)
    {
        // There is only one serial POLLIN descriptor
        serial_npfd = 1;
    }

    // If there are actually any poll descriptors
    npfd = seq_npfd + serial_npfd;
    if (npfd)
    {
        auto pfd = std::make_unique<pollfd[]>(npfd);
        uint8_t serial_buf[SERIAL_MIDI_ENCODING_BUF_SIZE];

        // Are there any sequencer poll descriptors?
        if (seq_npfd)
        {
            // Get the sequencer poll descriptiors
            snd_seq_poll_descriptors(_seq_handle, pfd.get(), seq_npfd, POLLIN);
        }
        
        // Are there any serial poll descriptors?
        if (serial_npfd)
        {
            // Add the serial poll descriptor after the sequencer entries (if any)
            pfd[seq_npfd].fd = _serial_midi_port_handle;
            pfd[seq_npfd].events = POLLIN;
            pfd[seq_npfd].revents = 0;
        }

        // Do forever (until the thread is exited)
        while (_run_midi_event_thread)
        {
            // Wait for sequencer MIDI events, or a timeout
            if (poll(pfd.get(), npfd, MIDI_POLL_TIMEOUT_MS) > 0)
            {
                snd_seq_event_t* ev = nullptr;

                // Process all sequencer MIDI events (if any)
                while (snd_seq_event_input(_seq_handle, &ev) > 0)
                {
                    bool push_msg = true;

                    // Is this a high priority MIDI event?
                    // We process these immediately, and not via the MIDI event queue
                    if (_is_high_priority_midi_event(ev->type)) {
                        // Process the high priority MIDI event
                        _process_high_priority_midi_event(ev);

                        // Push the event to the seq events queue (used by this function only)
                        seq_midi_events.push_back(*ev);
                    }
                    else {
                        // Process the MIDI event via the MIDI queue
                        // Get the MIDI event queue mutex
                        std::lock_guard<std::mutex> lock(_midi_event_queue_mutex);

                        // Make sure the MIDI events are optimised - that is, if an event for this MIDI event already
                        // exists, overwrite it rather than adding it to the queue
                        // Do this only for pitchbend, chanpress and CC events
                        for (auto itr=_push_midi_event_queue->rbegin(); itr != _push_midi_event_queue->rend(); ++itr) {
                            auto oev = *itr;
                            if (ev->type == SND_SEQ_EVENT_PITCHBEND && oev.type == SND_SEQ_EVENT_PITCHBEND &&
                                ev->data.control.channel == oev.data.control.channel) {
                                // Overwrite this event
                                *itr = *ev;
                                push_msg = false;
                                break; 
                            }
                            if (ev->type == SND_SEQ_EVENT_CHANPRESS && oev.type == SND_SEQ_EVENT_CHANPRESS &&
                                ev->data.control.channel == oev.data.control.channel) {
                                // Overwrite this event
                                *itr = *ev;
                                push_msg = false;
                                break; 
                            }
                            if (ev->type == SND_SEQ_EVENT_CONTROLLER && oev.type == SND_SEQ_EVENT_CONTROLLER &&
                                ev->data.control.param == oev.data.control.param &&
                                ev->data.control.channel == oev.data.control.channel) {
                                // Overwrite this event
                                *itr = *ev;
                                push_msg = false;
                                break; 
                            }                                                 
                        }
                        if (push_msg) {
                            // Push the event to the MIDI event queue
                            _push_midi_event_queue->push_back(*ev);

                            // Push the event to the  seq events queue (used by this function only)
                            seq_midi_events.push_back(*ev);
                        }
                    }
                }

                // If there was a POLLIN event for the serial MIDI
                if ((pfd[seq_npfd].revents & POLLIN) == POLLIN)
                {
                    // Process all serial MIDI events
                    // NOTE: The MIDI event queue is NOT used for serial MIDI events as the performance
                    // is automatically rate-limited by the speed of the serial port
                    // Firstly read the data available in the serial port
                    int res = ::read(_serial_midi_port_handle, &serial_buf, sizeof(serial_buf));
                    if (res > 0)
                    {
                        snd_seq_event_t ev;
                        uint8_t *buf = serial_buf;
                        int buf_size = res;

                        // Process the received serial MIDI data
                        while (buf_size > 0)
                        {
                            // Encode bytes to a sequencer event
                            res = snd_midi_event_encode(_serial_snd_midi_event, buf, buf_size, &ev);
                            if (res <= 0)
                            {
                                // No message to decode, stop processing the buffer
                                break;
                            }

                            // If the event type is NONE, it means more bytes are needed to
                            // process the message, which will be processed on the next poll
                            if (ev.type != SND_SEQ_EVENT_NONE)
                            {
                                bool push_msg = true;

                                // Make sure the MIDI events are optimised - that is, if an event for this MIDI event already
                                // exists, overwrite it rather than adding it to the queue
                                // Do this only for pitchbend, chanpress and CC events
                                for (auto itr=serial_midi_events.rbegin(); itr != serial_midi_events.rend(); ++itr) {
                                    auto oev = *itr;
                                    if (ev.type == SND_SEQ_EVENT_PITCHBEND && oev.type == SND_SEQ_EVENT_PITCHBEND &&
                                        ev.data.control.channel == oev.data.control.channel) {
                                        // Overwrite this event
                                        *itr = ev;
                                        push_msg = false;
                                        break; 
                                    }
                                    if (ev.type == SND_SEQ_EVENT_CHANPRESS && oev.type == SND_SEQ_EVENT_CHANPRESS &&
                                        ev.data.control.channel == oev.data.control.channel) {
                                        // Overwrite this event
                                        *itr = ev;
                                        push_msg = false;
                                        break; 
                                    }
                                    if (ev.type == SND_SEQ_EVENT_CONTROLLER && oev.type == SND_SEQ_EVENT_CONTROLLER &&
                                        ev.data.control.param == oev.data.control.param &&
                                        ev.data.control.channel == oev.data.control.channel) {
                                        // Overwrite this event
                                        *itr = ev;
                                        push_msg = false;
                                        break; 
                                    }                                                 
                                }
                                if (push_msg) {
                                    serial_midi_events.push_back(ev);
                                }
                            }

                            // If a buffer underflow occurs (should never happen)
                            if (res > buf_size)
                            {
                                // Buffer underflow, stop processing the buffer
                                break;
                            }

                            // Set the offset for the next message, if any
                            buf += res;
                            buf_size -= res;
                        }
                    }
                    for (snd_seq_event_t ev : serial_midi_events) {
                        // Process the event as a high priority or normal event
                        _is_high_priority_midi_event(ev.type) ?
                            _process_high_priority_midi_event(&ev) :
                            _process_normal_midi_event(&ev);
                    }                    
                }

                // Were any MIDI events received and processed?
                if ((seq_midi_events.size() > 0) || (serial_midi_events.size() > 0))
                {
                    // Currently all input MIDI events (from the sequencer and serial interfaces) are also output
                    // on the serial MIDI interface
                    std::memset(serial_buf, 0, sizeof(serial_buf));
                    for (snd_seq_event_t ev : seq_midi_events)
                    {
                        // Decode the input MIDI event into serial MIDI data
                        int res = snd_midi_event_decode(_serial_snd_midi_event, serial_buf, sizeof(serial_buf), &ev);
                        if (res > 0)
                        {
                            // Write the bytes to the serial MIDI port
                            // Ignore the return value for now
                            std::ignore = ::write(_serial_midi_port_handle, &serial_buf, res);

                            // Reset the event decoder
                            // This needs to be done after each event is processed (not sure why, but no
                            // big deal as it has no performance impact)
                            snd_midi_event_reset_decode(_serial_snd_midi_event);                            
                        }
                    }
                    for (snd_seq_event_t ev : serial_midi_events)
                    {
                        // Decode the input MIDI event into serial MIDI data
                        int res = snd_midi_event_decode(_serial_snd_midi_event, serial_buf, sizeof(serial_buf), &ev);
                        if (res > 0)
                        {
                            // Write the bytes to the serial MIDI port
                            // Ignore the return value for now
                            std::ignore = ::write(_serial_midi_port_handle, &serial_buf, res);

                            // Reset the event decoder
                            // This needs to be done after each event is processed (not sure why, but no
                            // big deal as it has no performance impact)
                            snd_midi_event_reset_decode(_serial_snd_midi_event);                            
                        }
                    }
                    // Make sure the all MIDI events are cleared for the next poll
                    seq_midi_events.clear();
                    serial_midi_events.clear();
                }
            }
        }
    }
    else
    {
        // No POLLIN descriptors
        DEBUG_BASEMGR_MSG("No MIDI file descriptors to poll");
    }
}

//----------------------------------------------------------------------------
// process_midi_event_queue
//----------------------------------------------------------------------------
void MidiDeviceManager::process_midi_event_queue()
{
    struct timespec poll_time;

    // Set the thread poll time (in nano-seconds)
    std::memset(&poll_time, 0, sizeof(poll_time));
    poll_time.tv_sec = MIDI_EVENT_QUEUE_POLL_SEC;
    poll_time.tv_nsec = MIDI_EVENT_QUEUE_POLL_NSEC;

    // Do forever (until the thread is exited)
    while (_run_midi_event_queue_thread) {
        // Sleep for the poll time
        ::nanosleep(&poll_time, NULL);

        // Swap the push/pop event queues
        {
            std::lock_guard<std::mutex> lock(_midi_event_queue_mutex);
            if (_pop_midi_event_queue == &_midi_event_queue_a) {
                _push_midi_event_queue = &_midi_event_queue_a;
                _pop_midi_event_queue = &_midi_event_queue_b;
            }
            else {
                _push_midi_event_queue = &_midi_event_queue_b;
                _pop_midi_event_queue = &_midi_event_queue_a;                
            }
        }

        // Process each normal MIDI event in the pop queue, and then clear the queue
        for (auto itr=_pop_midi_event_queue->begin(); itr != _pop_midi_event_queue->end(); ++itr) {
            _process_normal_midi_event(&*itr);
        }
        _pop_midi_event_queue->clear();
    }
}

//----------------------------------------------------------------------------
// _process_high_priority_midi_event
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_high_priority_midi_event(const snd_seq_event_t *seq_event)
{
    // Parse the event type
    switch (seq_event->type) {
        case SND_SEQ_EVENT_NOTEON:
        case SND_SEQ_EVENT_NOTEOFF: {
            // Send the MIDI event - channel filtering is done by the DAW
            utils::get_manager(NinaModule::SEQUENCER)->process_midi_event_direct(seq_event);
            _event_router->post_midi_event(new MidiEvent(module(), *seq_event));
            break;
        }

        case SND_SEQ_EVENT_START: {
            // Only process if the MIDI Clock In is enabled (non-zero param value)
            if (_midi_clk_in_param->get_value()) {            
                // Save the MIDI start time
                _midi_clock_count = 0;
                _tempo_filter_state = 0;
                _midi_clock_start = std::chrono::high_resolution_clock::now();

                // Start running the sequencer
                _start_stop_seq_run(true);
            }
            break;
        }

        case SND_SEQ_EVENT_STOP: {
            // Only process if the MIDI Clock In is enabled (non-zero param value)
            if (_midi_clk_in_param->get_value()) {            
                // Reset the MIDI clock
                _midi_clock_count = 0;
                _tempo_filter_state = 0;

                // Stop running the sequencer
                _start_stop_seq_run(false);
            }
            break;
        }        

        case SND_SEQ_EVENT_CLOCK: {
            // Only process if the MIDI Clock In is enabled (non-zero param value)
            if (_midi_clk_in_param->get_value()) {
                // Signal the sequencer and arpeggiator
                utils::seq_signal();
                utils::arp_signal();

                // We need to also calculate the tempo and update the tempo param
                // Wait for 24 pulses = 1 beat, normally a quarter note
                // Waiting reduces the CPU load and helps with calculation stability                
                _midi_clock_count++;
                if (_midi_clock_count == NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT) {
                    // Get the time duration in uS
                    auto end = std::chrono::high_resolution_clock::now();
                    float duration = std::chrono::duration_cast<std::chrono::microseconds>(end -  _midi_clock_start).count();

                    // Only process durations that are vaguely sensible
                    if (duration < MAX_TEMPO_DURATION) {
                        // Filter the duration to try and smooth out the value
                        (_tempo_filter_state == 0) ?
                            _tempo_filter_state = duration :
                            _tempo_filter_state += (duration - _tempo_filter_state) * 0.2;

                        // Calculate the tempo
                        float tempo = (60000.f / (duration / 1000.f));
                        tempo = std::roundf(tempo);

                        // If the tempo has changed
                        if (_tempo_param && (_tempo_param->get_value() != tempo)) {
                            // Set the new tempo and update the tempo timer
                            _tempo_param->set_value(tempo);
                            _tempo_timer->change_interval((US_PER_MINUTE / (tempo * NUM_MIDI_CLOCK_PULSES_PER_QTR_NOTE_BEAT)));                             
                            
                            // Send a param change
                            auto param_change = ParamChange(_tempo_param, module());
                            param_change.display = false;
                            _event_router->post_param_changed_event(new ParamChangedEvent(param_change));

                            // We need to recurse each mapped param and process it
                            _process_param_changed_mapped_params(LayerInfo::GetLayerMaskBit(0), 
                                                                 _tempo_param, _tempo_param->get_normalised_value(), _tempo_param, true);
                        }
                    }

                    // Reset the MIDI clock count
                    _midi_clock_start = end;
                    _midi_clock_count = 0;
                }
            }
            break;
        }
    }
}

//----------------------------------------------------------------------------
// _process_normal_midi_event
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_normal_midi_event(const snd_seq_event_t *seq_event)
{
    // Parse the event type
    switch (seq_event->type) {        
        case SND_SEQ_EVENT_CONTROLLER:
        {
            // If this is controller number 0
            if (seq_event->data.control.param == 0)
            {
                // Assume this is a bank select - check the value is within range
                if ((seq_event->data.control.value >= 0) && ((uint)seq_event->data.control.value < NUM_BANKS))
                {
                    // Process each Layer with a matching MIDI channel filter
                    for (uint i=0; i<NUM_LAYERS; i++) {
                        // If the MIDI channel filter matches
                        if (utils::get_layer_info(i).check_midi_channel_filter(seq_event->data.control.channel)) {
                            // Set the select bank index for this layer
                            _bank_select_index[i] = seq_event->data.control.value;
                        }
                    }
                }
            }
            else
            {
                // MIDI CC events can be mapped to another param
                // Firstly create the MIDI CC path to check against
                auto path = Param::ParamPath(this, CC_PARAM_NAME + std::to_string(seq_event->data.control.param));
                
                // Get the MIDI param
                auto param = utils::get_param(path.c_str());
                if (param)
                {
                    auto &ev = *seq_event;
                    bool block = false;
                    auto mode = _get_midi_echo_filter();

                    // Run the echo filtering algorithm if the MIDI echo filter is enabled 
                    if (mode == MidiEchoFilter::ECHO_FILTER)
                    {
                        // Process the previous messages in the log, removing old messages and checking
                        // for duplicate messages
                        auto end = std::chrono::high_resolution_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - _start_time);
                        auto it = _cc_prev_messages.begin();
                        while(it != _cc_prev_messages.end())
                        {
                            // Remove this message if is is older than the timeout threshold
                            uint64_t delta = elapsed.count() - std::get<1>(*it);
                            if (delta > MIDI_CC_ECHO_TIMEOUT)
                            {
                                // Remove the message
                                _cc_prev_messages.erase(it);
                            }
                            else
                            {
                                // If the message is an echo of one we have sent, then block the msg
                                snd_seq_event &old_msg = std::get<0>(*it);
                                if ((old_msg.data.control.param == ev.data.control.param) &&
                                    (old_msg.data.control.value == ev.data.control.value) &&
                                    (old_msg.data.control.channel == ev.data.control.channel)) 
                                {
                                    block = true;
                                }                                 
                                it++;
                            }
                        }
                    }
                    // If the MIDI echo filter is filter all - all CC messages are blocked
                    else if (mode == MidiEchoFilter::FILTER_ALL)
                    {
                        block = true;
                    }

                    // Dont send the MIDI CC message if its blocked
                    if (!block) 
                    {
                        // Get the CC value
                        float value = ev.data.control.value;

                        // Check if this is a MIDI all notes off message
                        if ((ev.data.control.param >= MIDI_CTL_ALL_NOTES_OFF) && (ev.data.control.param <= MIDI_CTL_MONO2)) {
                            // Set the param value to 1.0 as this CC message is translated into an All Notes Off param change
                            // sent to the DAW, where the value has to be > 0.5
                            value = 1.0;
                        }
                        else {
                            // Get the CC value and clip to min/max
                            if (value < MIDI_CC_MIN_VALUE)
                                value = MIDI_CC_MIN_VALUE;
                            else if (value > MIDI_CC_MAX_VALUE)
                                value = MIDI_CC_MAX_VALUE;
                            value = (value - MIDI_CC_MIN_VALUE) / (MIDI_CC_MAX_VALUE - MIDI_CC_MIN_VALUE);
                        }

                        // Is this MPE Y param and the channel is allocated to MPE?
                        if ((ev.data.control.param == MPE_Y_PARAM_MIDI_CC_CHANNEL) && utils::is_mpe_channel(seq_event->data.control.channel)) {
                            // Has the MPE Y param changed?
                            if (_mpe_y_param && (_mpe_y_param->get_value() != value)) {
                                // Update the MPE Y param value to a normalised float
                                _mpe_y_param->set_value(value);

                                // For efficiency set this param directly in the DAW manager
                                static_cast<DawManager *>(utils::get_manager(NinaModule::DAW))->set_param_direct(seq_event->data.control.channel, _mpe_y_param);
                            }
                        }
                        else {
                            // Get the layers to process for this event
                            uint layers_mask = _get_layers_mask(ev.data.control.channel);
                            if (layers_mask) {
                                // Process the mapped params for this param change
                                _process_param_changed_mapped_params(layers_mask, param, value, nullptr, false);
                            }
                        }
                    }
                }
            }
            break;
        }

        case SND_SEQ_EVENT_PITCHBEND:
        {
            // MIDI Pitch Bend events can be mapped to another param
            if (_pitch_bend_param)
            {
                // Get the Pitch Bend value, clip to min/max, and normalise it
                float value = seq_event->data.control.value;
                if (value < MIDI_PITCH_BEND_MIN_VALUE)
                    value = MIDI_PITCH_BEND_MIN_VALUE;
                else if (value > MIDI_PITCH_BEND_MAX_VALUE)
                    value = MIDI_PITCH_BEND_MAX_VALUE;
                value = (value - MIDI_PITCH_BEND_MIN_VALUE) / (MIDI_PITCH_BEND_MAX_VALUE - MIDI_PITCH_BEND_MIN_VALUE);

                // Is this channel is allocated to MPE?
                if (utils::is_mpe_channel(seq_event->data.control.channel)) {
                    // Has the MPE X param changed?
                    if (_mpe_x_param && (_mpe_x_param->get_value() != value)) {            
                        // Update the MPE X param value to a normalised float
                        _mpe_x_param->set_value(value);

                        // For efficiency set this param directly in the DAW manager
                        static_cast<DawManager *>(utils::get_manager(NinaModule::DAW))->set_param_direct(seq_event->data.control.channel, _mpe_x_param);
                    }
                }
                else {
                    // Get the layers to process for this event
                    uint layers_mask = _get_layers_mask(seq_event->data.control.channel);
                    if (layers_mask) {
                        // Process the mapped params for this param change
                        _process_param_changed_mapped_params(layers_mask, _pitch_bend_param, value, nullptr, false);
                    }
                }
            }
            //DEBUG_MSG("Pitch Bend event on Channel " << (int)(seq_event->data.control.channel) << " val " << (int)(seq_event->data.control.value));
            break;
        }

        case SND_SEQ_EVENT_CHANPRESS:
        {
            // MIDI Chanpress events can be mapped to another param
            if (_chanpress_param)
            {
                // Get the Chanpress value, clip to min/max, and normalise it
                float value = seq_event->data.control.value;
                if (value < MIDI_CHANPRESS_MIN_VALUE)
                    value = MIDI_CHANPRESS_MIN_VALUE;
                else if (value > MIDI_CHANPRESS_MAX_VALUE)
                    value = MIDI_CHANPRESS_MAX_VALUE;
                value = (value - MIDI_CHANPRESS_MIN_VALUE) / (MIDI_CHANPRESS_MAX_VALUE - MIDI_CHANPRESS_MIN_VALUE);
                                
                // Is this channel is allocated to MPE?
                if (utils::is_mpe_channel(seq_event->data.control.channel)) {
                    // Has the MPE Z param changed?
                    if (_mpe_z_param && (_mpe_y_param->get_value() != value)) { 
                        // Update the MPE Z param value to a normalised float
                        _mpe_z_param->set_value(value);

                        // For efficiency set this param directly in the DAW manager
                        static_cast<DawManager *>(utils::get_manager(NinaModule::DAW))->set_param_direct(seq_event->data.control.channel, _mpe_z_param);
                    }
                }
                else {
                    // Get the layers to process for this event
                    uint layers_mask = _get_layers_mask(seq_event->data.control.channel);
                    if (layers_mask) {                
                        // Process the mapped params for this param change
                        _process_param_changed_mapped_params(layers_mask, _chanpress_param, value, nullptr, false);
                    }
                }
            }
            //DEBUG_MSG("Chanpress event on Channel " << (int)(seq_event->data.control.channel) << " val " << (int)(seq_event->data.control.value));
            break;
        }
        
        case SND_SEQ_EVENT_KEYPRESS:
            // Send the MIDI event - channel filtering is done by the DAW
            utils::get_manager(NinaModule::DAW)->process_midi_event_direct(seq_event);
            break;  

        case SND_SEQ_EVENT_PGMCHANGE:
        {
            // Patch select - check the value is within range
            if ((seq_event->data.control.value >= 0) && ((uint)seq_event->data.control.value < NUM_BANK_PATCH_FILES))
            {
                // Process each Layer with a matching MIDI channel filter
                for (uint i=0; i<NUM_LAYERS; i++) {
                    // If the MIDI channel filter matches
                    if (utils::get_layer_info(i).check_midi_channel_filter(seq_event->data.control.channel)) {
                        auto id = PatchId();

                        // A patch has been selected for this layer, so load it
                        // Firstly check if a bank has been selected - if not, then get the current
                        // bank index
                        if (_bank_select_index[i] >= 0)
                        {
                            // Bank selected, set it
                            id.bank_num = _bank_select_index[i] + 1;
                            _bank_select_index[i] = -1;
                        }
                        else
                        {
                            // Get the current layer patch ID
                            auto current_id = utils::get_layer_info(i).get_patch_id();

                            // Set the current bank index
                            id.bank_num = current_id.bank_num;
                        }
                                    
                        // Set the patch index and load the patch
                        id.patch_num = seq_event->data.control.value + 1;
                        auto system_func = SystemFunc(SystemFuncType::LOAD_PATCH, id, MIDI_DEVICE);
                        system_func.layer_num = i;
                        _event_router->post_system_func_event(new SystemFuncEvent(system_func));
                    }
                }
            }
            break;
        }
    }   
}

//----------------------------------------------------------------------------
// _process_param_changed_mapped_params
//----------------------------------------------------------------------------
void MidiDeviceManager::_process_param_changed_mapped_params(uint layers_mask, const Param *param, float value, const Param *skip_param, bool displayed)
{
    bool current_layer_in_mask = LayerInfo::GetLayerMaskBit(utils::get_current_layer_info().layer_num()) & layers_mask;

    // Get the mapped params
    auto mapped_params = param->get_mapped_params();
    for (Param *mp : mapped_params)
    {
        // Because this function is recursive, we need to skip the param that
        // caused any recursion, so it is not processed twice
        if (skip_param && (mp == skip_param))
            continue;

        // Is this a system function? Only process if the current layer is also being processed
        if ((mp->type == ParamType::SYSTEM_FUNC) && current_layer_in_mask)
        {
            const SurfaceControlParam *sfc_param = nullptr; 

            // Retrieve the mapped parameters for this system function and parse them
            // to find the first physical control mapped to that function (if any)
            auto sf_mapped_params = mp->get_mapped_params();
            for (Param *sf_mp : sf_mapped_params)
            {
                // If this is mapped to a physical control, set that control and exit
                if (sf_mp->physical_control_param) {
                    sfc_param = static_cast<const SurfaceControlParam *>(sf_mp);
                    break;
                }
            }

            // Is there a physical control associated with this system function?
            if (sfc_param)
            {
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
                    system_func.num = param->param_id - utils::system_config()->get_first_multifn_switch_num();
                }
                _event_router->post_system_func_event(new SystemFuncEvent(system_func));
            }
            else 
            {
                // Create the system function event
                auto system_func = SystemFunc(static_cast<const SystemFuncParam *>(mp)->get_system_func_type(), value, mp->get_linked_param(), module());

                // Send the system function event
                _event_router->post_system_func_event(new SystemFuncEvent(system_func));
            }

            // Note: We don't recurse system function params as they are a system action to be performed
        }
        else
        {
            // If a MIDI param, handle it here
            if (mp->module == NinaModule::MIDI_DEVICE)
            {
                // Process the MIDI param change directly
                _process_midi_param_changed(mp);
            }
            else
            {
                // Process this param if:
                // - It is not a surface control param or
                // - It is a surface control param AND we are processing the current layer in this change
                if ((mp->module != NinaModule::SURFACE_CONTROL) || ((mp->module == NinaModule::SURFACE_CONTROL) && current_layer_in_mask)) {
                    // Update the mapped param value to a normalised float *if* in the current layer
                    if (current_layer_in_mask) {
                        mp->set_value(value);
                    }

                    // Send a param changed event - never show param changes on the GUI that come via a CC message
                    auto param_change = ParamChange(mp->get_path(), value, module());
                    param_change.display = false;
                    param_change.layers_mask = layers_mask;
                    _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
                }
            }

            // We need to recurse each mapped param and process it
            _process_param_changed_mapped_params(layers_mask, mp, value, param, displayed);
        }
    }
}

//----------------------------------------------------------------------------
// _start_stop_seq_run
//----------------------------------------------------------------------------
void MidiDeviceManager::_start_stop_seq_run(bool start)
{
    // Get the sequencer and arpeggiator run params
    auto seq_param = utils::get_param(NinaModule::SEQUENCER, SequencerParamId::RUN_PARAM_ID);
    auto arp_param = utils::get_param(NinaModule::ARPEGGIATOR, ArpeggiatorParamId::ARP_RUN_PARAM_ID);
    if (seq_param) {
        // Start/stop sequencer running
        seq_param->set_value(start ? 1.0 : 0.0);
        auto param_change = ParamChange(seq_param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }
    if (arp_param) {
        // Start/stop arpeggiator running
        arp_param->set_value(start ? 1.0 : 0.0);
        auto param_change = ParamChange(arp_param, module());
        _event_router->post_param_changed_event(new ParamChangedEvent(param_change));
    }    
}


//----------------------------------------------------------------------------
// _open_seq_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_open_seq_midi()
{
    // Open the ALSA Sequencer
    if (snd_seq_open(&_seq_handle, "hw", SND_SEQ_OPEN_DUPLEX, 0) < 0)
    {
        DEBUG_BASEMGR_MSG("Open ALSA Sequencer failed");
        _seq_handle = 0;
        return;
    }

    // Set the client name
    if (snd_seq_set_client_name(_seq_handle, "Nina_App:") < 0)
    {
        DEBUG_BASEMGR_MSG("Set sequencer client name failed");
        snd_seq_close(_seq_handle); 
        snd_config_update_free_global();
        _seq_handle = 0;      
        return;
    }

    // Create a simple port
    _seq_port = snd_seq_create_simple_port(_seq_handle, "Nina_App:",
                                           (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE |
                                            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ), 
                                           SND_SEQ_PORT_TYPE_APPLICATION);
    if (_seq_port < 0)
    {
        DEBUG_BASEMGR_MSG("Create ALSA simple port failed");
        snd_seq_close(_seq_handle); 
        snd_config_update_free_global();        
        _seq_handle = 0;
        _seq_port = -1;      
        return;
    }

    // Get the sequencer client ID
    snd_seq_client_info_t *cinfo;
    snd_seq_client_info_alloca(&cinfo);
    if (snd_seq_get_client_info(_seq_handle, cinfo) < 0)
    {
        DEBUG_BASEMGR_MSG("Could not retrieve sequencer client ID");
        snd_seq_close(_seq_handle);
        snd_config_update_free_global();        
        _seq_handle = 0;
        _seq_port = -1;      
        return;
    }
    _seq_client = snd_seq_client_info_get_client(cinfo);

    // Put the sequencer in non-blocking mode
    // Note this just applies to retrieving events, not the poll function
    if (snd_seq_nonblock(_seq_handle, SND_SEQ_NONBLOCK) < 0)
    {
        DEBUG_BASEMGR_MSG("Put sequencer in non-blocking mode failed");
        snd_seq_close(_seq_handle); 
        snd_config_update_free_global();        
        _seq_handle = 0;
        _seq_client = -1;
        _seq_port = -1;   
        return;     
    }
}

//----------------------------------------------------------------------------
// _open_serial_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_open_serial_midi()
{
    // Open the serial MIDI device (non-blocking)
    int ret1 = ::open(SERIAL_MIDI_DEV_NAME, O_RDWR|O_NONBLOCK);
    if (ret1 != -1)
    {
        termios tty;

        // Setup the serial port - firstly get the current settings
        ::tcgetattr(ret1, &tty);

        // Set Control Modes
        tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
        tty.c_cflag |= (CS8 | CREAD | CLOCAL);

        // Set Local Modes
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);

        // Set Input Modes
        tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

        // Set Output Modes
        tty.c_oflag &= ~(OPOST | ONLCR);

        // Set the baud rate to 31250 (note setting to 38400 here sets that baud
        // as we have a specific clock rate for the UART)
        ::cfsetispeed(&tty, B38400);
        ::cfsetospeed(&tty, B38400);

        // Update the settings
        ::tcsetattr(ret1, TCSANOW, &tty);

        // Port opened OK, create the serial MIDI event
        int ret2 = snd_midi_event_new(SERIAL_MIDI_ENCODING_BUF_SIZE, &_serial_snd_midi_event);
        if (ret2 == 0)
        {
            // Serial MIDI event created ok, set the serial MIDI port handle
            _serial_midi_port_handle = ret1;
        }
        else
        {
            // Create serial MIDI event failed
            DEBUG_BASEMGR_MSG("Create serial MIDI event failed: " << ret2);
            ::close(ret1);
        }
    }
    else
    {
        // An error occurred opening the serial MIDI device
        DEBUG_BASEMGR_MSG("Open serial MIDI device " << SERIAL_MIDI_DEV_NAME << " failed: " << ret1);
    }
}

//----------------------------------------------------------------------------
// _close_seq_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_close_seq_midi()
{
    // ALSA sequencer opened?
    if (_seq_handle)
    {
        // Close the  ALSA sequencer and free any allocated memory
        snd_seq_close(_seq_handle); 
        snd_config_update_free_global();
        _seq_handle = 0;
    }
}

//----------------------------------------------------------------------------
// _close_serial_midi
//----------------------------------------------------------------------------
void MidiDeviceManager::_close_serial_midi()
{
    // Serial MIDI port opened?
    if (_serial_midi_port_handle)
    {
        // Close it
        ::close(_serial_midi_port_handle);
        _serial_midi_port_handle = 0;

        // Free the MIDI event
        snd_midi_event_free(_serial_snd_midi_event);
        _serial_snd_midi_event = 0;
    }
}

//----------------------------------------------------------------------------
// _tempo_timer_callback
//----------------------------------------------------------------------------
void MidiDeviceManager::_tempo_timer_callback()
{
    // If not the MIDI clock source
    if (_midi_clk_in_param->get_value() == 0) { 
        // Signal the sequencer and arpeggiator
        utils::seq_signal();
        utils::arp_signal();
    }
}

//----------------------------------------------------------------------------
// _is_high_priority_midi_event
//----------------------------------------------------------------------------
bool MidiDeviceManager::_is_high_priority_midi_event(snd_seq_event_type_t type)
{
    // Return if this is a high priority MIDI event or not
    return (type == SND_SEQ_EVENT_NOTEON) || (type == SND_SEQ_EVENT_NOTEOFF) ||
           (type == SND_SEQ_EVENT_START) || (type == SND_SEQ_EVENT_STOP) ||
           (type == SND_SEQ_EVENT_CLOCK);
}

//----------------------------------------------------------------------------
// _get_layers_mask
//----------------------------------------------------------------------------
uint MidiDeviceManager::_get_layers_mask(unsigned char channel)
{
    uint layers_mask = 0;

    // Check each layer and add to the mask if it should be procssed
    // based on the channel
    for (uint i=0; i<NUM_LAYERS; i++) {
        if (utils::get_layer_info(i).check_midi_channel_filter(channel)) {
            layers_mask |= LayerInfo::GetLayerMaskBit(i);
        }
    }
    return layers_mask;
}

//----------------------------------------------------------------------------
// _get_midi_echo_filter
//----------------------------------------------------------------------------
inline MidiEchoFilter MidiDeviceManager::_get_midi_echo_filter()
{
    // Convert and return the mode
    int mode = (uint)floor(_midi_echo_filter_param->get_value() * MidiEchoFilter::NUM_ECHO_FILTERS);
    if (mode > MidiEchoFilter::FILTER_ALL)
        mode = MidiEchoFilter::FILTER_ALL;
    return MidiEchoFilter(mode);
}

//----------------------------------------------------------------------------
// _process_midi_devices
//----------------------------------------------------------------------------
static void *_process_midi_devices(void* data)
{
    auto midi_manager = static_cast<MidiDeviceManager*>(data);
    midi_manager->process_midi_devices();

    // To suppress warnings
    return nullptr;
}

//----------------------------------------------------------------------------
// _process_midi_event
//----------------------------------------------------------------------------
static void *_process_midi_event(void* data)
{
    auto midi_manager = static_cast<MidiDeviceManager*>(data);
    midi_manager->process_midi_event();

    // To suppress warnings
    return nullptr;
}

//----------------------------------------------------------------------------
// _process_midi_queue_event
//----------------------------------------------------------------------------
static void *_process_midi_queue_event(void* data)
{
    auto midi_manager = static_cast<MidiDeviceManager*>(data);
    midi_manager->process_midi_event_queue();

    // To suppress warnings
    return nullptr;
}
