/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ksync_agent_sandesh_h_
#define ksync_agent_sandesh_h_

#include <oper/agent_sandesh.h>
#include "interface_ksync.h"
#include <cmn/agent_cmn.h>

class AgentKsyncSandesh;
typedef class boost::shared_ptr<AgentKsyncSandesh> AgentKsyncSandeshPtr;
class AgentKsyncSandesh : public AgentSandesh {
public:
    AgentKsyncSandesh(const std::string &context); 
    ~AgentKsyncSandesh() { }
    void DoKsyncSandesh(AgentSandeshPtr sandesh);
    void DoKsyncSandeshInternal(AgentSandeshPtr sandesh);
    void SetResp();
    virtual bool UpdateResp(KSyncEntry *entry) = 0;
    virtual KSyncDBObject *AgentGetKsyncObject() = 0;
    virtual void Alloc() = 0;
};

////////////////////////////
//// ksync interface //////
///////////////////////////
class AgentKsyncIntfSandesh : public AgentKsyncSandesh {
public:
    AgentKsyncIntfSandesh(const std::string &context, 
                     const std::string &interface_id,
                     const std::string &interface_name );
    ~AgentKsyncIntfSandesh() { }
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

    bool UpdateResp(KSyncEntry *entry) ;
    KSyncDBObject *AgentGetKsyncObject() ;
    void Alloc() ;

private:
    friend class KSyncItfReq;
    Agent *agent_;
    DBTable *AgentGetTable();

    // Filters
    std::string interface_id;
    std::string interface_name;     // Key
};

////////////////////////////
//// ksync NextHopList //////
///////////////////////////
class AgentKsyncNhListSandesh : public AgentKsyncSandesh {
public:
    AgentKsyncNhListSandesh(const std::string &context) ;
    ~AgentKsyncNhListSandesh() { }
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

    bool UpdateResp(KSyncEntry *entry) ;
    KSyncDBObject *AgentGetKsyncObject() ;
    void Alloc() ;

private:
    friend class KSyncNhListReq;
    Agent *agent_;
    DBTable *AgentGetTable();

    // Filters
    std::string type;
    std::string vrf_id;     // Key
};


#endif
