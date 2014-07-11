/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/ifmap_dependency_manager.h"

#include <boost/bind.hpp>

#include "base/test/task_test_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

class TestEntry : public DBEntry {
  public:
    class TestEntryKey : public DBRequestKey {
      public:
        TestEntryKey(const std::string &name)
                : name_(name) {
        }
        virtual ~TestEntryKey() {
        }
        const std::string &name() const { return name_; }
      private:
        std::string name_;
    };

    TestEntry() : node_(NULL) {
    }

    virtual std::string ToString() const {
        std::stringstream ss;
        ss << "test-entry:" << name_;
        return ss.str();
    }

    virtual KeyPtr GetDBRequestKey() const {
        KeyPtr key(new TestEntryKey(name_));
        return key;
    }

    virtual void SetKey(const DBRequestKey *key) {
        const TestEntryKey *test_key = static_cast<const TestEntryKey *>(key);
        name_ = test_key->name();
    }

    virtual bool IsLess(const DBEntry &rhs) const {
        const TestEntry &entry = static_cast<const TestEntry &>(rhs);
        return name_ < entry.name_;
    }

    const std::string  &name() const { return name_; }

    IFMapNode *node() { return node_; }
    void set_node(IFMapNode *node) { node_ = node; }
    void reset_node() { node_ = NULL; }

  private:
    std::string name_;
    IFMapNode *node_;
};

class TestTable : public DBTable {
  public:
    TestTable(DB *db, const std::string &name)
            : DBTable(db, name) {
    }
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const {
        std::auto_ptr<DBEntry> entry(new TestEntry());
        entry->SetKey(key);
        return entry;
    }
    static DBTableBase *CreateTable(DB *db, const std::string &name) {
        TestTable *table = new TestTable(db, name);
        table->Init();
        return table;
    }
};

class IFMapDependencyManagerTest : public ::testing::Test {
  protected:
    IFMapDependencyManagerTest()
            : manager_(new IFMapDependencyManager(&database_, &graph_)),
              test_table_(NULL) {
    }

    virtual void SetUp() {
        DB::RegisterFactory("db.test.0", &TestTable::CreateTable);
        test_table_ = static_cast<TestTable *>(
            database_.CreateTable("db.test.0"));
        IFMapAgentLinkTable_Init(&database_, &graph_);
        vnc_cfg_Agent_ModuleInit(&database_, &graph_);
        manager_->Initialize();
    }

    virtual void TearDown() {
        RemoveAllObjects();

        manager_->Terminate();
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
            database_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
        assert(link_table);
        link_table->Clear();
        db_util::Clear(&database_);
    }

    void AddObject(const std::string &id_name) {
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new TestEntry::TestEntryKey(id_name));
        test_table_->Enqueue(&request);
    }

    void CreateObject(const std::string &id_typename,
                      const std::string &id_name) {
        IFMapTable *table = IFMapTable::FindTable(&database_, id_typename);
        IFMapNode *node = table->FindNode(id_name);
        ASSERT_TRUE(node != NULL);
        AddObject(id_name);
        task_util::WaitForIdle();
        TestEntry::TestEntryKey key(id_name);
        DBEntry *entry = test_table_->Find(&key);
        ASSERT_TRUE(entry != NULL);
        TestEntry *test_entry = static_cast<TestEntry *>(entry);
        manager_->SetObject(node, entry);
        test_entry->set_node(node);
    }

    void RemoveAllObjects() {
        DBTablePartition *tslice = static_cast<DBTablePartition *>(
            test_table_->GetTablePartition(0));
        typedef std::vector<std::string> IdList;
        IdList list;

        for (DBEntry *entry = tslice->GetFirst(); entry;
             entry = tslice->GetNext(entry)) {
            TestEntry *test_entry = static_cast<TestEntry *>(entry);
            manager_->ResetObject(test_entry->node());
            test_entry->reset_node();
            list.push_back(test_entry->name());
        }

        for (IdList::iterator iter = list.begin(); iter != list.end(); ++iter) {
            DBRequest request;
            request.oper = DBRequest::DB_ENTRY_DELETE;
            request.key.reset(new TestEntry::TestEntryKey(*iter));
            test_table_->Enqueue(&request);
        }
    }

    void ChangeEventHandler(DBEntry *entry) {
        change_list_.push_back(entry);
    }

    DB database_;
    DBGraph graph_;
    std::auto_ptr<IFMapDependencyManager> manager_;
    std::vector<DBEntry *> change_list_;
    TestTable *test_table_;
};

TEST_F(IFMapDependencyManagerTest, VirtualMachineEvent) {
    typedef IFMapDependencyManagerTest_VirtualMachineEvent_Test TestClass;
    manager_->Register(
        "service-instance",
        boost::bind(&TestClass::ChangeEventHandler, this, _1));

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "service-instance", "id-1");
    task_util::WaitForIdle();
    CreateObject("service-instance", "id-1");

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "virtual-machine", "id-1");
    ifmap_test_util::IFMapMsgLink(&database_, "service-instance", "id-1",
                                  "virtual-machine", "id-1",
                                  "virtual-machine-service-instance");
    task_util::WaitForIdle();
    ASSERT_EQ(1, change_list_.size());
    TestEntry *entry = static_cast<TestEntry *>(change_list_.at(0));
    EXPECT_EQ("id-1", entry->name());
}

TEST_F(IFMapDependencyManagerTest, TemplateEvent) {
    typedef IFMapDependencyManagerTest_TemplateEvent_Test TestClass;
    manager_->Register(
        "service-instance",
        boost::bind(&TestClass::ChangeEventHandler, this, _1));

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "service-instance", "id-1");
    task_util::WaitForIdle();
    CreateObject("service-instance", "id-1");

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "service-template", "id-1");
    ifmap_test_util::IFMapMsgLink(&database_, "service-instance", "id-1",
                                  "service-template", "id-1",
                                  "service-instance-service-template");
    task_util::WaitForIdle();
    ASSERT_EQ(1, change_list_.size());
    TestEntry *entry = static_cast<TestEntry *>(change_list_.at(0));
    EXPECT_EQ("id-1", entry->name());
}

TEST_F(IFMapDependencyManagerTest, VMIEvent) {
    typedef IFMapDependencyManagerTest_VMIEvent_Test TestClass;
    manager_->Register(
        "service-instance",
        boost::bind(&TestClass::ChangeEventHandler, this, _1));

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "service-instance", "id-1");
    task_util::WaitForIdle();
    CreateObject("service-instance", "id-1");

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "service-instance", "id-2");
    task_util::WaitForIdle();
    CreateObject("service-instance", "id-2");

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "virtual-machine", "id-1");
    ifmap_test_util::IFMapMsgLink(&database_, "service-instance", "id-1",
                                  "virtual-machine", "id-1",
                                  "virtual-machine-service-instance");

    ifmap_test_util::IFMapMsgNodeAdd(&database_,
                                     "virtual-machine-interface",
                                     "id-1-left");
    ifmap_test_util::IFMapMsgLink(
        &database_,
        "virtual-machine-interface", "id-1-left",
        "virtual-machine", "id-1",
        "virtual-machine-interface-virtual-machine");


    task_util::WaitForIdle();
    ASSERT_LE(1, change_list_.size());
    for (int i = 0; i < change_list_.size(); ++i) {
        TestEntry *entry = static_cast<TestEntry *>(change_list_.at(i));
        EXPECT_EQ("id-1", entry->name());
    }
    change_list_.clear();

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "virtual-machine", "id-1");
    ifmap_test_util::IFMapMsgLink(&database_, "service-instance", "id-1",
                                  "virtual-machine", "id-1",
                                  "virtual-machine-service-instance");

    ifmap_test_util::IFMapMsgNodeAdd(&database_,
                                     "virtual-machine-interface",
                                     "id-1-right");
    ifmap_test_util::IFMapMsgLink(
        &database_,
        "virtual-machine-interface", "id-1-right",
        "virtual-machine", "id-1",
        "virtual-machine-interface-virtual-machine");

    task_util::WaitForIdle();
    ASSERT_LE(1, change_list_.size());
    for (int i = 0; i < change_list_.size(); ++i) {
        TestEntry *entry = static_cast<TestEntry *>(change_list_.at(i));
        EXPECT_EQ("id-1", entry->name());
    }
    change_list_.clear();

    ifmap_test_util::IFMapMsgNodeAdd(&database_, "virtual-machine", "id-2");
    ifmap_test_util::IFMapMsgLink(&database_, "service-instance", "id-2",
                                  "virtual-machine", "id-2",
                                  "virtual-machine-service-instance");

    ifmap_test_util::IFMapMsgNodeAdd(&database_,
                                     "virtual-machine-interface",
                                     "id-2-left");
    ifmap_test_util::IFMapMsgLink(
        &database_,
        "virtual-machine-interface", "id-2-left",
        "virtual-machine", "id-2",
        "virtual-machine-interface-virtual-machine");

    task_util::WaitForIdle();
    ASSERT_LE(1, change_list_.size());
    for (int i = 0; i < change_list_.size(); ++i) {
        TestEntry *entry = static_cast<TestEntry *>(change_list_.at(i));
        EXPECT_EQ("id-2", entry->name());
    }
    change_list_.clear();
}

static void SetUp() {
}

static void TearDown() {
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
