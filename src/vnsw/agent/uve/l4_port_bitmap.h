/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_uve_h
#define vnsw_agent_flow_uve_h

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_machine_types.h>
#include <virtual_network_types.h>
#include <vrouter_types.h>
#include <port_bmap_types.h>

class FlowEntry;

struct L4PortBitmap {
    static const uint16_t kPortPerBucket = 256;
    static const uint16_t kBucketCount = (0x10000 / kPortPerBucket);
    static const uint16_t kBitsPerEntry = (sizeof(uint32_t) * 8);
    static const uint16_t kBmapCount = (kBucketCount / kBitsPerEntry);
    struct PortBitmap {
        PortBitmap() : counts_(), bitmap_(), bitmap_old_() {}
        ~PortBitmap() {}

        uint32_t counts_[kBucketCount];
        uint32_t bitmap_[kBmapCount];
        uint32_t bitmap_old_[kBmapCount];

        void AddPort(uint16_t port);
        bool Sync(std::vector<uint32_t> &bmap);
        void Encode(std::vector<uint32_t> &bmap);
        void Reset();
    };

    L4PortBitmap();
    ~L4PortBitmap();

    void AddPort(uint8_t proto, uint16_t sport, uint16_t dport);
    void Encode(PortBucketBitmap &bmap);
    void Reset();

    PortBitmap tcp_sport_;
    PortBitmap tcp_dport_;
    PortBitmap udp_sport_;
    PortBitmap udp_dport_;
};

#endif // vnsw_agent_flow_uve_h
