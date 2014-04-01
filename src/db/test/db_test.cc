/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/intrusive/avl_set.hpp>
#include <boost/functional/hash.hpp>
#include <boost/bind.hpp>
#include <tbb/atomic.h>

#include "db/db.h"
#include "db/db_table.h"
#include "db/db_entry.h"
#include "db/db_client.h"
#include "db/db_partition.h"
#include "db/db_table_walker.h"

#include "base/logging.h"
#include "testing/gunit.h"

class VlanTable;

struct VlanTableReqKey : public DBRequestKey {
    VlanTableReqKey(unsigned short tag) : tag(tag) {}
    unsigned short tag;
};

struct VlanTableReqData : public DBRequestData {
    VlanTableReqData(std::string desc) : description(desc) {};
    std::string description;
};

class Vlan : public DBEntry {
public:
    Vlan(unsigned short tag) : vlan_tag(tag) { }
    Vlan(unsigned short tag, std::string desc) 
        : vlan_tag(tag), description(desc) { }

    ~Vlan() {
    }

    bool IsLess(const DBEntry &rhs) const {
        const Vlan &a = static_cast<const Vlan &>(rhs);
        return vlan_tag < a.vlan_tag;  
    }

    void SetKey(const DBRequestKey *key) {
        const VlanTableReqKey *k = static_cast<const VlanTableReqKey *>(key);
        vlan_tag = k->tag;
    }

    unsigned short getTag() const {
        return vlan_tag;
    }

    std::string getDesc() const {
        return description;
    }

    std::string ToString() const {
        return "Vlan";
    }

    void updateDescription(const std::string &desc) {
        description.assign(desc);
    }

    virtual KeyPtr GetDBRequestKey() const {
        VlanTableReqKey *key = new VlanTableReqKey(vlan_tag);
        return KeyPtr(key);
    }
private:
    unsigned short vlan_tag;
    std::string description;
    DISALLOW_COPY_AND_ASSIGN(Vlan);
};

class VlanTable : public DBTable {
public:
    VlanTable(DB *db) : DBTable(db, "__vlan__.0") { };
    ~VlanTable() { };

    // Alloc a derived DBEntry
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const {
        const VlanTableReqKey *vkey = static_cast<const VlanTableReqKey *>(key);
        Vlan *vlan = new Vlan(vkey->tag);
        return std::auto_ptr<DBEntry>(vlan);
    };

    size_t Hash(const DBEntry *entry) const {
        return (static_cast<const Vlan *>(entry))->getTag();
    }

    size_t Hash(const DBRequestKey *key) const {
        return (static_cast<const VlanTableReqKey *>(key))->tag;
    }

    virtual DBEntry *Add(const DBRequest *req) {
        const VlanTableReqKey *key = static_cast<const VlanTableReqKey *>
            (req->key.get());
        const VlanTableReqData *data = static_cast<const VlanTableReqData *>
            (req->data.get());
        Vlan *vlan = new Vlan(key->tag);
        vlan->updateDescription(data->description);
        return vlan;
    };

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        const VlanTableReqData *data = static_cast<const VlanTableReqData *>
            (req->data.get());
        Vlan *vlan = static_cast<Vlan *>(entry);

        vlan->updateDescription(data->description);
        return true;
    };

    virtual void Delete(DBEntry *entry, const DBRequest *req) { };

    Vlan *Find(VlanTableReqKey *key) {
        Vlan vlan(key->tag);
        return static_cast<Vlan *>(DBTable::Find(&vlan));
    };

    static DBTableBase *CreateTable(DB *db, const std::string &name) {
        VlanTable *table = new VlanTable(db);
        table->Init();
        return table;
    }

    DISALLOW_COPY_AND_ASSIGN(VlanTable);
};

#include "db_test_cmn.h"

void RegisterFactory() {
    DB::RegisterFactory("db.test.vlan.0", &VlanTable::CreateTable);
    DB::RegisterFactory("db.test.vlan.1", &VlanTable::CreateTable);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    RegisterFactory();

    return RUN_ALL_TESTS();
}
