/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2022 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  event.cpp
 * @brief Event implementation.
 *-----------------------------------------------------------------------------
 */

#include "event.h"

//----------------------------------------------------------------------------
// BaseEvent
//----------------------------------------------------------------------------
BaseEvent::BaseEvent(NinaModule source_id, EventType type)
{
    // Initialise class data
    _source_id = source_id;
    _type = type;
}

//----------------------------------------------------------------------------
// ~BaseEvent
//----------------------------------------------------------------------------
BaseEvent::~BaseEvent()
{
    // Nothing specific to do - note vitual function
}

//----------------------------------------------------------------------------
// source_id
//----------------------------------------------------------------------------
NinaModule BaseEvent::source_id() const
{ 
    // Return the source ID
    return _source_id; 
}

//----------------------------------------------------------------------------
// type
//----------------------------------------------------------------------------
EventType BaseEvent::type() const
{
    // Return the type
    return _type; 
}

//----------------------------------------------------------------------------
// MidiEvent
//----------------------------------------------------------------------------
MidiEvent::MidiEvent(NinaModule source_id, const snd_seq_event_t &seq_event) :
    BaseEvent(source_id, EventType::MIDI)
{
    // Initialise class data
    _seq_event = seq_event;
}

//----------------------------------------------------------------------------
// ~MidiEvent
//----------------------------------------------------------------------------
MidiEvent::~MidiEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// seq_event
//----------------------------------------------------------------------------
snd_seq_event_t MidiEvent::seq_event() const
{
    // Return the sequencer event
    return _seq_event;
}

//----------------------------------------------------------------------------
// ParamChangedEvent
//----------------------------------------------------------------------------
ParamChangedEvent::ParamChangedEvent(const ParamChange &param_change) :
    BaseEvent(param_change.from_module, EventType::PARAM_CHANGED)
{
    // Initialise class data
    _param_change = param_change;
}

//----------------------------------------------------------------------------
// ~ParamChangedEvent
//----------------------------------------------------------------------------
ParamChangedEvent::~ParamChangedEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// param_change
//----------------------------------------------------------------------------
ParamChange ParamChangedEvent::param_change() const
{
    // Return the param change event
    return _param_change;
}

//----------------------------------------------------------------------------
// SystemFuncEvent
//----------------------------------------------------------------------------
SystemFuncEvent::SystemFuncEvent(const SystemFunc &system_func) :
    BaseEvent(system_func.from_module, EventType::SYSTEM_FUNC)
{
    // Initialise class data
    _system_func = system_func;
}

//----------------------------------------------------------------------------
// ~SystemFuncEvent
//----------------------------------------------------------------------------
SystemFuncEvent::~SystemFuncEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// system_func
//----------------------------------------------------------------------------
SystemFunc SystemFuncEvent::system_func() const
{
    // Return the ssytem function event
    return _system_func;
}

//----------------------------------------------------------------------------
// ReloadPresetsEvent
//----------------------------------------------------------------------------
ReloadPresetsEvent::ReloadPresetsEvent(NinaModule source_id, bool from_ab_toggle) :
    BaseEvent(source_id, EventType::RELOAD_PRESETS)
{
    //  Initialise class data
    _from_ab_toggle = from_ab_toggle;
}

//----------------------------------------------------------------------------
// ~ReloadPresetsEvent
//----------------------------------------------------------------------------
ReloadPresetsEvent::~ReloadPresetsEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// from_ab_toggle
//----------------------------------------------------------------------------
bool ReloadPresetsEvent::from_ab_toggle() const
{
    // Return if this reload was via an A/B toggle
    return _from_ab_toggle;
}

//----------------------------------------------------------------------------
// SurfaceControlFuncEvent
//----------------------------------------------------------------------------
SurfaceControlFuncEvent::SurfaceControlFuncEvent(const SurfaceControlFunc &sfc_func) :
    BaseEvent(sfc_func.from_module, EventType::SURFACE_CONTROL_FUNC)
{
    // Initialise class data
    _sfc_func = sfc_func;
}

//----------------------------------------------------------------------------
// ~SurfaceControlFuncEvent
//----------------------------------------------------------------------------
SurfaceControlFuncEvent::~SurfaceControlFuncEvent()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// sfc_func
//----------------------------------------------------------------------------
SurfaceControlFunc SurfaceControlFuncEvent::sfc_func() const
{
    // Return the Surface Control function
    return _sfc_func;
}
