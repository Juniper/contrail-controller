/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_DB_CLIENT_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_DB_CLIENT_H_
#include "cmn/agent.h"
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/route_common.h>
#include <oper/sg.h>
#include <oper/vrf.h>
#include <filter/acl.h>
#include <oper/health_check.h>

//Mac learning mgmt DB client module listens for all notification from
//DB table and triggers change on the dependent MAC entries.
//Add or Change of DB entry:
//  Request to revaluate DB entries is enqueued to MAC mgmt module
//  which in turn RESYNC's all the mac entries
//
//Delete of DB entry:
//  Request to delete DB Entry gets queued to MAC mgmt table,
//  which in turn deletes all the mac entries dependent on this
//  DB entry.
//
//Free of DB State:
//  DB state should be release till the depedent mac entries
//  have been deleted. Hence once the mac entries are deleted
//  request to free DB state gets enqued from mac mgmt module
//  to learning module, so that it happens in exclusion to DB.
//
class MacLearningDBClient {
public:
    struct MacLearningDBState : public DBState {
        MacLearningDBState(): deleted_(false){}
       bool deleted_;
       uint32_t gen_id_;
    };

    struct MacLearningIntfState: public MacLearningDBState {
        uint32_t l2_label_;
        VmInterface::SecurityGroupEntryList sg_l_;
        bool learning_enabled_;
        bool policy_enabled_;
        bool l2_active_;
        bool mac_ip_learning_enable_;
    };

    struct MacLearningVrfState : public MacLearningDBState {
        MacLearningVrfState(): deleted_(false) {}
        void Register(MacLearningDBClient *client, VrfEntry *vrf);
        void Unregister(VrfEntry *vrf);
        bool deleted_;
        bool learning_enabled_;
        uint32_t isid_;
        DBTableBase::ListenerId bridge_listener_id_;
        DBTableBase::ListenerId evpn_listener_id_;
    };
    struct MacLearningVnState : public MacLearningDBState {
        MacLearningVnState(): deleted_(false), mac_ip_learning_enabled_(false),
                                hc_uuid_() {}
        bool deleted_;
        bool mac_ip_learning_enabled_;
        boost::uuids::uuid hc_uuid_;
    };
    struct MacLearningHealthCheckState : public MacLearningDBState {
        MacLearningHealthCheckState(): deleted_(false) {}
        bool deleted_;
        uint32_t delay_;
        uint64_t delay_usecs_;
        uint32_t timeout_;
        uint64_t timeout_usecs_;
        uint32_t max_retries_;
        bool is_hc_enable_all_target_ips;
        std::set<IpAddress> hc_target_ip_list_;
    };

    struct MacLearningRouteState : public MacLearningDBState {
    };

    MacLearningDBClient(Agent *agent);
    virtual ~MacLearningDBClient();
    void Init();
    void Shutdown();
    void FreeDBState(const DBEntry *db_entry, uint32_t gen_id);
private:
    void AddEvent(const DBEntry *entry, MacLearningDBState *state);
    void DeleteEvent(const DBEntry *entry, MacLearningDBState *state);
    void ChangeEvent(const DBEntry *entry, MacLearningDBState *state);
    void ReleaseToken(const DBEntry *entry);
    void DeleteAllMac(const DBEntry *entry, MacLearningDBState *state);
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);
    void VrfNotify(DBTablePartBase *part, DBEntryBase *e);
    void RouteNotify(MacLearningVrfState *state,
                     Agent::RouteTableType type,
                     DBTablePartBase *partition,
                     DBEntryBase *e);
    void EvpnRouteNotify(MacLearningVrfState *state,
                     Agent::RouteTableType type,
                     DBTablePartBase *partition,
                     DBEntryBase *e);
    void VnNotify(DBTablePartBase *part, DBEntryBase *e);
    void HealthCheckNotify(DBTablePartBase *part, DBEntryBase *e);
    void FreeRouteState(const DBEntry *e, uint32_t gen_id);
    void EnqueueAgingTableDelete(const VrfEntry *vrf);
    Agent *agent_;
    DBTableBase::ListenerId interface_listener_id_;
    DBTableBase::ListenerId vrf_listener_id_;
    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId hc_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningDBClient);
};
#endif
