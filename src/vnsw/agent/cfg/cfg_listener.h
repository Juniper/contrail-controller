/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_cfg_listener_h
#define vnsw_cfg_listener_h

#include <map>

class IFMapNode;
class AgentConfig;

class CfgDBState : public DBState {
public:
    CfgDBState() : notify_count_(0) { };
    bool notify_count_;
};

class CfgListener {
public:
    CfgListener(AgentConfig *cfg);
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
    // Register DBTable for a IFMapNode
    void Register(std::string id_type, AgentDBTable *table,
                  int need_property_id);
    // Register callback function for a IFMapNode
    void Register(std::string id_type, NodeListenerCb callback,
                  int need_property_id);
    void Unregister(std::string type);

    AgentDBTable *GetOperDBTable(IFMapNode *node);
    NodeListenerCb GetCallback(IFMapNode *node);
    void NodeReSync(IFMapNode *node);
    void Init();
    void Shutdown();

    bool CanUseNode(IFMapNode *node);
    bool CanUseNode(IFMapNode *node, IFMapAgentTable *table);
    bool SkipNode(IFMapNode *node);
    bool SkipNode(IFMapNode *node, IFMapAgentTable *table);
private:
    void UpdateSeenState(DBTableBase *table, DBEntryBase *dbe,
                         CfgDBState *state, DBTableBase::ListenerId id);
    void LinkNotify(IFMapNode *node, CfgDBState *state,
                    DBTableBase::ListenerId id);
    CfgDBState *GetCfgDBState(IFMapTable *table, DBEntryBase *dbe,
                              DBTableBase::ListenerId &id);

    AgentConfig *agent_cfg_;

    CfgListenerIdMap cfg_listener_id_map_;
    CfgListenerMap cfg_listener_map_;
    CfgListenerCbMap cfg_listener_cb_map_;
    void NodeNotify(AgentDBTable *oper_table, IFMapNode *node);
    DISALLOW_COPY_AND_ASSIGN(CfgListener);
};

#endif
