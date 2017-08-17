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
class AgentKsyncSandesh {
public:
    AgentKsyncSandesh(const std::string &context);
    virtual ~AgentKsyncSandesh() { }
    void DoKsyncSandesh(AgentKsyncSandeshPtr sandesh);
    void DoKsyncSandeshInternal(AgentKsyncSandeshPtr sandesh);
    void SetResp();
    virtual bool UpdateResp(KSyncEntry *entry) = 0;
    virtual KSyncDBObject *AgentGetKsyncObject() = 0;
    virtual void Alloc() = 0;

protected:
    SandeshResponse *resp_;
    std::string context_;
    const std::string name_;
    Agent *agent_;
};

////////////////////////////
//// ksync interface //////
///////////////////////////
class AgentKsyncIntfSandesh : public AgentKsyncSandesh {
public:
    AgentKsyncIntfSandesh(const std::string &context);
    ~AgentKsyncIntfSandesh() { }
    bool UpdateResp(KSyncEntry *entry) ;
    KSyncDBObject *AgentGetKsyncObject() ;
    void Alloc() ;

private:
    friend class KSyncItfReq;
};

////////////////////////////
//// ksync NextHopList //////
///////////////////////////
class AgentKsyncNhListSandesh : public AgentKsyncSandesh {
public:
    AgentKsyncNhListSandesh(const std::string &context) ;
    ~AgentKsyncNhListSandesh() { }
    bool UpdateResp(KSyncEntry *entry) ;
    KSyncDBObject *AgentGetKsyncObject() ;
    void Alloc() ;

private:
    friend class KSyncNhListReq;
};
#endif /*ksync_agent_sandesh_h_*/
