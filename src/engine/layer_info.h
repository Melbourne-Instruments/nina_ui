/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2022-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  layer_info.h
 * @brief Layer Info class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _LAYER_INFO_H
#define _LAYER_INFO_H

#include "param.h"
#include "common.h"

// LayerInfo class
class LayerInfo
{
public:    
    // Helper functions
    static inline uint GetLayerMaskBit(uint layer_num) { return 1 << layer_num; }

    // Constructor
    LayerInfo(uint layer_num);

    // Destructor
    virtual ~LayerInfo();

    // Public functions
    uint layer_num() const;
    PatchId get_patch_id() const;
    PatchState get_patch_state() const;
    bool get_patch_modified() const;
    uint get_num_voices() const;
    uint get_midi_channel_filter() const;
    float get_morph_value() const;
    MpeMode get_mpe_mode() const;
    void set_patch_id(PatchId id);
    void set_patch_state(PatchState state);
    void set_patch_modified(bool modified);
    void set_num_voices(uint num_voices);
    void set_midi_channel_filter(uint midi_channel_filter);
    void set_morph_value(float morph_value);
    void set_mpe_mode(MpeMode mode);
    bool check_midi_channel_filter(unsigned char channel);

private:
    // Private variables
    uint _layer_num;
    PatchId _patch_id;
    PatchState _patch_state;
    bool _patch_modified;
    uint _num_voices;
    uint _midi_channel_filter;
    float _morph_value;
    MpeMode _mpe_mode;
};

#endif  // _LAYER_INFO_H
