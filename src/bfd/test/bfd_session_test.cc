/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_session.h"
#include "bfd/test/bfd_test_utils.h"

#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <testing/gunit.h>
#include <base/test/task_test_util.h>

using namespace BFD;

static const Discriminator localDiscriminator = 0x12345678;
static const Discriminator remoteDiscriminator = 0x87654321;
static const int detectionTimeMultiplier = 3;
const boost::asio::ip::address addr = boost::asio::ip::address::from_string("1.1.1.1");

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
        virtual void SendPacket(const boost::asio::ip::address &dstAddr, const ControlPacket *packet)  {
            ASSERT_EQ(localDiscriminator, packet->sender_discriminator);
            ASSERT_EQ(remoteDiscriminator, packet->receiver_discriminator);
            ASSERT_EQ(detectionTimeMultiplier, packet->detection_time_multiplier);
            savedPacket = *packet;
        }
        boost::optional<ControlPacket> savedPacket;
        virtual ~TestConnection() {}
    };
    SessionConfig config;
    ControlPacket packet;
    EventManager evm;
};

TEST_F(SessionTest, UpTest) {
    TestConnection tc;
    Session session(localDiscriminator, addr, &evm, config, &tc);

    EXPECT_EQ(kInit, session.local_state());
    packet.state = kInit;
    session.ProcessControlPacket(&packet);
    EXPECT_EQ(kUp, session.local_state());
}

TEST_F(SessionTest, PollRecvTest) {
    TestConnection tc;
    Session session(localDiscriminator, addr, &evm, config, &tc);

    packet.poll = true;
    session.ProcessControlPacket(&packet);

    EXPECT_TRUE(tc.savedPacket.is_initialized());
    EXPECT_EQ(0, tc.savedPacket.get().poll);
    EXPECT_EQ(1, tc.savedPacket.get().final);

    tc.savedPacket.reset();
    packet.poll = false;
    session.ProcessControlPacket(&packet);
    EXPECT_FALSE(tc.savedPacket.is_initialized());
}

TEST_F(SessionTest, PollSendTest) {
    TestConnection tc;
    Session session(localDiscriminator, addr, &evm, config, &tc);

    session.ProcessControlPacket(&packet);
    session.InitPollSequence();

    EXPECT_TRUE(tc.savedPacket.is_initialized());
    EXPECT_EQ(1, tc.savedPacket.get().poll);
    EXPECT_EQ(0, tc.savedPacket.get().final);
    tc.savedPacket.reset();

    EventManagerThread t(&evm);
    TASK_UTIL_EXPECT_TRUE(tc.savedPacket.is_initialized());
    EXPECT_EQ(1, tc.savedPacket.get().poll);
    EXPECT_EQ(0, tc.savedPacket.get().final);
    tc.savedPacket.reset();

    session.ProcessControlPacket(&packet);

    evm.Shutdown();
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

