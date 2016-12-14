/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __MAC_LEARNING_KEY_H__
#define __MAC_LEARNING_KEY_H__

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
