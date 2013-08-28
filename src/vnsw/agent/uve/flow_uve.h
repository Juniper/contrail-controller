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
        void DelPort(uint16_t port);
        bool Sync(std::vector<uint32_t> &bmap);
        void Encode(std::vector<uint32_t> &bmap);
    };

    L4PortBitmap() : tcp_sport_(), tcp_dport_(), udp_sport_(), udp_dport_() {}
    ~L4PortBitmap() {}

    void AddPort(uint8_t proto, uint16_t sport, uint16_t dport);
    void DelPort(uint8_t proto, uint16_t sport, uint16_t dport);
    bool Sync(PortBucketBitmap &bmap);
    void Encode(PortBucketBitmap &bmap);

    PortBitmap tcp_sport_;
    PortBitmap tcp_dport_;
    PortBitmap udp_sport_;
    PortBitmap udp_dport_;
};

class FlowUve {
public:
    FlowUve() { }
    virtual ~FlowUve() { }

    static void Init() {
        assert(singleton_ == NULL);
        singleton_ = new FlowUve();
    }

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    };

    static FlowUve *GetInstance() {return singleton_;}

    void NewFlow(const FlowEntry *flow);
    void DeleteFlow(const FlowEntry *flow);

private:
    static FlowUve *singleton_;
    DISALLOW_COPY_AND_ASSIGN(FlowUve);
};

#endif // vnsw_agent_flow_uve_h
