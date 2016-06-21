//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include "testing/gunit.h"

#include <boost/system/error_code.hpp>
#include "base/connection_info.h"
#include "io/event_manager.h"

using process::ConnectionStateManager;
using process::ConnectionState;
using process::ConnectionInfo;
using process::ProcessState;
using process::ConnectionStatus;
using process::ConnectionType;
using process::g_process_info_constants;
using process::GetProcessStateCb;
using process::ConnectionTypeName;

class ConnectionInfoTest : public ::testing::Test {
 protected:
    static void SetUpTestCase() {
        std::vector<ConnectionTypeName> expected_connections = boost::assign::list_of
         (ConnectionTypeName("Test", "Test1"))
         (ConnectionTypeName("Test", "Test2"));
        ConnectionStateManager::
            GetInstance()->Init(*evm_.io_service(), "Test",
            "ConnectionInfoTest", "0", boost::bind(
            &process::GetProcessStateCb, _1, _2, _3, expected_connections), "ObjectTest");
    }
    static void TearDownTestCase() {
        ConnectionStateManager::
            GetInstance()->Shutdown();
    }
    void PopulateConnInfo(ConnectionInfo *cinfo, const std::string &name,
        ConnectionStatus::type status, const std::string &description) {
        cinfo->set_type(g_process_info_constants.ConnectionTypeNames.find(
            ConnectionType::TEST)->second);
        cinfo->set_name(name);
        std::string eps("127.0.0.1:0");
        std::vector<std::string> epsv(1, eps);
        cinfo->set_server_addrs(epsv);
        cinfo->set_status(g_process_info_constants.ConnectionStatusNames.find(
            status)->second);
        cinfo->set_description(description);
    }
    void UpdateConnInfo(const std::string &name, ConnectionStatus::type status,
        const std::string &description, std::vector<ConnectionInfo> *vcinfo) {
        ConnectionInfo cinfo;
        PopulateConnInfo(&cinfo, name, status, description);
        vcinfo->push_back(cinfo);
    }
    void UpdateConnState(const std::string &name, ConnectionStatus::type status,
        const std::string &description,
        const std::vector<ConnectionInfo> &vcinfo) {
        boost::system::error_code ec;
        boost::asio::ip::address addr(boost::asio::ip::address::from_string(
            "127.0.0.1", ec));
        ASSERT_EQ(0, ec.value());
        process::Endpoint ep(addr, 0);
        // Set callback
        ConnectionStateManager::
            GetInstance()->SetProcessStateCb(boost::bind(
                &ConnectionInfoTest::VerifyProcessStateCb, this, _1, _2, _3,
                vcinfo));
        // Update
        ConnectionState::GetInstance()->Update(ConnectionType::TEST, name,
            status, ep, description);
    }
    void DeleteConnInfo(const std::string &name,
        std::vector<ConnectionInfo> *vcinfo) {
        const std::string ctype(
            g_process_info_constants.ConnectionTypeNames.find(
                ConnectionType::TEST)->second);
        for (size_t i = 0; i < vcinfo->size(); i++) {
            ConnectionInfo &tinfo(vcinfo->at(i));
            if (tinfo.get_type() == ctype &&
                tinfo.get_name() == name) {
                vcinfo->erase(vcinfo->begin() + i);
            }
        }
    }
    void DeleteConnState(const std::string &name,
        const std::vector<ConnectionInfo> &vcinfo) {
        // Set callback
        ConnectionStateManager::
            GetInstance()->SetProcessStateCb(boost::bind(
                &ConnectionInfoTest::VerifyProcessStateCb, this, _1, _2, _3,
                vcinfo));
        // Delete
        ConnectionState::GetInstance()->Delete(ConnectionType::TEST, name);
    }
    void VerifyProcessStateCb(const std::vector<ConnectionInfo> &cinfos,
        ProcessState::type &state, std::string &message,
        const std::vector<ConnectionInfo> &ecinfos) {
        state = ProcessState::FUNCTIONAL;
        EXPECT_EQ(ecinfos, cinfos);
    }

    static EventManager evm_;
};

EventManager ConnectionInfoTest::evm_;

TEST_F(ConnectionInfoTest, Basic) {
    std::vector<ConnectionInfo> vcinfo;
    // Verify update
    UpdateConnInfo("Test1", ConnectionStatus::UP, "Test1 UP", &vcinfo);
    UpdateConnState("Test1", ConnectionStatus::UP, "Test1 UP", vcinfo);
    UpdateConnInfo("Test2", ConnectionStatus::DOWN, "Test2 DOWN", &vcinfo);
    UpdateConnState("Test2", ConnectionStatus::DOWN, "Test2 DOWN", vcinfo);
    // Verify delete
    DeleteConnInfo("Test1", &vcinfo);
    DeleteConnState("Test1", vcinfo);
}

TEST_F(ConnectionInfoTest, Callback) {
    std::vector<ConnectionInfo> vcinfo;
    UpdateConnInfo("Test1", ConnectionStatus::UP, "Test1 UP", &vcinfo);
    ProcessState::type pstate;
    std::string message1;
    std::vector<ConnectionTypeName> expected_connections = boost::assign::list_of
         (ConnectionTypeName("Test", "Test1"));
    // Expected connection and conn_info are same
    GetProcessStateCb(vcinfo, pstate, message1, expected_connections);
    EXPECT_EQ(ProcessState::FUNCTIONAL, pstate);
    EXPECT_TRUE(message1.empty());
    std::string message2;
    expected_connections.push_back(ConnectionTypeName("Test","Test2"));
    // Expected connection more than conn_info
    GetProcessStateCb(vcinfo, pstate, message2, expected_connections);
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, pstate);
    EXPECT_EQ("Number of connections:1, Expected:2 Missing: Test:Test2", message2);
    // 2 expected connections are more than conn_info
    expected_connections.push_back(ConnectionTypeName("Test","Test3"));
    std::string message3;
    GetProcessStateCb(vcinfo, pstate, message3, expected_connections);
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, pstate);
    EXPECT_EQ("Number of connections:1, Expected:3 Missing: Test:Test2,Test:Test3", message3);
    expected_connections.pop_back();
    UpdateConnInfo("Test2", ConnectionStatus::DOWN, "Test2 DOWN", &vcinfo);
    std::string message4;
    GetProcessStateCb(vcinfo, pstate, message4, expected_connections);
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, pstate);
    EXPECT_EQ("Test:Test2 connection down", message4);
    UpdateConnInfo("Test3", ConnectionStatus::DOWN, "Test3 DOWN", &vcinfo);
    std::string message5;
    // More connection in conn_info than expected_connections
    GetProcessStateCb(vcinfo, pstate, message5, expected_connections);
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, pstate);
    EXPECT_EQ("Number of connections:3, Expected:2 Extra: Test:Test3", message5);
    std::string message6;
    expected_connections.push_back(ConnectionTypeName("Test","Test3"));
    GetProcessStateCb(vcinfo, pstate, message6, expected_connections);
    EXPECT_EQ(ProcessState::NON_FUNCTIONAL, pstate);
    EXPECT_EQ("Test:Test2, Test:Test3 connection down", message6);
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
