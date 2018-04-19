/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_cfg_listener_h
#define vnsw_cfg_listener_h

#include <map>

class IFMapNode;
class AgentDBTable;
class DB;

class CfgDBState : public DBState {
public:
    CfgDBState() : notify_count_(0), uuid_(boost::uuids::nil_uuid()) { };
    uint32_t notify_count_;
    boost::uuids::uuid uuid_;
};

class CfgListener {
public:
    CfgListener(DB *database);
    virtual ~CfgListener() { }

    // Data to register a DBTable listener
    struct CfgTableListenerInfo {
        AgentDBTable *table_;
        int need_property_id_;
    };

    // Data to register to non-DBTable listener
    typedef boost::function<void(IFMapNode *)> NodeListenerCb;
    struct CfgListenerInfo {
        NodeListenerCb cb_;
        int need_property_id_;
    };

    // DBTable to listner-id map
    typedef std::map<DBTableBase *, DBTableBase::ListenerId> CfgListenerIdMap;
    // Map of IFNode object name to DBTable
    typedef std::map<std::string, CfgTableListenerInfo> CfgListenerMap;
    // Map of IFNode to listener callback info
    typedef std::map<std::string, CfgListenerInfo> CfgListenerCbMap;

    void NodeListener(DBTablePartBase *partition, DBEntryBase *dbe);
    void LinkListener(DBTablePartBase *partition, DBEntryBase *dbe);
    void NodeCallback(DBTablePartBase *partition, DBEntryBase *dbe);
    // Regsiter for a IFMap link
    void LinkRegister(const std::string &link_mdata, AgentDBTable *table);
    // Register DBTable for a IFMapNode
    void Register(const std::string &id_type, AgentDBTable *table,
                  int need_property_id);
    // Register callback function for a IFMapNode
    void Register(const std::string &id_type, NodeListenerCb callback,
                  int need_property_id);
    void Unregister(std::string type);

    AgentDBTable *GetLinkOperDBTable(IFMapNode *node);
    AgentDBTable *GetOperDBTable(IFMapNode *node);
    NodeListenerCb GetCallback(IFMapNode *node);
    void NodeReSync(IFMapNode *node);
    void Init();
    void Shutdown();

    bool CanUseNode(IFMapNode *node);
    bool CanUseNode(IFMapNode *node, IFMapAgentTable *table);
    bool SkipNode(IFMapNode *node);
    bool SkipNode(IFMapNode *node, IFMapAgentTable *table);

    bool GetCfgDBStateUuid(IFMapNode *node, boost::uuids::uuid &id);

    // Callback invoked for each IFMap neighbor node
    typedef boost::function<void(const Agent *agent, const char *name,
                                 IFMapNode *node, AgentKey *key,
                                 AgentData *data)>
        IFMapNodeCb;
    // Iterate thru all IFMap neighbor nodes and invoke callback for each
    // neighbor of given type
    uint32_t ForEachAdjacentIFMapNode(const Agent *agent, IFMapNode *node,
                                      const char *name, AgentKey *key,
                                      AgentData *data, IFMapNodeCb cb);

    IFMapNode *FindAdjacentIFMapNode(const Agent *agent, IFMapNode *node,
                                     const char *name);
private:
    void UpdateSeenState(DBTableBase *table, DBEntryBase *dbe,
                         CfgDBState *state, DBTableBase::ListenerId id);
    void LinkNotify(IFMapLink *link, IFMapNode *node, IFMapNode *peer,
                    const std::string &peer_type, CfgDBState *state,
                    DBTableBase::ListenerId id);
    CfgDBState *GetCfgDBState(IFMapTable *table, DBEntryBase *dbe,
                              DBTableBase::ListenerId &id);

    DB *database_;

    CfgListenerIdMap cfg_listener_id_map_;
    CfgListenerMap cfg_listener_map_;
    CfgListenerMap cfg_link_listener_map_;
    CfgListenerCbMap cfg_listener_cb_map_;
    void NodeNotify(AgentDBTable *oper_table, IFMapNode *node);
    DISALLOW_COPY_AND_ASSIGN(CfgListener);
};

#endif
