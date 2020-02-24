/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_EXTENDED_COMMUNITY_SUB_CLUSTER_H_
#define SRC_BGP_EXTENDED_COMMUNITY_SUB_CLUSTER_H_

#include <boost/array.hpp>
#include <boost/system/error_code.hpp>

#include <string>

#include "bgp/bgp_common.h"
#include "bgp/extended-community/types.h"
#include "base/parse_object.h"

class SubCluster {
public:
    static const int kSize = 8;
    static SubCluster null_sub_cluster;
    typedef boost::array<uint8_t, kSize> bytes_type;

    SubCluster();
    explicit SubCluster(const bytes_type &data);
    SubCluster(as_t asn, const uint32_t id);

    bool IsNull() const { return operator==(SubCluster::null_sub_cluster); }
    uint8_t Type() const { return data_[0]; }
    uint8_t Subtype() const { return data_[1]; }

    bool operator<(const SubCluster &rhs) const {
        return data_ < rhs.data_;
    }
    bool operator>(const SubCluster &rhs) const {
        return data_ > rhs.data_;
    }
    bool operator==(const SubCluster &rhs) const {
        return data_ == rhs.data_;
    }
    bool operator!=(const SubCluster &rhs) const {
        return data_ != rhs.data_;
    }

    const bytes_type &GetExtCommunity() const { return data_; }
    const uint64_t GetExtCommunityValue() const {
        return get_value(data_.begin(), 8);
    }

    std::string ToString() const;
    static SubCluster FromString(const std::string &str,
        boost::system::error_code *error = NULL);
    uint32_t GetId() const;
    uint32_t GetAsn() const;

private:
    bytes_type data_;
};

#endif  // SRC_BGP_EXTENDED_COMMUNITY_SUB_CLUSTER_H_
