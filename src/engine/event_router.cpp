/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2022 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  event_router.h
 * @brief Event Router implementation.
 *-----------------------------------------------------------------------------
 */

#include "event_router.h"
#include "event.h"
#include "base_manager.h"
#include <iostream>
#include <unistd.h>


//----------------------------------------------------------------------------
// EventRouter
//----------------------------------------------------------------------------
EventRouter::EventRouter()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// ~EventRouter
//----------------------------------------------------------------------------
EventRouter::~EventRouter()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// register_event_listener
//----------------------------------------------------------------------------
void EventRouter::register_event_listener(EventListener *listener)
{
    std::vector<EventListener *> *event_listeners = nullptr;

    // Get the event listeners to modify
    switch(listener->event_type())
    {
    case EventType::MIDI:
        event_listeners = &_midi_event_listeners;
        break;

    case EventType::PARAM_CHANGED:
        event_listeners = &_param_changed_event_listeners;
        break;

    case EventType::SYSTEM_FUNC:
        event_listeners = &_system_func_event_listeners;
        break;

    case EventType::RELOAD_PRESETS:
        event_listeners = &_reload_presets_event_listeners;
        break;

    case EventType::SURFACE_CONTROL_FUNC:
        event_listeners = &_sfc_event_listeners;
        break;   
    }

    // The event listeners were found?
    if (event_listeners)
    {
        // Check if this listener is already registered
        for(auto el : *event_listeners)
        {
            // If already registered simply return with no
            // further action
            if (el == listener)
                return;
        }
        event_listeners->push_back(listener);
    }
}

//----------------------------------------------------------------------------
// post_midi_event
//----------------------------------------------------------------------------
void EventRouter::post_midi_event(const MidiEvent *event)
{
    // Go through all of the registered MIDI listeners, and check if they
    // are registered for this event
    for(auto el : _midi_event_listeners)
    {
        // Is the event listener listening for this event source?
        if(el->source_id() == event->source_id())
        {
            // Listener is registered for this event, post to that manager
            // Note we need to take a copy of the event, so each manager gets its own copy
            el->mgr()->post_msg(new MidiEvent(*event));
        }
    }

    // Delete the passed event - this is no longer needed as a copy is passed to each listener
    delete event;
}

//----------------------------------------------------------------------------
// post_param_changed_event
//----------------------------------------------------------------------------
void EventRouter::post_param_changed_event(const ParamChangedEvent *event)
{
    // Go through all of the registered Param Changed listeners, and check if they
    // are registered for this event
    for(auto el : _param_changed_event_listeners)
    {
        // Is the event listener listening for this event source?
        if(el->source_id() == event->source_id())
        {
            // Listener is registered for this event, post to that manager
            // Note we need to take a copy of the event, so each manager gets its own copy
            el->mgr()->post_msg(new ParamChangedEvent(*event));
        }
    }

    // Delete the passed event - this is no longer needed as a copy is passed to each listener
    delete event;
}

//----------------------------------------------------------------------------
// post_system_func_event
//----------------------------------------------------------------------------
void EventRouter::post_system_func_event(const SystemFuncEvent *event)
{
    // Go through all of the registered System Func listeners, and check if they
    // are registered for this event
    for(auto el : _system_func_event_listeners)
    {
        // Is the event listener listening for this event source?
        if(el->source_id() == event->source_id())
        {
            // Listener is registered for this event, post to that manager
            // Note we need to take a copy of the event, so each manager gets its own copy
            el->mgr()->post_msg(new SystemFuncEvent(*event));
        }
    }

    // Delete the passed event - this is no longer needed as a copy is passed to each listener
    delete event;
}

//----------------------------------------------------------------------------
// post_reload_presets_event
//----------------------------------------------------------------------------
void EventRouter::post_reload_presets_event(const ReloadPresetsEvent *event)
{
    // Go through all of the registered Reload Presets listeners, and check if they
    // are registered for this event
    for(auto el : _reload_presets_event_listeners)
    {
        // Is the event listener listening for this event source?
        if(el->source_id() == event->source_id())
        {
            // Listener is registered for this event, post to that manager
            // Note we need to take a copy of the event, so each manager gets its own copy
            el->mgr()->post_msg(new ReloadPresetsEvent(*event));
        }
    }

    // Delete the passed event - this is no longer needed as a copy is passed to each listener
    delete event;
}

//----------------------------------------------------------------------------
// post_sfc_func_event
//----------------------------------------------------------------------------
void EventRouter::post_sfc_func_event(const SurfaceControlFuncEvent *event)
{
    // Go through all of the registered Surface Control listeners, and check if they
    // are registered for this event
    for(auto el : _sfc_event_listeners)
    {
        // Is the event listener listening for this event source?
        if(el->source_id() == event->source_id())
        {
            // Listener is registered for this event, post to that manager
            // Note we need to take a copy of the event, so each manager gets its own copy
            el->mgr()->post_msg(new SurfaceControlFuncEvent(*event));
        }
    }

    // Delete the passed event - this is no longer needed as a copy is passed to each listener
    delete event;
}
