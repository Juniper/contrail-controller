/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_factory.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/bgp_xmpp_sandesh.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "xmpp/xmpp_factory.h"

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
        <session to=\'Y1\'>\
            <address-families>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
        <session to=\'Y2\'>\
            <address-families>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y1\'>\
        <identifier>192.168.1.1</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.1.1</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
        <session to=\'Y2\'>\
            <address-families>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y2\'>\
        <identifier>192.168.1.2</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.1.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
        <session to=\'Y1\'>\
            <address-families>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
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
// List of TypeDefinitions we want to test.
//
typedef ::testing::Types <
    TypeDefinition<
        BgpNeighborReq,
        BgpNeighborReqIterate,
        BgpNeighborListResp>,
    TypeDefinition<
        ShowBgpNeighborSummaryReq,
        ShowBgpNeighborSummaryReqIterate,
        ShowBgpNeighborSummaryResp> > TypeDefinitionList;

//
// Fixture class template - will be instantiated further below for each entry
// in TypeDefinitionList.
//
template <typename T>
class BgpShowNeighborTest : public ::testing::Test {
public:
    void ValidateResponse(Sandesh *sandesh,
            vector<string> &result, const string &next_batch) {
        typename T::RespT *resp = dynamic_cast<typename T::RespT *>(sandesh);
        EXPECT_TRUE(resp != NULL);
        EXPECT_EQ(result.size(), resp->get_neighbors().size());
        EXPECT_EQ(next_batch, resp->get_next_batch());
        for (size_t i = 0; i < resp->get_neighbors().size(); ++i) {
            EXPECT_EQ(result[i], resp->get_neighbors()[i].get_peer());
        }
        validate_done_ = true;
    }

protected:
    BgpShowNeighborTest()
        : thread_(&evm_), xmpp_server_x_(NULL), validate_done_(false) {
    }

    bool RequestIsDetail() const { return false; }

    virtual void SetUp() {
        bgp_server_x_.reset(new BgpServerTest(&evm_, "X"));
        bgp_server_x_->session_manager()->Initialize(0);
        xmpp_server_x_ =
            new XmppServerTest(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xmpp_server_x_->Initialize(0, false);
        bcm_x_.reset(
            new BgpXmppChannelManager(xmpp_server_x_, bgp_server_x_.get()));
        sandesh_context_.bgp_server = bgp_server_x_.get();
        sandesh_context_.xmpp_peer_manager = bcm_x_.get();
        Sandesh::set_client_context(&sandesh_context_);
        RegisterSandeshShowXmppExtensions(&sandesh_context_);

        bgp_server_y1_.reset(new BgpServerTest(&evm_, "Y1"));
        bgp_server_y1_->session_manager()->Initialize(0);
        bgp_server_y2_.reset(new BgpServerTest(&evm_, "Y2"));
        bgp_server_y2_->session_manager()->Initialize(0);

        thread_.Start();
        Configure();
        task_util::WaitForIdle();

        CreateAgents();
        SubscribeAgents();
    }

    virtual void TearDown() {
        ShutdownAgents();

        xmpp_server_x_->Shutdown();
        task_util::WaitForIdle();
        bgp_server_x_->Shutdown();
        bgp_server_y1_->Shutdown();
        bgp_server_y2_->Shutdown();
        task_util::WaitForIdle();

        bcm_x_.reset();
        TcpServerManager::DeleteServer(xmpp_server_x_);
        xmpp_server_x_ = NULL;

        DeleteAgents();

        task_util::WaitForIdle();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), config_template,
            bgp_server_x_->session_manager()->GetPort(),
            bgp_server_y1_->session_manager()->GetPort(),
            bgp_server_y2_->session_manager()->GetPort());

        bgp_server_x_->Configure(config);
        bgp_server_y1_->Configure(config);
        bgp_server_y2_->Configure(config);
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(2, bgp_server_x_->NumUpPeer());
        TASK_UTIL_EXPECT_EQ(2, bgp_server_y1_->NumUpPeer());
        TASK_UTIL_EXPECT_EQ(2, bgp_server_y2_->NumUpPeer());

        if (RequestIsDetail()) {
            vector<string> instance_names;
            for (int vn_idx = 100; vn_idx < 104; ++vn_idx) {
                string vn_name = string("vn") + integerToString(vn_idx);
                instance_names.push_back(vn_name);
            }
            NetworkConfig(instance_names);
            VerifyNetworkConfig(bgp_server_x_.get(), instance_names);
        }
    }

    void NetworkConfig(const vector<string> &instance_names) {
        bgp_util::NetworkConfigGenerate(bgp_server_x_->config_db(),
                                        instance_names);
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
        for (int idx = 0; idx < 12; ++idx) {
            string name = string("agent") + integerToString(900 + idx);
            string address = string("127.0.2.") + integerToString(idx);
            test::NetworkAgentMock *agent = new test::NetworkAgentMock(
                &evm_, name, xmpp_server_x_->GetPort(), address, "127.0.0.1");
            agents_.push_back(agent);
            agent_names_.push_back(name);
            TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
        }
    }

    void SubscribeAgents() {
        if (!RequestIsDetail())
            return;
        for (int vn_idx = 100; vn_idx < 104; ++vn_idx) {
            string vn_name = string("vn") + integerToString(vn_idx);
            for (int idx = 0; idx < 12; ++idx) {
                agents_[idx]->Subscribe(vn_name, vn_idx);
            }
        }
        // VNs * Tables per VN * Agents + Peers * AFs
        TASK_UTIL_EXPECT_EQ(4 * 4 * 12 + 2 * 1,
            bgp_server_x_->membership_mgr()->GetMembershipCount());
        task_util::WaitForIdle();
    }

    void ShutdownAgents() {
        for (int idx = 0; idx < 12; ++idx) {
            agents_[idx]->SessionDown();
            TASK_UTIL_EXPECT_FALSE(agents_[idx]->IsEstablished());
        }
    }

    void DeleteAgents() {
        for (int idx = 0; idx < 12; ++idx) {
            agents_[idx]->Delete();
        }
        STLDeleteValues(&agents_);
    }

    void PauseBgpPeerDelete() {
        task_util::TaskSchedulerLock lock;
        BgpPeer *peer_y1 = bgp_server_x_->FindMatchingPeer(
            BgpConfigManager::kMasterInstance, "Y1");
        peer_y1->deleter()->PauseDelete();
        BgpPeer *peer_y2 = bgp_server_x_->FindMatchingPeer(
            BgpConfigManager::kMasterInstance, "Y2");
        peer_y2->deleter()->PauseDelete();
    }

    void ResumeBgpPeerDelete() {
        task_util::TaskSchedulerLock lock;
        BgpPeer *peer_y1 = bgp_server_x_->FindMatchingPeer(
            BgpConfigManager::kMasterInstance, "Y1");
        peer_y1->deleter()->ResumeDelete();
        BgpPeer *peer_y2 = bgp_server_x_->FindMatchingPeer(
            BgpConfigManager::kMasterInstance, "Y2");
        peer_y2->deleter()->ResumeDelete();
    }

    void VerifyBgpPeerDelete() {
        BgpPeer *peer_y1 = bgp_server_x_->FindMatchingPeer(
            BgpConfigManager::kMasterInstance, "Y1");
        TASK_UTIL_EXPECT_TRUE(peer_y1->IsDeleted());
        BgpPeer *peer_y2 = bgp_server_x_->FindMatchingPeer(
            BgpConfigManager::kMasterInstance, "Y2");
        TASK_UTIL_EXPECT_TRUE(peer_y2->IsDeleted());
    }

    void AddBgpPeerNames(vector<string> *neighbor_names,
        const string &name = string()) {
        if (name.empty() || name == "Y1") {
            const BgpPeer *peer_y1 = bgp_server_x_->FindMatchingPeer(
                BgpConfigManager::kMasterInstance, "Y1");
            EXPECT_TRUE(peer_y1 != NULL);
            neighbor_names->push_back(peer_y1->peer_basename());
        }
        if (name.empty() || name == "Y2") {
            const BgpPeer *peer_y2 = bgp_server_x_->FindMatchingPeer(
                BgpConfigManager::kMasterInstance, "Y2");
            EXPECT_TRUE(peer_y2 != NULL);
            neighbor_names->push_back(peer_y2->peer_basename());
        }
    }

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> bgp_server_x_;
    boost::scoped_ptr<BgpServerTest> bgp_server_y1_;
    boost::scoped_ptr<BgpServerTest> bgp_server_y2_;
    XmppServerTest *xmpp_server_x_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_x_;
    bool validate_done_;
    BgpSandeshContext sandesh_context_;
    vector<test::NetworkAgentMock *> agents_;
    vector<string> agent_names_;
};

// Specialization to identify BgpNeighborReq.
template<>
bool BgpShowNeighborTest<TypeDefinition<BgpNeighborReq,
    BgpNeighborReqIterate,
    BgpNeighborListResp> >::RequestIsDetail() const {
    return true;
}

//
// Instantiate fixture class template for each entry in TypeDefinitionList.
//
TYPED_TEST_CASE(BgpShowNeighborTest, TypeDefinitionList);

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 0 through 3
// Should return all neighbors.
//
TYPED_TEST(BgpShowNeighborTest, Request1) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        this->AddBgpPeerNames(&neighbor_names);
        for (int idx = 0; idx < 12; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 14 (number of neighbors)
// Iteration limit = 0 through 3
// Should return all neighbors.
//
TYPED_TEST(BgpShowNeighborTest, Request2) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(14);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        this->AddBgpPeerNames(&neighbor_names);
        for (int idx = 0; idx < 12; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 1 through (Number of Bgp Peers + 1)
// Iteration limit = 1024
// Should return 2 bgp neighbors + first xmpp neighbor.
//
TYPED_TEST(BgpShowNeighborTest, Request3) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t page_limit = 1; page_limit <= 3; ++page_limit) {
        this->sandesh_context_.set_page_limit(page_limit);
        this->sandesh_context_.set_iter_limit(0);
        vector<string> neighbor_names;
        this->AddBgpPeerNames(&neighbor_names);
        neighbor_names.push_back(this->agent_names_[0]);
        string next_batch = this->agent_names_[1] + "||";
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 6
// Iteration limit = 0 through 3
// Should return 2 bgp neighbors + first 4 xmpp neighbors.
//
TYPED_TEST(BgpShowNeighborTest, Request4) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(6);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        this->AddBgpPeerNames(&neighbor_names);
        for (int idx = 0; idx < 4; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch = this->agent_names_[4] + "||";
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 0 through 3
// Search string = ""
// Should return all neighbors.
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch1) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        this->AddBgpPeerNames(&neighbor_names);
        for (int idx = 0; idx < 12; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 0 through 3
// Search string = "Y"
// Should return all neighbors with "Y".
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch2) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        this->AddBgpPeerNames(&neighbor_names);
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("Y");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 0 through 3
// Search string = "agent"
// Should return all neighbors with "agent".
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch3) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 0; idx < 12; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("agent");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 12 (number of matching neighbors)
// Iteration limit = 0 through 3
// Search string = "agent"
// Should return all neighbors with "agent".
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch4) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 0; idx < 12; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("agent");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 4
// Iteration limit = 0 through 3
// Search string = "agent"
// Should return first 4 neighbors with "agent".
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch5) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(4);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 0; idx < 4; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch = "agent904||agent";
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("agent");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 0 through 3
// Search string = "xyz"
// Should return empty list.
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch6) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("xyz");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 0 through 3
// Search string = "deleted"
// Should return empty list.
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch7) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("deleted");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "deleted"
// Should return all neighbors (they are marked deleted)
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch8) {
    typedef typename TypeParam::ReqT ReqT;

    this->PauseBgpPeerDelete();
    this->bcm_x_->SetQueueDisable(true);
    this->ShutdownAgents();
    TASK_UTIL_EXPECT_EQ(12, this->bcm_x_->GetQueueSize());
    this->bgp_server_x_->Shutdown(false);
    this->VerifyBgpPeerDelete();
    this->sandesh_context_.set_page_limit(0);
    this->sandesh_context_.set_iter_limit(0);
    vector<string> neighbor_names;
    this->AddBgpPeerNames(&neighbor_names);
    for (int idx = 0; idx < 12; ++idx) {
        neighbor_names.push_back(this->agent_names_[idx]);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
        _1, neighbor_names, next_batch));
    this->validate_done_ = false;
    ReqT *req = new ReqT;
    req->set_search_string("deleted");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    this->bcm_x_->SetQueueDisable(false);
    this->ResumeBgpPeerDelete();
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "Y1"
// Should return 1 neighbor.
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch9) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        this->AddBgpPeerNames(&neighbor_names, "Y1");
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("Y1");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "127.0.1.1"
// Should return 1 neighbor.
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch10) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        this->AddBgpPeerNames(&neighbor_names, "Y1");
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("127.0.1.1");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "agent907"
// Should return 1 neighbor.
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch11) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        neighbor_names.push_back("agent907");
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("agent907");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = empty
// Page limit = 64 (default)
// Iteration limit = 1024 (default)
// Search string = "127.0.2.7"
// Should return 1 neighbor.
//
TYPED_TEST(BgpShowNeighborTest, RequestWithSearch12) {
    typedef typename TypeParam::ReqT ReqT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        neighbor_names.push_back("agent907");
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqT *req = new ReqT;
        req->set_search_string("127.0.2.7");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = "agent901"
// Page limit = 64 (default)
// Iteration limit = 0 through 3
// Should return all neighbors including and after "agent901".
//
TYPED_TEST(BgpShowNeighborTest, RequestIterate1) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 1; idx < 12; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901||");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = "agent901"
// Page limit = 11
// Iteration limit = 0 through 3
// Should return all neighbors including and after "agent901".
//
TYPED_TEST(BgpShowNeighborTest, RequestIterate2) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(11);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 1; idx < 12; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901||");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = "agent901"
// Page limit = 4
// Iteration limit = 0 through 3
// Should return first 4 neighbors including and after "agent901".
//
TYPED_TEST(BgpShowNeighborTest, RequestIterate3) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(4);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 1; idx < 5; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch = "agent905||";
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901||");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = ""
// Page limit = 4
// Iteration limit = 0 through 3
// Should return empty list.
//
TYPED_TEST(BgpShowNeighborTest, RequestIterate4) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(4);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = malformed
// Page limit = 4
// Iteration limit = 0 through 3
// Should return empty list.
//
TYPED_TEST(BgpShowNeighborTest, RequestIterate5) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(4);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = malformed
// Page limit = 4
// Iteration limit = 0 through 3
// Should return empty list.
//
TYPED_TEST(BgpShowNeighborTest, RequestIterate6) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(4);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901|");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = "agent919"
// Page limit = 4
// Iteration limit = 0 through 3
// Should return empty list.
//
TYPED_TEST(BgpShowNeighborTest, RequestIterate7) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(4);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent919||");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = "agent901"
// Page limit = 64 (default)
// Iteration limit = 0 through 3
// Search string = "agent90"
// Should return all neighbors including and after "agent901" with "agent90"
//
TYPED_TEST(BgpShowNeighborTest, RequestIterateWithSearch1) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(0);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 1; idx < 10; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901||agent90");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = "agent901"
// Page limit = 4
// Iteration limit = 0 through 3
// Search string = "agent90"
// Should return first 4 neighbors including and after "agent901" with "agent90"
//
TYPED_TEST(BgpShowNeighborTest, RequestIterateWithSearch2) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(4);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 1; idx < 5; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch = "agent905||agent90";
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901||agent90");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = "agent901"
// Page limit = 9
// Iteration limit = 0 through 3
// Search string = "agent90"
// Should return first 9 neighbors including and after "agent901" with "agent90"
//
TYPED_TEST(BgpShowNeighborTest, RequestIterateWithSearch3) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(9);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        for (int idx = 1; idx < 10; ++idx) {
            neighbor_names.push_back(this->agent_names_[idx]);
        }
        string next_batch = "agent910||agent90";
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901||agent90");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

//
// Next neighbor = "agent901"
// Page limit = 9
// Iteration limit = 1 through 3
// Search string = "agent92"
// Should return empty list.
//
TYPED_TEST(BgpShowNeighborTest, RequestIterateWithSearch4) {
    typedef typename TypeParam::ReqIterateT ReqIterateT;

    for (uint32_t iter_limit = 0; iter_limit <= 3; ++iter_limit) {
        this->sandesh_context_.set_page_limit(9);
        this->sandesh_context_.set_iter_limit(iter_limit);
        vector<string> neighbor_names;
        string next_batch;
        Sandesh::set_response_callback(boost::bind(
            &BgpShowNeighborTest<TypeParam>::ValidateResponse, this,
            _1, neighbor_names, next_batch));
        this->validate_done_ = false;
        ReqIterateT *req = new ReqIterateT;
        req->set_iterate_info("agent901||agent92");
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(this->validate_done_);
    }
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
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
