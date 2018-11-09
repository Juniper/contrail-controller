/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_sg_hpp
#define vnsw_agent_sg_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <oper/oper_db.h>

struct SgKey : public AgentOperDBKey {
    SgKey(boost::uuids::uuid sg_uuid) : AgentOperDBKey(), sg_uuid_(sg_uuid) {}
    virtual ~SgKey() {}

    boost::uuids::uuid sg_uuid_;
};

struct SgData : public AgentOperDBData {
    SgData(Agent *agent, IFMapNode *node, const uint32_t &sg_id,
           const boost::uuids::uuid &egress_acl_id,
           const boost::uuids::uuid &ingress_acl_id) :
                   AgentOperDBData(agent, node), sg_id_(sg_id),
                   egress_acl_id_(egress_acl_id),
                   ingress_acl_id_(ingress_acl_id) {
    }
    virtual ~SgData() { }

    uint32_t sg_id_;
    boost::uuids::uuid egress_acl_id_;
    boost::uuids::uuid ingress_acl_id_;
};

class SgEntry : AgentRefCount<SgEntry>, public AgentOperDBEntry {
public:
    SgEntry(boost::uuids::uuid sg_uuid, uint32_t sg_id) :
        sg_uuid_(sg_uuid), sg_id_(sg_id),
        egress_acl_(NULL), ingress_acl_(NULL) {}
    SgEntry(boost::uuids::uuid sg_uuid) : sg_uuid_(sg_uuid) {}
    virtual ~SgEntry() {}

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;

    const boost::uuids::uuid &GetSgUuid() const {return sg_uuid_;};
    const uint32_t &GetSgId() const {return sg_id_;};
    const AclDBEntry *GetIngressAcl() const {return ingress_acl_.get();};
    const AclDBEntry *GetEgressAcl() const {return egress_acl_.get();};
    bool IsEgressAclSet() const { return (egress_acl_ != NULL);};
    bool IsIngressAclSet() const { return (ingress_acl_ != NULL);};
    bool IsAclSet() const { return (egress_acl_ != NULL || ingress_acl_ != NULL);};

    uint32_t GetRefCount() const {
        return AgentRefCount<SgEntry>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(SandeshTraceBufferPtr ptr,
                       AgentLogEvent::type event) const;
private:
    friend class SgTable;
    boost::uuids::uuid sg_uuid_;
    uint32_t sg_id_;
    AclDBEntryRef egress_acl_;
    AclDBEntryRef ingress_acl_;
    DISALLOW_COPY_AND_ASSIGN(SgEntry);
};

class SgTable : public AgentOperDBTable {
public:
    static const uint32_t kInvalidSgId = 0;
    SgTable(DB *db, const std::string &name) : AgentOperDBTable(db, name) { }
    virtual ~SgTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t  Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static SgTable *GetInstance() {return sg_table_;};

private:
    static SgTable* sg_table_;
    bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    DISALLOW_COPY_AND_ASSIGN(SgTable);
};

#endif // vnsw_agent_sg_hpp
