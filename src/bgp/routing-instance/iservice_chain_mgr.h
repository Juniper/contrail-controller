/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_ISERVICE_CHAIN_MGR_H_
#define SRC_BGP_ROUTING_INSTANCE_ISERVICE_CHAIN_MGR_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

class RoutingInstance;
class ServiceChainConfig;
class ServiceChainGroup;
class ShowServicechainInfo;

class IServiceChainMgr {
public:
    virtual ~IServiceChainMgr() { }

    virtual void ManagedDelete() = 0;
    virtual void StopServiceChain(RoutingInstance *rtinstance) = 0;
    virtual bool LocateServiceChain(RoutingInstance *rtinstance,
        const ServiceChainConfig &config) = 0;
    virtual void UpdateServiceChain(RoutingInstance *rtinstance,
        bool group_oper_state_up) = 0;

    virtual bool ServiceChainIsUp(RoutingInstance *rtinstance) const = 0;

    virtual size_t PendingQueueSize() const = 0;
    virtual size_t ResolvedQueueSize() const = 0;
    virtual uint32_t GetDownServiceChainCount() const = 0;
    virtual bool IsQueueEmpty() const = 0;
    virtual bool FillServiceChainInfo(RoutingInstance *rtinstance,
        ShowServicechainInfo *info) const = 0;
    virtual bool ServiceChainIsPending(RoutingInstance *rtinstance,
        std::string *reason = NULL) const = 0;

private:
    template <typename U> friend class ServiceChainIntegrationTest;
    template <typename U> friend class ServiceChainTest;

    virtual ServiceChainGroup *FindServiceChainGroup(
        RoutingInstance *rtinstance) = 0;
    virtual ServiceChainGroup *FindServiceChainGroup(
        const std::string &group_name) = 0;
    virtual void set_aggregate_host_route(bool value) = 0;
    virtual void DisableResolveTrigger() = 0;
    virtual void EnableResolveTrigger() = 0;
    virtual void DisableGroupTrigger() = 0;
    virtual void EnableGroupTrigger() = 0;
    virtual void DisableQueue() = 0;
    virtual void EnableQueue() = 0;
};

#endif  // SRC_BGP_ROUTING_INSTANCE_ISERVICE_CHAIN_MGR_H_
