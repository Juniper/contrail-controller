/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_IROUTE_AGGREGATOR_H_
#define SRC_BGP_ROUTING_INSTANCE_IROUTE_AGGREGATOR_H_

#include <stdint.h>

class BgpRoute;
class RoutingInstance;
class AggregateRouteEntriesInfo;

class IRouteAggregator {
public:
    virtual ~IRouteAggregator() { }

    virtual void Initialize() = 0;
    virtual void ProcessAggregateRouteConfig() = 0;
    virtual void UpdateAggregateRouteConfig() = 0;
    virtual void FlushAggregateRouteConfig() = 0;
    virtual uint32_t GetAggregateRouteCount() const = 0;

    virtual bool IsAggregateRoute(const BgpRoute *route) const = 0;
    virtual bool IsContributingRoute(const BgpRoute *route) const = 0;

    virtual bool FillAggregateRouteInfo(AggregateRouteEntriesInfo *info,
        bool summary) const = 0;

private:
    friend class RouteAggregatorTest;

    // Enable/Disable task triggers
    virtual void DisableRouteAggregateUpdate() = 0;
    virtual void EnableRouteAggregateUpdate() = 0;
    virtual size_t GetUpdateAggregateListSize() const = 0;

    virtual void DisableUnregResolveTask() = 0;
    virtual void EnableUnregResolveTask() = 0;
    virtual size_t GetUnregResolveListSize() const = 0;
};

#endif  // SRC_BGP_ROUTING_INSTANCE_IROUTE_AGGREGATOR_H_
