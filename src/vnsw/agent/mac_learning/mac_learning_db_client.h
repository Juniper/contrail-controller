/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __MAC_LEARNING_DB_CLIENT_H__
#define __MAC_LEARNING_DB_CLIENT_H__
#include "cmn/agent.h"
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/route_common.h>
#include <oper/sg.h>
#include <oper/vrf.h>
#include <filter/acl.h>

class MacLearningDBClient {
public:
    struct MacLearningDBState : public DBState {
        MacLearningDBState(): deleted_(false){}
       bool deleted_;
    };

    struct MacLearningIntfState: public MacLearningDBState {
        uint32_t l2_label_;
        VmInterface::SecurityGroupEntryList sg_l_;
        uint32_t learning_enabled_;
    };

    struct MacLearningVrfState : public MacLearningDBState {
        MacLearningVrfState(): deleted_(false) {}
        void Register(MacLearningDBClient *client, VrfEntry *vrf);
        void Unregister(VrfEntry *vrf);
        bool deleted_;
        bool learning_enabled_;
        DBTableBase::ListenerId bridge_listener_id_;
    };

    struct MacLearningRouteState : public MacLearningDBState {
    };

    MacLearningDBClient(Agent *agent);
    ~MacLearningDBClient();
    void Init();
    void Shutdown();
    void FreeDBState(const DBEntry *db_entry);
private:
    void AddEvent(const DBEntry *entry, MacLearningDBState *state);
    void DeleteEvent(const DBEntry *entry, MacLearningDBState *state);
    void ChangeEvent(const DBEntry *entry, MacLearningDBState *state);
    void DeleteAllMac(const DBEntry *entry, MacLearningDBState *state);
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);
    void VrfNotify(DBTablePartBase *part, DBEntryBase *e);
    void RouteNotify(MacLearningVrfState *state,
                     Agent::RouteTableType type,
                     DBTablePartBase *partition,
                     DBEntryBase *e);
    void FreeRouteState(const DBEntry *e);
    void EnqueueAgingTableDelete(const VrfEntry *vrf);
    Agent *agent_;
    DBTableBase::ListenerId interface_listener_id_;
    DBTableBase::ListenerId vrf_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningDBClient);
};
#endif
