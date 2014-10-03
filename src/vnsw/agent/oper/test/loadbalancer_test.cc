/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/loadbalancer.h"

#include <boost/uuid/random_generator.hpp>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

#include "cfg/cfg_listener.h"
#include "oper/ifmap_dependency_manager.h"
#include "oper/loadbalancer_properties.h"

using namespace std;
using boost::uuids::uuid;

class LoadbalancerTest : public ::testing::Test {
  protected:
    LoadbalancerTest()
            : config_listener_(&database_),
              manager_(new IFMapDependencyManager(&database_, &graph_)) {
    }

    virtual void SetUp() {
        DB::RegisterFactory("db.loadbalancer-pool.0",
                            &LoadbalancerTable::CreateTable);
        loadbalancer_table_ = static_cast<LoadbalancerTable *>(
            database_.CreateTable("db.loadbalancer-pool.0"));
        loadbalancer_table_->Initialize(&graph_, manager_.get());

        IFMapAgentLinkTable_Init(&database_, &graph_);
        vnc_cfg_Agent_ModuleInit(&database_, &graph_);

        config_listener_.Register("loadbalancer-pool",
                                  loadbalancer_table_,
                                  ::autogen::LoadbalancerPool::ID_PERMS);
        manager_->Initialize();
    }

    virtual void TearDown() {
        manager_->Terminate();
        config_listener_.Shutdown();

        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
            database_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
        assert(link_table);
        link_table->Clear();

        db_util::Clear(&database_);
        DB::ClearFactoryRegistry();
    }

    Loadbalancer *GetLoadbalancer(const boost::uuids::uuid &uuid) {
        LoadbalancerKey key(uuid);
        return static_cast<Loadbalancer *>(
            loadbalancer_table_->Find(&key, true));
    }

    void AddLoadbalancerMember(const boost::uuids::uuid &pool_id,
                               const char *ip_address, int port) {
        boost::uuids::random_generator gen;
        uuid member_id = gen();
        map<string, AutogenProperty *> pmap;
        autogen::LoadbalancerMemberType attr;
        attr.address = ip_address;
        attr.protocol_port = port;
        pmap.insert(make_pair("loadbalancer-member-properties", &attr));
        ifmap_test_util::IFMapMsgPropertySet(
            &database_, "loadbalancer-member", UuidToString(member_id),
            pmap, 0);

        ifmap_test_util::IFMapMsgLink(
            &database_, "loadbalancer-pool", UuidToString(pool_id),
            "loadbalancer-member", UuidToString(member_id),
            "loadbalancer-pool-loadbalancer-member");
    }

    void AddLoadbalancerHealthmonitor(
        const boost::uuids::uuid &pool_id,
        const char *url_path, const char *expected_codes) {
        boost::uuids::random_generator gen;
        uuid monitor_id = gen();
        map<string, AutogenProperty *> pmap;
        autogen::LoadbalancerHealthmonitorType attr;
        attr.url_path = url_path;
        attr.expected_codes = expected_codes;
        pmap.insert(make_pair("loadbalancer-healthmonitor-properties", &attr));
        ifmap_test_util::IFMapMsgPropertySet(
            &database_, "loadbalancer-healthmonitor", UuidToString(monitor_id),
            pmap, 0);

        ifmap_test_util::IFMapMsgLink(
            &database_, "loadbalancer-pool", UuidToString(pool_id),
            "loadbalancer-healthmonitor", UuidToString(monitor_id),
            "loadbalancer-pool-loadbalancer-healthmonitor");
    }

  protected:
    DB database_;
    DBGraph graph_;
    CfgListener config_listener_;
    std::auto_ptr<IFMapDependencyManager> manager_;
    LoadbalancerTable *loadbalancer_table_;
};

TEST_F(LoadbalancerTest, ConfigPool) {
    boost::uuids::random_generator gen;
    uuid pool_id = gen();
    uuid vip_id = gen();

    map<string, AutogenProperty *> pmap;
    autogen::LoadbalancerPoolType pool_attr;
    pool_attr.protocol = "HTTP";
    pool_attr.loadbalancer_method = "ROUND_ROBIN";
    pmap.insert(make_pair("loadbalancer-pool-properties", &pool_attr));
    ifmap_test_util::IFMapMsgPropertySet(
        &database_, "loadbalancer-pool", UuidToString(pool_id), pmap, 0);
    pmap.clear();

    autogen::VirtualIpType vip_attr;
    vip_attr.protocol = "HTTP";
    vip_attr.protocol_port = 80;
    vip_attr.connection_limit = 100;
    pmap.insert(make_pair("virtual-ip-properties", &vip_attr));
    ifmap_test_util::IFMapMsgPropertySet(
        &database_, "virtual-ip", UuidToString(vip_id), pmap, 0);
    pmap.clear();

    ifmap_test_util::IFMapMsgLink(
        &database_, "loadbalancer-pool", UuidToString(pool_id),
        "virtual-ip", UuidToString(vip_id),
        "virtual-ip-loadbalancer-pool");

    AddLoadbalancerMember(pool_id, "127.0.0.1", 80);
    AddLoadbalancerMember(pool_id, "127.0.0.2", 80);

    AddLoadbalancerHealthmonitor(pool_id, "http://foo.com/bar", "200");
    AddLoadbalancerHealthmonitor(pool_id, "http://foo.com/baz", "200");

    task_util::WaitForIdle();
    Loadbalancer *loadbalancer = GetLoadbalancer(pool_id);
    ASSERT_TRUE(loadbalancer != NULL);

    const LoadbalancerProperties *props = loadbalancer->properties();
    ASSERT_TRUE(props != NULL);
    EXPECT_EQ("HTTP", props->pool_properties().protocol);
    EXPECT_EQ("ROUND_ROBIN", props->pool_properties().loadbalancer_method);
    EXPECT_EQ(vip_id, props->vip_uuid());
    EXPECT_EQ("HTTP", props->vip_properties().protocol);
    EXPECT_EQ(80, props->vip_properties().protocol_port);
    EXPECT_EQ(100, props->vip_properties().connection_limit);
    ASSERT_EQ(2, props->members().size());

    std::vector<std::string> addresses;
    for (LoadbalancerProperties::MemberMap::const_iterator iter =
                 props->members().begin();
         iter != props->members().end(); ++iter) {
        addresses.push_back(iter->second.address);
    }
    std::sort(addresses.begin(), addresses.end());
    EXPECT_EQ("127.0.0.1", addresses.at(0));
    EXPECT_EQ("127.0.0.2", addresses.at(1));
    ASSERT_EQ(2, props->healthmonitors().size());
}

static void SetUp() {
}

static void TearDown() {
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
