/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <boost/uuid/string_generator.hpp>
#include <boost/scoped_array.hpp>
#include <base/logging.h>

#include <pugixml/pugixml.hpp>
#include <io/event_manager.h>
#include "http/http_request.h"
#include "http/http_session.h"
#include "http/http_server.h"
#include "http/client/http_client.h"
#include "http/client/http_curl.h"
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <oper/interface_common.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <test/test_cmn_util.h>
#include <services/services_sandesh.h>
#include <services/metadata_proxy.h>

#define MAX_WAIT_COUNT 500
#define BUF_SIZE 8192
#define vm1_ip "10.1.1.3"
#define METADATA_CHECK(condition)                                              \
                    do {                                                       \
                      client->WaitForIdle();                                   \
                      stats = Agent::GetInstance()->services()->               \
                              metadataproxy()->metadatastats();                \
                      if (++count == MAX_WAIT_COUNT)                           \
                          assert(0);                                           \
                      usleep(1000);                                            \
                    } while (condition);                                       \

class TestInterfaceTable : public InterfaceTable {
public:
    TestInterfaceTable() :
        InterfaceTable(Agent::GetInstance()->GetDB(), "test") {}
    ~TestInterfaceTable() {}

    bool FindVmUuidFromMetadataIp(const Ip4Address &ip,
                                  std::string *vm_ip,
                                  std::string *vm_uuid,
                                  std::string *vm_project_uuid) {
        *vm_ip = vm1_ip;
        *vm_uuid = "1234";
        *vm_project_uuid = "5678";
        return true;
    }
};

class MetadataTest : public ::testing::Test {
public:
    MetadataTest() : nova_api_proxy_(NULL), vm_http_client_(NULL),
                     done_(0), itf_count_(0) {
        rid_ = Agent::GetInstance()->GetInterfaceTable()->Register(
                boost::bind(&MetadataTest::ItfUpdate, this, _2));
    }

    ~MetadataTest() {
        Agent::GetInstance()->GetInterfaceTable()->Unregister(rid_);
    }

    void ItfUpdate(DBEntryBase *entry) {
        Interface *itf = static_cast<Interface *>(entry);
        tbb::mutex::scoped_lock lock(mutex_);
        unsigned int i;
        for (i = 0; i < itf_id_.size(); ++i)
            if (itf_id_[i] == itf->id())
                break;
        if (entry->IsDeleted()) {
            if (itf_count_ && i < itf_id_.size()) {
                itf_count_--;
                LOG(DEBUG, "Metadata test : interface deleted " << itf_id_[0]);
                itf_id_.erase(itf_id_.begin()); // we delete in create order
            }
        } else {
            if (i == itf_id_.size()) {
                itf_count_++;
                itf_id_.push_back(itf->id());
                LOG(DEBUG, "Metadata test : interface added " << itf->id());
            }
        }
    }

    uint32_t GetItfCount() { 
        tbb::mutex::scoped_lock lock(mutex_);
        return itf_count_; 
    }

    void WaitForItfUpdate(unsigned int expect_count) {
        int count = 0;
        while (GetItfCount() != expect_count) {
            if (++count == MAX_WAIT_COUNT)
                assert(0);
            usleep(1000);
        }
    }

    void CheckSandeshResponse(Sandesh *sandesh) {
    }

    void SetupLinkLocalConfig() {
        std::stringstream global_config;
        global_config << "<linklocal-services>\n" 
                      << "<linklocal-service-entry>\n"
	              << "<linklocal-service-name>metadata</linklocal-service-name>\n" 
	              << "<linklocal-service-ip></linklocal-service-ip>\n"
	              << "<linklocal-service-port>0</linklocal-service-port>\n"
	              << "<ip-fabric-DNS-service-name></ip-fabric-DNS-service-name>\n"
	              << "<ip-fabric-service-port>"
                      << nova_api_proxy_->GetPort()
                      << "</ip-fabric-service-port>\n"
	              << "<ip-fabric-service-ip>127.0.0.1</ip-fabric-service-ip>\n"
	              << "</linklocal-service-entry>\n"
	              << "</linklocal-services>";

        char buf[BUF_SIZE];
        int len = 0;
        memset(buf, 0, BUF_SIZE);
        AddXmlHdr(buf, len);
        AddNodeString(buf, len, "global-vrouter-config",
                      "default-global-system-config:default-global-vrouter-config",
                      1024, global_config.str().c_str());
        AddXmlTail(buf, len);
        ApplyXmlString(buf);
    }

    void StartHttpClient() {
        vm_http_client_ = new HttpClient(Agent::GetInstance()->GetEventManager());
        vm_http_client_->Init();
    }

    void StopHttpClient() {
        vm_http_client_->Shutdown();
        vm_http_client_ = NULL;
    }

    HttpConnection *SendHttpClientRequest() {
        boost::system::error_code ec;
        Ip4Address server(Ip4Address::from_string("127.0.0.1", ec));

        boost::asio::ip::tcp::endpoint http_ep;
        http_ep.address(server);
        http_ep.port(Agent::GetInstance()->GetMetadataServerPort());

        std::string uri("openstack");
        std::string header_options;
        header_options = "Connection: close";
        HttpConnection *conn = vm_http_client_->CreateConnection(http_ep);
        conn->RegisterEventCb(
              boost::bind(&MetadataTest::OnClientSessionEvent, this, _1, _2));
        conn->HttpGet(uri, false, false, header_options,
                      boost::bind(&MetadataTest::HandleHttpResponse,
                                  this, conn, _1, _2));
        return conn;
    }

    void HandleHttpResponse(HttpConnection *conn, std::string &msg,
                            boost::system::error_code &ec) {
        if (ec)
            assert(0);
        CloseClientSession(conn);
        done_++;
    }

    void CloseClientSession(HttpConnection *conn) {
        HttpClient *client = conn->client();
        client->RemoveConnection(conn);
    }

    void OnClientSessionEvent(HttpClientSession *session, TcpSession::Event event) {
        switch (event) {
            case TcpSession::CLOSE: {
                session->Connection()->client()->RemoveConnection(
                                                 session->Connection());
                break;
            }

            default:
                break;
        }
    }

    void StartNovaApiProxy() {
        nova_api_proxy_ = 
             new HttpServer(Agent::GetInstance()->GetEventManager());
        nova_api_proxy_->RegisterHandler(HTTP_WILDCARD_ENTRY,
             boost::bind(&MetadataTest::HandleNovaApiRequest, this, _1, _2));
        nova_api_proxy_->Initialize(0);
    }

    void StopNovaApiProxy() {
        nova_api_proxy_->Shutdown();
        nova_api_proxy_ = NULL;
    }

    void HandleNovaApiRequest(HttpSession *session, const HttpRequest *request) {
        int found = 0;
        const HttpRequest::HeaderMap &req_header = request->Headers();
        for (HttpRequest::HeaderMap::const_iterator it = req_header.begin();
             it != req_header.end(); ++it) {
            std::string option = it->first;
            if (option == "X-Forwarded-For" || option == "X-Instance-ID" ||
                option == "X-Instance-ID-Signature" || option == "X-Tenant-ID")
                found++;
        }

        if (found != 4)
            assert(0);

        const char body[] = "<html>\n"
                            "<head>\n"
                            " <title>Server Status Success</title>\n"
                            "</head>\n"
                            "</html>\n";
        char response[512];
        snprintf(response, sizeof(response),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html; charset=UTF-8\r\n"
                 "Content-Length: %u\r\n"
                 "\r\n%s", (unsigned int)strlen(body), body);

        session->Send(reinterpret_cast<const u_int8_t *>(response),
                      strlen(response), NULL);
        delete request;
    }

private:
    HttpServer *nova_api_proxy_;
    HttpClient *vm_http_client_;
    uint32_t done_;
    uint32_t itf_count_;
    DBTableBase::ListenerId rid_;
    std::vector<std::size_t> itf_id_;
    tbb::mutex mutex_;
};

TEST_F(MetadataTest, MetadataReqTest) {
    int count = 0;
    MetadataProxy::MetadataStats stats;
    struct PortInfo input[] = {
        {"vnet1", 1, vm1_ip, "00:00:00:01:01:01", 1, 1},
    };

    StartNovaApiProxy();
    SetupLinkLocalConfig();

    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    client->Reset();

    StartHttpClient();

    // If the local address is not same as VM address in this request,
    // agent will have an internal error as it cannot find the VM
    HttpConnection *conn = SendHttpClientRequest();
    METADATA_CHECK (stats.internal_errors < 1);
    EXPECT_EQ(1U, stats.requests);
    EXPECT_EQ(0U, stats.responses);
    EXPECT_EQ(0U, stats.proxy_sessions);
    CloseClientSession(conn);
    client->WaitForIdle();

    // Introspect request
    MetadataInfo *sand = new MetadataInfo();
    Sandesh::set_response_callback(
             boost::bind(&MetadataTest::CheckSandeshResponse, this, _1));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    // for agent to identify the vm, the remote end should have vm's ip;
    // overload the FindVmUuidFromMetadataIp to return true
    InterfaceTable *intf_table = Agent::GetInstance()->GetInterfaceTable();
    TestInterfaceTable *interface_table = new TestInterfaceTable();
    Agent::GetInstance()->SetInterfaceTable(static_cast<InterfaceTable *>(interface_table));
    SendHttpClientRequest();
    METADATA_CHECK (stats.responses < 1);
    Agent::GetInstance()->SetInterfaceTable(intf_table);
    EXPECT_EQ(2U, stats.requests);
    EXPECT_EQ(1U, stats.proxy_sessions);
    EXPECT_EQ(1U, stats.internal_errors);

    client->Reset();
    StopHttpClient();
    DeleteVmportEnv(input, 1, 1, 0); 
    client->WaitForIdle();

    StopNovaApiProxy();
    client->WaitForIdle();

    Agent::GetInstance()->services()->metadataproxy()->ClearStats();
}

void RouterIdDepInit(Agent *agent) {
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true, false);
    usleep(100000);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    client->WaitForIdle();
    delete client;
    return ret;
}
