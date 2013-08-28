/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONTROLLER_VRF_EXPORT_H__
#define __CONTROLLER_VRF_EXPORT_H__

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <oper/vrf.h>

class Inet4RouteExport;
class VrfExport {
public:
    struct State : DBState {
        State() : DBState(), exported_(false), force_chg_(false),
                  inet4_uc_walkid_(DBTableWalker::kInvalidWalkerId),
                  inet4_mc_walkid_(DBTableWalker::kInvalidWalkerId) {};
        ~State() {
            DBTableWalker *walker = Agent::GetDB()->GetWalker();

            if (inet4_uc_walkid_ != DBTableWalker::kInvalidWalkerId) {
                walker->WalkCancel(inet4_uc_walkid_);
            }

            if (inet4_mc_walkid_ != DBTableWalker::kInvalidWalkerId) {
                walker->WalkCancel(inet4_mc_walkid_);
            }
        };

        bool exported_;
        bool force_chg_;
        Inet4RouteExport *inet4_unicast_export_;
        Inet4RouteExport *inet4_multicast_export_;
        DBTableWalker::WalkId inet4_uc_walkid_;
        DBTableWalker::WalkId inet4_mc_walkid_;
    };

    static void Notify(AgentXmppChannel *, 
                       DBTablePartBase *partition, DBEntryBase *e);
};

#endif // __CONTROLLER_VRF_EXPORT_H__
