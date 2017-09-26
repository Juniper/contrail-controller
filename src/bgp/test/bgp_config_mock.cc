/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config.h"

#include "bgp/bgp_factory.h"

using std::make_pair;

class BgpMockConfigManager : public BgpConfigManager {
 public:
    explicit BgpMockConfigManager(BgpServer *server)
            : BgpConfigManager(server),
              localname_("bgp-mock-config-manager") {
    }
    void Terminate() {
    }

    virtual const std::string &localname() const {
        return localname_;
    }

    virtual InstanceMapRange InstanceMapItems(
        const std::string &name = std::string()) const {
        return make_pair(instance_map_.lower_bound(name), instance_map_.end());
    }

    virtual RoutingPolicyMapRange RoutingPolicyMapItems(
        const std::string &name = std::string()) const {
        return make_pair(routing_policy_map_.lower_bound(name),
                         routing_policy_map_.end());
    }

    virtual NeighborMapRange NeighborMapItems(
        const std::string &instance_name) const {
        return make_pair(neighbor_map_.begin(), neighbor_map_.end());
    }

    virtual int NeighborCount(const std::string &instance_name) const {
        return 0;
    }

    virtual void ResetRoutingInstanceIndexBit(int indec) {
        return;
    }

    virtual const BgpInstanceConfig *FindInstance(
        const std::string &name) const {
        return NULL;
    }

    virtual const BgpRoutingPolicyConfig *FindRoutingPolicy(
        const std::string &name) const {
        return NULL;
    }

    virtual const BgpProtocolConfig *GetProtocolConfig(
        const std::string &instance_name) const {
        return NULL;
    }

    virtual const BgpNeighborConfig *FindNeighbor(
        const std::string &instance_name, const std::string &name) const {
        return NULL;
    }

 private:
    std::string localname_;
    RoutingPolicyMap routing_policy_map_;
    InstanceMap instance_map_;
    NeighborMap neighbor_map_;
};

FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpConfigManager,
                        BgpMockConfigManager);
