/*
 * Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_IP_LEARNING_MAC_LEARNING_KEY_H_
#define SRC_VNSW_AGENT_MAC_IP_LEARNING_MAC_LEARNING_KEY_H_

struct  MacIpLearningKey {
    MacIpLearningKey(uint32_t vrf_id, const IpAddress ip):
        vrf_id_(vrf_id), ip_addr_(ip) {}
    const uint32_t vrf_id_;
    const IpAddress ip_addr_;

    bool IsLess(const MacIpLearningKey &rhs) const {
        if (vrf_id_ != rhs.vrf_id_) {
            return vrf_id_ < rhs.vrf_id_;
        }

        return ip_addr_ < rhs.ip_addr_;
    }
};

struct MacIpLearningKeyCmp {
    bool operator()(const MacIpLearningKey &lhs, const MacIpLearningKey &rhs) const {
        return lhs.IsLess(rhs);
    }
};
#endif
