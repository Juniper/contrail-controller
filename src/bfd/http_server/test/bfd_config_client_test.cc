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

//TODO server error-code to boost::system::error (currently ignoring e.g. 500 Interal)

using namespace BFD;


static const boost::asio::ip::address remote_host_1 = boost::asio::ip::address::from_string("10.5.3.206");
static const boost::asio::ip::address remote_host_2 = boost::asio::ip::address::from_string("10.5.3.1");


class BFDConfigClientTest : public ::testing::Test {
 public:
    BFDConfigClientTest() :
        session_state(kInit)
        {}

    // BfdJsonConfig config

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



    //void MonitorBFDSessionCallback(const std::vector<BfdJsonStateNotification> &session_states, )
};

#if 0
TEST_F(BFDConfigClientTest, Test1) {
    boost::asio::ip::tcp::endpoint ep;
    boost::system::error_code ec;
    std::string ip("127.0.0.1");
    ep.address(boost::asio::ip::address::from_string(ip, ec));
    ep.port(8090);

    EventManager evm;

    BFDConfigClient client(ep, &evm);
    EventManagerThread emt(&evm);

    client.Init();
    TASK_UTIL_EXPECT_TRUE(client.Initalized());

    EXPECT_TRUE(client.AddBFDHost(remote_host_1, boost::bind(&BFDConfigClientTest::AddBFDHostCallback, this, _1)));
    TASK_UTIL_EXPECT_TRUE(add_bfd_host_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, add_bfd_host_ec.get());

    EXPECT_TRUE(client.Initalized());

    BfdJsonState state;

//    monitor_ec.reset();
//    EXPECT_TRUE(client.Monitor(boost::bind(&BFDConfigClientTest::MonitorCallback, this, _1, _2)));
//    TASK_UTIL_EXPECT_TRUE(monitor_ec.is_initialized());
//    EXPECT_EQ(boost::system::errc::success, monitor_ec.get());
//    EXPECT_EQ(kDown, new_states.states[remote_host]);


    while (session_state != kUp) {
        get_bfd_session_ec.reset();
        EXPECT_TRUE(client.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
        TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
        EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
        boost::this_thread::sleep(boost::posix_time::seconds(1));
    }

    monitor_ec.reset();
    EXPECT_TRUE(client.Monitor(boost::bind(&BFDConfigClientTest::MonitorCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(monitor_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, monitor_ec.get());
    EXPECT_EQ(kUp, new_states.states[remote_host_1]);

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    monitor_ec.reset();
    EXPECT_TRUE(client.Monitor(boost::bind(&BFDConfigClientTest::MonitorCallback, this, _1, _2)));
    boost::this_thread::sleep(boost::posix_time::seconds(10));
    TASK_UTIL_EXPECT_TRUE(monitor_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, monitor_ec.get());
    EXPECT_EQ(kDown, new_states.states[remote_host_1]);

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kDown, session_state);


    EXPECT_TRUE(client.Initalized());
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    EXPECT_TRUE(client.Initalized());


    client.Stop();
}
#endif


#if 1
TEST_F(BFDConfigClientTest, TwoClients) {
    boost::asio::ip::tcp::endpoint ep;
    boost::system::error_code ec;
    std::string ip("127.0.0.1");
    ep.address(boost::asio::ip::address::from_string(ip, ec));
    ep.port(8090);

    EventManager evm;

    BFDConfigClient client1(ep, &evm);
    BFDConfigClient client2(ep, &evm);
    EventManagerThread emt(&evm);

    client1.Init();
    TASK_UTIL_EXPECT_TRUE(client1.Initalized());

    client2.Init();
    TASK_UTIL_EXPECT_TRUE(client2.Initalized());

    add_bfd_host_ec.reset();
    EXPECT_TRUE(client1.AddBFDHost(remote_host_1, boost::bind(&BFDConfigClientTest::AddBFDHostCallback, this, _1)));
    TASK_UTIL_EXPECT_TRUE(add_bfd_host_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, add_bfd_host_ec.get());

    add_bfd_host_ec.reset();
    EXPECT_TRUE(client2.AddBFDHost(remote_host_1, boost::bind(&BFDConfigClientTest::AddBFDHostCallback, this, _1)));
    TASK_UTIL_EXPECT_TRUE(add_bfd_host_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, add_bfd_host_ec.get());

    while (session_state != kUp) {
        monitor_ec.reset();
        EXPECT_TRUE(client1.Monitor(boost::bind(&BFDConfigClientTest::MonitorCallback, this, _1, _2)));
        TASK_UTIL_EXPECT_TRUE(monitor_ec.is_initialized());
        EXPECT_EQ(boost::system::errc::success, monitor_ec.get());
        session_state = new_states.states[remote_host_1];
     }

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client1.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client2.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    client1.Stop();

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client2.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    boost::this_thread::sleep(boost::posix_time::seconds(5));

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client2.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    client2.Stop();
}
#endif


#if 1
TEST_F(BFDConfigClientTest, Delete) {
    boost::asio::ip::tcp::endpoint ep;
    boost::system::error_code ec;
    std::string ip("127.0.0.1");
    ep.address(boost::asio::ip::address::from_string(ip, ec));
    ep.port(8090);

    EventManager evm;

    BFDConfigClient client1(ep, &evm);
    BFDConfigClient client2(ep, &evm);
    EventManagerThread emt(&evm);

    client1.Init();
    TASK_UTIL_EXPECT_TRUE(client1.Initalized());

    client2.Init();
    TASK_UTIL_EXPECT_TRUE(client2.Initalized());


    add_bfd_host_ec.reset();
    EXPECT_TRUE(client1.AddBFDHost(remote_host_1, boost::bind(&BFDConfigClientTest::AddBFDHostCallback, this, _1)));
    TASK_UTIL_EXPECT_TRUE(add_bfd_host_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, add_bfd_host_ec.get());

    while (session_state != kUp) {
        monitor_ec.reset();
        EXPECT_TRUE(client1.Monitor(boost::bind(&BFDConfigClientTest::MonitorCallback, this, _1, _2)));
        TASK_UTIL_EXPECT_TRUE(monitor_ec.is_initialized());
        EXPECT_EQ(boost::system::errc::success, monitor_ec.get());
        session_state = new_states.states[remote_host_1];
     }

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client1.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);


    get_bfd_session_ec.reset();
    EXPECT_TRUE(client1.GetBFDSession(remote_host_2,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_NE(boost::system::errc::success, get_bfd_session_ec.get());

    delete_bfd_host_ec.reset();
    EXPECT_TRUE(client1.DeleteBFDHost(remote_host_1,  boost::bind(&BFDConfigClientTest::DeleteCallback, this, _1)));
    TASK_UTIL_EXPECT_TRUE(delete_bfd_host_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, delete_bfd_host_ec.get());

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client1.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_NE(boost::system::errc::success, get_bfd_session_ec.get());


#if 0
    while (session_state != kUp) {
        monitor_ec.reset();
        EXPECT_TRUE(client1.Monitor(boost::bind(&BFDConfigClientTest::MonitorCallback, this, _1, _2)));
        TASK_UTIL_EXPECT_TRUE(monitor_ec.is_initialized());
        EXPECT_EQ(boost::system::errc::success, monitor_ec.get());
        session_state = new_states.states[remote_host_1];
     }



    get_bfd_session_ec.reset();
    EXPECT_TRUE(client1.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client2.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    client1.Stop();

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client2.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    boost::this_thread::sleep(boost::posix_time::seconds(5));

    get_bfd_session_ec.reset();
    EXPECT_TRUE(client2.GetBFDSession(remote_host_1,  boost::bind(&BFDConfigClientTest::GetBFDSessionCallback, this, _1, _2)));
    TASK_UTIL_EXPECT_TRUE(get_bfd_session_ec.is_initialized());
    EXPECT_EQ(boost::system::errc::success, get_bfd_session_ec.get());
    EXPECT_EQ(kUp, session_state);

    client2.Stop();
#endif
}
#endif

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
