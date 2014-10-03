/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/client/ifmap_state_machine.h"
#include <string>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/timer_impl.h"
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
#include <boost/lexical_cast.hpp>

#include "testing/gunit.h"

using ::testing::InvokeWithoutArgs;
using ::testing::Return;

using namespace std;

class IFMapChannelMock : public IFMapChannel {
public:
    explicit IFMapChannelMock(IFMapManager *manager, const std::string& user,
            const std::string& passwd, const std::string& certstore) :
        IFMapChannel(manager, user, passwd, certstore) {
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
        IDLE = 0,
        RESOLVE = 1,
        SSRC_CONNECT = 2,
        SSRC_HANDSHAKE = 3,
        SEND_NEWSESSION = 4,
        NEWSESSION_RESP_WAIT = 5,
        SEND_SUBSCRIBE = 6,
        SUBSCRIBE_RESP_WAIT = 7,
        ARC_CONNECT = 8,
        ARC_HANDSHAKE = 9,
        SEND_POLL = 10,
        POLL_RESP_WAIT = 11,
        SSRC_START = 12,
        CONNECT_TIMER_WAIT = 13,
    };

    void ConnectFailuresCallback();

protected:
    static const int kOpSuccess = 0;
    static const int kOpFailure = -1;
    static const size_t kReturnBytes = 100;

    IFMapStateMachineTest() :
            ifmap_server_(&db_, &graph_, evm_.io_service()),
            ifmap_manager_(&ifmap_server_, "https://10.1.2.3:8443", "user",
                           "passwd", "", NULL, evm_.io_service()),
            mock_channel_(new IFMapChannelMock(&ifmap_manager_, "user",
                          "passwd", "")),
            success_ec_(0, boost::system::system_category()),
            failure_ec_(boost::system::errc::connection_refused,
                        boost::system::system_category()),
            connect_failure_count_(0) {
        ifmap_manager_.SetChannel(mock_channel_);
        ifmap_manager_.state_machine()->set_max_connect_wait_interval_ms(5);
        ifmap_manager_.state_machine()->set_max_response_wait_interval_ms(30);
    }

    void Start(const std::string &host, const std::string &port) {
        ifmap_manager_.Start(host, port);
    }

    static void on_timeout(const boost::system::error_code &error,
                           bool *trigger) {
        if (error) {
            LOG(DEBUG, "Error is " << error.message());
            return;
        }
        *trigger = true;
    }

    void EventWaitMs(int ms_timeout) {
        bool is_expired = false;
        boost::system::error_code ec;
        TimerImpl timer(*(evm_.io_service()));
        timer.expires_from_now(ms_timeout, ec);
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
    void set_failure_error_code(boost::system::errc::errc_t boost_errno) {
        failure_ec_.assign(boost_errno, boost::system::system_category());
    }
    const std::string &get_host() {
        return mock_channel_->get_host();
    }
    const std::string &get_port() {
        return mock_channel_->get_port();
    }
    uint64_t get_timedout_count(const std::string &host,
                                const std::string &port) {
        IFMapChannel::PeerTimedoutInfo timedout_info =
            mock_channel_->GetTimedoutInfo(host, port);
        return timedout_info.timedout_cnt;
    }

private:
    EventManager evm_;
    DB db_;
    DBGraph graph_;
    IFMapServer ifmap_server_;
    IFMapManager ifmap_manager_;
    IFMapChannelMock *mock_channel_;
    boost::system::error_code success_ec_; // use to return success
    boost::system::error_code failure_ec_; // use to return failure
    int connect_failure_count_;
};

void IFMapStateMachineTest::ConnectFailuresCallback() {
    // n failures + 1 success
    state_machine()->ProcConnectResponse(
        (connect_failure_count_++ == 7) ? success_ec() : failure_ec());
    // Counter is incremented only on failure i.e. not incremented the first
    // time DoConnect() is called.
    EXPECT_EQ(state_machine()->ssrc_connect_attempts_get(),
              (connect_failure_count_ - 1));
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

    Start("10.1.2.3", "8443");
    // Wait for the sequence of events to occur
    EventWaitMs(100);
}

TEST_F(IFMapStateMachineTest, ReadPollRespError) {

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(1)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectionCleaned,
                        state_machine())));

    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcHandshakeResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), SendNewSessionRequest())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), NewSessionResponseWait())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .Times(2)
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .Times(2)
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
    // end the test after sending the third poll request
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
        .WillOnce(Return(kOpFailure));

    Start("10.1.2.3", "8443");
    // Wait for the sequence of events to occur
    EventWaitMs(100);
}

// seven consecutive connect failures to test exp-backoff
TEST_F(IFMapStateMachineTest, SevenSsrcConnectErrors) {

    // Set the response time to a very high value so that we dont run into it.
    state_machine()->set_max_response_wait_interval_ms(1000);

    // 1 regular, 7 connect failures
    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(8)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));

    // should be called for failures only
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(7)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectionCleaned,
                        state_machine())));

    // 7 connect failures, 1 success
    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(8)
        .WillRepeatedly(InvokeWithoutArgs(this, 
                        &IFMapStateMachineTest::ConnectFailuresCallback));

    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(1)
        // Just to end the test somewhere, return i.e. no action
        .WillOnce(Return());

    Start("10.1.2.3", "8443");
    // 7 timeouts, plus some buffer
    EventWaitMs(100);
}

// Each message read fails only once due to message-content not being ok
TEST_F(IFMapStateMachineTest, MessageContentError) {

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));
    // 3 failures and hence 3 calls
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(3)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectionCleaned,
                        state_machine())));
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

    Start("10.1.2.3", "8443");
    // Last event is send-poll-request. So, wait enough for all events to
    // complete.
    EventWaitMs(100);
}

// Each socket read operation fails only once
TEST_F(IFMapStateMachineTest, SocketReadError) {

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));
    // 3 read failures and hence 3 calls
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(3)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectionCleaned,
                        state_machine())));
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
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionResponse,
                        state_machine(), failure_ec(), kReturnBytes)))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .Times(3)
        .WillRepeatedly(Return(kOpSuccess));

    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(3)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(3)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                  &IFMapStateMachine::ProcSubscribeResponse,
                  state_machine(), failure_ec(), kReturnBytes)))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .Times(2)
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
        .WillOnce(InvokeWithoutArgs(boost::bind(
                  &IFMapStateMachine::ProcPollResponseRead,
                  state_machine(), failure_ec(), kReturnBytes)))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollResponseRead,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadPollResponse())
        .Times(1)
        .WillOnce(Return(kOpSuccess));

    Start("10.1.2.3", "8443");
    // Last event is send-poll-request. So, wait enough for all events to
    // complete.
    EventWaitMs(100);
}

// Each socket write operation fails only once
TEST_F(IFMapStateMachineTest, SocketWriteError) {

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));
    // 3 write failures and hence 3 calls
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(3)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectionCleaned,
                        state_machine())));
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
        .WillOnce(InvokeWithoutArgs(boost::bind(
                  &IFMapStateMachine::ProcNewSessionWrite,
                  state_machine(), failure_ec(), kReturnBytes)))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), NewSessionResponseWait())
        .Times(3)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .Times(3)
        .WillRepeatedly(Return(kOpSuccess));

    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(3)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                  &IFMapStateMachine::ProcSubscribeWrite,
                  state_machine(), failure_ec(), kReturnBytes)))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .Times(2)
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
                  state_machine(), failure_ec(), kReturnBytes)))
        .WillOnce(InvokeWithoutArgs(boost::bind(
                  &IFMapStateMachine::ProcPollWrite,
                  state_machine(), success_ec(), kReturnBytes)))
        // Just to end the test somewhere, return i.e. no action
        .WillOnce(Return());
    EXPECT_CALL(*mock_channel(), PollResponseWait())
        .Times(1)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollResponseRead,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadPollResponse())
        .Times(1)
        .WillOnce(Return(kOpSuccess));

    Start("10.1.2.3", "8443");
    // Last event is send-poll-request. So, wait enough for all events to
    // complete.
    EventWaitMs(100);
}

TEST_F(IFMapStateMachineTest, ResponseTimerExpiryTest) {

    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(3)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectionCleaned,
                        state_machine())));
    // 1 regular and 3 response timer expiries i.e. failures
    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(4)
        .WillRepeatedly(Return());

    Start("10.1.2.3", "8443");
    // 3 response-timer expiries, plus some buffer
    EventWaitMs(120);
    EXPECT_EQ(state_machine()->response_timer_expired_count_get(), 3);
}

class IFMapStateMachineErrnoTest : 
        public IFMapStateMachineTest,
        public ::testing::WithParamInterface<boost::system::errc::errc_t> {

public:
    IFMapStateMachineErrnoTest() : IFMapStateMachineTest(),
                                   run_id_(0), poll_write_count_(0) {
    }
    void CheckErrnoPostConditions(boost::system::errc::errc_t boost_errno,
                                  int in_count);
    void OneFailureCallback(Op current_op,
                            boost::system::errc::errc_t boost_errno);

private:
    int run_id_;
    int poll_write_count_; // used to end the test
};

void IFMapStateMachineErrnoTest::CheckErrnoPostConditions(
        boost::system::errc::errc_t boost_errno, int in_count) {
    if (boost_errno == boost::system::errc::timed_out) {
        uint64_t count = get_timedout_count(get_host(), get_port());
        EXPECT_EQ(count, in_count);
    }
}

// The value of run_id_ increments everytime we start a run/round. In each
// round, exactly one operation fails and all ops before the failed one
// succeed i.e in round 1, the first op fails; in round 2, the first succeeds
// but the second fails and so on.
void IFMapStateMachineErrnoTest::OneFailureCallback(Op current_op,
        boost::system::errc::errc_t boost_errno) {
    uint64_t old_count = get_timedout_count(get_host(), get_port());
    bool failure_case = false;

    switch(current_op) {
    case RESOLVE:
        run_id_++;
        failure_case = (run_id_ == 1);
        state_machine()->ProcResolveResponse(
            failure_case ? failure_ec() : success_ec());
        break;
    case SSRC_CONNECT:
        failure_case = (run_id_ == 2);
        state_machine()->ProcConnectResponse(
            failure_case ? failure_ec() : success_ec());
        break;
    case SSRC_HANDSHAKE:
        failure_case = (run_id_ == 3);
        state_machine()->ProcHandshakeResponse(
            failure_case ? failure_ec() : success_ec());
        break;
    case SEND_NEWSESSION:
        failure_case = (run_id_ == 4);
        state_machine()->ProcNewSessionWrite(
            (failure_case ? failure_ec() : success_ec()), kReturnBytes);
        break;
    case NEWSESSION_RESP_WAIT:
        failure_case = (run_id_ == 5);
        state_machine()->ProcNewSessionResponse(
            (failure_case ? failure_ec() : success_ec()), kReturnBytes);
        break;
    case SEND_SUBSCRIBE:
        failure_case = (run_id_ == 6);
        state_machine()->ProcSubscribeWrite(
            (failure_case ? failure_ec() : success_ec()), kReturnBytes);
        break;
    case SUBSCRIBE_RESP_WAIT:
        failure_case = (run_id_ == 7);
        state_machine()->ProcSubscribeResponse(
            (failure_case ? failure_ec() : success_ec()), kReturnBytes);
        break;
    case ARC_CONNECT:
        failure_case = (run_id_ == 8);
        state_machine()->ProcConnectResponse(
            failure_case ? failure_ec() : success_ec());
        break;
    case ARC_HANDSHAKE:
        failure_case = (run_id_ == 9);
        state_machine()->ProcHandshakeResponse(
            failure_case ? failure_ec() : success_ec());
        break;
    case SEND_POLL:
        poll_write_count_++;
        failure_case = (run_id_ == 10);
        if (poll_write_count_ != 4) {
            state_machine()->ProcPollWrite(
                (failure_case ? failure_ec() : success_ec()), kReturnBytes);
        }
        break;
    case POLL_RESP_WAIT:
        failure_case = (run_id_ == 11);
        state_machine()->ProcPollResponseRead(
            (failure_case ? failure_ec() : success_ec()), kReturnBytes);
        break;
    default:
        break;
    }

    if (failure_case && (poll_write_count_ != 4)) {
        // timedout_count should go up by 1
        CheckErrnoPostConditions(boost_errno, (old_count + 1));
    }
}

// Each operation receives an error
TEST_P(IFMapStateMachineErrnoTest, Errors) {

    set_failure_error_code(GetParam());

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(12)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineErrnoTest::OneFailureCallback,
                        this, IFMapStateMachineTest::RESOLVE, GetParam())));
    // should be called for failures only
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(11)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectionCleaned,
                        state_machine())));
    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(11)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineErrnoTest::OneFailureCallback,
                this, IFMapStateMachineTest::SSRC_CONNECT, GetParam())));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(10)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineErrnoTest::OneFailureCallback,
                this, IFMapStateMachineTest::SSRC_HANDSHAKE, GetParam())));
    EXPECT_CALL(*mock_channel(), SendNewSessionRequest())
        .Times(9)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineErrnoTest::OneFailureCallback,
                this, IFMapStateMachineTest::SEND_NEWSESSION, GetParam())));
    EXPECT_CALL(*mock_channel(), NewSessionResponseWait())
        .Times(8)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
             &IFMapStateMachineErrnoTest::OneFailureCallback,
             this, IFMapStateMachineTest::NEWSESSION_RESP_WAIT, GetParam())));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .Times(7)
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(7)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineErrnoTest::OneFailureCallback,
                this, IFMapStateMachineTest::SEND_SUBSCRIBE, GetParam())));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(6)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineErrnoTest::OneFailureCallback,
                this, IFMapStateMachineTest::SUBSCRIBE_RESP_WAIT, GetParam())));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .Times(5)
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), DoConnect(false))
        .Times(5)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineErrnoTest::OneFailureCallback,
                        this, IFMapStateMachineTest::ARC_CONNECT, GetParam())));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(false))
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineErrnoTest::OneFailureCallback,
                this, IFMapStateMachineTest::ARC_HANDSHAKE, GetParam())));
    // one extra SendPollRequest to end the test
    EXPECT_CALL(*mock_channel(), SendPollRequest())
        .Times(4)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachineErrnoTest::OneFailureCallback,
                        this, IFMapStateMachineTest::SEND_POLL, GetParam())));
    EXPECT_CALL(*mock_channel(), PollResponseWait())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineErrnoTest::OneFailureCallback,
                this, IFMapStateMachineTest::POLL_RESP_WAIT, GetParam())));
    EXPECT_CALL(*mock_channel(), ReadPollResponse())
        .Times(1)
        .WillRepeatedly(Return(kOpSuccess));

    Start("10.1.2.3", "8443");
    // 12 timeouts worth of wait, plus some buffer
    EventWaitMs(200);
}

// --gtest_filter=Errno/IFMapStateMachineErrnoTest.Errors/*
INSTANTIATE_TEST_CASE_P(Errno, IFMapStateMachineErrnoTest,
    ::testing::Values(boost::system::errc::connection_refused,
                      boost::system::errc::timed_out));

class IFMapStateMachineConnResetTest1 : 
        public IFMapStateMachineTest,
        public ::testing::WithParamInterface<IFMapStateMachineTest::Op> {

public:
    IFMapStateMachineConnResetTest1() : IFMapStateMachineTest(),
                reset_connection_(true) {
    }
    void ConnectionResetCallback(Op current_op, Op test_op);
    int GetTimes(int my_state, int test_state) {
        if (my_state <= test_state) {
            return 2;
        } else {
            return 1;
        }
    }

protected:
    bool HostPortSame() {
        if ((old_host_.compare(get_host()) == 0) && 
            (old_port_.compare(get_port()) == 0)) {
            return true;
        } else {
            return false;
        }
    }

private:
    void SaveCurrentHostPort() {
        old_host_ = get_host();
        old_port_ = get_port();
    }
    void ResetConnection(Op op) {
        int port = boost::lexical_cast<int>(get_port()) + op;
        std::string port_str = boost::lexical_cast<std::string>(port);

        SaveCurrentHostPort();
        state_machine()->ResetConnectionReqEnqueue(get_host(), port_str);
    }
    std::string old_host_;
    std::string old_port_;
    bool reset_connection_;
};

void IFMapStateMachineConnResetTest1::ConnectionResetCallback(Op current_op,
                                                              Op test_op) {

    switch(current_op) {
    case RESOLVE:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcResolveResponse(success_ec());
        }
        break;
    case SSRC_CONNECT:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcConnectResponse(success_ec());
        }
        break;
    case SSRC_HANDSHAKE:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcHandshakeResponse(success_ec());
        }
        break;
    case SEND_NEWSESSION:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcNewSessionWrite(success_ec(), kReturnBytes);
        }
        break;
    case NEWSESSION_RESP_WAIT:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcNewSessionResponse(success_ec(), kReturnBytes);
        }
        break;
    case SEND_SUBSCRIBE:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcSubscribeWrite(success_ec(), kReturnBytes);
        }
        break;
    case SUBSCRIBE_RESP_WAIT:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcSubscribeResponse(success_ec(), kReturnBytes);
        }
        break;
    case ARC_CONNECT:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcConnectResponse(success_ec());
        }
        break;
    case ARC_HANDSHAKE:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcHandshakeResponse(success_ec());
        }
        break;
    case SEND_POLL:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        } else {
            state_machine()->ProcPollWrite(success_ec(), kReturnBytes);
        }
        break;
    case POLL_RESP_WAIT:
        if (reset_connection_ && (current_op == test_op)) {
            reset_connection_ = false;
            ResetConnection(current_op);
        }
        break;
    default:
        break;
    }
}

// Simulate receiving a connection reset from the ifmap_manager in each state
// *except SsrcStart/ConnectTimerWait*
// This test will be executed once for each state listed in ConnReset and each
// execution will fail once in that state. Hence, all the states before the
// failing state will be visited twice and all the states after the failing
// state will be visited once.
TEST_P(IFMapStateMachineConnResetTest1, AllStates) {

    IFMapStateMachineTest::Op test_op = GetParam();

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(GetTimes(IFMapStateMachineTest::RESOLVE, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::RESOLVE, test_op)));
    // should be called for failures only
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        .Times(1)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachine::ProcConnectionCleaned, state_machine())));
    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(GetTimes(IFMapStateMachineTest::SSRC_CONNECT, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::SSRC_CONNECT, test_op)));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(GetTimes(IFMapStateMachineTest::SSRC_HANDSHAKE, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::SSRC_HANDSHAKE, test_op)));
    EXPECT_CALL(*mock_channel(), SendNewSessionRequest())
        .Times(GetTimes(IFMapStateMachineTest::SEND_NEWSESSION, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::SEND_NEWSESSION, test_op)));
    EXPECT_CALL(*mock_channel(), NewSessionResponseWait())
        .Times(GetTimes(IFMapStateMachineTest::NEWSESSION_RESP_WAIT,
                        GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::NEWSESSION_RESP_WAIT, test_op)));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(GetTimes(IFMapStateMachineTest::SEND_SUBSCRIBE, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::SEND_SUBSCRIBE, test_op)));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(GetTimes(IFMapStateMachineTest::SUBSCRIBE_RESP_WAIT,
                        GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::SUBSCRIBE_RESP_WAIT, test_op)));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), DoConnect(false))
        .Times(GetTimes(IFMapStateMachineTest::ARC_CONNECT, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::ARC_CONNECT, test_op)));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(false))
        .Times(GetTimes(IFMapStateMachineTest::ARC_HANDSHAKE, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::ARC_HANDSHAKE, test_op)));
    EXPECT_CALL(*mock_channel(), SendPollRequest())
        .Times(GetTimes(IFMapStateMachineTest::SEND_POLL, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::SEND_POLL, test_op)));
    EXPECT_CALL(*mock_channel(), PollResponseWait())
        .Times(GetTimes(IFMapStateMachineTest::POLL_RESP_WAIT, GetParam()))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest1::ConnectionResetCallback,
                this, IFMapStateMachineTest::POLL_RESP_WAIT, test_op)));

    Start("10.1.2.3", "8443");
    EventWaitMs(200);
    EXPECT_EQ(HostPortSame(), false);
}

// --gtest_filter=ConnReset1/IFMapStateMachineConnResetTest1.AllStates/*
INSTANTIATE_TEST_CASE_P(ConnReset1, IFMapStateMachineConnResetTest1,
    ::testing::Values(IFMapStateMachineTest::RESOLVE,
                      IFMapStateMachineTest::SSRC_CONNECT,
                      IFMapStateMachineTest::SSRC_HANDSHAKE,
                      IFMapStateMachineTest::SEND_NEWSESSION,
                      IFMapStateMachineTest::NEWSESSION_RESP_WAIT,
                      IFMapStateMachineTest::SEND_SUBSCRIBE,
                      IFMapStateMachineTest::SUBSCRIBE_RESP_WAIT,
                      IFMapStateMachineTest::ARC_CONNECT,
                      IFMapStateMachineTest::ARC_HANDSHAKE,
                      IFMapStateMachineTest::SEND_POLL,
                      IFMapStateMachineTest::POLL_RESP_WAIT));

class IFMapStateMachineConnResetTest2 :
        public IFMapStateMachineTest,
        public ::testing::WithParamInterface<IFMapStateMachineTest::Op> {

public:
    IFMapStateMachineConnResetTest2() : IFMapStateMachineTest() {
    }
    void HandleReconnect(Op test_op) {
        switch(test_op) {
        case SSRC_START:
            ConnectionResetAndClean();
            break;
        case CONNECT_TIMER_WAIT:
            // SsrcStart ([A]call ReconnectPreparation) -> ConnectTimerWait -> 
            // SsrcStart ([B]call ReconnectPreparation) -> ConnectTimerWait -> 
            // ServerResolve -> and so on
            if (state_machine()->last_state() ==
                                        IFMapStateMachine::CONNECTTIMERWAIT) {
                // Case [B]
                state_machine()->ProcConnectionCleaned();
            } else {
                // Case [A]
                ConnectionCleanAndReset();
            }
            break;
        default:
            break;
        }
    }

private:
    void ConnectionResetAndClean() {
        state_machine()->ResetConnectionReqEnqueue(get_host(), get_port());
        state_machine()->ProcConnectionCleaned();
    }
    void ConnectionCleanAndReset() {
        state_machine()->ProcConnectionCleaned();
        state_machine()->ResetConnectionReqEnqueue(get_host(), get_port());
    }
};

// Simulate receiving a connection reset from the ifmap_manager in
// SsrcStart and ConnectTimerWait
TEST_P(IFMapStateMachineConnResetTest2, HandleReconnect) {

    IFMapStateMachineTest::Op test_op = GetParam();

    EXPECT_CALL(*mock_channel(), DoResolve())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcResolveResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), ReconnectPreparation())
        // transitions with test_op SSRC_START:
        //      SsrcStart -> ConnectTimerWait -> ServerResolve -> and so on
        // transitions with test_op CONNECT_TIMER_WAIT:
        //      SsrcStart -> ConnectTimerWait -> SsrcStart -> ConnectTimerWait
        //                -> ServerResolve -> and so on
        .Times((test_op == IFMapStateMachineTest::SSRC_START ? 1 : 2))
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                &IFMapStateMachineConnResetTest2::HandleReconnect,
                this, test_op)));
    EXPECT_CALL(*mock_channel(), DoConnect(true))
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcConnectResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), DoSslHandshake(true))
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcHandshakeResponse,
                        state_machine(), success_ec())));
    EXPECT_CALL(*mock_channel(), SendNewSessionRequest())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), NewSessionResponseWait())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcNewSessionResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ExtractPubSessionId())
        .Times(2)
        .WillRepeatedly(Return(kOpSuccess));
    EXPECT_CALL(*mock_channel(), SendSubscribe())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeWrite,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), SubscribeResponseWait())
        .Times(2)
        .WillRepeatedly(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcSubscribeResponse,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadSubscribeResponseStr())
        .Times(2)
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
    // end the test after sending the second poll request
    EXPECT_CALL(*mock_channel(), SendPollRequest())
        .Times(3)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollWrite,
                        state_machine(), success_ec(), kReturnBytes)))
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollWrite,
                        state_machine(), success_ec(), kReturnBytes)))
        // Just to end the test somewhere, return i.e. no action
        .WillRepeatedly(Return());
    EXPECT_CALL(*mock_channel(), PollResponseWait())
        .Times(2)
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollResponseRead,
                        state_machine(), failure_ec(), kReturnBytes)))
        .WillOnce(InvokeWithoutArgs(boost::bind(
                        &IFMapStateMachine::ProcPollResponseRead,
                        state_machine(), success_ec(), kReturnBytes)));
    EXPECT_CALL(*mock_channel(), ReadPollResponse())
        .Times(1)
        .WillRepeatedly(Return(kOpSuccess));

    Start("10.1.2.3", "8443");
    EventWaitMs(50);
}

// --gtest_filter=ConnReset2/IFMapStateMachineConnResetTest2.HandleReconnect/*
INSTANTIATE_TEST_CASE_P(ConnReset2, IFMapStateMachineConnResetTest2,
    ::testing::Values(IFMapStateMachineTest::SSRC_START,
                      IFMapStateMachineTest::CONNECT_TIMER_WAIT));

int main(int argc, char **argv) {
    ConcurrencyChecker::disable_ = true;
    LoggingInit();
    ::testing::InitGoogleMock(&argc, argv);
    bool success = RUN_ALL_TESTS();
    return success;
}
