/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2020-2022 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  event.h
 * @brief Event definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _EVENT_H
#define _EVENT_H

#include "alsa/asoundlib.h"
#include "param.h"
#include "system_func.h"
#include "common.h"

// Event Type
enum class EventType
{
	MIDI,
	PARAM_CHANGED,
	SYSTEM_FUNC,
	RELOAD_PRESETS,
	SURFACE_CONTROL_FUNC
};

// Surface Control Function types
enum class SurfaceControlFuncType
{
	RESET_MULTIFN_SWITCHES,
	SET_MULTIFN_SWITCH,
	SET_SWITCH_VALUE,
	SET_CONTROL_HAPTIC_MODE,
	PUSH_POP_CONTROLS_STATE
};

// Surface Control Function
class SurfaceControlFunc
{
public:
    // Public data
    NinaModule from_module;
    SurfaceControlFuncType type;
	std::string control_path;
	std::string control_haptic_mode;
	std::string push_controls_state;
	std::string pop_controls_state;
	bool process_physical_control;
	bool set_switch;

    // Constructor/Destructor
    SurfaceControlFunc() {}
	SurfaceControlFunc(SurfaceControlFuncType type, NinaModule from_module)
	{
		this->type = type;
		this->from_module = from_module;
		this->set_switch = false;
		this->process_physical_control = true;
	}
    ~SurfaceControlFunc() {}

	// Public functions
	bool pushing_controls_state() const {
		return (type == SurfaceControlFuncType::PUSH_POP_CONTROLS_STATE) && (push_controls_state.size() > 0);
	}
	bool popping_controls_state() const {
		return (type == SurfaceControlFuncType::PUSH_POP_CONTROLS_STATE) && (pop_controls_state.size() > 0);
	}
};

// Base Event class (virtual)
class BaseEvent
{
public:
	// Constructor
	BaseEvent(NinaModule source_id, EventType type);

	// Destructor
	virtual ~BaseEvent() = 0;

	// Public functions
	NinaModule source_id() const;
	EventType type() const;

private:
	// Private data
    NinaModule _source_id;
    EventType _type;
};

// MIDI Event class
class MidiEvent : public BaseEvent
{
public:
	// Constructor/destructor
	MidiEvent(NinaModule source_id, const snd_seq_event_t &seq_event);
	~MidiEvent();

	// Public functions
    snd_seq_event_t seq_event() const;

private:
	// Private data
    snd_seq_event_t _seq_event;
};

// Param Changed Event class
class ParamChangedEvent : public BaseEvent
{
public:
	// Constructor/destructor
	ParamChangedEvent(const ParamChange &param_change);
	~ParamChangedEvent();

	// Public functions
    ParamChange param_change() const;

private:
	// Private data
    ParamChange _param_change;
};

// System Function Event class
class SystemFuncEvent : public BaseEvent
{
public:
	// Constructor/destructor
	SystemFuncEvent(const SystemFunc &system_func);
	~SystemFuncEvent();

	// Public functions
    SystemFunc system_func() const;

private:
	// Private data
    SystemFunc _system_func;
};

// Re-load Presets Event class
class ReloadPresetsEvent : public BaseEvent
{
public:
	// Constructor/destructor
	ReloadPresetsEvent(NinaModule source_id, bool from_ab_toggle=false);
	~ReloadPresetsEvent();

	// Public functions
	bool from_ab_toggle() const;

private:
	// Private data
	bool _from_ab_toggle;
};

// Surface Control Function Event class
class SurfaceControlFuncEvent : public BaseEvent
{
public:
	// Constructor/destructor
	SurfaceControlFuncEvent(const SurfaceControlFunc &sfc_func);
	~SurfaceControlFuncEvent();

	// Public functions
    SurfaceControlFunc sfc_func() const;

private:
	// Private data
    SurfaceControlFunc _sfc_func;	
};

#endif  // _EVENT_H
