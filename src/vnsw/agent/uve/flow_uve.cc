/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_machine_types.h>
#include <virtual_network_types.h>
#include <vrouter_types.h>
#include <uve/flow_uve.h>

#include <oper/interface_common.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <cmn/agent_cmn.h>
#include <uve/uve_client.h>
#include <uve/flow_uve.h>
#include <pkt/flow_proto.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////
// Routines to manage the L4 Port Bitmaps
////////////////////////////////////////////////////////////////////////////
void L4PortBitmap::PortBitmap::AddPort(uint16_t port) {
    int idx = port / kBucketCount;
    counts_[idx]++;
    if (counts_[idx] == 1) {
        bitmap_[idx / kBitsPerEntry] |= (1 << (idx % kBitsPerEntry));
    }
}

void L4PortBitmap::PortBitmap::DelPort(uint16_t port) {
    int idx = port / kBucketCount;
    counts_[idx]--;
    if (counts_[idx] == 0) {
        bitmap_[idx / kBitsPerEntry] &= ~(1 << (idx % kBitsPerEntry));
    }
}

void L4PortBitmap::PortBitmap::Encode(std::vector<uint32_t> &bmap) {
    for (int i = 0; i < L4PortBitmap::kBmapCount; i++) {
        bmap.push_back(bitmap_[i]);
    }
}

bool L4PortBitmap::PortBitmap::Sync(std::vector<uint32_t> &bmap) {
    bool changed = false;
    for (int i = 0; i < kBmapCount; i++) {
        if (bitmap_[i] != bitmap_old_[i]) {
            bitmap_old_[i] = bitmap_[i];
            changed = true;
        }
    }

    if (changed) {
        for (int i = 0; i < L4PortBitmap::kBmapCount; i++) {
            bmap.push_back(bitmap_old_[i]);
        }
    }

    return changed;
}

void L4PortBitmap::AddPort(uint8_t proto, uint16_t sport, uint16_t dport) {
    if (proto == IPPROTO_UDP) {
        udp_sport_.AddPort(sport);
        udp_dport_.AddPort(dport);
    }

    if (proto == IPPROTO_TCP) {
        tcp_sport_.AddPort(sport);
        tcp_dport_.AddPort(dport);
    }
}

void L4PortBitmap::DelPort(uint8_t proto, uint16_t sport, uint16_t dport) {
    if (proto == IPPROTO_UDP) {
        udp_sport_.DelPort(sport);
        udp_dport_.DelPort(dport);
    }

    if (proto == IPPROTO_TCP) {
        tcp_sport_.DelPort(sport);
        tcp_dport_.DelPort(dport);
    }
}

bool L4PortBitmap::Sync(PortBucketBitmap &bmap) {
    bool changed = false;

    std::vector<uint32_t> tmp;
    if (tcp_sport_.Sync(tmp)) {
        bmap.set_tcp_sport_bitmap(tmp);
        changed = true;
    }

    tmp.clear();
    if (tcp_dport_.Sync(tmp)) {
        bmap.set_tcp_dport_bitmap(tmp);
        changed = true;
    }

    tmp.clear();
    if (udp_sport_.Sync(tmp)) {
        bmap.set_udp_sport_bitmap(tmp);
        changed = true;
    }

    tmp.clear();
    if (udp_dport_.Sync(tmp)) {
        bmap.set_udp_dport_bitmap(tmp);
        changed = true;
    }

    return changed;
}

void L4PortBitmap::Encode(PortBucketBitmap &bmap) {
    std::vector<uint32_t> tmp;
    tcp_sport_.Encode(tmp);
    bmap.set_tcp_sport_bitmap(tmp);

    tmp.clear();
    tcp_dport_.Encode(tmp);
    bmap.set_tcp_dport_bitmap(tmp);

    tmp.clear();
    udp_sport_.Encode(tmp);
    bmap.set_udp_sport_bitmap(tmp);

    tmp.clear();
    udp_dport_.Encode(tmp);
    bmap.set_udp_dport_bitmap(tmp);
}

FlowUve *FlowUve::singleton_;

void FlowUve::NewFlow(const FlowEntry *flow) {
    UveClient::GetInstance()->NewFlow(flow);
}

void FlowUve::DeleteFlow(const FlowEntry *flow) {
    UveClient::GetInstance()->DeleteFlow(flow);
}
