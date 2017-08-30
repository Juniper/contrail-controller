/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef tsn_elector_agent_oper_hpp
#define tsn_elector_agent_oper_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/agent_route_walker.h>

struct TsnElectorState : public DBState {
    TsnElectorState(AgentRouteTable *table, TsnElector *elector);
    virtual ~TsnElectorState();

    DBTable::ListenerId inet4_id_;
    AgentRouteTable *inet4_table_;
};

class TsnElectorWalker : public AgentRouteWalker {
public:
    TsnElectorWalker(Agent *agent);
    virtual ~TsnElectorWalker();

    void LeaveTsnMastership();
    void AcquireTsnMastership();
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    bool master_;
    DISALLOW_COPY_AND_ASSIGN(TsnElectorWalker);
};

class TsnElector {
public:
    TsnElector(Agent *agent);
    virtual ~TsnElector();

    void Register();
    void Shutdown();
    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void RouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool IsMaster() const;
    const Agent *agent() const {return agent_;}

private:
    bool IsTsnNoForwardingEnabled() const;
    TsnElectorWalker *walker() {return walker_.get();}

    const Agent *agent_;
    DBTable::ListenerId vrf_listener_id_;
    std::vector<std::string> active_tsn_servers_;
    std::auto_ptr<TsnElectorWalker> walker_;
    DISALLOW_COPY_AND_ASSIGN(TsnElector);
};

#endif /* tsn_elector_agent_oper_hpp */
