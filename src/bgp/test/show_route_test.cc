/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"

#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace boost::assign;
using namespace boost::asio;
using namespace std;

static const char config_tmpl[] = "\
<config>\
    <bgp-router name=\'A\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to='B'/>\
        <session to='B'/>\
        <session to='B'/>\
    </bgp-router>\
    <bgp-router name=\'B\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to='A'/>\
        <session to='A'/>\
        <session to='A'/>\
    </bgp-router>\
    <routing-instance name =\"red\">\
    <bgp-router name=\'A\'>\
        <identifier>192.168.1.1</identifier>\
        <address>10.0.0.1</address>\
    </bgp-router>\
    </routing-instance>\
    <routing-instance name =\"blue\">\
    <bgp-router name=\'A\'>\
        <identifier>192.168.2.1</identifier>\
        <address>10.0.1.1</address>\
    </bgp-router>\
    </routing-instance>\
</config>\
";

class ShowRouteTestBase : public ::testing::Test {
public:
    bool IsReady() const { return true; }

protected:
    static bool validate_done_;

    virtual void SetUp() {
        evm_.reset(new EventManager());
        a_.reset(new BgpServerTest(evm_.get(), "A"));
        b_.reset(new BgpServerTest(evm_.get(), "B"));
        thread_.reset(new ServerThread(evm_.get()));

        a_->session_manager()->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
            a_->session_manager()->GetPort());
        b_->session_manager()->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
            b_->session_manager()->GetPort());
        thread_->Start();
    }

    virtual void TearDown() {
        a_->Shutdown();
        b_->Shutdown();
        evm_->Shutdown();
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
    }

    void Configure() {
        char config[4096];
        int port_a = a_->session_manager()->GetPort();
        int port_b = b_->session_manager()->GetPort();
        snprintf(config, sizeof(config), config_tmpl, port_a, port_b);
        a_->Configure(config);
        task_util::WaitForIdle();

        RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP, 1, 0);
        DB *db_a = a_.get()->database();

        for (int j = 0; j < 3; j++) {
            string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
            TASK_UTIL_ASSERT_TRUE(a_->FindPeerByUuid(
                BgpConfigManager::kMasterInstance, uuid) != NULL);
            peers_[j] = a_->FindPeerByUuid(
                BgpConfigManager::kMasterInstance, uuid);
            peers_[j]->IsReady_fnc_ =
                boost::bind(&ShowRouteTestBase::IsReady, this);
        }

        InetTable *table_a =
            static_cast<InetTable *>(db_a->FindTable("inet.0"));
        assert(table_a);
    }

    void AddInetRoute(std::string prefix_str, BgpPeer *peer,
                      const char *inst = NULL) {
        BgpAttrPtr attr_ptr;

        // Create a BgpAttrSpec to mimic a eBGP learnt route with Origin,
        // AS Path NextHop and Local Pref.
        BgpAttrSpec attr_spec;

        BgpAttrOrigin origin(BgpAttrOrigin::IGP);
        attr_spec.push_back(&origin);

        AsPathSpec path_spec;
        AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
        path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        path_seg->path_segment.push_back(65534);
        path_spec.path_segments.push_back(path_seg);
        attr_spec.push_back(&path_spec);

        BgpAttrNextHop nexthop(0x7f00007f);
        attr_spec.push_back(&nexthop);

        BgpAttrLocalPref local_pref(100);
        attr_spec.push_back(&local_pref);

        attr_ptr = a_.get()->attr_db()->Locate(attr_spec);

        // Find the inet.0 table in A and B.
        DB *db_a = a_.get()->database();
        InetTable *table_a = static_cast<InetTable *>(db_a->FindTable(
                inst ? string(inst) + ".inet.0" : "inet.0"));
        assert(table_a);

        // Create 3 IPv4 prefixes and the corresponding keys.
        const Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));

        const InetTable::RequestKey key(prefix, peer);

        DBRequest req;

        // Add prefix
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(new InetTable::RequestKey(prefix, peer));
        req.data.reset(new InetTable::RequestData(attr_ptr, 0, 0));
        table_a->Enqueue(&req);
        task_util::WaitForIdle();

        TASK_UTIL_ASSERT_TRUE(table_a->Find(&key) != NULL);
    }

    void DeleteInetRoute(std::string prefix_str, BgpPeer *peer, size_t size,
                         const char *inst = NULL) {

        //
        // Find the inet.0 table in A
        //
        DB *db_a = a_.get()->database();
        InetTable *table_a = static_cast<InetTable *>(db_a->FindTable(
                inst ? string(inst) + ".inet.0" : "inet.0"));
        assert(table_a);

        const Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
        const InetTable::RequestKey key(prefix, peer);
        DBRequest req;

        // Delete prefix
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new InetTable::RequestKey(prefix, peer));
        table_a->Enqueue(&req);
        task_util::WaitForIdle();

        TASK_UTIL_ASSERT_TRUE(table_a->Find(&key) == NULL);
        TASK_UTIL_EXPECT_EQ(size, table_a->Size());
    }

    void AddInetVpnRoute(std::string prefix_str, BgpPeer *peer) {
        BgpAttrPtr attr_ptr;

        // Create a BgpAttrSpec to mimic a eBGP learnt route with Origin,
        // AS Path NextHop and Local Pref.
        BgpAttrSpec attr_spec;

        BgpAttrOrigin origin(BgpAttrOrigin::IGP);
        attr_spec.push_back(&origin);

        AsPathSpec path_spec;
        AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
        path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        path_seg->path_segment.push_back(65534);
        path_spec.path_segments.push_back(path_seg);
        attr_spec.push_back(&path_spec);

        BgpAttrNextHop nexthop(0x7f00007f);
        attr_spec.push_back(&nexthop);

        BgpAttrLocalPref local_pref(100);
        attr_spec.push_back(&local_pref);

        attr_ptr = a_.get()->attr_db()->Locate(attr_spec);

        // Find the inet.0 table in A and B.
        DB *db_a = a_.get()->database();
        InetVpnTable *table_a =
            static_cast<InetVpnTable *>(db_a->FindTable("bgp.l3vpn.0"));
        assert(table_a);

        // Create 3 IPv4 prefixes and the corresponding keys.
        const InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));

        const InetVpnTable::RequestKey key(prefix, peer);

        DBRequest req;

        // Add prefix
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(new InetVpnTable::RequestKey(prefix, peer));
        req.data.reset(new InetVpnTable::RequestData(attr_ptr, 0, 0));
        table_a->Enqueue(&req);
        task_util::WaitForIdle();

        TASK_UTIL_ASSERT_TRUE(table_a->Find(&key) != NULL);
    }

    void DeleteInetVpnRoute(string prefix_str, BgpPeer *peer, size_t size) {
        DB *db_a = a_.get()->database();
        InetVpnTable *table_a =
            static_cast<InetVpnTable *>(db_a->FindTable("bgp.l3vpn.0"));
        assert(table_a);

        // Create 3 IPv4 prefixes and the corresponding keys.
        const InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
        const InetVpnTable::RequestKey key(prefix, peer);
        DBRequest req;

        //
        // Delete prefix
        //
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new InetVpnTable::RequestKey(prefix, peer));
        table_a->Enqueue(&req);
        task_util::WaitForIdle();

        TASK_UTIL_ASSERT_TRUE(table_a->Find(&key) == NULL);
        TASK_UTIL_EXPECT_EQ(size, table_a->Size());
    }

    static void ValidateShowRouteSandeshResponse(Sandesh *sandesh,
        vector<int> &result, int called_from_line) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);

        EXPECT_EQ(result.size(), resp->get_tables().size());

        cout << "From line number: " << called_from_line << endl;
        cout << "*****************************************************" << endl;
        for (size_t i = 0; i < resp->get_tables().size(); i++) {
            EXPECT_EQ(result[i], resp->get_tables()[i].routes.size());
            cout << resp->get_tables()[i].routing_instance << " "
                 << resp->get_tables()[i].routing_table_name << endl;
            for (size_t j = 0; j < resp->get_tables()[i].routes.size(); j++) {
                cout << resp->get_tables()[i].routes[j].prefix << " "
                     << resp->get_tables()[i].routes[j].paths.size() << endl;
            }
        }
        cout << "*****************************************************" << endl;
        validate_done_ = true;
    }

    static void ValidateShowRouteVrfSandeshResponse(Sandesh *sandesh,
        const string vrf, const char *prefix, int called_from_line) {
        ShowRouteVrfResp *resp = dynamic_cast<ShowRouteVrfResp *>(sandesh);
        EXPECT_TRUE(resp != NULL);
        EXPECT_EQ((prefix ? prefix : ""), resp->get_route().get_prefix());
        cout << "From line number: " << called_from_line << endl;
        cout << "*****************************************************" << endl;
        cout << vrf << " " << (prefix ? string(prefix) : "NULL") << endl;
        cout << "*****************************************************" << endl;
        validate_done_ = true;
    }

    auto_ptr<EventManager> evm_;
    auto_ptr<ServerThread> thread_;
    auto_ptr<BgpServerTest> a_;
    auto_ptr<BgpServerTest> b_;
    BgpPeerTest *peers_[3];
};

bool ShowRouteTestBase::validate_done_;

class ShowRouteTest1 : public ShowRouteTestBase {
};

TEST_F(ShowRouteTest1, Basic) {
    Configure();
    task_util::WaitForIdle();

    // Create 3 IPv4 prefixes and the corresponding keys.
    AddInetRoute("192.168.240.0/20", peers_[0]);
    AddInetRoute("192.168.242.0/24", peers_[1]); // Longer prefix
    AddInetRoute("192.168.3.0/24", peers_[2]);

    AddInetRoute("192.240.11.0/12", peers_[0], "red");
    AddInetRoute("192.168.12.0/24", peers_[1], "red");
    AddInetRoute("192.168.13.0/24", peers_[2], "red");

    AddInetRoute("192.240.11.0/12", peers_[0], "blue");
    AddInetRoute("192.168.12.0/24", peers_[1], "blue");
    AddInetRoute("192.168.23.0/24", peers_[2], "blue");

    AddInetVpnRoute("2:20:192.240.11.0/12", peers_[0]);
    AddInetVpnRoute("2:20:192.242.22.0/24", peers_[1]); // Longer prefix
    AddInetVpnRoute("2:20:192.168.33.0/24", peers_[2]);

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    std::vector<int> result = list_of(3)(3)(3)(3);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Exact match for 192.168.242.0/24
    show_req = new ShowRouteReq;
    result = list_of(1);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_prefix("192.168.242.0/24");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // longest match for 192.168.240.0/20 in inet.0 table.
    show_req = new ShowRouteReq;
    result = list_of(2);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_prefix("192.168.240.0/20");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // longest match for 2:20:192.240.11.0/12 in bgp.l3vpn.0 table.
    show_req = new ShowRouteReq;
    result = list_of(2);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_prefix("2:20:192.240.11.0/12");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    show_req = new ShowRouteReq;
    result = list_of(2);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance(BgpConfigManager::kMasterInstance);
    show_req->set_start_routing_table("inet.0");
    show_req->set_start_prefix("192.168.3.0/24");
    show_req->set_count(2);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    show_req = new ShowRouteReq;
    result = list_of(3)(3);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_routing_instance(
            "default-domain:default-project:ip-fabric:__default__");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    show_req = new ShowRouteReq;
    result = list_of(3);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_routing_table("blue.inet.0");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Delete all the routes added
    DeleteInetRoute("192.168.240.0/20", peers_[0], 2);
    DeleteInetRoute("192.168.242.0/24", peers_[1], 1);
    DeleteInetRoute("192.168.3.0/24", peers_[2], 0);

    DeleteInetRoute("192.240.11.0/12", peers_[0], 2, "red");
    DeleteInetRoute("192.168.12.0/24", peers_[1], 1, "red");
    DeleteInetRoute("192.168.13.0/24", peers_[2], 0, "red");

    DeleteInetRoute("192.240.11.0/12", peers_[0], 2, "blue");
    DeleteInetRoute("192.168.12.0/24", peers_[1], 1, "blue");
    DeleteInetRoute("192.168.23.0/24", peers_[2], 0, "blue");

    DeleteInetVpnRoute("2:20:192.240.11.0/12", peers_[0], 2);
    DeleteInetVpnRoute("2:20:192.242.22.0/24", peers_[1], 1);
    DeleteInetVpnRoute("2:20:192.168.33.0/24", peers_[2], 0);
}

class ShowRouteTest2 : public ShowRouteTestBase {
protected:
    virtual void SetUp() {
        ShowRouteTestBase::SetUp();
        Configure();
        task_util::WaitForIdle();

        AddInetRoute("192.168.11.0/24", peers_[0], "red");
        AddInetRoute("192.168.12.0/24", peers_[1], "red");
        AddInetRoute("192.168.13.0/24", peers_[2], "red");

        AddInetRoute("192.168.11.0/24", peers_[0], "blue");
        AddInetRoute("192.168.12.0/24", peers_[1], "blue");
        AddInetRoute("192.168.13.0/24", peers_[2], "blue");
    }

    virtual void TearDown() {
        DeleteInetRoute("192.168.11.0/24", peers_[0], 2, "red");
        DeleteInetRoute("192.168.12.0/24", peers_[1], 1, "red");
        DeleteInetRoute("192.168.13.0/24", peers_[2], 0, "red");

        DeleteInetRoute("192.168.11.0/24", peers_[0], 2, "blue");
        DeleteInetRoute("192.168.12.0/24", peers_[1], 1, "blue");
        DeleteInetRoute("192.168.13.0/24", peers_[2], 0, "blue");

        task_util::WaitForIdle();
        ShowRouteTestBase::TearDown();
    }
};

// Limit routes by instance.
TEST_F(ShowRouteTest2, ExactInstance1) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(3);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_routing_instance(instance);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by non-existent instance.
TEST_F(ShowRouteTest2, ExactInstance2) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "green", "blue1", "red1" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_routing_instance(instance);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by table.
TEST_F(ShowRouteTest2, ExactTable1) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(3);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_routing_table(table);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by non-existent table.
TEST_F(ShowRouteTest2, ExactTable2) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = {
        "blu.inet.0", "red.inet.1", "inetmcast", "inetmcast.0"
    };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_routing_table(table);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by exact prefix.
TEST_F(ShowRouteTest2, ExactPrefix1) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_list[] = {
        "192.168.11.0/24", "192.168.12.0/24", "192.168.13.0/24"
    };
    BOOST_FOREACH(const char *prefix, prefix_list) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1)(1);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix(prefix);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance and exact prefix.
TEST_F(ShowRouteTest2, ExactPrefix2) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.12.0/24");
        show_req->set_routing_instance(instance);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by table and exact prefix.
TEST_F(ShowRouteTest2, ExactPrefix3) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.12.0/24");
        show_req->set_routing_table(table);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix1) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_formats[] = { "192.168.0.0/16", "192.168/16" };
    BOOST_FOREACH(const char *prefix, prefix_formats) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(3)(3);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix(prefix);
        show_req->set_longer_match(true);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by non-existent matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix2) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_prefix("192.169.0.0/16");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);
}

// Limit routes by instance and matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix3) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(3);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.0.0/16");
        show_req->set_longer_match(true);
        show_req->set_routing_instance(instance);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by non-existent instance and matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix4) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "green", "blue1", "red1" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.0.0/16");
        show_req->set_longer_match(true);
        show_req->set_routing_instance(instance);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by table and matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix5) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(3);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.0.0/16");
        show_req->set_longer_match(true);
        show_req->set_routing_table(table);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by non-existent table and matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix6) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "green.inet.0", "blue.inet.1", "red.inet.1" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.0.0/16");
        show_req->set_longer_match(true);
        show_req->set_routing_table(table);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix7) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_list[] = {
        "192.168.11.0/24", "192.168.12.0/24", "192.168.13.0/24"
    };
    BOOST_FOREACH(const char *prefix, prefix_list) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1)(1);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix(prefix);
        show_req->set_longer_match(true);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Start from middle of blue table and go through red table.
TEST_F(ShowRouteTest2, StartPrefix1) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(2)(3);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("blue");
    show_req->set_start_routing_table("blue.inet.0");
    show_req->set_start_prefix("192.168.12.0/24");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);
}

// Start from middle of blue table and go through part of red table.
// Limit number of routes by count.
TEST_F(ShowRouteTest2, StartPrefix2) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(2)(2);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("blue");
    show_req->set_start_routing_table("blue.inet.0");
    show_req->set_start_prefix("192.168.12.0/24");
    show_req->set_count(4);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);
}

// Start from middle of red table and go through rest of red table.
TEST_F(ShowRouteTest2, StartPrefix3) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(2);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("red");
    show_req->set_start_routing_table("red.inet.0");
    show_req->set_start_prefix("192.168.12.0/24");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);
}

// Start from middle of blue table and go through red table.
// Limit routes by exact prefix.
TEST_F(ShowRouteTest2, StartPrefix4) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(1)(1);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("blue");
    show_req->set_start_routing_table("blue.inet.0");
    show_req->set_start_prefix("192.168.12.0/24");
    show_req->set_prefix("192.168.13.0/24");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);
}

// Start from middle of blue table and don't go through red table.
// Limit routes by exact prefix and count.
TEST_F(ShowRouteTest2, StartPrefix5) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(1);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("blue");
    show_req->set_start_routing_table("blue.inet.0");
    show_req->set_start_prefix("192.168.12.0/24");
    show_req->set_prefix("192.168.13.0/24");
    show_req->set_count(1);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);
}

// Start from middle of red table.
// Limit routes by exact prefix.
TEST_F(ShowRouteTest2, StartPrefix6) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(1);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("red");
    show_req->set_start_routing_table("red.inet.0");
    show_req->set_start_prefix("192.168.12.0/24");
    show_req->set_prefix("192.168.13.0/24");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);
}

// Start from middle of blue/red table.
// Limit routes by instance.
TEST_F(ShowRouteTest2, StartPrefix7) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(2);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_start_routing_instance(instance);
        show_req->set_start_routing_table(string(instance) + ".inet.0");
        show_req->set_start_prefix("192.168.12.0/24");
        show_req->set_routing_instance(instance);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Start from middle of blue/red table.
// Limit routes by count.
TEST_F(ShowRouteTest2, StartPrefix8) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_start_routing_instance(instance);
        show_req->set_start_routing_table(string(instance) + ".inet.0");
        show_req->set_start_prefix("192.168.12.0/24");
        show_req->set_count(1);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Start from middle of blue table and go through red table.
// Limit routes by matching prefix.
TEST_F(ShowRouteTest2, StartPrefix9) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(1)(1);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("blue");
    show_req->set_start_routing_table("blue.inet.0");
    show_req->set_start_prefix("192.168.12.0/24");
    show_req->set_prefix("192.168.13.0/24");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);
}

// Start from middle of blue/red table.
// Limit routes by table.
TEST_F(ShowRouteTest2, StartPrefix10) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(2);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_start_routing_instance(instance);
        show_req->set_start_routing_table(string(instance) + ".inet.0");
        show_req->set_start_prefix("192.168.12.0/24");
        show_req->set_routing_table(string(instance) + ".inet.0");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Start from middle of blue/red table.
// Limit routes by table and count.
TEST_F(ShowRouteTest2, StartPrefix11) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_start_routing_instance(instance);
        show_req->set_start_routing_table(string(instance) + ".inet.0");
        show_req->set_start_prefix("192.168.12.0/24");
        show_req->set_routing_table(string(instance) + ".inet.0");
        show_req->set_count(1);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Start from middle of blue/red table.
// Limit routes by table and matching prefix.
TEST_F(ShowRouteTest2, StartPrefix12) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1);
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_start_routing_instance(instance);
        show_req->set_start_routing_table(string(instance) + ".inet.0");
        show_req->set_start_prefix("192.168.12.0/24");
        show_req->set_routing_table(string(instance) + ".inet.0");
        show_req->set_prefix("192.168.13.0/24");
        show_req->set_longer_match(true);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

class ShowRouteVrfTest : public ShowRouteTest2 {
};

// Lookup prefix in each VRF.
TEST_F(ShowRouteVrfTest, Prefix1) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteVrfReq *show_req = new ShowRouteVrfReq;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteVrfSandeshResponse,
                _1, instance, "192.168.12.0/24", __LINE__));
        show_req->set_vrf(instance);
        show_req->set_prefix("192.168.12.0/24");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Lookup each prefix in blue VRF.
TEST_F(ShowRouteVrfTest, Prefix2) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_list[] = {
        "192.168.11.0/24", "192.168.12.0/24", "192.168.13.0/24"
    };
    BOOST_FOREACH(const char *prefix, prefix_list) {
        ShowRouteVrfReq *show_req = new ShowRouteVrfReq;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteVrfSandeshResponse,
                _1, "blue", prefix, __LINE__));
        show_req->set_vrf("blue");
        show_req->set_prefix(prefix);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Lookup prefix in non-existent VRF.
TEST_F(ShowRouteVrfTest, Prefix3) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue1", "rred" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteVrfReq *show_req = new ShowRouteVrfReq;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteVrfSandeshResponse,
                _1, instance, "", __LINE__));
        show_req->set_vrf(instance);
        show_req->set_prefix("192.168.12.0/24");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Lookup non-existent prefix in each VRF.
TEST_F(ShowRouteVrfTest, Prefix4) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteVrfReq *show_req = new ShowRouteVrfReq;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteVrfSandeshResponse,
                _1, instance, "", __LINE__));
        show_req->set_vrf(instance);
        show_req->set_prefix("192.168.15.0/24");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
}

static void TearDown() {
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
