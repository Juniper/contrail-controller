/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_sg_hpp
#define vnsw_agent_sg_hpp

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <cmn/agent_cmn.h>
#include <oper/agent_types.h>
#include <filter/acl.h>

using namespace boost::uuids;
using namespace std;

struct SgKey : public AgentKey {
    SgKey(uuid sg_uuid) : AgentKey(), sg_uuid_(sg_uuid) {} ;
    virtual ~SgKey() { };

    uuid sg_uuid_;
};

struct SgData : public AgentData {
    SgData(const uint32_t &sg_id, const uuid &acl_id) :
           AgentData(), sg_id_(sg_id), acl_id_(acl_id) {
    };
    virtual ~SgData() { };

    uint32_t sg_id_;
    uuid acl_id_;
};

class SgEntry : AgentRefCount<SgEntry>, public AgentDBEntry {
public:
    SgEntry(uuid sg_uuid, uint32_t sg_id) : sg_uuid_(sg_uuid), sg_id_(sg_id), 
                                            acl_(NULL) { };
    SgEntry(uuid sg_uuid) : sg_uuid_(sg_uuid) { };
    virtual ~SgEntry() { };

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;

    const uuid &GetSgUuid() const {return sg_uuid_;};
    const uint32_t &GetSgId() const {return sg_id_;};
    const AclDBEntry *GetAcl() const {return acl_.get();};
    bool IsAclSet() const { return (acl_ != NULL);};

    AgentDBTable *DBToTable() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<SgEntry>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(AgentLogEvent::type event) const;
private:
    friend class SgTable;
    uuid sg_uuid_;
    uint32_t sg_id_;
    AclDBEntryRef acl_;
    DISALLOW_COPY_AND_ASSIGN(SgEntry);
};

class SgTable : public AgentDBTable {
public:
    SgTable(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~SgTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t  Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

private:
    bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    DISALLOW_COPY_AND_ASSIGN(SgTable);
};

#endif // vnsw_agent_sg_hpp
