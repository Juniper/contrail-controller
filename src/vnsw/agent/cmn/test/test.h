/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_cmn_test_hpp
#define vnsw_agent_cmn_test_hpp

#include <cmn/agent_cmn.h>

using namespace boost::uuids;
using namespace std;

class EntryA;
class EntryB;

typedef boost::intrusive_ptr<EntryA> EntryARef;
typedef boost::intrusive_ptr<EntryB> EntryBRef;

struct EntryKey : public AgentKey {
    EntryKey() : AgentKey(), id_(0) { };
    EntryKey(int id) : AgentKey(), id_(id) { };
    int id_;
};

struct EntryData : public AgentData {
    EntryData() : AgentData(), data_(0), ref_id_(0) { };
    EntryData(int data, int ref_id) :
        AgentData(), data_(data), ref_id_(ref_id) { };
    int data_;
    int ref_id_;
};

/////////////////////////////////////////////////////////////////////////////
// EntryA and TableA implementation
/////////////////////////////////////////////////////////////////////////////
class EntryA : AgentRefCount<EntryA>, public AgentDBEntry {
public:
    EntryA(int id) :
        id_(id), data_(-1), ref_() { };
    virtual ~EntryA() { 
        if (data_ >= 0) {
            LOG(DEBUG, __PRETTY_FUNCTION__ << ": <" << id_ << " : " 
                << data_ << ">");
            free_count_++;
        }
    };

    bool IsLess(const DBEntry &rhs) const {
        const EntryA &entry = static_cast<const EntryA &>(rhs);
        return id_ < entry.id_;
    };

    virtual string ToString() const { return "EntryA"; };
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    int GetId() const {return id_;}

    AgentDBTable *DBToTable() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<EntryA>::GetRefCount();
    }
    static void ClearCount() {free_count_ = 0;};

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const {
        return false;
    }
    static int free_count_;
private:
    int id_;
    int data_;
    EntryBRef ref_;
    friend class TableA;
    DISALLOW_COPY_AND_ASSIGN(EntryA);
};

class TableA : public AgentDBTable {
public:
    TableA(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~TableA() { }

    void GetTables(DB *db) {
        b_table_ = static_cast<DBTable *>(db->FindTable("db.tablea.0"));
        assert(b_table_ != NULL);
    };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    EntryB *FindBRef(const EntryKey &key) const;

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void ClearCount() {del_count_ = 0;};
    static void Register();
    static int del_count_;

private:
    DBTable *b_table_;
    DISALLOW_COPY_AND_ASSIGN(TableA);
};

/////////////////////////////////////////////////////////////////////////////
// EntryB and TableB implementation
/////////////////////////////////////////////////////////////////////////////
class EntryB : AgentRefCount<EntryB>, public AgentDBEntry {
public:
    EntryB(int id) :
        id_(id), data_(-1) { };
    virtual ~EntryB() { 
        if (data_ >= 0) {
            LOG(DEBUG, __PRETTY_FUNCTION__ << ": <" << id_ << " : " 
                << data_ << ">");
            free_count_++;
        }
    };
    bool IsLess(const DBEntry &rhs) const {
        const EntryB &entry = static_cast<const EntryB &>(rhs);
        return id_ < entry.id_;
    };

    virtual string ToString() const { return "EntryB"; };
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    int GetId() const {return id_;}

    AgentDBTable *DBToTable() const;
    uint32_t GetRefCount() const {
      return AgentRefCount<EntryB>::GetRefCount();
    }
    static void ClearCount() {free_count_ = 0;};
    static int free_count_;

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const {
        return false;
    }

private:
    int id_;
    int data_;
    friend class TableB;
    DISALLOW_COPY_AND_ASSIGN(EntryB);
};

class TableB : public AgentDBTable {
public:
    TableB(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~TableB() { }

    void GetTables(DB *db) {
        a_table_ = static_cast<DBTable *>(db->FindTable("db.tableb.0"));
        assert(a_table_ != NULL);
    };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    EntryA *FindA(const EntryKey &key) const;

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void Register();

    static void ClearCount() {del_count_ = 0;};
    static int del_count_;
private:
    DBTable *a_table_;
    DISALLOW_COPY_AND_ASSIGN(TableB);
};

/////////////////////////////////////////////////////////////////////////////
// EntryC and TableC implementation
/////////////////////////////////////////////////////////////////////////////
struct EntryCKey : public AgentKey {
    EntryCKey() : AgentKey(), id_(0), id_a_(0) { };
    EntryCKey(int id, int id_a) : AgentKey(), id_(id), id_a_(id_a) { };
    int id_;
    int id_a_;
};

class TableC : public AgentDBTable {
public:
    TableC(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~TableC() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const {
        const EntryCKey *key = static_cast<const EntryCKey *>(k);
        EntryA *entry = new EntryA(key->id_);
        return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(entry));
    };
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req) {
        EntryCKey *key = static_cast<EntryCKey *>(req->key.get());
        EntryA *entry = new EntryA(key->id_);

        TableA *table_a =
            static_cast<TableA *>(database()->FindTable("db.tablea.0"));
        EntryKey key_a(key->id_a_);
        ref = static_cast<EntryA *>(table_a->FindActiveEntry(&key_a));

        return entry;
    }

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        EntryCKey *key = static_cast<EntryCKey *>(req->key.get());

        TableA *table_a =
            static_cast<TableA *>(database()->FindTable("db.tablea.0"));
        EntryKey key_a(key->id_a_);
        ref = static_cast<EntryA *>(table_a->FindActiveEntry(&key_a));

        return true;
    }

    virtual void Delete(DBEntry *entry, const DBRequest *req) {
        ref = NULL;
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name) {
        TableC *t = new TableC(db, name);
        t->Init();
        return t;
    };

    static void Register() {
        DB::RegisterFactory("db.tablec.0", &TableC::CreateTable);
    }

    EntryARef ref;
private:
    DISALLOW_COPY_AND_ASSIGN(TableC);
};

#endif // vnsw_agent_cmn_test_hpp
