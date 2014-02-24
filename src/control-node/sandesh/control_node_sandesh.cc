/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "control-node/control_node.h"
#include "control-node/sandesh/control_node_types.h"
#include "discovery/client/discovery_client.h"
#include "discovery_client_stats_types.h"

void ShutdownControlNode::HandleRequest() const {
    ControlNodeShutdown();
}
           
void DiscoveryClientSubscriberStatsReq::HandleRequest() const {

    DiscoveryClientSubscriberStatsResponse *resp =
        new DiscoveryClientSubscriberStatsResponse();
    resp->set_context(context());

    std::vector<DiscoveryClientSubscriberStats> stats_list;
    DiscoveryServiceClient *ds = ControlNode::GetControlNodeDiscoveryServiceClient();
    if (ds) {
        ds->FillDiscoveryServiceSubscriberStats(stats_list);
    }
 
    resp->set_subscriber(stats_list);
    resp->set_more(false);
    resp->Response();
}

void DiscoveryClientPublisherStatsReq::HandleRequest() const {

    DiscoveryClientPublisherStatsResponse *resp =
        new DiscoveryClientPublisherStatsResponse();
    resp->set_context(context());

    std::vector<DiscoveryClientPublisherStats> stats_list;
    DiscoveryServiceClient *ds = ControlNode::GetControlNodeDiscoveryServiceClient();
    if (ds) {
        ds->FillDiscoveryServicePublisherStats(stats_list);
    }

    resp->set_publisher(stats_list);
    resp->set_more(false);
    resp->Response();
}

