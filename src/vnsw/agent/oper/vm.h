/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_hpp
#define vnsw_agent_vm_hpp

#include <cmn/agent_cmn.h>
#include <agent_types.h>

using namespace boost::uuids;
using namespace std;

struct VmKey : public AgentKey {
    VmKey(uuid id) : AgentKey(), uuid_(id) {} ;
    virtual ~VmKey() { }

    uuid uuid_;
};

struct VmData : public AgentData {
    typedef vector<uuid> SGUuidList;
    VmData(const std::string &name, const SGUuidList &sg_list) :
        AgentData(), name_(name), sg_list_(sg_list) { }
    virtual ~VmData() { }

    std::string name_;
    SGUuidList sg_list_;
};

class VmEntry : AgentRefCount<VmEntry>, public AgentDBEntry {
public:
    static const int kVectorIncreaseSize = 16;
    VmEntry(uuid id) : 
        uuid_(id), name_("") { }
    virtual ~VmEntry() { }

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;
    const string &GetCfgName() const { return name_; }
    void SetCfgName(std::string name) { name_ = name; }

    const uuid &GetUuid() const { return uuid_; }

    uint32_t GetRefCount() const {
        return AgentRefCount<VmEntry>::GetRefCount();
    }

    void SendObjectLog(AgentLogEvent::type event) const;
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
private:
    friend class VmTable;
    uuid uuid_;
    std::string name_;
    DISALLOW_COPY_AND_ASSIGN(VmEntry);
};

class VmTable : public AgentDBTable {
public:
    VmTable(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~VmTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static VmTable *GetInstance() {return vm_table_;}

private:
    static VmTable *vm_table_;
    DISALLOW_COPY_AND_ASSIGN(VmTable);
};

#endif // vnsw_agent_vm_hpp
