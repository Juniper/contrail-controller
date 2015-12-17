/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_IROUTE_AGGREGATOR_H_
#define SRC_BGP_ROUTING_INSTANCE_IROUTE_AGGREGATOR_H_

#include <stdint.h>

class BgpRoute;
class RoutingInstance;

class IRouteAggregator {
public:
    virtual ~IRouteAggregator() { }

    virtual void ProcessAggregateRouteConfig() = 0;
    virtual void UpdateAggregateRouteConfig() = 0;
    virtual void FlushAggregateRouteConfig() = 0;

    virtual bool IsAggregateRoute(const BgpRoute *route) const = 0;
    virtual bool IsContributingRoute(const BgpRoute *route) const = 0;
private:
    template <typename U> friend class RouteAggregateTest;
};

#endif  // SRC_BGP_ROUTING_INSTANCE_IROUTE_AGGREGATOR_H_
