/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_QOS_CONFIG_H
#define __AGENT_OPER_QOS_CONFIG_H

#include <cmn/agent.h>
#include <oper_db.h>
#include <cmn/index_vector.h>

class Agent;
class DB;
class AgentQosConfigTable;
class AgentQosConfigData;

struct AgentQosConfigKey : public AgentOperDBKey {
    AgentQosConfigKey(const boost::uuids::uuid &uuid):
        uuid_(uuid) {}

    AgentQosConfigKey(const AgentQosConfigKey &rhs):
        uuid_(rhs.uuid_) {}

    bool IsLess(const AgentQosConfigKey &rhs) const {
        return uuid_ < rhs.uuid_;
    }

    boost::uuids::uuid uuid_;
};

typedef std::pair<uint32_t, uint32_t> AgentQosIdForwardingClassPair;
typedef std::map<uint32_t, uint32_t> AgentQosIdForwardingClassMap;

class AgentQosConfig :
    AgentRefCount<AgentQosConfig>, public AgentOperDBEntry {
public:
    static const uint32_t kDefaultQosMsgSize = 4098;
    typedef enum {
        VHOST,
        FABRIC,
        DEFAULT
    } Type;
    typedef std::pair<uint32_t, uint32_t> QosIdForwardingClassPair;
    typedef std::map<uint32_t, uint32_t> QosIdForwardingClassMap;

    AgentQosConfig(const boost::uuids::uuid uuid);
    virtual ~AgentQosConfig();

    KeyPtr GetDBRequestKey() const;
    std::string ToString() const;
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual bool DBEntrySandesh(Sandesh *resp, std::string &name) const;

    virtual bool Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual void SetKey(const DBRequestKey *key);

    virtual bool DeleteOnZeroRefCount() const {
        return false;
    }
    virtual void OnZeroRefCount() {};
    uint32_t GetRefCount() const {
        return AgentRefCount<AgentQosConfig>::GetRefCount();
    }

    boost::uuids::uuid uuid() const {
        return uuid_;
    }

    void set_id(uint32_t id) {
        id_ = id;
    }
    uint32_t id() const {
        return id_;
    }

    std::string name() const {
        return name_;
    }

    const QosIdForwardingClassMap& dscp_map() const {
        return dscp_map_;
    }

    const QosIdForwardingClassMap& vlan_priority_map() const {
        return vlan_priority_map_;
    }

    const QosIdForwardingClassMap& mpls_exp_map() const {
        return mpls_exp_map_;
    }

    Type type() const {
        return type_;
    }

    uint32_t default_forwarding_class() const {
        return default_forwarding_class_;
    }

    int MsgLen() { return kDefaultQosMsgSize; }

private:
    bool VerifyLinkToGlobalQosConfig(const Agent *agent,
                                     const AgentQosConfigData *data);
    void HandleVhostQosConfig(const Agent *agent,
                              const AgentQosConfigData *data, bool deleted);
    void HandleFabricQosConfig(const Agent *agent,
                               const AgentQosConfigData *data, bool deleted);
    void HandleGlobalQosConfig(const AgentQosConfigData *data);
    bool HandleQosForwardingMapChange(const Agent *agent,
                                      QosIdForwardingClassMap &map,
                                      const AgentQosIdForwardingClassMap &data_map);
    boost::uuids::uuid uuid_;
    std::string name_;
    uint32_t id_;
    QosIdForwardingClassMap dscp_map_;
    QosIdForwardingClassMap vlan_priority_map_;
    QosIdForwardingClassMap mpls_exp_map_;
    Type type_;
    uint32_t default_forwarding_class_;
    DISALLOW_COPY_AND_ASSIGN(AgentQosConfig);
};

struct AgentQosConfigData : public AgentOperDBData {
    AgentQosConfigData(const Agent *agent, IFMapNode *node) :
    AgentOperDBData(agent, node), default_forwarding_class_(0) {}

    std::string name_;
    AgentQosIdForwardingClassMap dscp_map_;
    AgentQosIdForwardingClassMap vlan_priority_map_;
    AgentQosIdForwardingClassMap mpls_exp_map_;
    AgentQosConfig::Type type_;
    uint32_t default_forwarding_class_;
};

class AgentQosConfigTable : public AgentOperDBTable {
public:
    typedef std::map<std::string, AgentQosConfig*> AgentQosConfigNameMap;
    typedef std::pair<std::string, AgentQosConfig*> AgentQosConfigNamePair;

    AgentQosConfigTable(Agent *agent, DB *db, const std::string &name);
    virtual ~AgentQosConfigTable();
    static const uint32_t kInvalidIndex=0xFFFF;
    static const uint32_t kDscpEntries = 63;
    static const uint32_t k801pEntries = 7;
    static const uint32_t kExpEntries = 7;


    static DBTableBase *CreateTable(Agent *agent, DB *db,
                                    const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;

    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
                             const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    virtual bool ProcessConfig(IFMapNode *node, DBRequest &req,
                               const boost::uuids::uuid &u);
    AgentQosConfigData* BuildData(IFMapNode *node);
    void ReleaseIndex(AgentQosConfig *qc);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    const AgentQosConfig* FindByName(const std::string &name) {
        AgentQosConfigNameMap::const_iterator it = name_map_.find(name);
        if (it == name_map_.end()) {
            return NULL;
        }
        return it->second;
    }

    const AgentQosConfig* FindByIndex(uint32_t idx) const {
        return index_table_.At(idx);
    }

    void EraseFabricQosConfig(const boost::uuids::uuid &uuid) {
        fabric_qos_config_uuids_.erase(uuid);
    }

    void InsertFabricQosConfig(const boost::uuids::uuid &uuid) {
        fabric_qos_config_uuids_.insert(uuid);
    }

    const boost::uuids::uuid GetActiveFabricQosConfig() {
        std::set<boost::uuids::uuid>::const_iterator it =
            fabric_qos_config_uuids_.begin();
        if (it == fabric_qos_config_uuids_.end()) {
            return boost::uuids::nil_uuid();
        }
        return *it;
    }

    void EraseVhostQosConfig(const boost::uuids::uuid &uuid) {
        fabric_qos_config_uuids_.erase(uuid);
    }

    void InsertVhostQosConfig(const boost::uuids::uuid &uuid) {
        fabric_qos_config_uuids_.insert(uuid);
    }

    const boost::uuids::uuid GetActiveVhostQosConfig() {
        std::set<boost::uuids::uuid>::const_iterator it =
            fabric_qos_config_uuids_.begin();
        if (it == fabric_qos_config_uuids_.end()) {
            return boost::uuids::nil_uuid();
        }
        return *it;
    }
private:
    AgentQosConfigNameMap name_map_;
    std::set<boost::uuids::uuid> vhost_qos_config_uuids_;
    std::set<boost::uuids::uuid> fabric_qos_config_uuids_;
    IndexVector<AgentQosConfig> index_table_;
    DISALLOW_COPY_AND_ASSIGN(AgentQosConfigTable);
};
#endif
