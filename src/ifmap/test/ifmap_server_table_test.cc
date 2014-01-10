/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_server_table.h"

#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/crc.hpp>      // for boost::crc_32_type

#include "base/logging.h"
#include "base/task.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "ifmap/autogen.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_util.h"
#include "ifmap/test/ifmap_test_util.h"
#include "testing/gunit.h"

using namespace std;

/// datatypes to be generated from xsd ///

namespace datatypes {

class Tenant : public IFMapIdentifier {
public:
    struct TenantAttrData : AutogenProperty {
        string data;
    };
    enum Property {
        ATTR
    };
    Tenant()
        : attr_set_(false) {
    }
    virtual string ToString() const {
        string repr;
        return repr;
    }
    virtual void EncodeUpdate(pugi::xml_node *parent) const {
    }
    virtual bool SetProperty(const string &attr_key,
                             AutogenProperty *data) {
        if (attr_key == "attr") {
            const TenantAttrData *dp =
                    static_cast<const TenantAttrData *>(data);
            attr_ = dp->data;
            attr_set_ = true;
        }
        return true;
    }
    virtual void ClearProperty(const string &attr_key) {
        if (attr_key == "attr") {
            attr_.clear();
            attr_set_ = false;
        }
    }
    virtual boost::crc_32_type::value_type CalculateCrc() {
        return 0;
    }

    bool empty() const {
        return !attr_set_;
    }

private:
    string attr_;
    bool attr_set_;
};

class VirtualNetwork : public IFMapIdentifier {
public:
    VirtualNetwork() { }
    virtual string ToString() const {
        string repr;
        return repr;
    }
    virtual void EncodeUpdate(pugi::xml_node *parent) const {
    }
    virtual bool SetProperty(const string &attr_key,
                            AutogenProperty *data) {
        return true;
    }
    virtual void ClearProperty(const string &attr_key) {
    }
    virtual boost::crc_32_type::value_type CalculateCrc() {
        return 0;
    }

private:
};

class VirtualMachine : public IFMapIdentifier {
public:
    VirtualMachine() { }
    virtual string ToString() const {
        string repr;
        return repr;
    }
    virtual void EncodeUpdate(pugi::xml_node *parent) const {
    }
    virtual bool SetProperty(const string &attr_key,
                             AutogenProperty *data) {
        return true;
    }
    virtual void ClearProperty(const string &attr_key) {
    }
    virtual boost::crc_32_type::value_type CalculateCrc() {
        return 0;
    }

private:
};

class Vswitch : public IFMapIdentifier {
public:
    Vswitch() { }
    virtual string ToString() const {
        string repr;
        return repr;
    }
    virtual void EncodeUpdate(pugi::xml_node *parent) const {
    }
    virtual bool SetProperty(const string &attr_key,
                            AutogenProperty *data) {
        return true;
    }
    virtual void ClearProperty(const string &attr_key) {
    }
    virtual boost::crc_32_type::value_type CalculateCrc() {
        return 0;
    }

private:
};

class VmVswitchAssociation : public IFMapLinkAttr {
public:
    struct VmVswitchAssociationData : AutogenProperty {
        string data;
    };

    VmVswitchAssociation() { }
    virtual string ToString() const {
        string repr;
        return repr;
    }
    virtual void EncodeUpdate(pugi::xml_node *parent) const {
    }
    virtual bool SetData(const AutogenProperty *data) {
        const VmVswitchAssociationData *dp =
                static_cast<const VmVswitchAssociationData *>(data);
        if (attr_ != dp->data) {
            attr_ = dp->data;
            return true;
        }
        return false;
    }
    virtual boost::crc_32_type::value_type CalculateCrc() {
        return 0;
    }

    const string &attr() const { return attr_; }

private:
    string attr_;
};

template <typename ObjectType>
class IFMapDB : public IFMapServerTable {
public:
    IFMapDB(DB *db, const string &name, DBGraph *graph)
            : IFMapServerTable(db, name, graph) {
        size_t start = name.find('.');
        size_t end = name.find('.', start + 1);
        typename_ = string(name, start + 1, end - (start + 1));
        boost::replace_all(typename_, "_", "-");
    }

    virtual const char *Typename() const {
        return typename_.c_str();
    }

    static DBTable *CreateTable(DB *db, const string &name) {
        DBGraph *graph = db->GetGraph("__ifmap__");
        DBTable *tbl = new IFMapDB(db, name, graph);
        tbl->Init();
        return tbl;
    }

protected:
    IFMapObject *AllocObject() {
        return new ObjectType();
    }

private:
    string typename_;
};

typedef IFMapDB<Tenant> DBTable_tenant;
typedef IFMapDB<VirtualNetwork> DBTable_virtual_network;
typedef IFMapDB<VirtualMachine> DBTable_virtual_machine;
typedef IFMapDB<Vswitch> DBTable_vswitch;
typedef IFMapDB<VmVswitchAssociation> DBTable_vm_vswitch_association;

static void ModuleInit(DB *db) {
    db->RegisterFactory("__ifmap__.tenant.0", &DBTable_tenant::CreateTable);
    db->CreateTable("__ifmap__.tenant.0");
    db->RegisterFactory("__ifmap__.virtual_network.0",
                        &DBTable_virtual_network::CreateTable);
    db->CreateTable("__ifmap__.virtual_network.0");
    db->RegisterFactory("__ifmap__.virtual_machine.0",
                        &DBTable_virtual_machine::CreateTable);
    db->CreateTable("__ifmap__.virtual_machine.0");
    db->RegisterFactory("__ifmap__.vswitch.0",
                        &DBTable_vswitch::CreateTable);
    db->CreateTable("__ifmap__.vswitch.0");

    db->RegisterFactory("__ifmap__.vm_vswitch_association.0",
                        &DBTable_vm_vswitch_association::CreateTable);
    db->CreateTable("__ifmap__.vm_vswitch_association.0");
}
}  // namespace


class IFMapServerTableTest : public ::testing::Test {
protected:
    IFMapServerTableTest() {
        db_.SetGraph("__ifmap__", &graph_);
    }

    virtual void SetUp() {
        datatypes::ModuleInit(&db_);
        IFMapLinkTable_Init(&db_, &graph_);
    }

    virtual void TearDown() {
        IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
            db_.FindTable("__ifmap_metadata__.0"));
        ltable->Clear();
        IFMapTable::ClearTables(&db_);
        Wait();
        db_.Clear();
        DB::ClearFactoryRegistry();
    }

    IFMapNode *TableLookup(const string &type, const string &name) {
        IFMapTable *tbl = IFMapTable::FindTable(&db_, type);
        if (tbl == NULL) {
            return NULL;
        }
        IFMapTable::RequestKey reqkey;
        reqkey.id_name = name;
        auto_ptr<DBEntry> key(tbl->AllocEntry(&reqkey));
        return static_cast<IFMapNode *>(tbl->Find(key.get()));
    }

    void IFMapSetProperty(const string &type, const string &objid,
                          const string &attr) {
        auto_ptr<DBRequest> request(new DBRequest());
        request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        IFMapTable *tbl = IFMapTable::FindTable(&db_, type);
        IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
        request->key.reset(key);
        key->id_name = objid;
        IFMapServerTable::RequestData *data =
            new IFMapServerTable::RequestData();
        request->data.reset(data);
        size_t loc = attr.find('=');
        data->metadata = attr.substr(0, loc);

        if (type == "tenant") {
            datatypes::Tenant::TenantAttrData *dp =
                    new datatypes::Tenant::TenantAttrData();
            dp->data = attr.substr(loc + 1);
            data->content.reset(dp);
        }

        tbl->Enqueue(request.get());
    }

    void IFMapClearProperty(const string &type, const string &objid,
                          const string &attr) {
        auto_ptr<DBRequest> request(new DBRequest());
        request->oper = DBRequest::DB_ENTRY_DELETE;
        IFMapTable *tbl = IFMapTable::FindTable(&db_, type);
        IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
        request->key.reset(key);
        key->id_name = objid;
        IFMapServerTable::RequestData *data =
            new IFMapServerTable::RequestData();
        request->data.reset(data);
        data->metadata = attr;
        tbl->Enqueue(request.get());
    }

    void IFMapMsgLink(const string &lhs, const string &rhs,
                      const string &lid, const string &rid) {
        string metadata = lhs + "-" + rhs;
        ifmap_test_util::IFMapMsgLink(&db_, lhs, lid, rhs, rid, metadata);
    }

    void IFMapMsgUnlink(const string &lhs, const string &rhs,
                        const string &lid, const string &rid) {
        string metadata = lhs + "-" + rhs;
        ifmap_test_util::IFMapMsgUnlink(&db_, lhs, lid, rhs, rid, metadata);
    }

    void IFMapMsgLinkAttr(const string &meta,
                          const string &lhs, const string &rhs,
                          const string &lid, const string &rid,
                          const string &attr) {
        auto_ptr<DBRequest> request(new DBRequest());
        request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        ifmap_test_util::IFMapLinkCommon(request.get(), lhs, lid, rhs, rid,
                                         meta);
        IFMapServerTable::RequestData *data =
            static_cast<IFMapServerTable::RequestData *>(request->data.get());
        if (meta == "vm-vswitch-association") {
            typedef datatypes::VmVswitchAssociation::VmVswitchAssociationData
                    DataType;
            DataType *dp = new DataType();
            data->content.reset(dp);
            dp->data = attr;
        }
        IFMapTable *tbl = IFMapTable::FindTable(&db_,lhs);
        tbl->Enqueue(request.get());
    }

    int TableCount(DBTable *table) {
        DBTablePartition *partition =
            static_cast<DBTablePartition *>(table->GetTablePartition(0));
        int count = 0;
        for (DBEntryBase *entry = partition->GetFirst(); entry;
             entry = partition->GetNext(entry)) {
            count++;
        }
        return count;
    }

    void Wait() {
        static const int kTimeout = 1;
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        for (int i = 0; i < (kTimeout * 1000); i++) {
            if (scheduler->IsEmpty()) {
                break;
            }
            usleep(1000);
        }
    }

    DB db_;
    DBGraph graph_;
};

struct graph_visitor {
    graph_visitor() : count(0) { }
    void Visit(DBGraphVertex *entry) {
        LOG(DEBUG, "visit: " << entry->ToString());
        count++;
    }
    int count;
};

TEST_F(IFMapServerTableTest, CreateDelete) {
    IFMapSetProperty("tenant", "foo", "attr=baz");
    IFMapSetProperty("tenant", "bar", "attr=zoo");
    IFMapMsgLink("tenant", "virtual-network", "foo", "vn1");
    Wait();

    IFMapTable *tbl = IFMapTable::FindTable(&db_, "tenant");
    EXPECT_EQ(2, TableCount(tbl));

    IFMapClearProperty("tenant", "foo", "attr");
    IFMapClearProperty("tenant", "bar", "attr");
    Wait();

    EXPECT_EQ(1, TableCount(tbl));
    IFMapNode *tn1 = TableLookup("tenant", "foo");
    ASSERT_TRUE(tn1 != NULL);

    IFMapMsgUnlink("tenant", "virtual-network", "foo", "vn1");
    Wait();

    for (DBGraph::edge_iterator iter = graph_.edge_list_begin();
         iter != graph_.edge_list_end(); ++iter) {
        IFMapNode *lhs = static_cast<IFMapNode *>(iter->first);
        IFMapNode *rhs = static_cast<IFMapNode *>(iter->second);
        LOG(DEBUG, lhs->ToString() << " - " << rhs->ToString());
    }

    EXPECT_EQ(0, TableCount(tbl));

    tbl = IFMapTable::FindTable(&db_, "virtual-network");
    EXPECT_EQ(0, TableCount(tbl));    
}

TEST_F(IFMapServerTableTest, Traversal) {
    IFMapSetProperty("tenant", "foo:bar", "attr=baz");
    IFMapMsgLink("tenant", "virtual-network", "foo:bar", "vn1");
    IFMapMsgLink("virtual-network", "virtual-machine", "vn1", "vm1");

    // Wait for operations to complete
    Wait();

    IFMapNode *vn1 = TableLookup("virtual-network", "vn1");
    ASSERT_TRUE(vn1 != NULL);
    int count = 0;
    for (DBGraphVertex::adjacency_iterator iter = vn1->begin(&graph_);
         iter != vn1->end(&graph_); ++iter) {
        LOG(DEBUG, iter->ToString());
        count++;
    }
    EXPECT_EQ(2, count);

    IFMapMsgLinkAttr("vm-vswitch-association", "vswitch", "virtual-machine",
                     "aa01", "vm1", "x");
    Wait();
    IFMapNode *vs1 = TableLookup("vswitch", "aa01");
    ASSERT_TRUE(vs1 != NULL);

    graph_visitor visitor;
    
    graph_.Visit(vs1, boost::bind(&graph_visitor::Visit, &visitor, _1), 0);
    EXPECT_EQ(5, visitor.count);

    IFMapMsgLink("tenant", "virtual-network", "foo:bar", "vn2");
    IFMapMsgLink("virtual-network", "virtual-machine", "vn2", "vm2");
    IFMapMsgLinkAttr("vm-vswitch-association", "vswitch", "virtual-machine",
                     "aa02", "vm2", "x");
    Wait();

    // walk the networks associated with vs1
    IFMapTypenameFilter criteria;
    criteria.exclude_vertex.push_back("tenant");    
    criteria.exclude_edge.push_back(
        "source=virtual-network,target=virtual-machine");

    LOG(DEBUG, "filtered visit 1");
    graph_visitor f1;
    graph_.Visit(vs1, boost::bind(&graph_visitor::Visit, &f1, _1), 0, criteria);
    EXPECT_EQ(4, f1.count);

    IFMapMsgLink("virtual-network", "virtual-machine", "vn2", "vm1");
    Wait();

    // repeat the walk
    LOG(DEBUG, "filtered visit 2");
    graph_visitor f2;
    graph_.Visit(vs1, boost::bind(&graph_visitor::Visit, &f2, _1), 0, criteria);
    EXPECT_EQ(5, f2.count);
    
    IFMapMsgUnlink("virtual-network", "virtual-machine", "vn2", "vm1");
    Wait();

    // walk the networks not associated with vs1
}

TEST_F(IFMapServerTableTest, DuplicateLink) {
    IFMapMsgLink("tenant", "virtual-network", "foo:bar", "vn1");
    IFMapMsgLink("tenant", "virtual-network", "foo:bar", "vn1");
    Wait();

    IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    ASSERT_TRUE(ltable != NULL);

    EXPECT_EQ(1, TableCount(ltable));

    IFMapMsgLink("virtual-network", "virtual-machine", "vn1", "vm1");
    IFMapMsgLinkAttr("vm-vswitch-association", "vswitch", "virtual-machine",
                     "aa01", "vm1", "x");
    Wait();
    EXPECT_EQ(4, TableCount(ltable));

    IFMapMsgLinkAttr("vm-vswitch-association", "vswitch", "virtual-machine",
                     "aa01", "vm1", "y");
    Wait();
    EXPECT_EQ(4, TableCount(ltable));

    IFMapNode *node = TableLookup("vm-vswitch-association", "attr(aa01,vm1)");

    ASSERT_TRUE(node != NULL);
    datatypes::VmVswitchAssociation *vma =
            static_cast<datatypes::VmVswitchAssociation *>(node->GetObject());
    EXPECT_EQ("y", vma->attr());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
