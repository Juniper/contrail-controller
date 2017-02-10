/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_RTARGET_RTARGET_PREFIX_H_
#define SRC_BGP_RTARGET_RTARGET_PREFIX_H_

#include <boost/system/error_code.hpp>

#include <string>

#include "bgp/bgp_attr.h"
#include "bgp/bgp_common.h"
#include "bgp/rtarget/rtarget_address.h"

class RTargetPrefix {
public:
    static const std::string kDefaultPrefixString;

    RTargetPrefix();
    explicit RTargetPrefix(const BgpProtoPrefix &prefix);
    RTargetPrefix(as4_t as, RouteTarget rtarget)
        : as_(as), rtarget_(rtarget) {
    }

    static int FromProtoPrefix(const BgpProtoPrefix &proto_prefix,
                               RTargetPrefix *prefix);
    static int FromProtoPrefix(BgpServer *server,
                               const BgpProtoPrefix &proto_prefix,
                               const BgpAttr *attr, RTargetPrefix *prefix,
                               BgpAttrPtr *new_attr, uint32_t *label,
                               uint32_t *l3_label);
    static RTargetPrefix FromString(const std::string &str,
                                    boost::system::error_code *errorp = NULL);

    std::string ToString() const;
    RouteTarget rtarget() const { return rtarget_; }
    as4_t as() const { return as_; }
    void BuildProtoPrefix(BgpProtoPrefix *prefix) const;
    int CompareTo(const RTargetPrefix &rhs) const;
    bool operator==(const RTargetPrefix &rhs) const {
        return (CompareTo(rhs) == 0);
    }

private:
    as4_t as_;
    RouteTarget rtarget_;
};

#endif  // SRC_BGP_RTARGET_RTARGET_PREFIX_H_
