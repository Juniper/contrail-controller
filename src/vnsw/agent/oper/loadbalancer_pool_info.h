/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VNSW_AGENT_OPER_LBPOOL_INFO_H__
#define VNSW_AGENT_OPER_LBPOOL_INFO_H__

#include <map>
#include <boost/uuid/uuid.hpp>
#include "schema/vnc_cfg_types.h"

class LoadBalancerPoolInfo {
public:
    typedef std::map<boost::uuids::uuid, autogen::LoadbalancerMemberType>
            MemberMap;
    typedef std::map<boost::uuids::uuid,
            autogen::LoadbalancerHealthmonitorType> HealthmonitorMap;
    LoadBalancerPoolInfo();
    ~LoadBalancerPoolInfo();

    int CompareTo(const LoadBalancerPoolInfo &rhs) const;

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

    std::string DiffString(const LoadBalancerPoolInfo *current) const;

    const MemberMap &members() const {
        return members_;
    }

    MemberMap *members() {
        return &members_;
    }

    const HealthmonitorMap &healthmonitors() const {
        return monitors_;
    }

    HealthmonitorMap *healthmonitors() {
        return &monitors_;
    }

    const std::vector<autogen::KeyValuePair> &custom_attributes() const {
        return custom_attributes_;
    }

    void set_custom_attributes(const std::vector<autogen::KeyValuePair>
                               &custom_attributes) {
        custom_attributes_ = custom_attributes;
    }

private:
    autogen::LoadbalancerPoolType pool_;
    boost::uuids::uuid vip_uuid_;
    autogen::VirtualIpType vip_;
    MemberMap members_;
    HealthmonitorMap monitors_;
    std::vector<autogen::KeyValuePair> custom_attributes_;
};

#endif  // VNSW_AGENT_OPER_LBPOOL_INFO_H__
