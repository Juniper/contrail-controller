/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_NET_ESI_H_
#define SRC_NET_ESI_H_

#include <boost/system/error_code.hpp>
#include <string>

#include "base/util.h"

class EthernetSegmentId {
public:
    static const int kSize = 10;
    static const EthernetSegmentId kZeroEsi;
    static const EthernetSegmentId kMaxEsi;

    enum EsiType {
        CONFIGURED = 0,
        LACP_BASED = 1,
        STP_BASED = 2,
        MAC_BASED = 3,
        IP_BASED = 4,
        AS_BASED = 5
    };

    EthernetSegmentId();
    EthernetSegmentId(const uint8_t *data);
    EthernetSegmentId(const EthernetSegmentId &rhs) {
        memcpy(data_, rhs.data_, kSize);
    }

    std::string ToString() const;
    static EthernetSegmentId FromString(const std::string &str,
        boost::system::error_code *errorp = NULL);

    EthernetSegmentId &operator=(const EthernetSegmentId &rhs) {
        memcpy(data_, rhs.data_, kSize);
        return *this;
    }

    bool IsZero() const { return CompareTo(EthernetSegmentId::kZeroEsi) == 0; }
    uint8_t Type() const { return data_[0]; }

    int CompareTo(const EthernetSegmentId &rhs) const;
    bool operator==(const EthernetSegmentId &rhs) const {
        return CompareTo(rhs) == 0;
    }
    bool operator!=(const EthernetSegmentId &rhs) const {
        return CompareTo(rhs) != 0;
    }
    bool operator<(const EthernetSegmentId &rhs) const {
        return CompareTo(rhs) < 0;
    }
    bool operator>(const EthernetSegmentId &rhs) const {
        return CompareTo(rhs) > 0;
    }

    const uint8_t *GetData() const { return data_; }

private:
    uint8_t data_[kSize];
};

#endif  // SRC_NET_ESI_H_
