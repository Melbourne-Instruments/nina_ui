/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  tempo.cpp
 * @brief Tempo implementation.
 *-----------------------------------------------------------------------------
 */
#include <stdint.h>
#include <cstring>
#include <iostream>
#include <cmath>
#include "tempo.h"

// Constants
constexpr TempoNoteValue DEFAULT_TEMPO_NOTE_VALUE = TempoNoteValue::QUARTER;

//----------------------------------------------------------------------------
// Tempo
//----------------------------------------------------------------------------
Tempo::Tempo()
{
	// Initialise private data
	_bpm = DEFAULT_TEMPO_BPM;
	_note_value = DEFAULT_TEMPO_NOTE_VALUE;
}

//----------------------------------------------------------------------------
// Tempo
//----------------------------------------------------------------------------
Tempo::Tempo(unsigned int bpm)
{
	// Initialise private data
	_bpm = bpm;
	_note_value = DEFAULT_TEMPO_NOTE_VALUE;
}

//----------------------------------------------------------------------------
// ~Tempo
//----------------------------------------------------------------------------
Tempo::~Tempo()
{
	// Nothing specific to do
}

//----------------------------------------------------------------------------
// get_note_duration
//----------------------------------------------------------------------------
uint Tempo::get_note_duration() const
{
	float multiplier;

	// Get the bpm multipler
	switch (_note_value)
    {
        case QUARTER:
            multiplier = 1;
            break;

        case EIGHTH:
            multiplier = 2;
            break;

        case SIXTEENTH:
            multiplier = 4;
            break;

        case THIRTYSECOND:
            multiplier = 8;
            break;

        case QUARTER_TRIPLETS:
            multiplier = 1.5;
            break;

        case EIGHTH_TRIPLETS:
            multiplier = 3;
            break;

        case SIXTEENTH_TRIPLETS:
            multiplier = 6;
            break;

        case THIRTYSECOND_TRIPLETS:
            multiplier = 12;
            break;

        default:
            multiplier = 1;
            break;
    }

	// Return the calculated note duration including both note-on and note-off at
    // a 50% duty cycle	
	return ((uint)std::floor(_bpm * multiplier) << 1);
}

//----------------------------------------------------------------------------
// get_bmp
//----------------------------------------------------------------------------
unsigned int Tempo::get_bpm() const
{
    // Return the Tempo BPM
    return _bpm;
}

//----------------------------------------------------------------------------
// set_bpm
//----------------------------------------------------------------------------
void Tempo::set_bpm(unsigned int bpm)
{
    // Make sure the bpm is within the allowed limits
    if (bpm < MIN_TEMPO_BPM)
        _bpm = MIN_TEMPO_BPM;
    else if (bpm > MAX_TEMPO_BPM)
        _bpm = MAX_TEMPO_BPM;
    else
        _bpm = bpm;
}

//----------------------------------------------------------------------------
// set_note_value
//----------------------------------------------------------------------------
void Tempo::set_note_value(TempoNoteValue note_value)
{
	// Set the note value
	_note_value = note_value;
}
