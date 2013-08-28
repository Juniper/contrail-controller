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

class Vlan : public DBEntryBase {
public:
    boost::intrusive::avl_set_member_hook<> node_;
    Vlan(unsigned short tag) : vlan_tag(tag) { }
    Vlan(unsigned short tag, std::string desc) 
    : vlan_tag(tag), description(desc) { }
    friend bool operator< (const Vlan &a, const Vlan &b) {   
        return a.vlan_tag < b.vlan_tag;  
    }   

    ~Vlan() {
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
};

// Table shard
class VlanTablePart : public DBTablePartBase {
public:
    typedef boost::intrusive::member_hook<Vlan,
        boost::intrusive::avl_set_member_hook<>,
        &Vlan::node_> VlanSetMember;
    typedef boost::intrusive::avltree<Vlan, VlanSetMember> Tree;
    
    VlanTablePart(VlanTable *parent, int index);

    virtual void Process(DBClient *client, DBRequest *req);
    
    void Add(Vlan *entry) {
        tree_.insert_unique(*entry);
        Notify(entry);
    }
 
    void Change(Vlan *entry) {
        Notify(entry);
    }

    void Remove(DBEntryBase *db_entry) {
        Vlan *vlan = dynamic_cast<Vlan *>(db_entry);
        tree_.erase(*vlan);
        delete vlan;
    }
    
    Vlan *Find(const Vlan &key) {
        Tree::iterator loc = tree_.find(key);
        if (loc != tree_.end()) {
            return loc.operator->();
        }
        return NULL;
    }

    Vlan *lower_bound(const DBEntryBase *key) {
        const Vlan *vlan = static_cast<const Vlan *>(key);
        Tree::iterator it = tree_.lower_bound(*vlan);
        if (it != tree_.end()) {
            return (it.operator->());
        }
        return NULL;
    }
    
    virtual DBEntryBase *GetFirst() {
        Tree::iterator it = tree_.begin();
        if (it == tree_.end()) {
            return NULL;
        }
        return it.operator->();        
    }
    
    virtual DBEntryBase *GetNext(const DBEntryBase *entry) {
        const Vlan *vlan = static_cast<const Vlan *> (entry);
        Tree::const_iterator it = tree_.iterator_to(*vlan);
        it++;
        if (it == tree_.end()) {
            return NULL;
        }
        return const_cast<Vlan *>(it.operator->());
    }

private:
    Tree tree_;
    DISALLOW_COPY_AND_ASSIGN(VlanTablePart);
};

class VlanTable : public DBTableBase {
public:
    VlanTable(DB *db) : DBTableBase(db, "__vlan__.0") {
        for (int i = 0; i < DB::PartitionCount(); i++) {
            partitions_.push_back(new VlanTablePart(this, i));
        }
    }
    ~VlanTable() { 
        STLDeleteValues(&partitions_);
    }

    // Return the table partition for a specific request.
    virtual DBTablePartBase *GetTablePartition(const DBRequestKey *req) {
        int part = GetPartitionId(req);
        return partitions_[part];
    }

    virtual DBTablePartBase *GetTablePartition(const DBEntryBase *entry) {
        int part = GetPartitionId(entry);
        return partitions_[part];
    }

    // Return the table partition for a partition id
    virtual DBTablePartBase *GetTablePartition(const int part) {
        return partitions_[part];
    }

    // Change notification handler.
    virtual void Change(DBEntryBase *entry) {
    }

    virtual int GetPartitionId(const DBRequestKey *req) {
        const VlanTableReqKey *vlanreq = 
            dynamic_cast<const VlanTableReqKey *>(req);
        int value = boost::hash_value(vlanreq->tag);
        return value % DB::PartitionCount();
    }

    virtual int GetPartitionId(const DBEntryBase *entry) {
        const Vlan *vlan = dynamic_cast<const Vlan *>(entry);
        int value = boost::hash_value(vlan->getTag());
        return value % DB::PartitionCount();
    }

    virtual std::auto_ptr<const DBEntryBase> GetEntry(const DBRequestKey *req) const {
        const VlanTableReqKey *vlanreqkey = 
            static_cast<const VlanTableReqKey *>(req);
        Vlan *vlan = new Vlan(vlanreqkey->tag);
        return std::auto_ptr<const DBEntryBase>(vlan);
    }

    // Input handler
    virtual void Input(VlanTablePart *root, DBClient *client, DBRequest *req) {
        VlanTableReqKey *vlanreq = 
            static_cast<VlanTableReqKey *>(req->key.get());
        Vlan *vlan = NULL;

        vlan = Find(vlanreq);
        if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
            VlanTableReqData *vlandata = 
                static_cast<VlanTableReqData *>(req->data.get());
            if (vlan) {
                // The entry may currently be marked as deleted.
                vlan->ClearDelete();
                vlan->updateDescription(vlandata->description);
                root->Change(vlan);
            } else {
                Vlan *vlan = new Vlan(vlanreq->tag, vlandata->description);
                root->Add(vlan);
            }
        } else {
            root->Delete(vlan);
        }
    }

    // Perform a Vlan lookup.
    Vlan *Find(const DBRequestKey *vlanreq) {
        VlanTablePart *rtp = partitions_[GetPartitionId(vlanreq)];
        const VlanTableReqKey *vlanreqkey = 
            static_cast<const VlanTableReqKey *>(vlanreq);
        Vlan vlan(vlanreqkey->tag);
        return rtp->Find(vlan);
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name) {
        VlanTable *table = new VlanTable(db);
        return table;
    }

private:
    std::vector<VlanTablePart *> partitions_;
    DISALLOW_COPY_AND_ASSIGN(VlanTable);
};

VlanTablePart::VlanTablePart(VlanTable *parent, int index) 
    : DBTablePartBase(parent, index) {
}

void VlanTablePart::Process(DBClient *client, DBRequest *req) {
    VlanTable *table = dynamic_cast<VlanTable *>(parent());
    table->Input(this, client, req);
}

#include "db_test_cmn.h"

void RegisterFactory() {
    DB::RegisterFactory("db.test.vlan.0", &VlanTable::CreateTable);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    RegisterFactory();

    return RUN_ALL_TESTS();
}
