/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_esi_h
#define ctrlplane_esi_h

#include <boost/system/error_code.hpp>

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

    static EthernetSegmentId FromString(const std::string &str,
        boost::system::error_code *errorp = NULL);

    EthernetSegmentId();
    EthernetSegmentId(const uint8_t *data);

    bool IsZero() { return CompareTo(EthernetSegmentId::kZeroEsi) == 0; }
    uint8_t Type() const { return data_[0]; }

    int CompareTo(const EthernetSegmentId &rhs) const;
    bool operator==(const EthernetSegmentId &rhs) const {
        return CompareTo(rhs) == 0;
    }
    bool operator<(const EthernetSegmentId &rhs) const {
        return CompareTo(rhs) < 0;
    }
    bool operator>(const EthernetSegmentId &rhs) const {
        return CompareTo(rhs) > 0;
    }

    std::string ToString() const;
    const uint8_t *GetData() const { return data_; }

private:
    uint8_t data_[kSize];
};

#endif
