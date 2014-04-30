/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONTROLLER_VRF_EXPORT_H__
#define __CONTROLLER_VRF_EXPORT_H__

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <oper/vrf.h>

class RouteExport;
class VrfExport {
public:
    struct State : DBState {
        State();
        ~State();
        bool exported_;
        bool force_chg_;
        RouteExport *rt_export_[Agent::ROUTE_TABLE_MAX];
    };

    static void Notify(AgentXmppChannel *, 
                       DBTablePartBase *partition, DBEntryBase *e);
};

#endif // __CONTROLLER_VRF_EXPORT_H__
