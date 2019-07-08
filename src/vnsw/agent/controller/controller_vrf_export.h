/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONTROLLER_VRF_EXPORT_H__
#define __CONTROLLER_VRF_EXPORT_H__

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

class AgentXmppChannel;
class RouteExport;

class VrfExport {
public:
    struct State : DBState {
        State();
        ~State();
        bool exported_;
        bool mcast_exported_; //Tracks MC notofication to MC builder
        bool force_chg_;
        RouteExport *rt_export_[Agent::ROUTE_TABLE_MAX];
        uint64_t last_sequence_number_;

        // Conditions to decide if route in this VRF can be exported.
        bool IsExportable(uint64_t sequence_number);
    };

    static void Notify(const Agent *agent, AgentXmppChannel *,
                       DBTablePartBase *partition, DBEntryBase *e);
};

#endif // __CONTROLLER_VRF_EXPORT_H__
