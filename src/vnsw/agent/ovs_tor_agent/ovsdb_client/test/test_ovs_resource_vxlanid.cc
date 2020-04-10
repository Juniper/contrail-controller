/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <base/logging.h>

#include "ovs_tor_agent/ovsdb_client/ovsdb_entry.h"
#include "ovs_tor_agent/ovsdb_client/ovsdb_object.h"
#include "ovs_tor_agent/ovsdb_client/ovsdb_resource_vxlan_id.h"

using namespace OVSDB;

OvsdbResourceVxLanIdTable index_table;

class VxlanTable : public OvsdbObject {
public:
    VxlanTable() : OvsdbObject(NULL) {}
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
};

class VxlanEntry : public OvsdbEntry {
public:
    VxlanEntry(VxlanTable *table, const std::string &name)
        : OvsdbEntry(table), name_(name), id_(&index_table, this), add_count_(0),
        change_count_(0), delete_count_(0) {
    }
    virtual ~VxlanEntry() {}

    bool Add() {
        add_count_++;
        return true;
    }
    bool Change() {
        change_count_++;
        return true;
    }
    bool Delete() {
        delete_count_++;
        return true;
    }

    bool IsLess(const KSyncEntry& entry) const {
        const VxlanEntry &v_entry =
            static_cast<const VxlanEntry&>(entry);
        return (name_ < v_entry.name_);
    }

    std::string ToString() const {return "VxLan Entry";}
    KSyncEntry* UnresolvedReference() {
        return NULL;
    }

    std::string name_;
    OvsdbResourceVxLanId id_;
    uint32_t add_count_;
    uint32_t change_count_;
    uint32_t delete_count_;
};

KSyncEntry *VxlanTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const VxlanEntry *k_entry =
        static_cast<const VxlanEntry *>(key);
    VxlanEntry *entry = new VxlanEntry(this, k_entry->name_);
    return entry;
}

VxlanTable *vxlan_table = NULL;

class OvsdbResourceVxLanIdTest : public ::testing::Test {
public:
    OvsdbResourceVxLanIdTest() {
    }

    ~OvsdbResourceVxLanIdTest() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(OvsdbResourceVxLanIdTest, ResourceAlloc) {
    VxlanEntry key(vxlan_table, "vxlan_1");
    VxlanEntry *entry = static_cast<VxlanEntry*>(vxlan_table->Create(&key));

    // acquire vxlan id and verify
    EXPECT_TRUE(entry->id_.AcquireVxLanId(1));
    EXPECT_EQ(entry->id_.active_vxlan_id(), 1U);
    entry->id_.set_active_vxlan_id(1);
    EXPECT_EQ(entry->id_.active_vxlan_id(), 1U);
    EXPECT_TRUE(entry->id_.AcquireVxLanId(2));
    EXPECT_EQ(entry->id_.active_vxlan_id(), 1U);
    entry->id_.set_active_vxlan_id(2);
    EXPECT_EQ(entry->id_.active_vxlan_id(), 2U);
    EXPECT_TRUE(entry->id_.AcquireVxLanId(3));
    EXPECT_EQ(entry->id_.active_vxlan_id(), 2U);

    vxlan_table->Delete(entry);
}

TEST_F(OvsdbResourceVxLanIdTest, Resourcewait) {
    VxlanEntry key(vxlan_table, "vxlan_1");
    VxlanEntry *entry = static_cast<VxlanEntry*>(vxlan_table->Create(&key));
    VxlanEntry key_1(vxlan_table, "vxlan_2");
    VxlanEntry *entry_1 = static_cast<VxlanEntry*>(vxlan_table->Create(&key_1));
    VxlanEntry key_2(vxlan_table, "vxlan_3");
    VxlanEntry *entry_2 = static_cast<VxlanEntry*>(vxlan_table->Create(&key_2));

    // acquire vxlan id and verify
    EXPECT_TRUE(entry->id_.AcquireVxLanId(1));
    entry->id_.set_active_vxlan_id(1);
    EXPECT_EQ(entry->id_.active_vxlan_id(), 1U);

    // acquire vxlan id 2 and verify
    EXPECT_TRUE(entry_1->id_.AcquireVxLanId(2));
    entry_1->id_.set_active_vxlan_id(2);
    EXPECT_EQ(entry_1->id_.active_vxlan_id(), 2U);

    // change from vxlan id 1 to 2 and wait for resource
    EXPECT_FALSE(entry->id_.AcquireVxLanId(2));
    EXPECT_EQ(entry->id_.VxLanId(), 0U);
    EXPECT_TRUE(entry->id_.AcquireVxLanId(3));
    EXPECT_EQ(entry->id_.active_vxlan_id(), 1U);
    EXPECT_TRUE(entry->id_.AcquireVxLanId(1));

    // change from vxlan id 1 to 2 and wait for resource
    EXPECT_FALSE(entry->id_.AcquireVxLanId(2));
    EXPECT_FALSE(entry_2->id_.AcquireVxLanId(2));
    EXPECT_EQ(entry->id_.VxLanId(), 0U);
    EXPECT_EQ(entry_2->id_.VxLanId(), 0U);
    EXPECT_TRUE(entry->id_.AcquireVxLanId(1));
    EXPECT_EQ(entry->id_.active_vxlan_id(), 1U);
    EXPECT_TRUE(entry_2->id_.AcquireVxLanId(4));
    EXPECT_EQ(entry_2->id_.active_vxlan_id(), 4U);

    // change from vxlan id cyclic manner
    EXPECT_FALSE(entry->id_.AcquireVxLanId(2));
    EXPECT_EQ(entry->id_.VxLanId(), 0U);
    EXPECT_FALSE(entry_1->id_.AcquireVxLanId(1));
    EXPECT_EQ(entry_1->id_.VxLanId(), 0U);
    // release active id
    entry->id_.set_active_vxlan_id(0);
    EXPECT_EQ(entry_1->id_.VxLanId(), 1U);
    EXPECT_EQ(entry_1->id_.active_vxlan_id(), 2U);
    entry_1->id_.set_active_vxlan_id(1);
    EXPECT_EQ(entry_1->id_.active_vxlan_id(), 1U);
    EXPECT_EQ(entry->id_.VxLanId(), 2U);
    entry->id_.set_active_vxlan_id(2);
    EXPECT_EQ(entry->id_.active_vxlan_id(), 2U);

    vxlan_table->Delete(entry);
    vxlan_table->Delete(entry_1);
    vxlan_table->Delete(entry_2);
}

int main(int argc, char *argv[]) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    vxlan_table = new VxlanTable();
    int ret =  RUN_ALL_TESTS();
    delete vxlan_table;
    return ret;
}
