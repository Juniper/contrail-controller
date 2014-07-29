/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VNSW_AGENT_OPER_LOADBALANCER_PROPERTIES_H__
#define VNSW_AGENT_OPER_LOADBALANCER_PROPERTIES_H__

#include <map>
#include <boost/uuid/uuid.hpp>
#include "schema/vnc_cfg_types.h"

class LoadbalancerProperties {
public:
    typedef std::map<boost::uuids::uuid, autogen::LoadbalancerMemberType>
            MemberMap;
    typedef std::map<boost::uuids::uuid,
            autogen::LoadbalancerHealthmonitorType> HealthmonitorMap;
    LoadbalancerProperties();
    ~LoadbalancerProperties();

    int CompareTo(const LoadbalancerProperties &rhs) const;

    const autogen::LoadbalancerPoolType &pool_properties() const {
        return pool_;
    }

    void set_pool_properties(const autogen::LoadbalancerPoolType &pool) {
        pool_ = pool;
    }

    const boost::uuids::uuid &vip_uuid() const {
        return vip_uuid_;
    }

    void set_vip_uuid(const boost::uuids::uuid &uuid) {
        vip_uuid_ = uuid;
    }

    const autogen::VirtualIpType &vip_properties() const {
        return vip_;
    }

    void set_vip_properties(const autogen::VirtualIpType &vip) {
        vip_ = vip;
    }

    std::string DiffString(const LoadbalancerProperties *current) const;

private:
    autogen::LoadbalancerPoolType pool_;
    boost::uuids::uuid vip_uuid_;
    autogen::VirtualIpType vip_;
    MemberMap members_;
    HealthmonitorMap monitors_;
};

#endif  // VNSW_AGENT_OPER_LOADBALANCER_PROPERTIES_H__
