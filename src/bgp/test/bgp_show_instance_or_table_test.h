/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/test/event_manager_test.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_factory.h"

using std::cout;
using std::endl;
using std::string;
using std::vector;

static const char *config_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
    </bgp-router>\
</config>\
";

//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template <typename T1, typename T2, typename T3>
struct TypeDefinition {
  typedef T1 ReqT;
  typedef T2 ReqIterateT;
  typedef T3 RespT;
};

//
// Fixture class template - will be instantiated in source test files.
//
template <typename T>
class BgpShowInstanceOrTableTest : public ::testing::Test {
public:
    void ValidateResponse(Sandesh *sandesh,
            vector<string> &result, const string &next_batch) {
        typename T::RespT *resp = dynamic_cast<typename T::RespT *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_instances().size());
        TASK_UTIL_EXPECT_EQ(next_batch, resp->get_next_batch());
        for (size_t i = 0; i < resp->get_instances().size(); ++i) {
            TASK_UTIL_EXPECT_EQ(result[i], resp->get_instances()[i].get_name());
            cout << resp->get_instances()[i].log() << endl;
        }
        validate_done_ = true;
    }

protected:
    BgpShowInstanceOrTableTest()
        : thread_(&evm_), xmpp_server_(NULL), validate_done_(false) {
    }

    bool RequestIsConfig() const { return false; }
    bool RequestIsDetail() const { return false; }
    void AddInstanceOrTableName(vector<string> *names, const string &name) {
        names->push_back(name);
    }

    virtual void SetUp() {
        server_.reset(new BgpServerTest(&evm_, "X"));
        server_->session_manager()->Initialize(0);
        xmpp_server_ =
            new XmppServerTest(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xmpp_server_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " << xmpp_server_->GetPort());
        bcm_.reset(new BgpXmppChannelManager(xmpp_server_, server_.get()));

        sandesh_context_.bgp_server = server_.get();
        sandesh_context_.xmpp_peer_manager = bcm_.get();

        thread_.Start();
        Configure();
        task_util::WaitForIdle();

        CreateAgents();
        SubscribeAgents();
    }

    virtual void TearDown() {
        ShutdownAgents();

        xmpp_server_->Shutdown();
        task_util::WaitForIdle();
        server_->Shutdown();
        task_util::WaitForIdle();

        bcm_.reset();
        TcpServerManager::DeleteServer(xmpp_server_);
        xmpp_server_ = NULL;

        DeleteAgents();

        task_util::WaitForIdle();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), config_template,
            server_->session_manager()->GetPort());
        server_->Configure(config);
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_EQ(64512, server_->autonomous_system());
        TASK_UTIL_EXPECT_EQ(64512, server_->local_autonomous_system());

        vector<string> instance_names;
        for (int idx = 900; idx < 912; ++idx) {
            string vn_name = string("vn") + integerToString(idx);
            instance_names.push_back(vn_name);
        }
        NetworkConfig(instance_names);
        VerifyNetworkConfig(server_.get(), instance_names);
    }

    void NetworkConfig(const vector<string> &instance_names) {
        bgp_util::NetworkConfigGenerate(server_->config_db(), instance_names);
    }

    void VerifyNetworkConfig(BgpServerTest *server,
        const vector<string> &instance_names) {
        for (vector<string>::const_iterator iter = instance_names.begin();
             iter != instance_names.end(); ++iter) {
            TASK_UTIL_WAIT_NE_NO_MSG(
                     server->routing_instance_mgr()->GetRoutingInstance(*iter),
                     NULL, 1000, 10000, "Wait for routing instance..");
            const RoutingInstance *rti =
                server->routing_instance_mgr()->GetRoutingInstance(*iter);
            TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
                0, 1000, 10000, "Wait for vn index..");
        }
    }

    void CreateAgents() {
        if (!RequestIsDetail())
            return;
        agent1_.reset(new test::NetworkAgentMock(&evm_, "agent1",
            xmpp_server_->GetPort(), "127.0.0.1", "127.0.0.11"));
        TASK_UTIL_EXPECT_TRUE(agent1_->IsEstablished());
        agent2_.reset(new test::NetworkAgentMock(&evm_, "agent2",
            xmpp_server_->GetPort(), "127.0.0.1", "127.0.0.12"));
        TASK_UTIL_EXPECT_TRUE(agent2_->IsEstablished());
    }

    void SubscribeAgents() {
        if (!RequestIsDetail())
            return;
        for (int idx = 900; idx < 912; ++idx) {
            string vn_name = string("vn") + integerToString(idx);
            agent1_->Subscribe(vn_name, idx);
            agent2_->Subscribe(vn_name, idx);
        }
        TASK_UTIL_EXPECT_EQ((912 - 900) * 4 * 2, // VNs * Tables per VN * Agents
            server_->membership_mgr()->GetMembershipCount());
        task_util::WaitForIdle();
    }

    void ShutdownAgents() {
        if (!RequestIsDetail())
            return;
        agent1_->SessionDown();
        TASK_UTIL_EXPECT_FALSE(agent1_->IsEstablished());
        agent2_->SessionDown();
        TASK_UTIL_EXPECT_FALSE(agent2_->IsEstablished());
    }

    void DeleteAgents() {
        if (!RequestIsDetail())
            return;
        agent1_->Delete();
        agent2_->Delete();
    }

    void PauseResumeInstanceDeletion(bool pause) {
        if (RequestIsConfig())
            return;
        task_util::TaskSchedulerLock lock;
        RoutingInstanceMgr *rim = server_->routing_instance_mgr();
        for (RoutingInstanceMgr::name_iterator it1 = rim->name_begin();
             it1 != rim->name_end(); ++it1) {
            RoutingInstance *rtinstance = it1->second;
            if (pause) {
                rtinstance->deleter()->PauseDelete();
            } else {
                rtinstance->deleter()->ResumeDelete();
            }
            RoutingInstance::RouteTableList tables = rtinstance->GetTables();
            for (RoutingInstance::RouteTableList::iterator it2 = tables.begin();
                 it2 != tables.end(); ++it2) {
                BgpTable *table = it2->second;
                if (pause) {
                    table->deleter()->PauseDelete();
                } else {
                    table->deleter()->ResumeDelete();
                }
            }
        }
    }

    void PauseInstanceDeletion() {
        PauseResumeInstanceDeletion(true);
    }

    void ResumeInstanceDeletion() {
        PauseResumeInstanceDeletion(false);
    }

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> server_;
    XmppServerTest *xmpp_server_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_;
    bool validate_done_;
    BgpSandeshContext sandesh_context_;
    boost::scoped_ptr<test::NetworkAgentMock> agent1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent2_;
};

// Declare a type-parameterized test case.
TYPED_TEST_CASE_P(BgpShowInstanceOrTableTest);

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Should return all instances.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, Request1) {
    typedef typename TypeParam::ReqT ReqT;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    this->AddInstanceOrTableName(&names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 5
// Should return all instances.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, Request2) {
    typedef typename TypeParam::ReqT ReqT;

    this->sandesh_context_.set_iter_limit(5);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    this->AddInstanceOrTableName(&names, BgpConfigManager::kMasterInstance);

    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 13 (number of instances)
// Iteration limit = 1024 (default)
// Should return all instances.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, Request3) {
    typedef typename TypeParam::ReqT ReqT;

    this->sandesh_context_.set_page_limit(13);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    this->AddInstanceOrTableName(&names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 4
// Iteration limit = 1024 (default)
// Should return first 4 instances.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, Request4) {
    typedef typename TypeParam::ReqT ReqT;

    this->sandesh_context_.set_page_limit(4);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    this->AddInstanceOrTableName(&names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 903; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn903||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 4
// Iteration limit = 2
// Should return first 4 instances.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, Request5) {
    typedef typename TypeParam::ReqT ReqT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    this->AddInstanceOrTableName(&names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 903; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn903||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = ""
// Should return all instances.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch0) {
    typedef typename TypeParam::ReqT ReqT;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    this->AddInstanceOrTableName(&names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "vn"
// Should return all instances with "vn".
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch1) {
    typedef typename TypeParam::ReqT ReqT;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 5
// Search string = "vn"
// Should return all instances with "vn".
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch2) {
    typedef typename TypeParam::ReqT ReqT;

    this->sandesh_context_.set_iter_limit(5);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 12 (number of matching instances)
// Iteration limit = 1024 (default)
// Search string = "vn"
// Should return all instances with "vn".
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch3) {
    typedef typename TypeParam::ReqT ReqT;

    this->sandesh_context_.set_page_limit(12);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 4
// Iteration limit = 1024 (default)
// Search string = "vn"
// Should return first 4 instances with "vn".
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch4) {
    typedef typename TypeParam::ReqT ReqT;

    this->sandesh_context_.set_page_limit(4);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 900; idx < 904; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn904||vn";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 4
// Iteration limit = 2
// Search string = "vn"
// Should return first 4 instances with "vn".
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch5) {
    typedef typename TypeParam::ReqT ReqT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 900; idx < 904; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn904||vn";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "xyz"
// Should return empty list.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch6) {
    typedef typename TypeParam::ReqT ReqT;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("xyz");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "xyz"
// Should return empty list.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch7) {
    typedef typename TypeParam::ReqT ReqT;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("xyz");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "deleted"
// Should return empty list.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch8) {
    typedef typename TypeParam::ReqT ReqT;
    if (this->RequestIsConfig())
        return;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("deleted");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "deleted"
// Should return all instances (they are marked deleted)
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch9) {
    typedef typename TypeParam::ReqT ReqT;
    if (this->RequestIsConfig())
        return;

    this->PauseInstanceDeletion();
    this->server_->Shutdown(false);
    task_util::WaitForIdle();
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    this->AddInstanceOrTableName(&names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("deleted");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    this->ResumeInstanceDeletion();
}

//
// Next instance = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "vn907"
// Should return 1 instance.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestWithSearch10) {
    typedef typename TypeParam::ReqT ReqT;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    this->AddInstanceOrTableName(&names, "vn907");
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("vn907");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Should return all instances including and after "vn901"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate1) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 64 (default)
// Iteration limit = 5
// Should return all instances including and after "vn901"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate2) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_iter_limit(5);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 11
// Iteration limit = 1024 (default)
// Should return all instances including and after "vn901"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate3) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(11);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 4
// Iteration limit = 1024 (default)
// Should return first 4 instances including and after "vn901"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate4) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 905; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn905||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 4
// Iteration limit = 2
// Should return first 4 instances after "vn901"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate5) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 905; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn905||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = empty
// Page limit = 4
// Iteration limit = 2
// Should return empty list.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate6) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = malformed
// Page limit = 4
// Iteration limit = 2
// Should return empty list.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate7) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = malformed
// Page limit = 4
// Iteration limit = 2
// Should return empty list.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate8) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901|");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn919"
// Page limit = 4
// Iteration limit = 2
// Should return empty list.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterate9) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn919||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "vn90"
// Should return all instances including and after "vn901" with "vn90"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterateWithSearch1) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 910; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 64 (default)
// Iteration limit = 4
// Search string = "vn90"
// Should return all instances including and after "vn901" with "vn90"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterateWithSearch2) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_iter_limit(4);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 910; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 4
// Iteration limit = 1024 (default)
// Search string = "vn90"
// Should return first 4 instances including and after "vn901" with "vn90"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterateWithSearch3) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 905; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn905||vn90";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 9
// Iteration limit = 1024 (default)
// Search string = "vn90"
// Should return first 9 instances including and after "vn901" with "vn90"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterateWithSearch4) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(9);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 910; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn910||vn90";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 4
// Iteration limit = 2
// Search string = "vn90"
// Should return first 4 instances including and after "vn901" with "vn90"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterateWithSearch5) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 905; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn905||vn90";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 9
// Iteration limit = 3
// Search string = "vn90"
// Should return first 4 instances including and after "vn901" with "vn90"
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterateWithSearch6) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(9);
    this->sandesh_context_.set_iter_limit(3);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    for (int idx = 901; idx < 910; ++idx) {
        string name = string("vn") + integerToString(idx);
        this->AddInstanceOrTableName(&names, name);
    }
    string next_batch = "vn910||vn90";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 4
// Iteration limit = 2
// Search string = "vn92"
// Should return empty list.
//
TYPED_TEST_P(BgpShowInstanceOrTableTest, RequestIterateWithSearch7) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    this->sandesh_context_.set_page_limit(4);
    this->sandesh_context_.set_iter_limit(2);
    Sandesh::set_client_context(&this->sandesh_context_);
    vector<string> names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowInstanceOrTableTest<TypeParam>::ValidateResponse, this,
        _1, names, next_batch));
    this->validate_done_ = false;
    ReqIterateT *req = new ReqIterateT;
    req->set_iterate_info("vn901||vn92");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
}

// Register all test patterns.
// They will be instantiated from source test files.
REGISTER_TYPED_TEST_CASE_P(BgpShowInstanceOrTableTest,
    Request1,
    Request2,
    Request3,
    Request4,
    Request5,
    RequestWithSearch0,
    RequestWithSearch1,
    RequestWithSearch2,
    RequestWithSearch3,
    RequestWithSearch4,
    RequestWithSearch5,
    RequestWithSearch6,
    RequestWithSearch7,
    RequestWithSearch8,
    RequestWithSearch9,
    RequestWithSearch10,
    RequestIterate1,
    RequestIterate2,
    RequestIterate3,
    RequestIterate4,
    RequestIterate5,
    RequestIterate6,
    RequestIterate7,
    RequestIterate8,
    RequestIterate9,
    RequestIterateWithSearch1,
    RequestIterateWithSearch2,
    RequestIterateWithSearch3,
    RequestIterateWithSearch4,
    RequestIterateWithSearch5,
    RequestIterateWithSearch6,
    RequestIterateWithSearch7);
