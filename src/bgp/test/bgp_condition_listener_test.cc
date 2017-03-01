/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_condition_listener.h"

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using std::auto_ptr;
using std::make_pair;
using std::map;
using std::multimap;
using std::string;
using std::vector;

class TestMatchState : public ConditionMatchState {
public:
    TestMatchState() : count_(1), del_seen_(false) {
    }
    void inc_seen() {
        count_++;
    }
    bool del_seen() {
        return del_seen_;
    }
    int seen() {
        return count_;
    }
    void set_del_seen() {
        del_seen_ = true;
    }
private:
    int count_;
    bool del_seen_;
};

template <typename PrefixT, typename RouteT>
class TestConditionMatch : public ConditionMatch {
public:
    typedef map<PrefixT, BgpRoute *> MatchList;
    TestConditionMatch(Address::Family family, const PrefixT &prefix,
                       bool hold_db_state)
        : family_(family), prefix_(prefix), hold_db_state_(hold_db_state) {
    }

    bool Match(BgpServer *server, BgpTable *table,
               BgpRoute *route, bool deleted) {
        RouteT *ip_route = dynamic_cast<RouteT *>(route);

        BgpConditionListener *listener = server->condition_listener(family_);
        TestMatchState *state = static_cast<TestMatchState *>(
            listener->GetMatchState(table, route, this));

        // Some random match on key of the route
        if (prefix_.prefixlen() < ip_route->GetPrefix().prefixlen()) {
            tbb::mutex::scoped_lock lock(mutex_);
            if (deleted) {
                if (state) {
                    assert(state->del_seen() != true);
                    if (!hold_db_state_) {
                        assert(match_list_.erase(ip_route->GetPrefix()));
                        listener->RemoveMatchState(table, route, this);
                        delete state;
                    } else {
                        state->set_del_seen();
                    }
                }
            } else {
                if (state == NULL) {
                    match_list_.insert(make_pair(ip_route->GetPrefix(), route));
                    state = new TestMatchState();
                    listener->SetMatchState(table, route, this, state);
                } else {
                    state->inc_seen();
                }
            }
            return true;
        }
        return false;
    }

    string ToString() const {
        return "TestConditionMatch";
    }

    bool matched_routes_empty() {
        tbb::mutex::scoped_lock lock(mutex_);
        return match_list_.empty();
    }

    int matched_routes_size() {
        tbb::mutex::scoped_lock lock(mutex_);
        return match_list_.size();
    }

    BgpRoute *lookup_matched_routes(const PrefixT &prefix) {
        tbb::mutex::scoped_lock lock(mutex_);
        typename MatchList::iterator it = match_list_.find(prefix);;
        if (it == match_list_.end()) {
            return NULL;
        }
        return it->second;
    }

    void remove_matched_route(const PrefixT &prefix) {
        tbb::mutex::scoped_lock lock(mutex_);
        typename MatchList::iterator it = match_list_.find(prefix);;
        assert(it != match_list_.end());
        match_list_.erase(it);
    }

private:
    Address::Family family_;
    tbb::mutex mutex_;
    MatchList match_list_;
    PrefixT prefix_;
    bool hold_db_state_;
};

//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template <typename T1, typename T2, typename T3>
struct TypeDefinition {
  typedef T1 TableT;
  typedef T2 PrefixT;
  typedef T3 RouteT;
  typedef TestConditionMatch<PrefixT, RouteT> ConditionMatchT;
};

// TypeDefinitions that we want to test.
typedef TypeDefinition<InetTable, Ip4Prefix, InetRoute> InetDefinition;
typedef TypeDefinition<Inet6Table, Inet6Prefix, Inet6Route> Inet6Definition;

//
// Fixture class template - will be instantiated later for each TypeDefinition.
//
template <typename T>
class BgpConditionListenerTest : public ::testing::Test {
protected:
    typedef typename T::TableT TableT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::RouteT RouteT;
    typedef typename T::ConditionMatchT ConditionMatchT;

    BgpConditionListenerTest()
        : evm_(new EventManager()),
          config_db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
          bgp_server_(new BgpServer(evm_.get())),
          family_(GetFamily()),
          ipv6_prefix_("::ffff:"),
          listener_(bgp_server_->condition_listener(GetFamily())) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }

    ~BgpConditionListenerTest() {
    }

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
    }

    string GetTableName(const string &instance) const {
        if (family_ == Address::INET) {
            return instance + ".inet.0";
        }
        if (family_ == Address::INET6) {
            return instance + ".inet6.0";
        }
        assert(false);
        return "";
    }

    string BuildHostAddress(const string &ipv4_addr) const {
        if (family_ == Address::INET) {
            return ipv4_addr + "/32";
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_addr + "/128";
        }
        assert(false);
        return "";
    }

    string BuildHostAddress(const string &ipv4_prefix,
                            uint8_t byte3, uint8_t byte4) const {
        if (family_ == Address::INET) {
            return ipv4_prefix + "." + integerToString(byte3) + "." +
                integerToString(byte4) + "/32";
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + "." +
                integerToString(byte3) + "." + integerToString(byte4) + "/128";
        }
        assert(false);
        return "";
    }

    string BuildPrefix(const string &ipv4_prefix, uint8_t ipv4_plen) const {
        if (family_ == Address::INET) {
            return ipv4_prefix + "/" + integerToString(ipv4_plen);
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + "/" +
                integerToString(96 + ipv4_plen);
        }
        assert(false);
        return "";
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        BgpIfmapConfigManager *config_manager =
            static_cast<BgpIfmapConfigManager *>(bgp_server_->config_manager());
        config_manager->Initialize(&config_db_, &config_graph_, "localhost");
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        db_util::Clear(&config_db_);
    }

    void NetworkConfig(const vector<string> &instance_names,
                       const multimap<string, string> &connections) {
        bgp_util::NetworkConfigGenerate(&config_db_, instance_names,
                                        connections);
    }

    void AddRoute(const string &instance_name, const string &prefix,
                  int localpref = 0) {
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new typename TableT::RequestKey(nlri, NULL));

        BgpAttrSpec attr_spec;

        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, 0, 0));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(GetTableName(instance_name)));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    void DeleteRoute(const string &instance_name, const string &prefix) {
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new typename TableT::RequestKey(nlri, NULL));

        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(GetTableName(instance_name)));
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    void AddRoutingInstance(string name) {
        string target = string("target:64496:") + integerToString(100);
        RoutingInstanceMgr *rtmgr = bgp_server_->routing_instance_mgr();
        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", name,
                                      "route-target", target,
                                      "instance-target");
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                            rtmgr->GetRoutingInstance(name));
    }

    void RemoveRoutingInstance(string name) {
        // Cache a copy of the export route-targets before the instance is
        // deleted.
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        const RoutingInstance::RouteTargetList
            target_list(rti->GetExportList());
        BOOST_FOREACH(RouteTarget target, target_list) {
            ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                            "routing-instance", name,
                                            "route-target", target.ToString(),
                                            "instance-target");
        }

        TASK_UTIL_EXPECT_EQ(static_cast<RoutingInstance *>(NULL),
                bgp_server_->routing_instance_mgr()->GetRoutingInstance(name));
    }

    void AddMatchCondition(string name, string match,
                           bool hold_db_state = false) {
        ConcurrencyScope scope("bgp::Config");
        PrefixT prefix = PrefixT::FromString(match);
        match_.reset(new ConditionMatchT(family_, prefix, hold_db_state));
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        BgpTable *table = rti->GetTable(family_);
        assert(table);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        listener_->AddMatchCondition(table, match_.get(),
                                     BgpConditionListener::RequestDoneCb());
        scheduler->Start();
    }


    TestMatchState *GetMatchState(string name, const string &prefix) {
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        BgpTable *table = rti->GetTable(family_);
        assert(table);

        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);

        ConditionMatchT *match =
            static_cast<ConditionMatchT *>(match_.get());
        BgpRoute *route = match->lookup_matched_routes(nlri);
        assert(route);

        TestMatchState *state = static_cast<TestMatchState *>(
            listener_->GetMatchState(table, route, match_.get()));
        return state;
    }

    void RemoveMatchState(string name, const string &prefix) {
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        BgpTable *table = rti->GetTable(family_);
        assert(table);

        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);

        ConditionMatchT *match =
            static_cast<ConditionMatchT *>(match_.get());
        BgpRoute *route = match->lookup_matched_routes(nlri);
        assert(route);

        TestMatchState *state = static_cast<TestMatchState *>(
            listener_->GetMatchState(table, route, match_.get()));
        assert(state);

        listener_->RemoveMatchState(table, route, match_.get());
        delete state;

        // Remove the matched route
        match->remove_matched_route(nlri);
    }

    void DeleteDone(BgpTable *table, ConditionMatch *obj) {
        listener_->UnregisterMatchCondition(table, obj);
    }

    void RemoveMatchCondition(string name) {
        ConcurrencyScope scope("bgp::Config");
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        BgpTable *table = rti->GetTable(family_);
        assert(table);

        BgpConditionListener::RequestDoneCb callback =
            boost::bind(&BgpConditionListenerTest::DeleteDone, this, _1, _2);
        listener_->RemoveMatchCondition(table, match_.get(), callback);
        task_util::WaitForIdle();
    }

    auto_ptr<EventManager> evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    Address::Family family_;
    string ipv6_prefix_;
    BgpConditionListener *listener_;
    ConditionMatchPtr match_;
};

// Specialization of GetFamily for INET.
template<>
Address::Family BgpConditionListenerTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template<>
Address::Family BgpConditionListenerTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types <InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(BgpConditionListenerTest, TypeDefinitionList);

TYPED_TEST(BgpConditionListenerTest, Basic) {
    typedef typename TypeParam::ConditionMatchT ConditionMatchT;

    this->AddRoutingInstance("blue");
    task_util::WaitForIdle();

    this->AddMatchCondition("blue", this->BuildPrefix("192.168.1.0", 24));
    task_util::WaitForIdle();
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.2"));
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.3"));
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.4"));

    ConditionMatchT *match = static_cast<ConditionMatchT *>(this->match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 3));

    this->RemoveMatchCondition("blue");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.2"));
    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.3"));
    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.4"));
}

TYPED_TEST(BgpConditionListenerTest, AddWalk) {
    typedef typename TypeParam::ConditionMatchT ConditionMatchT;

    this->AddRoutingInstance("blue");
    task_util::WaitForIdle();

    // Add the match condition on table with existing routes
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.2"));
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.3"));
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.4"));
    this->AddRoute("blue", this->BuildPrefix("192.168.0.0", 16));

    this->AddMatchCondition("blue", this->BuildPrefix("192.168.1.0", 24));
    task_util::WaitForIdle();

    ConditionMatchT *match = static_cast<ConditionMatchT *>(this->match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 3));

    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.2"));
    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.3"));
    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.4"));
    this->DeleteRoute("blue", this->BuildPrefix("192.168.0.0", 16));

    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    this->RemoveMatchCondition("blue");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    this->AddRoute("blue", this->BuildHostAddress("192.168.1.2"));
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.3"));
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.4"));
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.2"));
    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.3"));
    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.4"));
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());
}


TYPED_TEST(BgpConditionListenerTest, DelWalk) {
    typedef typename TypeParam::ConditionMatchT ConditionMatchT;

    this->AddRoutingInstance("blue");
    task_util::WaitForIdle();

    // Add the match condition on table with existing routes
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.2"));
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.3"));
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.4"));
    this->AddRoute("blue", this->BuildPrefix("192.168.0.0", 16));

    this->AddMatchCondition("blue", this->BuildPrefix("192.168.1.0", 24));
    task_util::WaitForIdle();

    ConditionMatchT *match = static_cast<ConditionMatchT *>(this->match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 3));

    // Remove the match condition with matched routes in the table
    this->RemoveMatchCondition("blue");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.2"));
    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.3"));
    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.4"));
    this->DeleteRoute("blue", this->BuildPrefix("192.168.0.0", 16));
}

TYPED_TEST(BgpConditionListenerTest, Stress) {
    typedef typename TypeParam::ConditionMatchT ConditionMatchT;

    this->AddRoutingInstance("blue");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 1024; ++idx) {
        this->AddRoute("blue",
            this->BuildHostAddress("192.168", idx / 256, idx % 256));
    }
    this->AddMatchCondition("blue", this->BuildPrefix("192.168.0.0", 24));
    task_util::WaitForIdle();

    ConditionMatchT *match = static_cast<ConditionMatchT *>(this->match_.get());
    TASK_UTIL_EXPECT_EQ(match->matched_routes_size(), 1024);

    this->RemoveMatchCondition("blue");

    TASK_UTIL_EXPECT_EQ(0, match->matched_routes_size());
    for (int idx = 0; idx < 1024; ++idx) {
        this->DeleteRoute("blue",
            this->BuildHostAddress("192.168", idx / 256, idx % 256));
    }
    task_util::WaitForIdle();
}

TYPED_TEST(BgpConditionListenerTest, State) {
    typedef typename TypeParam::ConditionMatchT ConditionMatchT;

    this->AddRoutingInstance("blue");
    task_util::WaitForIdle();

    this->AddMatchCondition("blue", this->BuildPrefix("192.168.1.0", 24), true);
    task_util::WaitForIdle();
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.1"));

    ConditionMatchT *match = static_cast<ConditionMatchT *>(this->match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 1));

    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.1"));
    TASK_UTIL_EXPECT_FALSE(match->matched_routes_empty());


    this->RemoveMatchState("blue", this->BuildHostAddress("192.168.1.1"));

    this->RemoveMatchCondition("blue");
    task_util::WaitForIdle();
}

TYPED_TEST(BgpConditionListenerTest, ChangeNotify) {
    typedef typename TypeParam::ConditionMatchT ConditionMatchT;

    this->AddRoutingInstance("blue");
    task_util::WaitForIdle();

    this->AddMatchCondition("blue", this->BuildPrefix("192.168.1.0", 24));
    task_util::WaitForIdle();
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.1"));

    ConditionMatchT *match = static_cast<ConditionMatchT *>(this->match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 1));

    this->AddRoute("blue", this->BuildHostAddress("192.168.1.1"), 100);
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.1"), 200);
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.1"), 300);
    this->AddRoute("blue", this->BuildHostAddress("192.168.1.1"), 400);
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 1));

    // Module saw the route 5 times
    TestMatchState *state =
        this->GetMatchState("blue", this->BuildHostAddress("192.168.1.1"));
    TASK_UTIL_EXPECT_EQ(5, state->seen());

    this->DeleteRoute("blue", this->BuildHostAddress("192.168.1.1"));
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    this->RemoveMatchCondition("blue");
    task_util::WaitForIdle();
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
