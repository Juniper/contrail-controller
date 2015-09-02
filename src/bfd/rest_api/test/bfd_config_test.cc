/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/rest_api/bfd_json_config.h"

#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <rapidjson/document.h>

#include "testing/gunit.h"
#include "base/logging.h"

#include "http/client/http_client.h"
#include "http/http_server.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "io/event_manager.h"

#include "bfd/test/bfd_test_utils.h"
#include "bfd/rest_api/bfd_rest_client.h"
#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/bfd_common.h"
#include "bfd/bfd_udp_connection.h"
#include "bfd/rest_api/bfd_rest_server.h"
#include "bfd/rest_api/bfd_client_session.h"

using namespace BFD;

const boost::asio::ip::address addr1 =
    boost::asio::ip::address::from_string("1.1.1.1");
const boost::asio::ip::address addr2 =
    boost::asio::ip::address::from_string("2.2.2.2");
const int config_port1 = 8090;
const int config_port2 = 8091;

class BFDTestServer {
 public:
    BFDTestServer(TestCommunicatorManager *communicationManager,
                  EventManager *evm,
                  const boost::asio::ip::address &addr,
                  const int port) :
             communicator(new TestCommunicator(communicationManager, addr)),
             bfd_server(evm, communicator.get()),
             http(new HttpServer(evm)), config_server(&bfd_server) {
        config_ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
        config_ep.port(port);
        http->RegisterHandler(HTTP_WILDCARD_ENTRY,
                boost::bind(&RESTServer::HandleRequest,
                            &config_server, _1, _2));
        http->Initialize(port);
        communicationManager->registerServer(addr,
            boost::bind(&Server::ProcessControlPacket, &bfd_server, _1));
    }

    void Shutdown() {
        http->Shutdown();
        http->ClearSessions();
        http->WaitForEmpty();
        TcpServerManager::DeleteServer(http);
    }

    ~BFDTestServer() {
    }

    boost::scoped_ptr<Connection> communicator;
    Server bfd_server;
    HttpServer *http;
    RESTServer config_server;
    boost::asio::ip::tcp::endpoint config_ep;
};

class BFDConfigTest : public ::testing::Test {
 public:
    BFDConfigTest() : evm(new EventManager),
                      emt(evm.get()),
                      communicationManager(evm->io_service()),
                      server1(&communicationManager, evm.get(),
                              addr1, config_port1),
                      server2(&communicationManager, evm.get(),
                              addr2, config_port2),
                      session_state(kInit) {
        task_util::WaitForIdle();
    }

    void Shutdown() {
        server1.Shutdown();
        server2.Shutdown();
        evm->Shutdown();
    }

    ~BFDConfigTest() {
        Shutdown();
        task_util::WaitForIdle();
    }

    void AddBFDHostCallback(boost::system::error_code ec) {
        add_bfd_host_ec = ec;
    }

    void GetSessionCallback(const REST::JsonState &state,
                            boost::system::error_code ec) {
        session_state = state.bfd_local_state;
        get_bfd_session_ec = ec;
    }

    void MonitorCallback(const REST::JsonStateMap &new_states,
                         boost::system::error_code ec) {
        monitor_ec = ec;
        this->new_states = new_states;
    }

    void DeleteCallback(boost::system::error_code ec) {
        delete_bfd_host_ec = ec;
    }

    void WaitForState(RESTClient *client, const boost::asio::ip::address &addr,
                      BFDState state, bool state_changed = true) {
        if (state_changed) {
            while (session_state != state) {
                monitor_ec.reset();
                EXPECT_TRUE(client->Monitor(boost::bind(
                    &BFDConfigTest::MonitorCallback, this, _1, _2)));
                TASK_UTIL_EXPECT_TRUE(monitor_ec.is_initialized());
                EXPECT_EQ(boost::system::errc::success, monitor_ec.get());
                session_state = new_states.states[addr];
             }
        } else {
            get_bfd_session_ec.reset();
            EXPECT_TRUE(client->GetSession(addr,
                boost::bind(&BFDConfigTest::GetSessionCallback,
                            this, _1, _2)));
            TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
            EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
            EXPECT_EQ(state, session_state);
        }
    }

    void AddBFDHost(RESTClient *client, const boost::asio::ip::address &addr) {
        add_bfd_host_ec.reset();
        EXPECT_TRUE(client->AddBFDHost(addr,
            boost::bind(&BFDConfigTest::AddBFDHostCallback, this, _1)));
        TASK_UTIL_EXPECT_TRUE(add_bfd_host_ec.is_initialized());
        EXPECT_EQ(boost::system::errc::success, add_bfd_host_ec.get());
    }

    void DeleteBFDHost(RESTClient *client,
                       const boost::asio::ip::address &addr) {
        delete_bfd_host_ec.reset();
        EXPECT_TRUE(client->DeleteBFDHost(addr,
            boost::bind(&BFDConfigTest::DeleteCallback, this, _1)));
        TASK_UTIL_EXPECT_TRUE(delete_bfd_host_ec.is_initialized());
        EXPECT_EQ(boost::system::errc::success, delete_bfd_host_ec.get());
    }

 protected:
    boost::scoped_ptr<EventManager> evm;
    EventManagerThread emt;
    TestCommunicatorManager communicationManager;
    BFDTestServer server1, server2;

    boost::optional<boost::system::error_code> add_bfd_host_ec;
    boost::optional<boost::system::error_code> get_bfd_session_ec;
    boost::optional<boost::system::error_code> delete_bfd_host_ec;
    boost::optional<boost::system::error_code> monitor_ec;
    BFDState session_state;
    REST::JsonStateMap new_states;
};

TEST_F(BFDConfigTest, Test0) {
    RESTClient *client1(new RESTClient(server1.config_ep, evm.get()));
    client1->Init();
    client1->Stop();
    // TODO(bfd) Is there a better way to wait till related workqueue is empty?
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    delete client1;
}

TEST_F(BFDConfigTest, Test1) {
    RESTClient *client1(new RESTClient(server1.config_ep, evm.get()));
    RESTClient *client2(new RESTClient(server2.config_ep, evm.get()));

    client1->Init();
    TASK_UTIL_EXPECT_TRUE(client1->is_initialized());

    client2->Init();
    TASK_UTIL_EXPECT_TRUE(client2->is_initialized());

    AddBFDHost(client1, addr2);
    AddBFDHost(client2, addr1);

    WaitForState(client1, addr2, kUp);
    WaitForState(client2, addr1, kUp);

    DeleteBFDHost(client1, addr2);
    WaitForState(client2, addr1, kDown);

    AddBFDHost(client1, addr2);
    WaitForState(client2, addr1, kUp);

    client1->Stop();
    WaitForState(client2, addr1, kDown);
    client2->Stop();

    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    delete client1;
    delete client2;
}

TEST_F(BFDConfigTest, SelfSession) {
    RESTClient *client1(new RESTClient(server1.config_ep, evm.get()));

    client1->Init();
    TASK_UTIL_EXPECT_TRUE(client1->is_initialized());

    AddBFDHost(client1, addr1);
    WaitForState(client1, addr1, kUp);

    client1->Stop();
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    delete client1;
}

TEST_F(BFDConfigTest, DoubleClient) {
    RESTClient *client1(new RESTClient(server1.config_ep, evm.get()));
    RESTClient *client1b(new RESTClient(server1.config_ep, evm.get()));
    RESTClient *client2(new RESTClient(server2.config_ep, evm.get()));

    client1->Init();
    TASK_UTIL_EXPECT_TRUE(client1->is_initialized());
    client1b->Init();
    TASK_UTIL_EXPECT_TRUE(client1b->is_initialized());
    client2->Init();
    TASK_UTIL_EXPECT_TRUE(client2->is_initialized());

    AddBFDHost(client1, addr2);
    AddBFDHost(client1b, addr2);
    AddBFDHost(client2, addr1);

    WaitForState(client1, addr2, kUp);
    WaitForState(client1b, addr2, kUp);
    WaitForState(client2, addr1, kUp);

    DeleteBFDHost(client2, addr1);
    WaitForState(client1, addr2, kDown);
    WaitForState(client1b, addr2, kDown);

    AddBFDHost(client2, addr1);
    WaitForState(client2, addr1, kUp);
    WaitForState(client1b, addr2, kUp);
    WaitForState(client1, addr2, kUp);

    client1->Stop();
    // dictated by long BFD timeout defaults (RFC)
    boost::this_thread::sleep(boost::posix_time::milliseconds(2000));
    WaitForState(client2, addr1, kUp, false);
    WaitForState(client1b, addr2, kUp, false);
    client1b->Stop();
    WaitForState(client2, addr1, kDown);
    client2->Stop();

    boost::this_thread::sleep(boost::posix_time::milliseconds(2000));
    delete client1;
    delete client1b;
    delete client2;
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
