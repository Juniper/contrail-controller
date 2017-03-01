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

class BgpShowRouteSummaryTest : public ::testing::Test {
public:
    void ValidateResponse(Sandesh *sandesh,
            vector<string> &result, const string &next_batch) {
        const ShowRouteSummaryResp *resp =
            dynamic_cast<const ShowRouteSummaryResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_tables().size());
        TASK_UTIL_EXPECT_EQ(next_batch, resp->get_next_batch());
        for (size_t i = 0; i < resp->get_tables().size(); ++i) {
            TASK_UTIL_EXPECT_EQ(result[i], resp->get_tables()[i].get_name());
            cout << resp->get_tables()[i].log() << endl;
        }
        validate_done_ = true;
    }

protected:
    BgpShowRouteSummaryTest()
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
        server_->Shutdown();
        task_util::WaitForIdle();

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

    void AddInstanceTables(vector<string> *table_names, const string &name) {
        if (name == BgpConfigManager::kMasterInstance) {
            table_names->push_back("bgp.ermvpn.0");
            table_names->push_back("bgp.evpn.0");
            table_names->push_back("bgp.l3vpn-inet6.0");
            table_names->push_back("bgp.l3vpn.0");
            table_names->push_back("bgp.rtarget.0");
            table_names->push_back("inet.0");
            table_names->push_back("inet6.0");
        } else {
            table_names->push_back(name + ".ermvpn.0");
            table_names->push_back(name + ".evpn.0");
            table_names->push_back(name + ".inet.0");
            table_names->push_back(name + ".inet6.0");
        }
    }

    void PauseResumeTableDeletion(bool pause) {
        task_util::TaskSchedulerLock lock;
        RoutingInstanceMgr *rim = server_->routing_instance_mgr();
        for (RoutingInstanceMgr::name_iterator it1 = rim->name_begin();
             it1 != rim->name_end(); ++it1) {
            RoutingInstance *rtinstance = it1->second;
            for (RoutingInstance::RouteTableList::iterator it2 =
                 rtinstance->GetTables().begin();
                 it2 != rtinstance->GetTables().end(); ++it2) {
                BgpTable *table = it2->second;
                if (pause) {
                    table->deleter()->PauseDelete();
                } else {
                    table->deleter()->ResumeDelete();
                }
            }
        }
    }

    void PauseTableDeletion() {
        PauseResumeTableDeletion(true);
    }

    void ResumeTableDeletion() {
        PauseResumeTableDeletion(false);
    }

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> server_;
    bool validate_done_;
    BgpSandeshContext sandesh_context_;
};

// Parameterize the iteration limit.
class BgpShowRouteSummaryParamTest :
    public BgpShowRouteSummaryTest,
    public ::testing::WithParamInterface<int> {
};

//
// Next instance = empty
// Page limit = 64 (default)
// Should return all tables.
//
TEST_P(BgpShowRouteSummaryParamTest, Request1) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    AddInstanceTables(&table_names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 54 (number of tables)
// Should return all tables.
//
TEST_P(BgpShowRouteSummaryParamTest, Request2) {
    sandesh_context_.set_page_limit(54);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    AddInstanceTables(&table_names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 19
// Should return first 19 tables.
//
TEST_P(BgpShowRouteSummaryParamTest, Request3) {
    sandesh_context_.set_page_limit(19);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    AddInstanceTables(&table_names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 903; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn903||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 16
// Should return first 19 tables.
//
TEST_P(BgpShowRouteSummaryParamTest, Request4) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    AddInstanceTables(&table_names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 903; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn903||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Search string = ""
// Should return all tables.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch1) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    AddInstanceTables(&table_names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Search string = "vn"
// Should return all tables with "vn".
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch2) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 48 (number of matching tables)
// Search string = "vn"
// Should return all tables with "vn".
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch3) {
    sandesh_context_.set_page_limit(48);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 45 (includes one table from last matching instance)
// Search string = "vn"
// Should return all tables with "vn".
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch4) {
    sandesh_context_.set_page_limit(45);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 16
// Search string = "vn"
// Should return tables from first 4 instances with "vn".
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch5) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 900; idx < 904; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn904||vn";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 13
// Search string = "vn"
// Should return tables from first 4 instances with "vn".
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch6) {
    sandesh_context_.set_page_limit(13);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 900; idx < 904; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn904||vn";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("vn");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Search string = "xyz"
// Should return empty list.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch7) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("xyz");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Search string = "deleted"
// Should return empty list.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch8) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("deleted");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 64 (default)
// Search string = "deleted"
// Should return all tables (they are marked deleted)
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch9) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    PauseTableDeletion();
    server_->Shutdown(false);
    task_util::WaitForIdle();
    vector<string> table_names;
    AddInstanceTables(&table_names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("deleted");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
    ResumeTableDeletion();
}

//
// Next instance = empty
// Page limit = 19
// Search string = "deleted"
// Should return first 19 tables (they are marked deleted)
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch10) {
    sandesh_context_.set_page_limit(19);
    sandesh_context_.set_iter_limit(GetParam());
    PauseTableDeletion();
    server_->Shutdown(false);
    task_util::WaitForIdle();
    vector<string> table_names;
    AddInstanceTables(&table_names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 903; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn903||deleted";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("deleted");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
    ResumeTableDeletion();
}

//
// Next instance = empty
// Page limit = 16
// Search string = "deleted"
// Should return first 19 tables (they are marked deleted)
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch11) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    PauseTableDeletion();
    server_->Shutdown(false);
    task_util::WaitForIdle();
    vector<string> table_names;
    AddInstanceTables(&table_names, BgpConfigManager::kMasterInstance);
    for (int idx = 900; idx < 903; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn903||deleted";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("deleted");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
    ResumeTableDeletion();
}

//
// Next instance = empty
// Page limit = 64 (default)
// Search string = "vn907"
// Should return tables for 1 instance.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch12) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    AddInstanceTables(&table_names, "vn907");
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("vn907");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 2
// Search string = "vn907"
// Should return tables for 1 instance.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestWithSearch13) {
    sandesh_context_.set_page_limit(2);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    AddInstanceTables(&table_names, "vn907");
    string next_batch = "vn908||vn907";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReq *req = new ShowRouteSummaryReq;
    req->set_search_string("vn907");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 64 (default)
// Should return tables for all instances including and after "vn901"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate1) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 44
// Should return tables for all instances including and after "vn901"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate2) {
    sandesh_context_.set_page_limit(44);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 41
// Should return tables for all instances including and after "vn901"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate3) {
    sandesh_context_.set_page_limit(41);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 912; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 16
// Should return tables for first 4 instances including and after "vn901"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate4) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 905; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn905||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 13
// Should return tables for first 4 instances including and after "vn901"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate5) {
    sandesh_context_.set_page_limit(13);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 905; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn905||";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = empty
// Page limit = 16
// Should return empty list.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate6) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = malformed
// Page limit = 16
// Should return empty list.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate7) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = malformed
// Page limit = 16
// Should return empty list.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate8) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901|");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn919"
// Page limit = 16
// Should return empty list.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterate9) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn919||");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 64 (default)
// Search string = "vn90"
// Should return tables for 10 instances including and after "vn901" with "vn90"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterateWithSearch1) {
    sandesh_context_.set_page_limit(0);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 910; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 16
// Search string = "vn90"
// Should return tables for 4 instances including + after "vn901" with "vn90"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterateWithSearch2) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 905; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn905||vn90";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 13
// Search string = "vn90"
// Should return tables for 4 instances including + after "vn901" with "vn90"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterateWithSearch3) {
    sandesh_context_.set_page_limit(13);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 905; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn905||vn90";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 36
// Search string = "vn90"
// Should return tables for 9 instances including and after "vn901" with "vn90"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterateWithSearch4) {
    sandesh_context_.set_page_limit(36);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 910; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn910||vn90";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 33
// Search string = "vn90"
// Should return tables for 9 instances including and after "vn901" with "vn90"
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterateWithSearch5) {
    sandesh_context_.set_page_limit(36);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    for (int idx = 901; idx < 910; ++idx) {
        string name = string("vn") + integerToString(idx);
        AddInstanceTables(&table_names, name);
    }
    string next_batch = "vn910||vn90";
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||vn90");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

//
// Next instance = "vn901"
// Page limit = 16
// Search string = "vn92"
// Should return empty list.
//
TEST_P(BgpShowRouteSummaryParamTest, RequestIterateWithSearch6) {
    sandesh_context_.set_page_limit(16);
    sandesh_context_.set_iter_limit(GetParam());
    vector<string> table_names;
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &BgpShowRouteSummaryParamTest::ValidateResponse, this,
        _1, table_names, next_batch));
    validate_done_ = false;
    ShowRouteSummaryReqIterate *req = new ShowRouteSummaryReqIterate;
    req->set_iterate_info("vn901||vn92");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Instantiate for each value of iteration limit from 0 through 9.
// Note that 0 implies the default iteration limit.
INSTANTIATE_TEST_CASE_P(Instance, BgpShowRouteSummaryParamTest,
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
