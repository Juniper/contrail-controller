/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rtarget_prefix_h
#define ctrlplane_rtarget_prefix_h

#include <boost/system/error_code.hpp>

#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_common.h"
#include "bgp/rtarget/rtarget_address.h"

class RTargetPrefix {
public:
    RTargetPrefix();
    explicit RTargetPrefix(const BgpProtoPrefix &prefix);
    RTargetPrefix(as4_t as, RouteTarget rtarget) 
        : as_(as), rtarget_(rtarget) {
    }
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


#endif
