//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#include <testing/gunit.h>
#include <gmock/gmock.h>

#include <boost/scoped_ptr.hpp>
#include <boost/system/error_code.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>

#include <base/logging.h>
#include <io/process_signal.h>
#include <io/event_manager.h>

using namespace process;

class SignalMock : public Signal {
 public:
    SignalMock(EventManager *evm, const Signal::SignalCallbackMap &smap,
        const std::vector<Signal::SignalChildHandler> &chandlers,
        bool always_handle_sigchild) :
            Signal(evm, smap, chandlers, always_handle_sigchild) {
    }
    virtual ~SignalMock() {
    }
    MOCK_METHOD3(WaitPid, int(int pid, int *status, int options));
};

class ProcessSignalTest : public ::testing::Test {
 public:
    void HandleSignal(const boost::system::error_code &a_ec, int a_sig,
        int e_sig) {
        signal_handler_called_++;
        ASSERT_EQ(boost::system::errc::success, a_ec);
        EXPECT_EQ(a_sig, e_sig);
    }

    void HandleSigChild(const boost::system::error_code &a_ec, int a_sig,
        pid_t a_pid, int a_status, int e_sig, pid_t e_pid, int e_status) {
        sigchild_handler_called_++;
        ASSERT_EQ(boost::system::errc::success, a_ec);
        EXPECT_EQ(a_sig, e_sig);
        EXPECT_EQ(a_pid, e_pid);
        EXPECT_EQ(a_status, e_status);
    }

 protected:
    ProcessSignalTest() {
    }

    ~ProcessSignalTest() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    void SendSignal(int sig) {
        boost::system::error_code ec;
        psignal_->HandleSig(ec, sig);
    }

    int signal_handler_called_;
    int sigchild_handler_called_;
    EventManager evm_;
    boost::scoped_ptr<SignalMock> psignal_;
};

using ::testing::DoAll;
using ::testing::SetArgPointee;
using ::testing::Return;
using ::testing::_;

TEST_F(ProcessSignalTest, Basic) {
    signal_handler_called_ = 0;
    Signal::SignalCallbackMap smap = boost::assign::map_list_of
        (SIGHUP, std::vector<Signal::SignalHandler>(1,
            boost::bind(&ProcessSignalTest::HandleSignal, this, _1, _2,
            SIGHUP)))
        (SIGTERM, std::vector<Signal::SignalHandler>(1,
            boost::bind(&ProcessSignalTest::HandleSignal, this, _1, _2,
            SIGTERM)));
    std::vector<Signal::SignalChildHandler> chandler = boost::assign::list_of
        (boost::bind(&ProcessSignalTest::HandleSigChild, this, _1, _2,
        _3, _4, SIGCHLD, 333, 333));
    psignal_.reset(new SignalMock(&evm_, smap, chandler, false));
    EXPECT_CALL(*psignal_, WaitPid(_, _, _))
        .Times(2)
        .WillOnce(DoAll(SetArgPointee<1>(333), Return(333)))
        .WillOnce(Return(-1));
    // Now call the signals
    SendSignal(SIGTERM);
    SendSignal(SIGHUP);
    SendSignal(SIGCHLD);
    psignal_->Terminate();
    psignal_.reset();
    EXPECT_EQ(2, signal_handler_called_);
    EXPECT_EQ(1, sigchild_handler_called_);
}

int main(int argc, char *argv[]) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
