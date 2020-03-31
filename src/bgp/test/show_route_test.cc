/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_show_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

using namespace boost::assign;
using namespace boost::asio;
using namespace std;
using boost::lexical_cast;
using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;

static const char config_template1[] = "\
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

// Use BgpPeerShowTest to make ToString() unique even with multiple peering
// sessions between the same pair of BgpServers, by overriding with ToUVEKey()
class BgpPeerShowTest : public BgpPeerTest {
public:
    BgpPeerShowTest(BgpServer *server, RoutingInstance *rtinst,
                    const BgpNeighborConfig *config) :
            BgpPeerTest(server, rtinst, config) {
    }
    const std::string &ToString() const { return ToUVEKey(); }
};

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

    void Configure(const char *config_template = config_template1) {
        char config[4096];
        int port_a = a_->session_manager()->GetPort();
        int port_b = b_->session_manager()->GetPort();
        snprintf(config, sizeof(config), config_template, port_a, port_b);
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
            peers_[j]->set_is_ready_fnc(
                boost::bind(&ShowRouteTestBase::IsReady, this));
        }

        InetTable *table_a =
            static_cast<InetTable *>(db_a->FindTable("inet.0"));
        assert(table_a);
    }

    void TableListener(DBTablePartBase *root, DBEntryBase *entry) {
    }

    int Register(const char *inst, const string &name) {
        DB *db_a = a_.get()->database();
        InetTable *table_a = static_cast<InetTable *>(db_a->FindTable(
                inst ? string(inst) + ".inet.0" : "inet.0"));
        assert(table_a);
        return table_a->Register(
            boost::bind(&ShowRouteTestBase::TableListener, this, _1, _2),
            name);
    }

    void Unregister(const char *inst, int id) {
        DB *db_a = a_.get()->database();
        InetTable *table_a = static_cast<InetTable *>(db_a->FindTable(
                inst ? string(inst) + ".inet.0" : "inet.0"));
        assert(table_a);
        table_a->Unregister(id);
    }

    size_t ListenerCount(const char *inst) {
        DB *db_a = a_.get()->database();
        InetTable *table_a = static_cast<InetTable *>(db_a->FindTable(
                inst ? string(inst) + ".inet.0" : "inet.0"));
        assert(table_a);
        return table_a->GetListenerCount();
    }

    void DestroyPathResolver(const char *inst) {
        DB *db_a = a_.get()->database();
        InetTable *table =
            static_cast<InetTable *>(db_a->FindTable(string(inst) + ".inet.0"));
        table->DestroyPathResolver();
    }

    void DestroyRouteAggregator(const char *instance, Address::Family fmly) {
        RoutingInstance *rti =
            a_->routing_instance_mgr()->GetRoutingInstance(instance);
        rti->DestroyRouteAggregator(fmly);
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
                         const char *inst = NULL, bool check_route = true) {

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

        if (check_route) {
            TASK_UTIL_EXPECT_EQ(static_cast<InetRoute *>(NULL),
                                table_a->Find(&key));
        }
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

    static void ValidateShowRouteSandeshResponse(Sandesh *sandesh,
        vector<int> &result, int called_from_line, string next_batch) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);

        cout << "From line number: " << called_from_line << endl;
        EXPECT_EQ(result.size(), resp->get_tables().size());
        size_t retval = next_batch.compare(resp->get_next_batch());
        EXPECT_EQ(retval, 0);

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

    static void ValidateShowRouteSandeshRespSubstr(Sandesh *sandesh,
        vector<int> &result, int called_from_line, string substring) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);

        string next_batch = resp->get_next_batch();
        size_t pos = next_batch.find(substring);
        EXPECT_NE(pos, std::string::npos);

        ValidateShowRouteSandeshResponse(sandesh, result, called_from_line);
    }

    // Caller must make sure that number of elements in sorted_list is the same
    // as the number of routes in the table 'table_name'.
    static void ValidateShowRouteSandeshRespSort(Sandesh *sandesh,
        vector<int> &result, int called_from_line, string table_name,
        string sorted_list[]) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);
        EXPECT_NE(table_name.size(), 0);

        ValidateShowRouteSandeshResponse(sandesh, result, called_from_line);
        // Compare the received route list with the expected route list
        for (size_t i = 0; i < resp->get_tables().size(); ++i) {
            if (resp->get_tables()[i].routing_instance.compare(table_name)
                != 0) {
                continue;
            }
            // Found our table. Now compare all its routes with the sorted
            // list.
            for (size_t j = 0; j < resp->get_tables()[i].routes.size(); ++j) {
                size_t retval = resp->get_tables()[i].routes[j].prefix.
                    compare(sorted_list[j]);
                if (retval != 0) {
                    cout << "Expected " <<  sorted_list[j] << " ,Received "
                         << resp->get_tables()[i].routes[j].prefix << endl;
                }
                EXPECT_EQ(retval, 0);
            }
            break;
        }
    }

    static void ValidateShowRouteListenersSandeshResponse(Sandesh *sandesh,
        vector<int> &ids, vector<string> names, int called_from_line) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);

        EXPECT_EQ(1, resp->get_tables().size());
        const ShowRouteTable &table = resp->get_tables()[0];
        const vector<ShowTableListener> &listeners = table.get_listeners();
        EXPECT_EQ(ids.size(), listeners.size());
        EXPECT_EQ(names.size(), listeners.size());

        cout << "From line number: " << called_from_line << endl;
        cout << "*****************************************************" << endl;
        cout << "Listeners for " << table.routing_table_name << endl;
        for (size_t i = 0; i < listeners.size(); i++) {
            EXPECT_EQ(ids[i], listeners[i].id);
            EXPECT_EQ(names[i], listeners[i].name);
            cout << "Id: " << listeners[i].id << " ";
            cout << "Name: " << listeners[i].name << endl;
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

TEST_F(ShowRouteTest1, SortingTest) {
    Configure();
    task_util::WaitForIdle();

    // Add 10 routes in some random order
    AddInetRoute("1.2.3.0/24", peers_[0], "red");
    AddInetRoute("1.2.3.16/28", peers_[0], "red");
    AddInetRoute("24.0.0.0/8", peers_[0], "red");
    AddInetRoute("1.2.0.0/16", peers_[0], "red");
    AddInetRoute("1.2.3.32/28", peers_[0], "red");
    AddInetRoute("1.2.4.0/24", peers_[0], "red");
    AddInetRoute("12.0.0.0/8", peers_[0], "red");
    AddInetRoute("1.2.3.48/28", peers_[0], "red");
    AddInetRoute("1.2.5.0/24", peers_[0], "red");
    AddInetRoute("4.0.0.0/8", peers_[0], "red");

    // Sort the routes added above in an array. We will compare this list with
    // the received list to check if the routes are properly sorted.
    string sorted_list[] = {"1.2.0.0/16", "1.2.3.0/24",
            "1.2.3.16/28", "1.2.3.32/28", "1.2.3.48/28", "1.2.4.0/24",
            "1.2.5.0/24", "4.0.0.0/8", "12.0.0.0/8", "24.0.0.0/8"};

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    std::vector<int> result = {1, 10};
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshRespSort, _1, result, __LINE__,
                    "red", sorted_list));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    size_t rem_count = 9;
    DeleteInetRoute("1.2.3.0/24", peers_[0], rem_count--, "red");
    DeleteInetRoute("1.2.3.16/28", peers_[0], rem_count--, "red");
    DeleteInetRoute("24.0.0.0/8", peers_[0], rem_count--, "red");
    DeleteInetRoute("1.2.0.0/16", peers_[0], rem_count--, "red");
    DeleteInetRoute("1.2.3.32/28", peers_[0], rem_count--, "red");
    DeleteInetRoute("1.2.4.0/24", peers_[0], rem_count--, "red");
    DeleteInetRoute("12.0.0.0/8", peers_[0], rem_count--, "red");
    DeleteInetRoute("1.2.3.48/28", peers_[0], rem_count--, "red");
    DeleteInetRoute("1.2.5.0/24", peers_[0], rem_count--, "red");
    DeleteInetRoute("4.0.0.0/8", peers_[0], rem_count, "red");
    EXPECT_EQ(rem_count, 0);
}

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

    std::vector<int> result = {3, 3, 1, 3, 3};
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
    result = list_of(1).convert_to_container<vector<int> >();
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
    result = list_of(2).convert_to_container<vector<int> >();
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
    result = list_of(2).convert_to_container<vector<int> >();
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
    result = list_of(2).convert_to_container<vector<int> >();
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
    result = list_of(3)(1)(3).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_routing_instance(
            "default-domain:default-project:ip-fabric:__default__");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    show_req = new ShowRouteReq;
    result = list_of(3).convert_to_container<vector<int> >();
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

TEST_F(ShowRouteTest1, TableListeners) {
    Configure();
    TASK_UTIL_EXPECT_EQ(0, ListenerCount("blue"));

    AddInetRoute("192.240.11.0/12", peers_[0], "blue");
    AddInetRoute("192.168.12.0/24", peers_[1], "blue");
    AddInetRoute("192.168.23.0/24", peers_[2], "blue");

    string name0("Blue Listener 0");
    string name1("Blue Listener 1");
    string name2("Blue Listener 2");
    string name3("Blue Listener 3");
    int id0 = Register("blue", name0);
    int id1 = Register("blue", name1);
    int id2 = Register("blue", name2);
    int id3 = Register("blue", name3);
    TASK_UTIL_EXPECT_EQ(4, ListenerCount("blue"));

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    vector<int> ids = list_of(id0)(id1)(id2)(id3)
        .convert_to_container<vector<int> >();
    vector<string> names = list_of(name0)(name1)(name2)(name3)
        .convert_to_container<vector<string> >();
    ShowRouteReq *show_req = new ShowRouteReq;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteListenersSandeshResponse, _1, ids, names,
            __LINE__));
    show_req->set_routing_table("blue.inet.0");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    Unregister("blue", id2);
    TASK_UTIL_EXPECT_EQ(3, ListenerCount("blue"));

    ids = list_of(id0)(id1)(id3).convert_to_container<vector<int> >();
    names = list_of(name0)(name1)(name3).convert_to_container<vector<string> >();
    show_req = new ShowRouteReq;
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteListenersSandeshResponse, _1, ids, names,
            __LINE__));
    show_req->set_routing_table("blue.inet.0");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    Unregister("blue", id0);
    Unregister("blue", id1);
    Unregister("blue", id3);
    TASK_UTIL_EXPECT_EQ(0, ListenerCount("blue"));

    DeleteInetRoute("192.240.11.0/12", peers_[0], 2, "blue");
    DeleteInetRoute("192.168.12.0/24", peers_[1], 1, "blue");
    DeleteInetRoute("192.168.23.0/24", peers_[2], 0, "blue");
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
        vector<int> result = list_of(3).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(3).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(1)(1).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(3)(3).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(3).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(3).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(1)(1).convert_to_container<vector<int> >();
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

// Limit routes by shorter matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix8) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_formats[] = {
        "192.168.11.255/32",
        "192.168.11.254/31",
        "192.168.11.252/30",
        "192.168.11.248/29",
        "192.168.11.240/28"
    };
    BOOST_FOREACH(const char *prefix, prefix_formats) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1)(1).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix(prefix);
        show_req->set_shorter_match(true);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by non-existent shorter matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix9) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_formats[] = {
        "192.168.14.255/32",
        "192.168.14.254/31",
        "192.168.14.252/30",
        "192.168.14.248/29",
        "192.168.14.240/28"
    };
    BOOST_FOREACH(const char *prefix, prefix_formats) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix(prefix);
        show_req->set_shorter_match(true);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance and shorter matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix10) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "blue", "red" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.11.240/28");
        show_req->set_shorter_match(true);
        show_req->set_routing_instance(instance);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by non-existent instance and shorter matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix11) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *instance_names[] = { "green", "blue1", "red1" };
    BOOST_FOREACH(const char *instance, instance_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.11.240/28");
        show_req->set_shorter_match(true);
        show_req->set_routing_instance(instance);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by table and shorter matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix12) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.11.240/28");
        show_req->set_shorter_match(true);
        show_req->set_routing_table(table);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by non-existent table and shorter matching prefix.
TEST_F(ShowRouteTest2, MatchingPrefix13) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "green.inet.0", "blue.inet.1", "red.inet.1" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix("192.168.11.240/28");
        show_req->set_shorter_match(true);
        show_req->set_routing_table(table);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by regex prefix.
// Regex matches all prefixes in both tables.
TEST_F(ShowRouteTest2, MatchingPrefix14) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_formats[] = {
        "192.168",
        "/24",
        "168.1[1-3].0/24"
    };
    const vector<int> result[] = {
    list_of(3)(1)(3).convert_to_container<vector<int> >(),
    list_of(3)(3).convert_to_container<vector<int> >(),
    list_of(3)(3).convert_to_container<vector<int> >()
    };
    int i = 0;
    BOOST_FOREACH(const char *prefix, prefix_formats) {
        ShowRouteReq *show_req = new ShowRouteReq;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result[i], __LINE__));
        show_req->set_prefix(prefix);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    i++;
    }
}

// Limit routes by regex prefix.
// Regex matches subset of prefixes in both tables.
TEST_F(ShowRouteTest2, MatchingPrefix15) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_formats[] = {
        "2.168.1[1-2]",
        "192.168.1[1-2]",
        "168.1[1-2].0/24"
    };
    BOOST_FOREACH(const char *prefix, prefix_formats) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(2)(2).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix(prefix);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by regex prefix.
// Regex does not match any prefixes.
TEST_F(ShowRouteTest2, MatchingPrefix16) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_formats[] = {
        "192.168.14",
        "168.17",
        "168.1[4-9]",
    };
    BOOST_FOREACH(const char *prefix, prefix_formats) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix(prefix);
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by regex prefix.
// Use invalid regex pattern.
TEST_F(ShowRouteTest2, Invalidregex) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *prefix_formats[] = {
        "192.168.14",
        "168.17",
        "168.1[4-9", // Invalid due to missing ']'
    };
    BOOST_FOREACH(const char *prefix, prefix_formats) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
        show_req->set_prefix(prefix);
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
    vector<int> result = list_of(2)(1)(3).convert_to_container<vector<int> >();
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
    vector<int> result = list_of(2)(1)(1).convert_to_container<vector<int> >();
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
    vector<int> result = list_of(2).convert_to_container<vector<int> >();
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
    vector<int> result = list_of(1)(1).convert_to_container<vector<int> >();
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
    vector<int> result = list_of(1).convert_to_container<vector<int> >();
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
    vector<int> result = list_of(1).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(2).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
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
    vector<int> result = list_of(1)(1).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(2).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
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
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
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

class ShowRouteTest3 : public ShowRouteTestBase {
protected:
    // This value is the same as ShowRouteHandler::kMaxCount and indicates the
    // maximum number of routes that will be returned. Its not used but is here
    // just for reference.
    static const uint32_t kMaxCount = 100;
    virtual void SetUp() {
        ShowRouteTestBase::SetUp();
        Configure();
        task_util::WaitForIdle();
        sandesh_context.bgp_server = a_.get();
        sandesh_context.set_test_mode(true);
        Sandesh::set_client_context(&sandesh_context);
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        ShowRouteTestBase::TearDown();
    }
    BgpSandeshContext sandesh_context;
};

TEST_F(ShowRouteTest3, PageLimit1) {

    // Add kMaxCount routes.
    std::string plen = "/32";
    in_addr src;
    int ip1 = 0x01020000;
    for (int i = 0; i < 100; ++i) {
        src.s_addr = htonl(ip1 | i);
        string ip = string(inet_ntoa(src)) + plen;
        AddInetRoute(ip, peers_[0], "red");
    }
    // Add another route with prefix that will make it the last route so that
    // its easy to verify.
    AddInetRoute("10.1.1.0/24", peers_[0], "red");

    // (kMaxCount+1) routes. Read should return the first kMaxCount entries.
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(100).convert_to_container<vector<int> >();
    string next_batch =
        "||||||red||red.inet.0||10.1.1.0/24||0||||||||false||false";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_start_routing_instance("red");
    show_req->set_start_routing_table("red.inet.0");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    DeleteInetRoute("10.1.1.0/24", peers_[0], 100, "red");
    for (int i = 99; i >= 0; --i) {
        src.s_addr = htonl(ip1 | i);
        string ip = string(inet_ntoa(src)) + plen;
        DeleteInetRoute(ip, peers_[0], i, "red");
    }
}

TEST_F(ShowRouteTest3, PageLimit2) {

    // Add (< kMaxCount) routes
    std::string plen = "/32";
    std::string ip = "1.2.3.";
    for (int host = 0; host < 50; ++host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        AddInetRoute(repr.str(), peers_[0], "red");
    }

    // Should get back all the routes.
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(50).convert_to_container<vector<int> >();
    string next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_start_routing_instance("red");
    show_req->set_start_routing_table("red.inet.0");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    for (int host = 0; host < 50; ++host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        DeleteInetRoute(repr.str(), peers_[0], (49 - host), "red");
    }
}

TEST_F(ShowRouteTest3, PageLimit3) {

    // Add equal number of routes in blue and red so that their total is
    // kMaxCount.
    std::string plen = "/32";
    std::string ip = "1.2.3.";
    for (int host = 0; host < 49; ++host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        AddInetRoute(repr.str(), peers_[0], "blue");
        AddInetRoute(repr.str(), peers_[0], "red");
    }

    // Should get back all routes for both instances since:
    // total_routes == kMaxCount.
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(49)(1)(49).convert_to_container<vector<int> >();
    string next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_start_routing_instance("blue");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    for (int host = 0; host < 49; ++host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        DeleteInetRoute(repr.str(), peers_[0], (48 - host), "blue");
        DeleteInetRoute(repr.str(), peers_[0], (48 - host), "red");
    }
}

TEST_F(ShowRouteTest3, PageLimit4) {

    // Add equal number of routes in blue and red so that their total is
    // greater than kMaxCount.
    std::string plen = "/32";
    std::string ip = "1.2.3.";
    for (int host = 0; host < 90; ++host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        AddInetRoute(repr.str(), peers_[0], "blue");
        AddInetRoute(repr.str(), peers_[0], "red");
    }

    // Ask to start with 'blue'. We should get back 90 blue and 10 red routes
    // for a total of kMaxCount.
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(90)(1)(9).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("blue");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for 40 routes. We should get back 40 since its less than kMaxCount.
    show_req = new ShowRouteReq;
    result = list_of(40).convert_to_container<vector<int> >();
    string next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(40);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back kMaxCount. The return count
    // in next_batch should have 80 and the next IP should be 1.2.3.10 in red.
    // We will get back [blue:all routes] and [red:1.2.3.0 to 1.2.3.9].
    show_req = new ShowRouteReq;
    result = list_of(90)(1)(9).convert_to_container<vector<int> >();
    next_batch =
        "||||||red||red.inet.0||1.2.3.9/32||80||||||||false||false";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(180);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for 95 routes. We should get back 90 blue and 5 red routes for a
    // total of kMaxCount.
    show_req = new ShowRouteReq;
    result = list_of(90)(1)(4).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(95);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for 89 routes. We should only get back 89 blue routes and no red
    // routes.
    show_req = new ShowRouteReq;
    result = list_of(89).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(89);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for 91 routes. We should get back 90 blue and 1 red route.
    show_req = new ShowRouteReq;
    result = list_of(90)(1)(1).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(92);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    for (int host = 89; host >= 0; --host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        DeleteInetRoute(repr.str(), peers_[0], host, "blue");
        DeleteInetRoute(repr.str(), peers_[0], host, "red");
    }
}

TEST_F(ShowRouteTest3, PageLimit5) {

    // Add equal number of routes in blue and red so that their total is
    // greater than kMaxCount i.e. 80 each in blue and red.
    // Total 80 - 2 subnets, 10.10.1 and 10.10.2, each with 40 addresses.
    std::string ip = "10.10.";
    std::string plen = "/32";
    for (int subnet = 1; subnet < 3; ++subnet) {
        for (int host = 0; host < 40; ++host) {
            std::ostringstream repr;
            repr << ip << subnet << "." << host << plen;
            AddInetRoute(repr.str(), peers_[0], "blue");
            AddInetRoute(repr.str(), peers_[0], "red");
        }
    }

    // Ask for only blue instance. We should get back all 80 blue routes since
    // its less than kMaxCount.
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(80).convert_to_container<vector<int> >();
    string next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_instance("blue");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for only red instance. We should get back all 80 red routes since
    // its less than kMaxCount.
    show_req = new ShowRouteReq;
    result = list_of(80).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_instance("red");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes only in blue.inet.0. We should get back all 80 routes.
    show_req = new ShowRouteReq;
    result = list_of(80).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_table("blue.inet.0");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes only in red.inet.0. We should get back all 80 routes.
    show_req = new ShowRouteReq;
    result = list_of(80).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_table("red.inet.0");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for a specific prefix that exists in both 'blue' and 'red'. We
    // should get back exactly 2 routes.
    show_req = new ShowRouteReq;
    result = list_of(1)(1).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_prefix("10.10.1.5/32");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match' in blue. We should get back all blue
    // routes since all the routes match the filter.
    show_req = new ShowRouteReq;
    result = list_of(80).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_instance("blue");
    show_req->set_prefix("10.10.0.0/16");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match' in red. We should get back all red
    // routes since all the routes match the filter.
    show_req = new ShowRouteReq;
    result = list_of(80).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_instance("red");
    show_req->set_prefix("10.10.0.0/16");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for 8 routes with 'longer match' in blue. We should get back 8 blue
    // routes.
    show_req = new ShowRouteReq;
    result = list_of(8).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_routing_instance("blue");
    show_req->set_prefix("10.10.0.0/16");
    show_req->set_longer_match(true);
    show_req->set_count(8);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for 15 routes with 'longer match' in red. We should get back 15 red
    // routes.
    show_req = new ShowRouteReq;
    result = list_of(15).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_routing_instance("red");
    show_req->set_prefix("10.10.0.0/16");
    show_req->set_longer_match(true);
    show_req->set_count(15);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match'. Since all routes match this filter,
    // we should get back 80 blue and 20 red routes.
    show_req = new ShowRouteReq;
    result = list_of(80)(20).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_prefix("10.10.0.0/16");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match'. Each instance has 40 routes with
    // this filter. We should get back 40 blue and 40 red routes.
    show_req = new ShowRouteReq;
    result = list_of(40)(40).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_prefix("10.10.1.0/24");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match'. Each instance has 40 routes with
    // this filter. Although we are asking for 81 routes, we should get back
    // 80 routes, 40 blue and 40 red routes.
    show_req = new ShowRouteReq;
    result = list_of(40)(40).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_prefix("10.10.1.0/24");
    show_req->set_longer_match(true);
    show_req->set_count(81);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match' only in 'blue'. Each instance has 40
    // routes with this filter. We should get back only 40 blue routes.
    show_req = new ShowRouteReq;
    result = list_of(40).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_instance("blue");
    show_req->set_prefix("10.10.2.0/24");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match' only in 'red'. Each instance has 40
    // routes with this filter. We should get back only 40 red routes.
    show_req = new ShowRouteReq;
    result = list_of(40).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_instance("red");
    show_req->set_prefix("10.10.1.0/24");
    show_req->set_longer_match(true);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match' only in 'blue'. Each instance has 40
    // routes with this filter. But, we should get back only 30 blue routes.
    show_req = new ShowRouteReq;
    result = list_of(30).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_routing_instance("blue");
    show_req->set_prefix("10.10.1.0/24");
    show_req->set_longer_match(true);
    show_req->set_count(30);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match' only in 'red'. Each instance has 40
    // routes with this filter. But, we should get back only 25 red routes.
    show_req = new ShowRouteReq;
    result = list_of(25).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_routing_instance("red");
    show_req->set_prefix("10.10.2.0/24");
    show_req->set_longer_match(true);
    show_req->set_count(25);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for 65 routes with 'longer match' in all instances. Each instance
    // has 40 routes with this filter. We should get back all 40 blue routes
    // and 25 red routes matching the filter.
    show_req = new ShowRouteReq;
    result = list_of(40)(25).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_prefix("10.10.2.0/24");
    show_req->set_longer_match(true);
    show_req->set_count(65);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match' only in 'blue'. Each instance has 40
    // routes with this filter. Although we are asking for 90, we should get
    // back 40 blue routes.
    show_req = new ShowRouteReq;
    result = list_of(40).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_instance("blue");
    show_req->set_prefix("10.10.2.0/24");
    show_req->set_longer_match(true);
    show_req->set_count(90);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for routes with 'longer match' only in 'red'. Each instance has 40
    // routes with this filter. Although we are asking for 45, we should get
    // back 40 red routes.
    show_req = new ShowRouteReq;
    result = list_of(40).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_routing_instance("red");
    show_req->set_prefix("10.10.1.0/24");
    show_req->set_longer_match(true);
    show_req->set_count(45);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for 80 routes. We will collect 81 i.e collect the first red route
    // too. But, we should pop it off and not send it back. So, we should get
    // routes from only one instance.
    // The next request extends this by 1 and we should get the first red route
    // too.
    show_req = new ShowRouteReq;
    result = list_of(80).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(80);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Should get back the first red route too.
    show_req = new ShowRouteReq;
    result = list_of(80)(1).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(81);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    int cnt = 79;
    for (int subnet = 2; subnet >= 1; --subnet) {
        for (int host = 39; host >= 0; --host) {
            std::ostringstream repr;
            repr << ip << subnet << "." << host << plen;
            DeleteInetRoute(repr.str(), peers_[0], cnt, "blue");
            DeleteInetRoute(repr.str(), peers_[0], cnt, "red");
            --cnt;
        }
    }
}

TEST_F(ShowRouteTest3, PageLimit6) {

    std::string plen = "/32";
    std::string ip = "1.2.3.";
    for (int host = 0; host < 100; ++host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        AddInetRoute(repr.str(), peers_[0], "blue");
        AddInetRoute(repr.str(), peers_[0], "red");
    }

    // Even though we ask for 200 entries, we will get back only kMaxCount.
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(100).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_start_routing_instance("blue");
    show_req->set_count(200);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    for (int host = 99; host >= 0; --host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        DeleteInetRoute(repr.str(), peers_[0], host, "blue");
        DeleteInetRoute(repr.str(), peers_[0], host, "red");
    }
}

// Test the value of 'next_batch' in the response.
TEST_F(ShowRouteTest3, PageLimit7) {

    AddInetRoute("10.1.1.0/24", peers_[0], "blue");
    AddInetRoute("20.1.1.0/24", peers_[0], "blue");
    AddInetRoute("30.1.1.0/24", peers_[0], "blue");
    AddInetRoute("40.1.1.0/24", peers_[0], "blue");
    AddInetRoute("50.1.1.0/24", peers_[0], "red");
    AddInetRoute("60.1.1.0/24", peers_[0], "red");
    AddInetRoute("70.1.1.0/24", peers_[0], "red");
    AddInetRoute("80.1.1.0/24", peers_[0], "red");

    // Request only the first route; next_batch should be the second route.
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(1).convert_to_container<vector<int> >();
    string next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(1);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Number requested is at the border of 'blue' and 'red'. next_batch should
    // be the first route in 'red'.
    show_req = new ShowRouteReq;
    result = list_of(4).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(4);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Number requested is in the middle of the second instance i.e. 'red'.
    // next_batch should be the third route in 'red'.
    show_req = new ShowRouteReq;
    result = list_of(4)(1)(1).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(6);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Number requested is at the end of the second instance, 'red'.
    // 'next_batch' should be empty.
    show_req = new ShowRouteReq;
    result = list_of(4)(1)(3).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(8);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Number requested is greater than total number of routes.
    // 'next_batch' should be empty.
    show_req = new ShowRouteReq;
    result = list_of(4)(1)(4).convert_to_container<vector<int> >();
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(9);
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    DeleteInetRoute("10.1.1.0/24", peers_[0], 3, "blue");
    DeleteInetRoute("20.1.1.0/24", peers_[0], 2, "blue");
    DeleteInetRoute("30.1.1.0/24", peers_[0], 1, "blue");
    DeleteInetRoute("40.1.1.0/24", peers_[0], 0, "blue");

    DeleteInetRoute("50.1.1.0/24", peers_[0], 3, "red");
    DeleteInetRoute("60.1.1.0/24", peers_[0], 2, "red");
    DeleteInetRoute("70.1.1.0/24", peers_[0], 1, "red");
    DeleteInetRoute("80.1.1.0/24", peers_[0], 0, "red");
}

TEST_F(ShowRouteTest3, PageLimit8) {

    // Add equal number of routes in blue and red so that their total is
    // greater than kMaxCount.
    std::string plen = "/32";
    std::string ip = "1.2.3.";
    for (int host = 0; host < 90; ++host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        AddInetRoute(repr.str(), peers_[0], "blue");
        AddInetRoute(repr.str(), peers_[0], "red");
    }

    // Ask for all 180 routes. We should get back kMaxCount. The return count
    // in next_batch should have 80 and the next IP should be 1.2.3.10 in red.
    // We will get back [blue:all routes] and [red:1.2.3.0 to 1.2.3.9].
    // matching source based filter is also specified.
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(90)(10).convert_to_container<vector<int> >();
    string next_batch = "||||||red||red.inet.0||1.2.3.10/32||80||" +
                        peers_[0]->ToString() + "||||||false||false";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(180);
    show_req->set_source(peers_[0]->ToString());
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back kMaxCount. The return count
    // in next_batch should have 80 and the next IP should be 1.2.3.10 in red.
    // We will get back [blue:all routes] and [red:1.2.3.0 to 1.2.3.9].
    // matching protocol based filter is also specified.
    show_req = new ShowRouteReq;
    result = list_of(90)(10).convert_to_container<vector<int> >();
    next_batch =
        "||||||red||red.inet.0||1.2.3.10/32||80||||BGP||||false||false";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(180);
    show_req->set_protocol("BGP");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back kMaxCount. The return count
    // in next_batch should have 80 and the next IP should be 1.2.3.10 in red.
    // We will get back [blue:all routes] and [red:1.2.3.0 to 1.2.3.9].
    // matching source and protocol based filters are also specified.
    show_req = new ShowRouteReq;
    result = list_of(90)(10).convert_to_container<vector<int> >();
    next_batch = "||||||red||red.inet.0||1.2.3.10/32||80||" +
                        peers_[0]->ToString() + "||BGP||||false||false";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(180);
    show_req->set_source(peers_[0]->ToString());
    show_req->set_protocol("BGP");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back kMaxCount. The return count
    // in next_batch should have 80 and the next IP should be 1.2.3.10 in red.
    // We will get back [blue:all routes] and [red:1.2.3.0 to 1.2.3.9].
    // matching source and family based filters are also specified.
    show_req = new ShowRouteReq;
    result = list_of(90)(10).convert_to_container<vector<int> >();
    next_batch = "||||||red||red.inet.0||1.2.3.10/32||80||" +
                        peers_[0]->ToString() + "||||inet||false||false";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(180);
    show_req->set_source(peers_[0]->ToString());
    show_req->set_family("inet");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back kMaxCount. The return count
    // in next_batch should have 80 and the next IP should be 1.2.3.10 in red.
    // We will get back [blue:all routes] and [red:1.2.3.0 to 1.2.3.9].
    // matching source, protocol and family based filters are also specified.
    show_req = new ShowRouteReq;
    result = list_of(90)(10).convert_to_container<vector<int> >();
    next_batch = "||||||red||red.inet.0||1.2.3.10/32||80||" +
                        peers_[0]->ToString() + "||BGP||inet||false||false";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    show_req->set_count(180);
    show_req->set_source(peers_[0]->ToString());
    show_req->set_protocol("BGP");
    show_req->set_family("inet");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back none as non-matching source
    // and protocol based filters are also specified.
    show_req = new ShowRouteReq;
    result.clear();
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_count(180);
    show_req->set_source(peers_[1]->ToString());
    show_req->set_protocol("BGP");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back none as matching source
    // and non-matching protocol based filters are also specified.
    show_req = new ShowRouteReq;
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_count(180);
    show_req->set_source(peers_[0]->ToString());
    show_req->set_protocol("XMPP");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back none as non-matching source
    // and non-matching protocol based filters are also specified.
    show_req = new ShowRouteReq;
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_count(180);
    show_req->set_source(peers_[1]->ToString());
    show_req->set_protocol("XMPP");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back none as matching source
    // and non-matching family based filters are also specified.
    show_req = new ShowRouteReq;
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_count(180);
    show_req->set_source(peers_[0]->ToString());
    show_req->set_family("inet6");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back none as non-matching source
    // and matching family based filters are also specified.
    show_req = new ShowRouteReq;
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_count(180);
    show_req->set_source(peers_[1]->ToString());
    show_req->set_family("inet");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Ask for all 180 routes. We should get back none as non-matching source
    // and non-matching family based filters are also specified.
    show_req = new ShowRouteReq;
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__));
    show_req->set_count(180);
    show_req->set_source(peers_[1]->ToString());
    show_req->set_family("inet6");
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    for (int host = 89; host >= 0; --host) {
        std::ostringstream repr;
        repr << ip << host << plen;
        DeleteInetRoute(repr.str(), peers_[0], host, "blue");
        DeleteInetRoute(repr.str(), peers_[0], host, "red");
    }
}

TEST_F(ShowRouteTest3, SimulateClickingNextBatch) {

    // Add 400 routes and read them in 4 batches of 100 routes each.
    std::string plen = "/32";
    in_addr src;
    int ip1 = 0x01020000;
    for (int i = 0; i < 400; ++i) {
        src.s_addr = htonl(ip1 | i);
        string ip = string(inet_ntoa(src)) + plen;
        AddInetRoute(ip, peers_[0], "red");
    }

    // (kMaxCount+1) routes. We should get [1.2.0.0 to 1.2.0.99]
    // i.e. 100 entries
    ShowRouteReq *show_req = new ShowRouteReq;
    vector<int> result = list_of(1)(99).convert_to_container<vector<int> >();
    string next_batch =
        "||||||red||red.inet.0||1.2.0.99/32||300||||||||false||false";
    show_req->set_count(400);
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Fill values from next_batch above. We should get
    // [1.2.0.100 to 1.2.0.199] i.e. 100 entries.
    show_req = new ShowRouteReq;
    show_req->set_start_routing_instance("red");
    show_req->set_start_routing_table("red.inet.0");
    show_req->set_start_prefix("1.2.0.100/32");
    show_req->set_count(300);
    show_req->set_longer_match(false);
    next_batch = "||||||red||red.inet.0||1.2.0.200/32||200||||||||false||false";
    result = list_of(100).convert_to_container<vector<int> >();
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Fill values from next_batch above. We should get
    // [1.2.0.200 to 1.2.0.255] AND [1.2.1.0 to 1.2.1.43] i.e. (56+44)
    show_req = new ShowRouteReq;
    show_req->set_start_routing_instance("red");
    show_req->set_start_routing_table("red.inet.0");
    show_req->set_start_prefix("1.2.0.200/32");
    show_req->set_count(200);
    show_req->set_longer_match(false);
    next_batch = "||||||red||red.inet.0||1.2.1.44/32||100||||||||false||false";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    // Fill values from next_batch above. We should get
    // [1.2.1.44 to 1.2.1.143] i.e. last 100 entries.
    show_req = new ShowRouteReq;
    show_req->set_start_routing_instance("red");
    show_req->set_start_routing_table("red.inet.0");
    show_req->set_start_prefix("1.2.1.44/32");
    show_req->set_count(100);
    show_req->set_longer_match(false);
    next_batch = "";
    Sandesh::set_response_callback(boost::bind(
        ValidateShowRouteSandeshResponse, _1, result, __LINE__, next_batch));
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    TASK_UTIL_EXPECT_EQ(true, validate_done_);

    for (int i = 399; i >= 0; --i) {
        src.s_addr = htonl(ip1 | i);
        string ip = string(inet_ntoa(src)) + plen;
        DeleteInetRoute(ip, peers_[0], i, "red");
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

class ShowRouteTest4 : public ShowRouteTestBase {
protected:
    virtual void SetUp() {
        ShowRouteTestBase::SetUp();
        Configure();
        task_util::WaitForIdle();

        AddInetRoute("192.168.11.0/24", peers_[0], "red");
        AddInetRoute("192.168.11.0/24", peers_[1], "red");
        AddInetRoute("192.168.11.0/24", NULL, "red");

        AddInetRoute("192.168.12.0/24", peers_[0], "blue");
        AddInetRoute("192.168.12.0/24", peers_[1], "blue");
        AddInetRoute("192.168.12.0/24", NULL, "blue");

        AddInetRoute("192.168.13.0/24", NULL, "red");
        AddInetRoute("192.168.13.0/24", NULL, "blue");
    }

    virtual void TearDown() {
        DeleteInetRoute("192.168.11.0/24", peers_[0], 2, "red", false);
        DeleteInetRoute("192.168.11.0/24", peers_[1], 2, "red", false);
        DeleteInetRoute("192.168.11.0/24", NULL, 1, "red");

        DeleteInetRoute("192.168.12.0/24", peers_[0], 2, "blue", false);
        DeleteInetRoute("192.168.12.0/24", peers_[1], 2, "blue", false);
        DeleteInetRoute("192.168.12.0/24", NULL, 1, "blue");

        DeleteInetRoute("192.168.13.0/24", NULL, 0, "red");
        DeleteInetRoute("192.168.13.0/24", NULL, 0, "blue");

        task_util::WaitForIdle();
        ShowRouteTestBase::TearDown();
    }
};

// Limit routes by instance and source.
TEST_F(ShowRouteTest4, Source1) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_source(peers_[1]->ToString());
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance and non-existent source.
TEST_F(ShowRouteTest4, Source2) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_source(peers_[2]->ToString());
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance and protocol.
TEST_F(ShowRouteTest4, Source3) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_protocol(peers_[1]->IsXmppPeer() ? "XMPP" : "BGP");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance and non-existent protocol.
TEST_F(ShowRouteTest4, Source4) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_protocol(!peers_[1]->IsXmppPeer() ? "XMPP" : "BGP");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance, source and protocol.
TEST_F(ShowRouteTest4, Source5) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_source(peers_[1]->ToString());
        show_req->set_protocol(peers_[1]->IsXmppPeer() ? "XMPP" : "BGP");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance, non-existent source and non-existent protocol.
TEST_F(ShowRouteTest4, Source6) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_source(peers_[2]->ToString());
        show_req->set_protocol(!peers_[1]->IsXmppPeer() ? "XMPP" : "BGP");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance, existent source and non-existent protocol.
TEST_F(ShowRouteTest4, Source7) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_source(peers_[1]->ToString());
        show_req->set_protocol(!peers_[1]->IsXmppPeer() ? "XMPP" : "BGP");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance, non-existent source and existent protocol.
TEST_F(ShowRouteTest4, Source8) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_source(peers_[2]->ToString());
        show_req->set_protocol(peers_[1]->IsXmppPeer() ? "XMPP" : "BGP");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance and family.
TEST_F(ShowRouteTest4, Source9) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(2).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_family("inet");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance, source and family.
TEST_F(ShowRouteTest4, Source10) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_source(peers_[1]->ToString());
        show_req->set_family("inet");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance, source, protocol and family.
TEST_F(ShowRouteTest4, Source11) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result = list_of(1).convert_to_container<vector<int> >();
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_protocol("BGP");
        show_req->set_source(peers_[1]->ToString());
        show_req->set_family("inet");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance and non-existent family.
TEST_F(ShowRouteTest4, Source12) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_family("l3vpn");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Limit routes by instance, non-existent source, non-existent protocol and
// non-existent family.
TEST_F(ShowRouteTest4, Source13) {
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);

    const char *table_names[] = { "blue.inet.0", "red.inet.0" };
    BOOST_FOREACH(const char *table, table_names) {
        ShowRouteReq *show_req = new ShowRouteReq;
        vector<int> result;
        Sandesh::set_response_callback(
            boost::bind(ValidateShowRouteSandeshResponse, _1, result,
                        __LINE__));
        show_req->set_routing_table(table);
        show_req->set_protocol("XMPP");
        show_req->set_source(peers_[2]->ToString());
        show_req->set_family("l3vpn");
        validate_done_ = false;
        show_req->HandleRequest();
        show_req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }
}

// Targeted test for ShowRouteHandler::ConvertReqIterateToReq()
TEST_F(ShowRouteTest4, TestConvertReqIterateToReq) {
    ShowRouteReqIterate *req_iterate = new ShowRouteReqIterate();
    ShowRouteReq *req = new ShowRouteReq();

    int i = 1;
    req_iterate->set_route_info(lexical_cast<string>(i));
    while (true) {
        // Expect false until all 12 parameters are encoded.
        EXPECT_EQ(i == 12, ShowRouteHandler::ConvertReqIterateToReq(req_iterate,
                                                                    req));
        if (i == 12)
            break;
        if (++i == 11) {
            req_iterate->set_route_info(req_iterate->get_route_info() +
                ShowRouteHandler::kIterSeparator + "true");
        } else if (i == 12) {
            req_iterate->set_route_info(req_iterate->get_route_info() +
                ShowRouteHandler::kIterSeparator + "false");
        } else {
            req_iterate->set_route_info(req_iterate->get_route_info() +
                ShowRouteHandler::kIterSeparator + lexical_cast<string>(i));
        }
    }

    for (int i = 1; i <= 12; i++) {
        string expected = lexical_cast<string>(i);
        switch (i) {
        case 1:
            EXPECT_EQ(expected, req->get_routing_instance());
            break;
        case 2:
            EXPECT_EQ(expected, req->get_routing_table());
            break;
        case 3:
            EXPECT_EQ(expected, req->get_prefix());
            break;
        case 4:
            EXPECT_EQ(expected, req->get_start_routing_instance());
            break;
        case 5:
            EXPECT_EQ(expected, req->get_start_routing_table());
            break;
        case 6:
            EXPECT_EQ(expected, req->get_start_prefix());
            break;
        case 7:
            EXPECT_EQ(expected, lexical_cast<string>(req->get_count()));
            break;
        case 8:
            EXPECT_EQ(expected, req->get_source());
            break;
        case 9:
            EXPECT_EQ(expected, req->get_protocol());
            break;
        case 10:
            EXPECT_EQ(expected, req->get_family());
        case 11:
            EXPECT_EQ(true, req->get_longer_match());
        case 12:
            EXPECT_EQ(false, req->get_shorter_match());
            break;
        }
    }

    // Release() deletes allocated memory to req_iterate and req.
    req_iterate->Release();
    req->Release();
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<BgpPeer>(boost::factory<BgpPeerShowTest *>());
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
