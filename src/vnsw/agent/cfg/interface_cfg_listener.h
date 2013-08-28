/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_cfg_listener_hpp
#define vnsw_agent_interface_cfg_listener_hpp

#include <cmn/agent_cmn.h>
#include <oper/vn.h>
#include <cfg/interface_cfg.h>
#include <cfg/init_config.h>

using namespace boost::uuids;

class InterfaceCfgClient {
public:
    // Map from intf-uuid to intf-name
    typedef std::map<uuid, IFMapNode *> UuidToIFNodeTree;
    typedef std::pair<uuid, IFMapNode *> UuidIFNodePair;

    InterfaceCfgClient() { };
    virtual ~InterfaceCfgClient() { };

    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void CfgNotify(DBTablePartBase *partition, DBEntryBase *e);
    IFMapNode *UuidToIFNode(const uuid &u);
    static void Init();
    static void Shutdown();
private:
    struct CfgState : DBState {
        bool seen_;
    };

    static InterfaceCfgClient *singleton_;
    DBTableBase::ListenerId cfg_listener_id_;
    UuidToIFNodeTree uuid_ifnode_tree_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceCfgClient);
};

#endif // vnsw_agent_interface_cfg_listener_hpp
