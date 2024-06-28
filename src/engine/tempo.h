/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  tempo.h
 * @brief Tempo class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _TEMPO_H
#define _TEMPO_H

#include <string>
#include <atomic>

// Constants
constexpr float MIN_TEMPO_BPM     = 10;
constexpr float MAX_TEMPO_BPM     = 240;
constexpr float DEFAULT_TEMPO_BPM = 30;

// Tempo Note Value
enum TempoNoteValue: int
{
    QUARTER = 0,
    EIGHTH = 1,
    SIXTEENTH = 2,
    THIRTYSECOND = 3,
    QUARTER_TRIPLETS = 4,
    EIGHTH_TRIPLETS = 5,
    SIXTEENTH_TRIPLETS = 6,
    THIRTYSECOND_TRIPLETS = 7,
    NUM_TEMPO_NOTE_VALUES = 8  
};

// Old Tempo Note Value
enum OldTempoNoteValue: int
{
    OLD_WHOLE = 0,
    OLD_QUARTER = 1,
    OLD_EIGHTH = 2,
    OLD_SIXTEENTH = 3,
    OLD_THIRTYSECOND = 4,
    OLD_NUM_TEMPO_NOTE_VALUES = 5
};

// Tempo class
class Tempo
{
public:
    // Constructor
    Tempo();
    Tempo(unsigned int bpm);

    // Destructor
    virtual ~Tempo();

    // Public functions
    uint get_note_duration() const;
    unsigned int get_bpm() const;
    void set_bpm(unsigned int bpm);
    void set_note_value(TempoNoteValue note_value);

private:
    // Private data
    std::atomic<unsigned int> _bpm;
    std::atomic<TempoNoteValue> _note_value;
};

#endif  // _TEMPO_H
