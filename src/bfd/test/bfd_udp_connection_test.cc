/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */


#include "bfd/bfd_udp_connection.h"
#include "bfd/bfd_control_packet.h"
#include "bfd/test/bfd_test_utils.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <testing/gunit.h>
#include "test/task_test_util.h"
#include "base/logging.h"


using namespace BFD;

class BFDTest : public ::testing::Test {
 public:
    void ComparePacket(const ControlPacket *p1, const ControlPacket *p2) {
        LOG(INFO, p1->toString());
        LOG(INFO, p2->toString());
        cmpResult = (*p1 == *p2);
    }
    boost::optional<bool> cmpResult;
};


TEST_F(BFDTest, UDPConnection) {
    const int port1 = 10001;
    const int port2 = 10002;

    EventManager em;
    UDPConnectionManager communicationManager1(&em, port1, port2);
    UDPConnectionManager communicationManager2(&em, port2, port1);

    const boost::asio::ip::address addr = boost::asio::ip::address::from_string("127.0.0.1");

    ControlPacket packet;
    packet.poll = true;
    packet.final = false;
    packet.control_plane_independent = false;
    packet.authentication_present = false;
    packet.demand = false;
    packet.multipoint = false;

    packet.detection_time_multiplier = 5;
    packet.length = kMinimalPacketLength;
    packet.sender_discriminator = 100;
    packet.receiver_discriminator = 18;
    packet.diagnostic = kEchoFunctionFailed;
    packet.state = kDown;
    packet.desired_min_tx_interval = boost::posix_time::milliseconds(100);
    packet.required_min_rx_interval = boost::posix_time::milliseconds(200);
    packet.required_min_echo_rx_interval = boost::posix_time::milliseconds(0);
    packet.sender_host = addr;

    communicationManager2.RegisterCallback(boost::bind(&BFDTest::ComparePacket, this, &packet, _1));
    EventManagerThread evmThread(&em);
    communicationManager1.SendPacket(addr, &packet);

    TASK_UTIL_EXPECT_EQ(true, cmpResult.is_initialized());
    EXPECT_EQ(true, cmpResult.get());
}


int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
