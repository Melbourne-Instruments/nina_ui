/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  layer_info.cpp
 * @brief Layer Info implementation.
 *-----------------------------------------------------------------------------
 */

#include <assert.h>
#include "layer_info.h"

//----------------------------------------------------------------------------
// LayerInfo
//----------------------------------------------------------------------------
LayerInfo::LayerInfo(uint layer_num)
{
    // Initialise class data
    assert(layer_num < NUM_LAYERS);
    _layer_num = layer_num;
}

//----------------------------------------------------------------------------
// ~LayerInfo
//----------------------------------------------------------------------------
LayerInfo::~LayerInfo()
{
    // Nothing specific to do
}

//----------------------------------------------------------------------------
// layer_num
//----------------------------------------------------------------------------
uint LayerInfo::layer_num() const
{ 
    // Return the layer number
    return _layer_num; 
}

//----------------------------------------------------------------------------
// get_patch_id
//----------------------------------------------------------------------------
PatchId LayerInfo::get_patch_id() const
{
    // Return the layer Patch ID
    return _patch_id;
}

//----------------------------------------------------------------------------
// get_patch_state
//----------------------------------------------------------------------------
PatchState LayerInfo::get_patch_state() const
{
    // Return the layer Patch state
    return _patch_state;
}

//----------------------------------------------------------------------------
// get_patch_modified
//----------------------------------------------------------------------------
bool LayerInfo::get_patch_modified() const
{
    // Return the layer patch modified indicator
    return _patch_modified;
}

//----------------------------------------------------------------------------
// get_num_voices
//----------------------------------------------------------------------------
uint LayerInfo::get_num_voices() const
{
    // Return the layer number of voices
    return _num_voices;
}

//----------------------------------------------------------------------------
// get_midi_channel_filter
//----------------------------------------------------------------------------
uint LayerInfo::get_midi_channel_filter() const
{
    // Return the layer MIDI channel filter
    return _midi_channel_filter;
}

//----------------------------------------------------------------------------
// get_morph_value
//----------------------------------------------------------------------------
float LayerInfo::get_morph_value() const
{
    // Return the morph value
    return _morph_value;
}

//----------------------------------------------------------------------------
// get_mpe_mode
//----------------------------------------------------------------------------
MpeMode LayerInfo::get_mpe_mode() const
{
    // Return the MPE mode
    return _mpe_mode;
}

//----------------------------------------------------------------------------
// set_patch_id
//----------------------------------------------------------------------------
void LayerInfo::set_patch_id(PatchId id)
{
    // Set the layer Patch ID
    _patch_id = id;
}

//----------------------------------------------------------------------------
// set_patch_state
//----------------------------------------------------------------------------
void LayerInfo::set_patch_state(PatchState state)
{
    // Set the layer Patch State
    _patch_state = state;
}

//----------------------------------------------------------------------------
// set_patch_modified
//----------------------------------------------------------------------------
void LayerInfo::set_patch_modified(bool modified)
{
    // Set the patch modified indicator
    _patch_modified = modified;
}

//----------------------------------------------------------------------------
// set_num_voices
//----------------------------------------------------------------------------
void LayerInfo::set_num_voices(uint num_voices)
{
    // Set the layer number of voices
    _num_voices = num_voices;
}

//----------------------------------------------------------------------------
// set_midi_channel_filter
//----------------------------------------------------------------------------
void LayerInfo::set_midi_channel_filter(uint midi_channel_filter)
{
    // Set the layer MIDI channel filter
    _midi_channel_filter = midi_channel_filter;
}

//----------------------------------------------------------------------------
// set_midi_channel_filter
//----------------------------------------------------------------------------
void LayerInfo::set_morph_value(float morph_value)
{
    // Set the morph value
    _morph_value = morph_value;
}

//----------------------------------------------------------------------------
// set_midi_channel_filter
//----------------------------------------------------------------------------
void LayerInfo::set_mpe_mode(MpeMode mode)
{
    // Set the MPE mode
    _mpe_mode = mode;
}

//----------------------------------------------------------------------------
// check_midi_channel_filter
//----------------------------------------------------------------------------
bool LayerInfo::check_midi_channel_filter(unsigned char channel)
{
    return (_midi_channel_filter == 0) || (channel == (_midi_channel_filter - 1));
}
