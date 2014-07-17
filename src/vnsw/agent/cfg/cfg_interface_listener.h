/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_cfg_listener_hpp
#define vnsw_agent_interface_cfg_listener_hpp

#include <cmn/agent_cmn.h>

using namespace boost::uuids;

class AgentConfig;

class InterfaceCfgClient {
public:
    // Map from intf-uuid to intf-name
    typedef std::map<uuid, IFMapNode *> UuidToIFNodeTree;
    typedef std::pair<uuid, IFMapNode *> UuidIFNodePair;

    InterfaceCfgClient(AgentConfig *cfg) : agent_cfg_(cfg) { };
    virtual ~InterfaceCfgClient() { };

    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void CfgNotify(DBTablePartBase *partition, DBEntryBase *e);
    void RouteTableNotify(DBTablePartBase *partition, DBEntryBase *e);
    IFMapNode *UuidToIFNode(const uuid &u);
    void Init();
    void Shutdown();
private:
    struct CfgState : DBState {
        bool seen_;
    };

    AgentConfig *agent_cfg_;
    DBTableBase::ListenerId cfg_listener_id_;
    DBTableBase::ListenerId cfg_route_table_listener_id_;
    UuidToIFNodeTree uuid_ifnode_tree_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceCfgClient);
};

#endif // vnsw_agent_interface_cfg_listener_hpp
