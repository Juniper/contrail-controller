/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_cmn_test_subop_hpp
#define vnsw_agent_cmn_test_subop_hpp

#include <cmn/agent_cmn.h>

using namespace boost::uuids;
using namespace std;

class EntryC;

struct EntryKey : public AgentKey {
    EntryKey() : AgentKey(), id_(0) { };
    EntryKey(int id) : AgentKey(), id_(id) { };
    EntryKey(AgentKey::DBSubOperation sub_op, int id) : AgentKey(sub_op), id_(id) { };
    int id_;
};

struct EntryData : public AgentData {
    EntryData() : AgentData(), data_(0) { };
    EntryData(int data) : AgentData(), data_(data) { };
    int data_;
};

/////////////////////////////////////////////////////////////////////////////
// EntryC and TableC implementation
/////////////////////////////////////////////////////////////////////////////
class EntryC : AgentRefCount<EntryC>, public AgentDBEntry {
public:
    EntryC(int id) :
        id_(id), data_(-1) { };
    virtual ~EntryC() { 
        free_count_++;
    };

    bool IsLess(const DBEntry &rhs) const {
        const EntryC &entry = static_cast<const EntryC &>(rhs);
        return id_ < entry.id_;
    };

    virtual string ToString() const { return "EntryC"; };
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    int GetId() const {return id_;}

    AgentDBTable *DBToTable() const;
    uint32_t GetRefCount() const {
	return AgentRefCount<EntryC>::GetRefCount();
    };
    static void ClearCount() {free_count_ = 0;};

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const {};
    static int free_count_;

    int id_;
    int data_;
private:
    friend class TableC;
    DISALLOW_COPY_AND_ASSIGN(EntryC);
};

class TableC : public AgentDBTable {
public:
    TableC(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~TableC() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    DBEntry *Add(const DBRequest *req);
    bool OnChange(DBEntry *entry, const DBRequest *req);
    void Delete(DBEntry *entry, const DBRequest *req);

    bool Resync(DBEntry *entry, DBRequest *req);
    bool Delete(DBRequest *req);

    EntryC *FindC(int id);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void ClearCount() {
        add_count_ = 0;
        change_count_ = 0;
        resync_count_ = del_count_ = 0;
    }
    static void Register();

    static int add_count_;
    static int change_count_;

    static int resync_count_;
    static int del_count_;

private:
    DISALLOW_COPY_AND_ASSIGN(TableC);
};

#endif // vnsw_agent_cmn_test_subop_hpp
