/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */


#include "base/regex.h"
typedef contrail::regex regex_t;

#include "base/test/task_test_util.h"
#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/test/bfd_test_utils.h"

#include <boost/asio.hpp>
#include <boost/optional.hpp>

#include <testing/gunit.h>

using namespace BFD;

static const Discriminator localDiscriminator = 0x12345678;
static const Discriminator remoteDiscriminator = 0x87654321;
static const int detectionTimeMultiplier = 3;
const boost::asio::ip::address addr = boost::asio::ip::address::from_string("1.1.1.1");

class SessionMock : public Session {
public:
    SessionMock(Discriminator localDiscriminator,
                boost::asio::ip::address remoteHost, EventManager *evm,
                const SessionConfig &config, Connection *communicator) :
            Session(localDiscriminator, SessionKey(remoteHost), evm, config,
                    communicator) {
    }

    bool TriggerRecvTimerExpired() { return RecvTimerExpired(); }
};

class SessionTest : public ::testing::Test {
  public:
    SessionTest() {
        config.desiredMinTxInterval = boost::posix_time::seconds(1);
        config.requiredMinRxInterval = boost::posix_time::seconds(1);
        config.detectionTimeMultiplier = detectionTimeMultiplier;

        packet.diagnostic = kNoDiagnostic;
        packet.state = kDown;
        packet.poll = false;
        packet.final = false;
        packet.control_plane_independent = false;
        packet.authentication_present = false;
        packet.demand = false;
        packet.multipoint = false;
        packet.detection_time_multiplier = 5;
        packet.sender_discriminator = remoteDiscriminator;
        packet.receiver_discriminator = localDiscriminator;
        packet.desired_min_tx_interval = boost::posix_time::seconds(1);
        packet.required_min_rx_interval = boost::posix_time::seconds(1);
        packet.required_min_echo_rx_interval = boost::posix_time::seconds(0);
    }
    class TestConnection : public Connection {
      public:
        virtual void SendPacket(
            const boost::asio::ip::udp::endpoint &local_endpoint,
            const boost::asio::ip::udp::endpoint &remote_endpoint,
            const SessionIndex &session_index,
            const boost::asio::mutable_buffer &pkt, int pktSize) {
            ControlPacket *packet =
                ParseControlPacket(boost::asio::buffer_cast<const uint8_t *>(
                                       pkt), pktSize);
            ASSERT_EQ(localDiscriminator, packet->sender_discriminator);
            ASSERT_EQ(remoteDiscriminator, packet->receiver_discriminator);
            ASSERT_EQ(detectionTimeMultiplier,
                      packet->detection_time_multiplier);
            savedPacket.reset(packet);
            const uint8_t *p = boost::asio::buffer_cast<const uint8_t *>(pkt);
            delete[] p;
        }
        virtual void NotifyStateChange(const SessionKey &key, const bool &up) {
        }
        virtual Server *GetServer() const { return server_; }
        virtual void SetServer(Server *server) { server_ = server; }
        boost::scoped_ptr<ControlPacket> savedPacket;
        virtual ~TestConnection() {}
      private:
        Server *server_;
    };
    SessionConfig config;
    ControlPacket packet;
    EventManager evm;
};

TEST_F(SessionTest, UpTest) {
    TestConnection tc;
    SessionMock session(localDiscriminator, addr, &evm, config, &tc);

    EXPECT_EQ(kDown, session.local_state());
    packet.state = kInit;
    session.ProcessControlPacket(&packet);
    EXPECT_EQ(kUp, session.local_state());
    std::cout << session.toString();
}

TEST_F(SessionTest, PollRecvTest) {
    TestConnection tc;
    SessionMock session(localDiscriminator, addr, &evm, config, &tc);

    packet.poll = true;
    session.ProcessControlPacket(&packet);

    EXPECT_TRUE(NULL != tc.savedPacket.get());
    EXPECT_EQ(0, tc.savedPacket->poll);
    EXPECT_EQ(1, tc.savedPacket->final);

    tc.savedPacket.reset();
    packet.poll = false;
    session.ProcessControlPacket(&packet);
    EXPECT_EQ(NULL, tc.savedPacket.get());
}

TEST_F(SessionTest, PollSendTest) {
    TestConnection tc;
    SessionMock session(localDiscriminator, addr, &evm, config, &tc);

    session.ProcessControlPacket(&packet);
    session.InitPollSequence();

    EXPECT_TRUE(NULL != tc.savedPacket.get());
    EXPECT_EQ(1, tc.savedPacket->poll);
    EXPECT_EQ(0, tc.savedPacket->final);
    tc.savedPacket.reset();

    EventManagerThread t(&evm);
    TASK_UTIL_EXPECT_TRUE(NULL != tc.savedPacket.get());
    EXPECT_EQ(1, tc.savedPacket->poll);
    EXPECT_EQ(0, tc.savedPacket->final);
    tc.savedPacket.reset();

    session.ProcessControlPacket(&packet);

    evm.Shutdown();
}

TEST_F(SessionTest, RecvTimerExpiredTest) {
    TestConnection tc;
    SessionMock session(localDiscriminator, addr, &evm, config, &tc);

    EXPECT_EQ(kDown, session.local_state());
    packet.state = kInit;
    session.ProcessControlPacket(&packet);
    EXPECT_EQ(kUp, session.local_state());

    session.TriggerRecvTimerExpired();
    EXPECT_EQ(kDown, session.local_state());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
