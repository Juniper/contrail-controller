/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_KEY_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_KEY_H_

struct  MacLearningKey {
    MacLearningKey(uint32_t vrf_id, const MacAddress &mac):
        vrf_id_(vrf_id), mac_(mac) {}
    const uint32_t vrf_id_;
    const MacAddress mac_;

    bool IsLess(const MacLearningKey &rhs) const {
        if (vrf_id_ != rhs.vrf_id_) {
            return vrf_id_ < rhs.vrf_id_;
        }

        return mac_ < rhs.mac_;
    }
};

struct MacLearningKeyCmp {
    bool operator()(const MacLearningKey &lhs, const MacLearningKey &rhs) const {
        return lhs.IsLess(rhs);
    }
};
#endif
