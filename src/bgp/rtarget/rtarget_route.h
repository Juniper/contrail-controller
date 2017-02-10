/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_RTARGET_RTARGET_ROUTE_H_
#define SRC_BGP_RTARGET_RTARGET_ROUTE_H_

#include <string>
#include <vector>

#include "base/util.h"
#include "bgp/bgp_route.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/rtarget/rtarget_prefix.h"
#include "net/bgp_af.h"

class BgpAttr;
class BgpProtoPrefix;

class RTargetRoute : public BgpRoute {
public:
    explicit RTargetRoute(const RTargetPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;

    virtual std::string ToString() const;

    const RTargetPrefix &GetPrefix() const {
        return prefix_;
    }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);
    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  const BgpAttr *attr = NULL,
                                  uint32_t label = 0,
                                  uint32_t l3_label = 0) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
                                      IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const RTargetRoute &rhs = static_cast<const RTargetRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::IPv4; }
    virtual u_int8_t Safi() const { return BgpAf::RTarget; }

private:
    RTargetPrefix prefix_;
    DISALLOW_COPY_AND_ASSIGN(RTargetRoute);
};

#endif  // SRC_BGP_RTARGET_RTARGET_ROUTE_H_
