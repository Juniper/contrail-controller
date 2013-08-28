/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_condition_listener.h"


#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

using namespace std;
using namespace tbb;
using boost::system::error_code;
using namespace pugi;

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

class TestConditionMatch : public ConditionMatch {
public:
    typedef std::map<Ip4Prefix, BgpRoute *> MatchList;
    TestConditionMatch(Ip4Prefix &prefix, bool hold_db_state) 
        : prefix_(prefix), hold_db_state_(hold_db_state) {
    }

    bool Match(BgpServer *server, BgpTable *table, 
               BgpRoute *route, bool deleted) {
        InetRoute *inet_route = dynamic_cast<InetRoute *>(route);

        BgpConditionListener *listener = server->condition_listener();
        TestMatchState *state = 
            static_cast<TestMatchState *>(listener->GetMatchState(table, route,
                                                                  this));

        // Some random match on key of the route
        if (prefix_.prefixlen() < inet_route->GetPrefix().prefixlen()) {
            mutex::scoped_lock lock(mutex_);
            if (deleted) {
                if (state) {
                    assert(state->del_seen() != true);
                    if (!hold_db_state_) {
                        assert(match_list_.erase(inet_route->GetPrefix()));
                        listener->RemoveMatchState(table, route, this);
                        delete state;
                    } else {
                        state->set_del_seen();
                    }
                }
            } else {
                if (state == NULL) {
                    match_list_.insert(std::make_pair(inet_route->GetPrefix(), 
                                                      route));
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

    bool matched_routes_empty() {
        mutex::scoped_lock lock(mutex_);
        return match_list_.empty();
    }

    int matched_routes_size() {
        mutex::scoped_lock lock(mutex_);
        return match_list_.size();
    }

    BgpRoute *lookup_matched_routes(const Ip4Prefix &prefix) {
        mutex::scoped_lock lock(mutex_);
        MatchList::iterator it = match_list_.find(prefix);;
        if (it == match_list_.end()) {
            return NULL;
        }
        return it->second;
    }

    void remove_matched_route(const Ip4Prefix &prefix) {
        mutex::scoped_lock lock(mutex_);
        MatchList::iterator it = match_list_.find(prefix);;
        assert(it != match_list_.end());
        match_list_.erase(it);
    }

private:
    tbb::mutex mutex_;
    MatchList match_list_;
    Ip4Prefix prefix_;
    bool hold_db_state_;
};

class BgpConditionListenerTest : public ::testing::Test {
protected:
    BgpConditionListenerTest()
        : evm_(new EventManager()), bgp_server_(new BgpServer(evm_.get())) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }
    ~BgpConditionListenerTest() {
    }

    virtual void SetUp() {
        IFMapServerParser *parser =
            IFMapServerParser::GetInstance("bgp_schema");
        bgp_schema_ParserInit(parser);
        bgp_server_->config_manager()->Initialize(&config_db_, &config_graph_,
                                                  "localhost");
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        db_util::Clear(&config_db_);
        IFMapServerParser *parser =
            IFMapServerParser::GetInstance("bgp_schema");
        parser->MetadataClear("bgp_schema");
    }

    void NetworkConfig(const vector<string> &instance_names,
                       const multimap<string, string> &connections) {
        string netconf(
            bgp_util::NetworkConfigGenerate(instance_names, connections));
        IFMapServerParser *parser =
            IFMapServerParser::GetInstance("bgp_schema");
        parser->Receive(&config_db_, netconf.data(), netconf.length(), 0);
    }

    void AddInetRoute(const string &instance_name, const string &prefix, 
                      int localpref=0) {
        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetTable::RequestKey(nlri, NULL));

        BgpAttrSpec attr_spec;

        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, 0, 0));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
        routes_added_.insert(std::make_pair(instance_name, prefix));
    }

    void DeleteInetRoute(const string &instance_name, const string &prefix) {
        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(nlri, NULL));

        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
        task_util::WaitForIdle();
        RouteMap::iterator it = routes_added_.find(instance_name);
        for(; it != routes_added_.end() && it->first == instance_name; it++) {
            if (it->second == prefix) {
                break;
            }
        }
        if (it != routes_added_.end())
            routes_added_.erase(it);
    }

    BgpRoute *InetRouteLookup(const string &instance_name, 
                              const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        TASK_UTIL_EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);
        InetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    void AddRoutingInstance(string name) {
        stringstream target;
        RoutingInstanceMgr *rtmgr = bgp_server_->routing_instance_mgr();
        target << "target:64496:" << 100;
        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", name,
                                      "route-target", target.str(),
                                      "instance-target");
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                            rtmgr->GetRoutingInstance(name));
    }

    void RemoveRoutingInstance(string name) {
        //
        // Cache a copy of the export route-targets before the instance is
        // deleted
        //
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        const RoutingInstance::RouteTargetList
            target_list(rti->GetExportList());
        BOOST_FOREACH(RouteTarget tgt, target_list) {
            ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                            "routing-instance", name,
                                            "route-target", tgt.ToString(),
                                            "instance-target");
        }

        TASK_UTIL_EXPECT_EQ(static_cast<RoutingInstance *>(NULL),
                bgp_server_->routing_instance_mgr()->GetRoutingInstance(name));
    }

    void AddMatchCondition(string name, std::string match, 
                           bool hold_db_state = false) {
        ConcurrencyScope scope("bgp::Config");
        BgpConditionListener *listener = bgp_server_->condition_listener();
        Ip4Prefix prefix = Ip4Prefix::FromString(match);
        match_.reset(new TestConditionMatch(prefix, hold_db_state));
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        BgpTable *table = rti->GetTable(Address::INET);
        assert(table);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        listener->AddMatchCondition(table, match_.get(), 
                                    BgpConditionListener::RequestDoneCb());
        scheduler->Start();
    }


    TestMatchState *GetMatchState(string name, const string &prefix) {
        BgpConditionListener *listener = bgp_server_->condition_listener();
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        BgpTable *table = rti->GetTable(Address::INET);
        assert(table);

        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);

        TestConditionMatch *match = 
            static_cast<TestConditionMatch *>(match_.get());
        BgpRoute *route = match->lookup_matched_routes(nlri);
        assert(route);

        TestMatchState *state = 
            static_cast<TestMatchState *>(listener->GetMatchState(table, route,
                                                              match_.get()));
        return state;
    }

    void RemoveMatchState(string name, const string &prefix) {
        BgpConditionListener *listener = bgp_server_->condition_listener();
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        BgpTable *table = rti->GetTable(Address::INET);
        assert(table);

        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        TASK_UTIL_EXPECT_FALSE(error != 0);

        TestConditionMatch *match = 
            static_cast<TestConditionMatch *>(match_.get());
        BgpRoute *route = match->lookup_matched_routes(nlri);
        assert(route);

        TestMatchState *state = 
            static_cast<TestMatchState *>(listener->GetMatchState(table, route,
                                                              match_.get()));
        assert(state);

        listener->RemoveMatchState(table, route, match_.get());
        delete state;

        // Remove the matched route
        match->remove_matched_route(nlri);
    }

    void DeleteDone(BgpTable *table, ConditionMatch *obj) {
        BgpConditionListener *listener = bgp_server_->condition_listener();
        listener->UnregisterCondition(table, obj);
    }

    void RemoveMatchCondition(string name) {
        ConcurrencyScope scope("bgp::Config");
        BgpConditionListener *listener = bgp_server_->condition_listener();
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        BgpTable *table = rti->GetTable(Address::INET);
        assert(table);

        BgpConditionListener::RequestDoneCb callback = 
            boost::bind(&BgpConditionListenerTest::DeleteDone, this, _1, _2);
        listener->RemoveMatchCondition(table, match_.get(), callback);
        task_util::WaitForIdle();
    }

    auto_ptr<EventManager> evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    ConditionMatchPtr match_;
    typedef std::multimap<std::string, std::string> RouteMap;
    RouteMap routes_added_;
};

TEST_F(BgpConditionListenerTest, Basic) {
    AddRoutingInstance("blue");
    task_util::WaitForIdle();

    AddMatchCondition("blue", "192.168.1.0/24");
    task_util::WaitForIdle();
    AddInetRoute("blue", "192.168.1.2/32");
    AddInetRoute("blue", "192.168.1.3/32");
    AddInetRoute("blue", "192.168.1.4/32");

    TestConditionMatch *match = 
        static_cast<TestConditionMatch *>(match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 3));

    RemoveMatchCondition("blue");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    DeleteInetRoute("blue", "192.168.1.2/32");
    DeleteInetRoute("blue", "192.168.1.3/32");
    DeleteInetRoute("blue", "192.168.1.4/32");
}

TEST_F(BgpConditionListenerTest, AddWalk) {
    AddRoutingInstance("blue");
    task_util::WaitForIdle();

    // Add the match condition on table with existing routes
    AddInetRoute("blue", "192.168.1.2/32");
    AddInetRoute("blue", "192.168.1.3/32");
    AddInetRoute("blue", "192.168.1.4/32");
    AddInetRoute("blue", "192.168.0.0/16");

    AddMatchCondition("blue", "192.168.1.0/24");
    task_util::WaitForIdle();

    TestConditionMatch *match = 
        static_cast<TestConditionMatch *>(match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 3));

    DeleteInetRoute("blue", "192.168.1.2/32");
    DeleteInetRoute("blue", "192.168.1.3/32");
    DeleteInetRoute("blue", "192.168.1.4/32");
    DeleteInetRoute("blue", "192.168.0.0/16");

    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    RemoveMatchCondition("blue");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    AddInetRoute("blue", "192.168.1.2/32");
    AddInetRoute("blue", "192.168.1.3/32");
    AddInetRoute("blue", "192.168.1.4/32");
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    DeleteInetRoute("blue", "192.168.1.2/32");
    DeleteInetRoute("blue", "192.168.1.3/32");
    DeleteInetRoute("blue", "192.168.1.4/32");
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());
}


TEST_F(BgpConditionListenerTest, DelWalk) {
    AddRoutingInstance("blue");
    task_util::WaitForIdle();

    // Add the match condition on table with existing routes
    AddInetRoute("blue", "192.168.1.2/32");
    AddInetRoute("blue", "192.168.1.3/32");
    AddInetRoute("blue", "192.168.1.4/32");
    AddInetRoute("blue", "192.168.0.0/16");

    AddMatchCondition("blue", "192.168.1.0/24");
    task_util::WaitForIdle();

    TestConditionMatch *match = 
        static_cast<TestConditionMatch *>(match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 3));

    // Remove the match condition with matched routes in the table
    RemoveMatchCondition("blue");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    DeleteInetRoute("blue", "192.168.1.2/32");
    DeleteInetRoute("blue", "192.168.1.3/32");
    DeleteInetRoute("blue", "192.168.1.4/32");
    DeleteInetRoute("blue", "192.168.0.0/16");
}

TEST_F(BgpConditionListenerTest, Stress) {
    AddRoutingInstance("blue");
    task_util::WaitForIdle();

    for (int i = 0; i < 1024; i++) {
        ostringstream route;
        route << "192.168." << (i/256) << "." << (i%256) << "/32";
        AddInetRoute("blue", route.str());
    }
    AddMatchCondition("blue", "192.168.0.0/16");
    task_util::WaitForIdle();

    TestConditionMatch *match = 
        static_cast<TestConditionMatch *>(match_.get());
    TASK_UTIL_EXPECT_EQ(match->matched_routes_size(), 1024);

    RemoveMatchCondition("blue");

    TASK_UTIL_EXPECT_EQ(match->matched_routes_size(), 0);
    for (RouteMap::iterator it = routes_added_.begin(), next; 
         it != routes_added_.end(); it = next) {
        next = it;
        next++;
        DeleteInetRoute(it->first, it->second);
    }
    task_util::WaitForIdle();
}

TEST_F(BgpConditionListenerTest, State) {
    AddRoutingInstance("blue");
    task_util::WaitForIdle();

    AddMatchCondition("blue", "192.168.1.0/24", true);
    task_util::WaitForIdle();
    AddInetRoute("blue", "192.168.1.1/32");

    TestConditionMatch *match = 
        static_cast<TestConditionMatch *>(match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 1));

    DeleteInetRoute("blue", "192.168.1.1/32");
    TASK_UTIL_EXPECT_FALSE(match->matched_routes_empty());


    RemoveMatchState("blue", "192.168.1.1/32");

    RemoveMatchCondition("blue");
    task_util::WaitForIdle();
}

TEST_F(BgpConditionListenerTest, ChangeNotify) {
    AddRoutingInstance("blue");
    task_util::WaitForIdle();

    AddMatchCondition("blue", "192.168.1.0/24");
    task_util::WaitForIdle();
    AddInetRoute("blue", "192.168.1.1/32");

    TestConditionMatch *match = 
        static_cast<TestConditionMatch *>(match_.get());
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 1));

    AddInetRoute("blue", "192.168.1.1/32", 01);
    AddInetRoute("blue", "192.168.1.1/32", 02);
    AddInetRoute("blue", "192.168.1.1/32", 03);
    AddInetRoute("blue", "192.168.1.1/32", 04);
    TASK_UTIL_EXPECT_TRUE((match->matched_routes_size() == 1));

    TestMatchState *state = GetMatchState("blue", "192.168.1.1/32");
    // Module saw the route 5 times
    TASK_UTIL_EXPECT_EQ(5, state->seen());

    DeleteInetRoute("blue", "192.168.1.1/32");
    TASK_UTIL_EXPECT_TRUE(match->matched_routes_empty());

    RemoveMatchCondition("blue");
    task_util::WaitForIdle();
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
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
