/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_BRIDGE_DOMAIN_H_
#define SRC_VNSW_AGENT_BRIDGE_DOMAIN_H_

#include <boost/scoped_ptr.hpp>
#include <cmn/agent.h>
#include <oper_db.h>

class BridgeDomainTable;

struct BridgeDomainKey : public AgentOperDBKey {
    BridgeDomainKey(const boost::uuids::uuid &id) :
        AgentOperDBKey(), uuid_(id) { }
    BridgeDomainKey(const boost::uuids::uuid &id, DBSubOperation sub_op) :
        AgentOperDBKey(sub_op), uuid_(id) { }
    virtual ~BridgeDomainKey() { }

    boost::uuids::uuid uuid_;
};

struct BridgeDomainData : public AgentOperDBData {
    BridgeDomainData(Agent *agent, IFMapNode *node):
        AgentOperDBData(agent, node), name_(""),
        vn_uuid_(boost::uuids::nil_uuid()), isid_(0),
        learning_enabled_(false), bmac_vrf_name_(""),
        pbb_etree_enabled_(false), mac_aging_time_(0) {}

    std::string name_;
    boost::uuids::uuid vn_uuid_;
    uint32_t isid_;
    bool learning_enabled_;
    std::string bmac_vrf_name_;
    bool pbb_etree_enabled_;
    uint32_t mac_aging_time_;
};

class BridgeDomainEntry : AgentRefCount<BridgeDomainEntry>,
                          public AgentOperDBEntry {
public:
    BridgeDomainEntry(const BridgeDomainTable *table,
                      const boost::uuids::uuid &id);
    virtual ~BridgeDomainEntry() {}

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    bool DBEntrySandesh(Sandesh *resp, std::string &name) const;
    bool Change(const BridgeDomainTable *table, const BridgeDomainData *data);
    void Delete();

    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &name() const { return name_; }
    const VnEntry* vn() const {
        return vn_.get();
    }

    uint32_t isid() const {
        return isid_;
    }

    uint32_t GetRefCount() const {
        return AgentRefCount<BridgeDomainEntry>::GetRefCount();
    }

    const VrfEntry* vrf() const {
        return vrf_.get();
    }

    bool learning_enabled() const {
        return learning_enabled_;
    }

    bool pbb_etree_enabled() const {
        return pbb_etree_enabled_;
    }

    bool mac_aging_time() const {
        return mac_aging_time_;
    }

    bool layer2_control_word() const {
        return layer2_control_word_;
    }

private:
    friend class BridgeDomainTable;
    void UpdateVrf(const BridgeDomainData *data);

    const BridgeDomainTable *table_;
    boost::uuids::uuid uuid_;
    std::string name_;
    VnEntryRef vn_;
    uint32_t isid_;
    VrfEntryRef vrf_;
    std::string bmac_vrf_name_;
    bool learning_enabled_;
    bool pbb_etree_enabled_;
    uint32_t mac_aging_time_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(BridgeDomainEntry);
};

class BridgeDomainTable : public AgentOperDBTable {
public:
    BridgeDomainTable(Agent *agent, DB *db, const std::string &name);
    virtual ~BridgeDomainTable();

    static DBTableBase *CreateTable(Agent *agent, DB *db,
                                    const std::string &name);
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
                             const boost::uuids::uuid &u);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
                       const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    BridgeDomainEntry* Find(const boost::uuids::uuid &u);
private:
    DISALLOW_COPY_AND_ASSIGN(BridgeDomainTable);
};
#endif
