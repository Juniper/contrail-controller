/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */


#include <rapidjson/document.h>

#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <testing/gunit.h>
#include <base/logging.h>

#include <boost/asio/ip/tcp.hpp>
#include <bfd/http_server/bfd_json_config.h>
#include <http/client/http_client.h>

#include "base/util.h"
#include "base/test/task_test_util.h"
#include "io/event_manager.h"

#include "bfd/test/bfd_test_utils.h"

#include <bfd/http_server/bfd_config_client.h>

#include <bfd/bfd_server.h>
#include <bfd/bfd_session.h>

#include <bfd/bfd_common.h>
#include <bfd/bfd_udp_connection.h>

#include <bfd/http_server/bfd_config_server.h>
#include <bfd/http_server/bfd_client_session.h>

#include <http/http_server.h>

using namespace BFD;


class BFDTestServer {
 public:
    boost::scoped_ptr<Connection> communicator;
    BFDServer bfd_server;
    HttpServer *http;
    BFDConfigServer config_server;
    boost::asio::ip::tcp::endpoint config_ep;

    BFDTestServer(TestCommunicatorManager *communicationManager, EventManager *evm,
            const boost::asio::ip::address &addr, const int port) :
         communicator(new TestCommunicator(communicationManager, addr)),
         bfd_server(evm, communicator.get()),
         http(new HttpServer(evm)), config_server(&bfd_server) {
        config_ep.address(boost::asio::ip::address::from_string("127.0.0.1"));
        config_ep.port(port);
        http->RegisterHandler(HTTP_WILDCARD_ENTRY,
                boost::bind(&BFDConfigServer::HandleRequest, &config_server, _1, _2));
        http->Initialize(port);
        communicationManager->registerServer(addr, boost::bind(&BFDServer::ProcessControlPacket, &bfd_server, _1));
    }

    void Shutdown() {
        http->Shutdown();
        http->ClearSessions();
        http->WaitForEmpty();
        TcpServerManager::DeleteServer(http);
    }

    ~BFDTestServer() {
    }
};

const boost::asio::ip::address addr1 = boost::asio::ip::address::from_string("1.1.1.1");
const boost::asio::ip::address addr2 = boost::asio::ip::address::from_string("2.2.2.2");
const int config_port1 = 8090;
const int config_port2 = 8091;

class BFDConfigTest : public ::testing::Test {
 public:
    EventManager *evm;
    TestCommunicatorManager communicationManager;
    BFDTestServer server1, server2;
    EventManagerThread emt;

    BFDConfigTest() : evm(new EventManager), communicationManager(evm->io_service()),
            server1(&communicationManager, evm, addr1, config_port1),
            server2(&communicationManager, evm, addr2, config_port2),
            emt(evm), session_state(kInit) {
        task_util::WaitForIdle();
    }

    void Shutdown() {
        evm->Shutdown();
        server1.Shutdown();
        server2.Shutdown();
    }

    ~BFDConfigTest() {
        Shutdown();
        task_util::WaitForIdle();
        delete evm;
    }

    boost::optional<boost::system::error_code> add_bfd_host_ec;
    boost::optional<boost::system::error_code> get_bfd_session_ec;
    boost::optional<boost::system::error_code> delete_bfd_host_ec;
    boost::optional<boost::system::error_code> monitor_ec;
    BFDState session_state;
    BfdJsonStateMap new_states;


    void AddBFDHostCallback(boost::system::error_code ec) {
        LOG(INFO, "AddBFDHostCallback: " << ec.message());

        add_bfd_host_ec = ec;
    }

    void GetBFDSessionCallback(const BfdJsonState &state, boost::system::error_code ec) {
        LOG(INFO, "GetBFDSessionCallback: " << ec.message());

        session_state = state.bfd_local_state;
        get_bfd_session_ec = ec;
    }

    void MonitorCallback(const BfdJsonStateMap &new_states, boost::system::error_code ec) {
        LOG(INFO, "MonitorCallback: " << ec.message());

        monitor_ec = ec;
        this->new_states = new_states;
    }

    void DeleteCallback(boost::system::error_code ec) {
        LOG(INFO, "DeleteCallback: " << ec.message());

        delete_bfd_host_ec = ec;
    }

    void WaitForState(BFDConfigClient *client, const boost::asio::ip::address &addr,
            BFDState state, bool state_changed = true) {
        if (state_changed) {
            while (session_state != state) {
                monitor_ec.reset();
                EXPECT_TRUE(client->Monitor(boost::bind(&BFDConfigTest::MonitorCallback, this, _1, _2)));
                TASK_UTIL_EXPECT_TRUE(monitor_ec.is_initialized());
                EXPECT_EQ(boost::system::errc::success, monitor_ec.get());
                session_state = new_states.states[addr];
             }
        } else {
            get_bfd_session_ec.reset();
            EXPECT_TRUE(client->GetBFDSession(addr,  boost::bind(&BFDConfigTest::GetBFDSessionCallback, this, _1, _2)));
            TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
            EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
            EXPECT_EQ(state, session_state);
        }
    }

    void AddBFDHost(BFDConfigClient *client, const boost::asio::ip::address &addr) {
        add_bfd_host_ec.reset();
        EXPECT_TRUE(client->AddBFDHost(addr, boost::bind(&BFDConfigTest::AddBFDHostCallback, this, _1)));
        TASK_UTIL_EXPECT_TRUE(add_bfd_host_ec.is_initialized());
        EXPECT_EQ(boost::system::errc::success, add_bfd_host_ec.get());
    }

    void DeleteBFDHost(BFDConfigClient *client, const boost::asio::ip::address &addr) {
        delete_bfd_host_ec.reset();
        EXPECT_TRUE(client->DeleteBFDHost(addr,  boost::bind(&BFDConfigTest::DeleteCallback, this, _1)));
        TASK_UTIL_EXPECT_TRUE(delete_bfd_host_ec.is_initialized());
        EXPECT_EQ(boost::system::errc::success, delete_bfd_host_ec.get());
    }
};


TEST_F(BFDConfigTest, Test1) {
    BFDConfigClient *client1(new BFDConfigClient(server1.config_ep, evm));
    BFDConfigClient *client2(new BFDConfigClient(server2.config_ep, evm));

    client1->Init();
    TASK_UTIL_EXPECT_TRUE(client1->Initalized());

    client2->Init();
    TASK_UTIL_EXPECT_TRUE(client2->Initalized());

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

    boost::this_thread::sleep(boost::posix_time::seconds(2));
}

TEST_F(BFDConfigTest, SelfSession) {
    BFDConfigClient *client1(new BFDConfigClient(server1.config_ep, evm));

    client1->Init();
    TASK_UTIL_EXPECT_TRUE(client1->Initalized());

    AddBFDHost(client1, addr1);
    WaitForState(client1, addr1, kUp);

    boost::this_thread::sleep(boost::posix_time::seconds(2));
}

TEST_F(BFDConfigTest, DoubleClient) {
    BFDConfigClient *client1(new BFDConfigClient(server1.config_ep, evm));
    BFDConfigClient *client1b(new BFDConfigClient(server1.config_ep, evm));
    BFDConfigClient *client2(new BFDConfigClient(server2.config_ep, evm));

    client1->Init();
    TASK_UTIL_EXPECT_TRUE(client1->Initalized());
    client1b->Init();
    TASK_UTIL_EXPECT_TRUE(client1b->Initalized());
    client2->Init();
    TASK_UTIL_EXPECT_TRUE(client2->Initalized());

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
    boost::this_thread::sleep(boost::posix_time::seconds(5));
    WaitForState(client2, addr1, kUp, false);
    WaitForState(client1b, addr2, kUp, false);
    client1b->Stop();
    WaitForState(client2, addr1, kDown);

    boost::this_thread::sleep(boost::posix_time::seconds(2));
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
