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

    void InterfaceConfigNotify(DBTablePartBase *partition, DBEntryBase *e);
    void IfMapVmiNotify(DBTablePartBase *partition, DBEntryBase *e);
    void IfMapInterfaceRouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    IFMapNode *UuidToIFNode(const uuid &u) const;

    void Init();
    void Shutdown();

private:
    struct InterfaceConfigState : DBState {
        boost::uuids::uuid vmi_uuid_;

        InterfaceConfigState(const boost::uuids::uuid &u) :
            DBState(), vmi_uuid_(u) {
            }
        virtual ~InterfaceConfigState() { }
    };

    struct VmiState : DBState {
        boost::uuids::uuid uuid_;
        VmiState(const boost::uuids::uuid &u) : uuid_(u) { }
        virtual ~VmiState() { }
    };

    void NotifyUuidAdd(Agent *agent, IFMapNode *node,
                       const boost::uuids::uuid &u) const;
    void NotifyUuidDel(Agent *agent, const boost::uuids::uuid &u) const;

    AgentConfig *agent_cfg_;
    DBTableBase::ListenerId intf_cfg_listener_id_;
    DBTableBase::ListenerId ifmap_vmi_listener_id_;
    DBTableBase::ListenerId ifmap_intf_route_listener_id_;
    UuidToIFNodeTree uuid_ifnode_tree_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceCfgClient);
};

#endif // vnsw_agent_interface_cfg_listener_hpp
