/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef hbf_agent_oper_hpp
#define hbf_agent_oper_hpp

#include <netinet/in.h>
#include <net/ethernet.h>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/vn.h>
#include <oper/agent_route_walker.h>

extern SandeshTraceBufferPtr HBFTraceBuf;

#define HBFTRACE(obj, ...)                                                        \
do {                                                                             \
    HBF##obj::TraceMsg(HBFTraceBuf, __FILE__, __LINE__, __VA_ARGS__);       \
} while (false)

struct HBFIntfDBState : DBState {
    HBFIntfDBState(bool lintf, std::string projname) :
        lintf_(lintf), projname_(projname){}
    AgentRouteWalkerPtr vrf_walker_;
    bool lintf_; //Is left interface?
    std::string projname_;
};

class HBFHandler {
public:
    HBFHandler(Agent *agent);
    virtual ~HBFHandler() {
    }

    //Registered for VMI notification
    void ModifyVmInterface(DBTablePartBase *partition, DBEntryBase *e);
    void Register();
    bool IsHBFLInterface(VmInterface *vm_itf);
    bool IsHBFRInterface(VmInterface *vm_itf);

    void Terminate();
    const Agent *agent() const {return agent_;}

private:
    Agent *agent_;

    DBTable::ListenerId interface_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(HBFHandler);
};

class HBFVrfWalker : public AgentRouteWalker {
public:
    HBFVrfWalker(const std::string &name, Agent *agent);
    virtual ~HBFVrfWalker();

    void Start(uint32_t hbf_intf_, bool hbf_lintf_, std::string projname);
    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

    static void WalkDone(HBFVrfWalker *walker) {
        LOG(ERROR, "HBF, walk done, releasing the walker");
        walker->mgr()->ReleaseWalker(walker);
    }

private:
    uint32_t hbf_intf_;
    bool hbf_lintf_;
    std::string projname_;
    DISALLOW_COPY_AND_ASSIGN(HBFVrfWalker);
};
#endif
