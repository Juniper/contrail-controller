/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONTROLLER_EXPORT_H__
#define __CONTROLLER_EXPORT_H__

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <oper/inet4_ucroute.h>
#include <oper/inet4_mcroute.h>
#include <oper/nexthop.h>
#include <controller/controller_vrf_export.h>

class AgentPath;

class Inet4RouteExport {
public:
    struct State : DBState {
        State();
        virtual ~State() {};

        bool exported_;
        bool force_chg_;
        Ip4Address server_;
        uint32_t label_;
        std::string vn_;
        SecurityGroupList sg_list_;

        bool Changed(const AgentPath *path) const;
        void Update(const AgentPath *path);
    };

    Inet4RouteExport(Inet4RouteTable *rt_);
    Inet4RouteExport(Inet4McRouteTable *rt_);
    ~Inet4RouteExport();
    void Notify(AgentXmppChannel *bgp_xmpp_peer, bool associate,
                DBTablePartBase *partition, DBEntryBase *e);
    static Inet4RouteExport* UnicastInit(Inet4UcRouteTable *table,
                                  AgentXmppChannel *bgp_xmpp_peer); 
    static Inet4RouteExport* MulticastInit(Inet4McRouteTable *table,
                                  AgentXmppChannel *bgp_xmpp_peer); 
    void ManagedDelete();
    DBTableBase::ListenerId GetListenerId() const {return id_;};
    void Unregister();
    bool DeleteState(DBTablePartBase *partition, DBEntryBase *entry);
    static void Walkdone(DBTableBase *partition, Inet4RouteExport *rt);
private:
    DBTableBase::ListenerId id_;
    Inet4RouteTable *rt_table_;
    bool marked_delete_;
    uint32_t state_added_;
    void MulticastNotify(AgentXmppChannel *bgp_xmpp_peer, bool associate,
                DBTablePartBase *partition, DBEntryBase *e);
    void UnicastNotify(AgentXmppChannel *bgp_xmpp_peer, 
                DBTablePartBase *partition, DBEntryBase *e);
    LifetimeRef<Inet4RouteExport> table_delete_ref_;
};

#endif // __CONTROLLER_EXPORT_H__
