/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONTROLLER_EXPORT_H__
#define __CONTROLLER_EXPORT_H__

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/nexthop.h>
#include <oper/agent_path.h>

class AgentPath;
class EcmpLoadBalance;

class RouteExport {
public:
    struct State : DBState {
        State();
        virtual ~State() {};

        bool exported_;
        bool ingress_replication_exported_; //Used by multicast
        bool fabric_multicast_exported_; //Used by multicast
        bool force_chg_;
        uint32_t label_;
        std::string vn_;
        SecurityGroupList sg_list_;
        CommunityList communities_;
        TunnelType::Type tunnel_type_;
        PathPreference path_preference_;
        //destination and source are valid for tunnel NH.
        std::string destination_;
        std::string source_;
        EcmpLoadBalance ecmp_load_balance_;

        bool Changed(const AgentRoute *route, const AgentPath *path) const;
        void Update(const AgentRoute *route, const AgentPath *path);
    };
    RouteExport(AgentRouteTable *rt);
    ~RouteExport();

    void Notify(const Agent *agent,
                AgentXmppChannel *bgp_xmpp_peer,
                bool associate,
                Agent::RouteTableType type,
                DBTablePartBase *partition,
                DBEntryBase *e);
    void ManagedDelete();
    DBTableBase::ListenerId GetListenerId() const {return id_;};
    void Unregister();
    bool DeleteState(DBTablePartBase *partition, DBEntryBase *entry);

    bool MulticastRouteCanDissociate(const AgentRoute *route);
    static void Walkdone(DBTableBase *partition, RouteExport *rt);
    static RouteExport* Init(AgentRouteTable *table, 
                             AgentXmppChannel *bgp_xmpp_peer);
private:    
    DBTableBase::ListenerId id_;
    AgentRouteTable *rt_table_;
    bool marked_delete_;
    uint32_t state_added_;
    void MulticastNotify(AgentXmppChannel *bgp_xmpp_peer, bool associate,
                DBTablePartBase *partition, DBEntryBase *e);
    void UnicastNotify(AgentXmppChannel *bgp_xmpp_peer, 
                DBTablePartBase *partition, DBEntryBase *e,
                Agent::RouteTableType type);
    void SubscribeFabricMulticast(const Agent *agent,
                                  AgentXmppChannel *bgp_xmpp_peer,
                                  AgentRoute *route,
                                  RouteExport::State *state);
    void SubscribeIngressReplication(Agent *agent,
                                     AgentXmppChannel *bgp_xmpp_peer,
                                     AgentRoute *route,
                                     RouteExport::State *state);
    LifetimeRef<RouteExport> table_delete_ref_;
};

#endif // __CONTROLLER_EXPORT_H__
