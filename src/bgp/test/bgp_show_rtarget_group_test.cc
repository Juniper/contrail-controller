/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

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

class BgpShowRtGroupTest : public ::testing::Test {
public:
    void ValidateResponse(Sandesh *sandesh,
            vector<string> &result, const string &next_batch) {
        const ShowRtGroupResp *resp =
            dynamic_cast<const ShowRtGroupResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_rtgroup_list().size());
        TASK_UTIL_EXPECT_EQ(next_batch, resp->get_next_batch());
        for (size_t i = 0; i < resp->get_rtgroup_list().size(); ++i) {
            TASK_UTIL_EXPECT_EQ(result[i],
                resp->get_rtgroup_list()[i].get_rtarget());
            cout << resp->get_rtgroup_list()[i].log() << endl;
        }
        validate_done_ = true;
    }

protected:
    BgpShowRtGroupTest()
        : thread_(&evm_), validate_done_(false) {
    }

    virtual void SetUp() {
        server_.reset(new BgpServerTest(&evm_, "X"));
        server_->session_manager()->Initialize(0);
        sandesh_context_.bgp_server = server_.get();
        Sandesh::set_client_context(&sandesh_context_);

        thread_.Start();
        Configure();
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        evm_.Shutdown();
        server_->Shutdown();
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
        for (int idx = 1; idx <= 12; ++idx) {
            string vn_name = string("vn") + integerToString(idx);
            instance_names.push_back(vn_name);
        }
        NetworkConfig(instance_names);
        VerifyNetworkConfig(instance_names);
    }

    void NetworkConfig(const vector<string> &instance_names) {
        bgp_util::NetworkConfigGenerate(server_->config_db(), instance_names);
    }

    void VerifyNetworkConfig(const vector<string> &instance_names) {
        RoutingInstanceMgr *rim = server_->routing_instance_mgr();
        int idx = 1;
        for (vector<string>::const_iterator iter = instance_names.begin();
             iter != instance_names.end(); ++iter, ++idx) {
            TASK_UTIL_EXPECT_TRUE(rim->GetRoutingInstance(*iter) != NULL);
            const RoutingInstance *rtinstance = rim->GetRoutingInstance(*iter);
            TASK_UTIL_EXPECT_EQ(1U, rtinstance->GetExportList().size());
            string rtarget_str = string("target:64496:") + integerToString(idx);
            TASK_UTIL_EXPECT_EQ(rtarget_str,
                rtinstance->GetExportList().begin()->ToString());
        }
    }

    string GetVitRTargetName(int idx) {
        string rtarget_name("target:192.168.0.1:");
        rtarget_name += integerToString(idx);
        return  rtarget_name;
    }

    string GetESRTargetName() {
        string rtarget_name("target:64512:7999999");
        return  rtarget_name;
    }

    string GetRTargetName(int idx) {
        string rtarget_name("target:64496:");
        rtarget_name += integerToString(idx);
        return  rtarget_name;
    }

    void AddVitRTargetName(vector<string> *rtarget_names, int idx) {
        rtarget_names->push_back(GetVitRTargetName(idx));
    }

    void AddESRTargetName(vector<string> *rtarget_names) {
        rtarget_names->push_back(GetESRTargetName());
    }

    void AddRTargetName(vector<string> *rtarget_names, int idx) {
        rtarget_names->push_back(GetRTargetName(idx));
    }

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> server_;
    bool validate_done_;
    BgpSandeshContext sandesh_context_;
};

// Parameterize the iteration limit.
class BgpShowRtGroupParamTest :
    public BgpShowRtGroupTest,
    public ::testing::WithParamInterface<int> {
};

//
// Next rtarget = empty
// Page limit = 64 (default)
// Should return all rtargets.
//
TEST_P(BgpShowRtGroupParamTest, Request1) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 1; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    AddESRTargetName(&rtargets);
    for (int idx = 1; idx <= 12; ++idx) {
        AddVitRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 24 (number of rtargets)
// Should return all rtargets.
//
TEST_P(BgpShowRtGroupParamTest, Request2) {
    sandesh_context_.set_page_limit(25);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 1; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    AddESRTargetName(&rtargets);
    for (int idx = 1; idx <= 12; ++idx) {
        AddVitRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 4
// Should return first 4 rtargets.
//
TEST_P(BgpShowRtGroupParamTest, Request3) {
    sandesh_context_.set_page_limit(4);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 1; idx <= 4; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    string next_batch = GetRTargetName(5) + "||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 64 (default)
// Search string = ""
// Should return all rtargets.
//
TEST_P(BgpShowRtGroupParamTest, RequestWithSearch1) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 1; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    AddESRTargetName(&rtargets);
    for (int idx = 1; idx <= 12; ++idx) {
        AddVitRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->set_search_string("");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 64 (default)
// Search string = "64496"
// Should return all rtargets with "64496".
//
TEST_P(BgpShowRtGroupParamTest, RequestWithSearch2) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 1; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->set_search_string("64496");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 24 (number of matching rtargets)
// Search string = "target"
// Should return all rtargets with "target".
//
TEST_P(BgpShowRtGroupParamTest, RequestWithSearch3) {
    sandesh_context_.set_page_limit(25);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 1; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    AddESRTargetName(&rtargets);
    for (int idx = 1; idx <= 12; ++idx) {
        AddVitRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->set_search_string("target");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 4
// Search string = "64496"
// Should return first 4 rtargets with "64496".
//
TEST_P(BgpShowRtGroupParamTest, RequestWithSearch4) {
    sandesh_context_.set_page_limit(4);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 1; idx <= 4; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    string next_batch = GetRTargetName(5) + "||64496";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->set_search_string("64496");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 64 (default)
// Search string = "xyz"
// Should return empty list.
//
TEST_P(BgpShowRtGroupParamTest, RequestWithSearch5) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->set_search_string("xyz");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 64 (default)
// Search string = "target:64496:7"
// Should return 1 rtarget.
//
TEST_P(BgpShowRtGroupParamTest, RequestWithSearch6) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    AddRTargetName(&rtargets, 7);
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->set_search_string(GetRTargetName(7));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 1
// Search string = "target:64496:7"
// Should return 1 rtarget.
//
TEST_P(BgpShowRtGroupParamTest, RequestWithSearch7) {
    sandesh_context_.set_page_limit(1);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    AddRTargetName(&rtargets, 7);
    string next_batch = GetRTargetName(8) + "||" + GetRTargetName(7);
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReq *req = new ShowRtGroupReq;
    req->set_search_string(GetRTargetName(7));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = "target:64496:2"
// Page limit = 64 (default)
// Should return all rtargets including and after "target:64496:2"
//
TEST_P(BgpShowRtGroupParamTest, RequestIterate1) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 2; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    AddESRTargetName(&rtargets);
    for (int idx = 1; idx <= 12; ++idx) {
        AddVitRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2) + "||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = "target:64496:2"
// Page limit = 23
// Should return all rtargets including and after "target:64496:2"
//
TEST_P(BgpShowRtGroupParamTest, RequestIterate2) {
    sandesh_context_.set_page_limit(24);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 2; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    AddESRTargetName(&rtargets);
    for (int idx = 1; idx <= 12; ++idx) {
        AddVitRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2) + "||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = "target:64496:2"
// Page limit = 4
// Should return first 4 rtargets including and after "target:64496:2"
//
TEST_P(BgpShowRtGroupParamTest, RequestIterate3) {
    sandesh_context_.set_page_limit(4);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 2; idx <= 5; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    string next_batch = GetRTargetName(6) + "||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2) + "||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = empty
// Page limit = 4
// Should return empty list.
//
TEST_P(BgpShowRtGroupParamTest, RequestIterate4) {
    sandesh_context_.set_page_limit(4);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info("");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = malformed
// Page limit = 4
// Should return empty list.
//
TEST_P(BgpShowRtGroupParamTest, RequestIterate5) {
    sandesh_context_.set_page_limit(4);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = malformed
// Page limit = 4
// Should return empty list.
//
TEST_P(BgpShowRtGroupParamTest, RequestIterate6) {
    sandesh_context_.set_page_limit(4);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2) + "|");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = "target:64496:19"
// Page limit = 4
// Should return empty list.
//
TEST_P(BgpShowRtGroupParamTest, RequestIterate7) {
    sandesh_context_.set_page_limit(4);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(19) + "|");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = "target:64496:2"
// Page limit = 64 (default)
// Search string = "target:64496:1"
// Should return 3 matching rtargets including and after "target:64496:10"
//
TEST_P(BgpShowRtGroupParamTest, RequestIterateWithSearch1) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 10; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2) + "||" + GetRTargetName(1));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = "target:64496:2"
// Page limit = 15
// Search string = "target:64496:1"
// Should return 3 matching rtargets including and after "target:64496:10"
//
TEST_P(BgpShowRtGroupParamTest, RequestIterateWithSearch2) {
    sandesh_context_.set_page_limit(15);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 10; idx <= 12; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2) + "||" + GetRTargetName(1));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = "target:64496:2"
// Page limit = 2
// Search string = "target:64496:1"
// Should return 2 matching rtargets including and after "target:64496:10"
//
TEST_P(BgpShowRtGroupParamTest, RequestIterateWithSearch3) {
    sandesh_context_.set_page_limit(2);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    for (int idx = 10; idx <= 11; ++idx) {
        AddRTargetName(&rtargets, idx);
    }
    string next_batch = GetRTargetName(12) + "||" + GetRTargetName(1);
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2) + "||" + GetRTargetName(1));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next rtarget = "target:64496:2"
// Page limit = 4
// Search string = "target:64496:19"
// Should return empty list.
//
TEST_P(BgpShowRtGroupParamTest, RequestIterateWithSearch4) {
    sandesh_context_.set_page_limit(4);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> rtargets;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRtGroupParamTest::ValidateResponse, this,
        _1, rtargets, next_batch));
    validate_done_ = false;
    ShowRtGroupReqIterate *req = new ShowRtGroupReqIterate;
    req->set_iterate_info(GetRTargetName(2) + "||" + GetRTargetName(19));
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Instantiate for each value of iteration limit from 0 through 9.
// Note that 0 implies the default iteration limit.
INSTANTIATE_TEST_CASE_P(Instance, BgpShowRtGroupParamTest,
    ::testing::Range(0, 10));

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
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
