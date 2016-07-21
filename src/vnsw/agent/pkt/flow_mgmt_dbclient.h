/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AGENT_FLOW_MGMT_DBCLIENT_H__
#define __AGENT_FLOW_MGMT_DBCLIENT_H__

#include "pkt/flow_mgmt.h"

class EcmpLoadBalance;
////////////////////////////////////////////////////////////////////////////
// Request to the Flow Management module
////////////////////////////////////////////////////////////////////////////
class FlowMgmtDbClient {
public:
    struct FlowMgmtState : public DBState {
        FlowMgmtState() : gen_id_(0), deleted_(false) { }
        virtual ~FlowMgmtState() { }

        void IncrementGenId() { gen_id_++; }
        uint32_t gen_id_;
        bool deleted_;
    };

    struct VnFlowHandlerState : public FlowMgmtState {
        AclDBEntryConstRef acl_;
        AclDBEntryConstRef macl_;
        AclDBEntryConstRef mcacl_;
        bool enable_rpf_;
        bool flood_unknown_unicast_;
        VnFlowHandlerState(const AclDBEntry *acl, 
                           const AclDBEntry *macl,
                           const AclDBEntry *mcacl, bool enable_rpf,
                           bool flood_unknown_unicast) :
           acl_(acl), macl_(macl), mcacl_(mcacl), enable_rpf_(enable_rpf),
           flood_unknown_unicast_(flood_unknown_unicast){ }
        virtual ~VnFlowHandlerState() { }
    };

    struct VmIntfFlowHandlerState : public FlowMgmtState {
        VmIntfFlowHandlerState(const VnEntry *vn) : vn_(vn),
            vrf_assign_acl_(NULL), is_vn_qos_config_(false) { }
        virtual ~VmIntfFlowHandlerState() { }

        VnEntryConstRef vn_;
        bool policy_;
        VmInterface::SecurityGroupEntryList sg_l_;
        AclDBEntryConstRef vrf_assign_acl_;
        bool is_vn_qos_config_;
        AgentQosConfigConstRef qos_config_;
    };

    struct VrfFlowHandlerState : public FlowMgmtState {
        VrfFlowHandlerState() : deleted_(false) {}
        virtual ~VrfFlowHandlerState() {}

        // Register to all the route tables of intrest
        void Register(FlowMgmtDbClient *client, VrfEntry *vrf);
        void Unregister(FlowMgmtDbClient *client, VrfEntry *vrf);

        // Unregister from the route tables
        bool Unregister(VrfEntry *vrf);

        DBTableBase::ListenerId GetListenerId(Agent::RouteTableType type) {
            if (type == Agent::INET4_UNICAST)
                return inet_listener_id_;
            if (type == Agent::INET6_UNICAST)
                return inet6_listener_id_;
            if (type == Agent::BRIDGE)
                return bridge_listener_id_;
            assert(0);
        }
        DBTableBase::ListenerId inet_listener_id_;
        DBTableBase::ListenerId inet6_listener_id_;
        DBTableBase::ListenerId bridge_listener_id_;
        bool deleted_;
    };

    struct RouteFlowHandlerState : public FlowMgmtState {
        RouteFlowHandlerState() : sg_l_(), active_nh_(NULL), local_nh_(NULL),
            ecmp_load_balance_() {}
        virtual ~RouteFlowHandlerState() { }
        typedef std::map<InterfaceConstRef, IpAddress> FixedIpMap;
        typedef std::pair<InterfaceConstRef, IpAddress> FixedIpEntry;

        SecurityGroupList sg_l_;
        const NextHop* active_nh_;
        const NextHop* local_nh_;
        FixedIpMap fixed_ip_map_;
        EcmpLoadBalance ecmp_load_balance_;
    };

    struct NhFlowHandlerState : public FlowMgmtState {
        NhFlowHandlerState() { }
        virtual ~NhFlowHandlerState() { }
    };

    struct AclFlowHandlerState : public FlowMgmtState {
        AclFlowHandlerState() { }
        virtual ~AclFlowHandlerState() { }
    };

    FlowMgmtDbClient(Agent *agent, FlowMgmtManager *mgr);
    virtual ~FlowMgmtDbClient();

    void Init();
    void Shutdown();
    bool FreeDBState(const DBEntry *entry, uint32_t gen_id);
    void FreeVrfState(VrfEntry *vrf, uint32_t gen_id);

private:
    friend class FlowMgmtRouteTest;
    void AddEvent(const DBEntry *entry, FlowMgmtState *state);
    void DeleteEvent(const DBEntry *entry, FlowMgmtState *state);
    void ChangeEvent(const DBEntry *entry, FlowMgmtState *state);

    void FreeInterfaceState(Interface *intf, uint32_t gen_id);
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);

    void FreeVnState(VnEntry *vn, uint32_t gen_id);
    void VnNotify(DBTablePartBase *part, DBEntryBase *e);

    void FreeAclState(AclDBEntry *acl, uint32_t gen_id);
    void AclNotify(DBTablePartBase *part, DBEntryBase *e);

    void FreeNhState(NextHop *nh, uint32_t gen_id);
    void NhNotify(DBTablePartBase *part, DBEntryBase *e);

    void VrfNotify(DBTablePartBase *part, DBEntryBase *e);

    void TraceMsg(AgentRoute *entry, const AgentPath *path,
                  const SecurityGroupList &sg_list, bool deleted);
    void FreeRouteState(AgentRoute *route, uint32_t gen_id);
    void RouteNotify(VrfFlowHandlerState *vrf_state, Agent::RouteTableType type,
                     DBTablePartBase *partition, DBEntryBase *e);
    bool HandleTrackingIpChange(const AgentRoute *rt,
                                RouteFlowHandlerState *state);
    Agent *agent_;
    FlowMgmtManager *mgr_;

    DBTableBase::ListenerId acl_listener_id_;
    DBTableBase::ListenerId interface_listener_id_;
    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId vm_listener_id_;
    DBTableBase::ListenerId vrf_listener_id_;
    DBTableBase::ListenerId nh_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(FlowMgmtDbClient);
};

#endif //  __AGENT_FLOW_MGMT_DBCLIENT_H__
