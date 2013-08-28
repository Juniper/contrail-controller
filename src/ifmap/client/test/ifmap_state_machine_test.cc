/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/client/ifmap_state_machine.h"
#include <string>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/event_manager.h"
#include "ifmap/client/ifmap_manager.h"
#include "ifmap/client/ifmap_channel.h"
#include "ifmap/ifmap_server.h"

#include <boost/asio.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/system/error_code.hpp>

#include "testing/gunit.h"

using ::testing::InvokeWithoutArgs;
using ::testing::Return;

using namespace std;

class IFMapChannelMock : public IFMapChannel {
public:
    explicit IFMapChannelMock(IFMapManager *manager, const std::string& url,
            const std::string& user, const std::string& passwd,
            const std::string& certstore) :
        IFMapChannel(manager, url, user, passwd, certstore) {
    }

    MOCK_METHOD0(ReconnectPreparation, void());
    MOCK_METHOD0(DoResolve, void());
    MOCK_METHOD1(DoConnect, void(bool));
    MOCK_METHOD1(DoSslHandshake, void(bool));
    MOCK_METHOD0(SendNewSessionRequest, void());
    MOCK_METHOD0(NewSessionResponseWait, void());
    MOCK_METHOD0(ExtractPubSessionId, int());
    MOCK_METHOD0(SendSubscribe, void());
    MOCK_METHOD0(SubscribeResponseWait, void());
    MOCK_METHOD0(ReadSubscribeResponseStr, int());
    MOCK_METHOD0(SendPollRequest, void());
    MOCK_METHOD0(PollResponseWait, void());
    MOCK_METHOD0(ReadPollResponse, int());
};

class IFMapStateMachineTest : public ::testing::Test {
public:
    enum Op {
        NONE = 0,
        RESOLVE = 1,
        CONNECT = 2,
        HANDSHAKE = 3,
        NS_WRITE = 4,
        NS_RESPONSE = 5,
        SUB_WRITE = 6,
        SUB_RESPONSE = 7,
        READ_SUB_RESPONSE = 8,
        ARC_CONNECT = 9,
        ARC_HANDSHAKE = 10,
        POLL_WRITE = 11,
        POLL_RESPONSE = 12,
    };

    void ConnectFailuresCallback();
    void OneFailureCallback(Op current_op);

protected:
    static const int kOpSuccess = 0;
    static const int kOpFailure = -1;
    static const size_t kReturnBytes = 100;

    IFMapStateMachineTest() :
            ifmap_server_(&db_, &graph_, evm_.io_service()),
            ifmap_manager_(&ifmap_server_, "https://10.1.2.115:8443", "user",
                           "passwd", "", NULL, evm_.io_service()),
            mock_channel_(new IFMapChannelMock(&ifmap_manager_,
                          "https://10.1.2.115:8443", "user", "passwd", "")),
            success_ec_(0, boost::system::system_category()),
            failure_ec_(boost::system::errc::connection_refused,
                        boost::system::system_category()) {
        ifmap_manager_.SetChannel(mock_channel_);
    }

    void Start() {
        ifmap_manager_.Start();
    }

    static void on_timeout(const boost::system::error_code &error,
                           bool *trigger) {
        if (error) {
            LOG(DEBUG, "Error is " << error.message());
            return;
        }
        *trigger = true;
    }

    void EventWait(int timeout) {
        bool is_expired = false;
        boost::asio::deadline_timer timer(*(evm_.io_service()));
        timer.expires_from_now(boost::posix_time::seconds(timeout));
        timer.async_wait(boost::bind(&IFMapStateMachineTest::on_timeout,
                         boost::asio::placeholders::error, &is_expired));
        while (!is_expired) {
            evm_.RunOnce();
            task_util::WaitForIdle();
        }
    }

    IFMapChannelMock *mock_channel() { return mock_channel_; }
    IFMapStateMachine *state_machine() {
        return ifmap_manager_.state_machine();
    }
    boost::system::error_code success_ec() { return success_ec_; }
    boost::system::error_code failure_ec() { return failure_ec_; }

private:
    EventManager evm_;
    DB db_;
    DBGraph graph_;
    IFMapServer ifmap_server_;
    IFMapManager ifmap_manager_;
    IFMapChannelMock *mock_channel_;
    boost::system::error_code success_ec_; // use to return success
    boost::system::error_code failure_ec_; // use to return failure
};

void IFMapStateMachineTest::ConnectFailuresCallback() {
    // n failures + 1 success
    static int count = 0;
    state_machine()->ProcConnectResponse(
        (count++ == 7) ? success_ec() : failure_ec());
}

// The value of run_id increments everytime we start a run/round. In each
// round, exactly one operation fails and all ops before the failed one
// succeed i.e in round 1, the first op fails; in round 2, the first succeeds
// but the second fails and so on.
void IFMapStateMachineTest::OneFailureCallback(Op current_op) {
    static int run_id = 0;
    static int poll_write_count = 0; // used to end the test
    switch(current_op) {
    case RESOLVE:
        run_id++;
        state_machine()->ProcResolveResponse(
            (run_id == 1) ? failure_ec() : success_ec());
        break;
    case CONNECT:
        state_machine()->ProcConnectResponse(
            (run_id == 2) ? failure_ec() : success_ec());
        break;
    case HANDSHAKE:
        state_machine()->ProcHandshakeResponse(
            (run_id == 3) ? failure_ec() : success_ec());
        break;
    case NS_WRITE:
        state_machine()->ProcNewSessionWrite(
            ((run_id == 4) ? failure_ec() : success_ec()), kReturnBytes);
        break;
    case NS_RESPONSE:
        state_machine()->ProcNewSessionResponse(
            ((run_id == 5) ? failure_ec() : success_ec()), kReturnBytes);
        break;
    case SUB_WRITE:
        state_machine()->ProcSubscribeWrite(
            ((run_id == 6) ? failure_ec() : success_ec()), kReturnBytes);
        break;
    case SUB_RESPONSE:
        state_machine()->ProcSubscribeResponse(
            ((run_id == 7) ? failure_ec() : success_ec()), kReturnBytes);
        break;
    case ARC_CONNECT:
        state_machine()->ProcConnectResponse(
            (run_id == 8) ? failure_ec() : success_ec());
        break;
    case ARC_HANDSHAKE:
        state_machine()->ProcHandshakeResponse(
            (run_id == 9) ? failure_ec() : success_ec());
        break;
    case POLL_WRITE:
        poll_write_count++;
        if (poll_write_count != 4) {
            state_machine()->ProcPollWrite(
                ((run_id == 10) ? failure_ec() : success_ec()), kReturnBytes);
        }
        break;
    case POLL_RESPONSE:
        state_machine()->ProcPollResponseRead(
            ((run_id == 11) ? failure_ec() : success_ec()), kReturnBytes);
        break;
    default:
        break;
    }
}

// Every operation succeeds
TEST_F(IFMapStateMachineTest, ErrorlessRun) {

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcHandshakeResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), SendNewSessionRequest())
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), NewSessionResponseWait())
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .Times(1)
        .WillOnce(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .Times(1)
        .WillOnce(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), DoConnect(false))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(false))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcHandshakeResponse,
                        state_machine(), success_ec())));
    // end the test after sending the second poll request
    EXPECT_CALL(*mock_channel(), SendPollRequest())
        .Times(2)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollWrite,
                        state_machine(), success_ec(), kReturnBytes)))
        // Just to end the test somewhere, return i.e. no action
        .WillOnce(Return());
    EXPECT_CALL(*mock_channel(), PollResponseWait())
        .Times(1)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollResponseRead,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadPollResponse())
        .Times(1)
        .WillOnce(Return(kOpSuccess));

    Start();
    // Wait for the sequence of events to occur
    EventWait(2);
}

// seven consecutive connect failures to test exp-backoff
TEST_F(IFMapStateMachineTest, SevenSsrcConnectFailures) {

    // 1 regular, 7 connect failures
    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(8)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));

    // should be called for failures only
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(7);

    // 7 connect failures, 1 success
    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(8)
        .WillRepeatedly(InvokeWithoutArgs(this, 
                        &IFMapStateMachineTest::ConnectFailuresCallback));

    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(1)
        // Just to end the test somewhere, return i.e. no action
        .WillOnce(Return());

    Start();
    // 7 timeouts, 1s, 2s, 4s, 8s, 16s, 30s, 30s
    EventWait(95);
}

// Each operation fails only once to test almost all code paths.
// The last operation executes once, the second-last executes twice and so on.
TEST_F(IFMapStateMachineTest, OneErrorRun) {

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(12)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::RESOLVE)));
    // should be called for failures only
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(11);
    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(11)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::CONNECT)));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(10)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::HANDSHAKE)));
    EXPECT_CALL(*mock_channel(), SendNewSessionRequest())
        .Times(9)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::NS_WRITE)));
    EXPECT_CALL(*mock_channel(), NewSessionResponseWait())
        .Times(8)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::NS_RESPONSE)));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .Times(7)
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(7)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::SUB_WRITE)));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(6)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::SUB_RESPONSE)));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .Times(5)
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), DoConnect(false))
        .Times(5)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::ARC_CONNECT)));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(false))
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::ARC_HANDSHAKE)));
    // one extra SendPollRequest to end the test
    EXPECT_CALL(*mock_channel(), SendPollRequest())
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::POLL_WRITE)));
    EXPECT_CALL(*mock_channel(), PollResponseWait())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineTest::OneFailureCallback,
                        this, IFMapStateMachineTest::POLL_RESPONSE)));
    EXPECT_CALL(*mock_channel(), ReadPollResponse())
        .Times(1)
        .WillRepeatedly(Return(kOpSuccess));

    Start();
    // 12 seconds worth of timeouts
    EventWait(15);
}

// Each message read operation fails only once
TEST_F(IFMapStateMachineTest, MessageContentError) {

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));
    // 3 failures and hence 3 calls
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(3);
    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcHandshakeResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), SendNewSessionRequest())
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), NewSessionResponseWait())
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .Times(4)
        .WillOnce(Return(kOpFailure))
        .WillRepeatedly(Return(kOpSuccess));

    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(3)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(3)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .Times(3)
        .WillOnce(Return(kOpFailure))
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), DoConnect(false))
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(false))
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcHandshakeResponse,
                        state_machine(), success_ec())));
    // End the test after sending the second poll request. Hence one more call
    // than the other mock functions below
    EXPECT_CALL(*mock_channel(), SendPollRequest())
        .Times(3)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollWrite,
                        state_machine(), success_ec(), kReturnBytes)))
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollWrite,
                        state_machine(), success_ec(), kReturnBytes)))
        // Just to end the test somewhere, return i.e. no action
        .WillOnce(Return());
    EXPECT_CALL(*mock_channel(), PollResponseWait())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollResponseRead,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadPollResponse())
        .Times(2)
        .WillOnce(Return(kOpFailure))
        .WillOnce(Return(kOpSuccess));

    Start();
    EventWait(12);
}

TEST_F(IFMapStateMachineTest, ResponseTimerExpiryTest) {

    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(3);
    // 1 regular and 3 response timer expiries i.e. failures
    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(4)
        .WillRepeatedly(Return());

    Start();
    // 3 response-timer expiries (15s), 1, 2, and 4s connect-timer expiries
    EventWait(24);
    EXPECT_EQ(state_machine()->connection_attempts_get(), 3);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleMock(&argc, argv);
    bool success = RUN_ALL_TESTS();
    return success;
}
