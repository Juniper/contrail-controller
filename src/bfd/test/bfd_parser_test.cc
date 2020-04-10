/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#include "bfd/bfd_control_packet.h"

#include <boost/scoped_ptr.hpp>

#include "base/logging.h"
#include "base/regex.h"

typedef contrail::regex regex_t;
#include <testing/gunit.h>

using namespace BFD;

class BFDTest : public ::testing::Test {
  protected:
};

TEST_F(BFDTest, Test1) {
    uint8_t buf[] = { 0x20, 0xA0, 0x02, 0x18,
                      0x11, 0x22, 0x33, 0x44,
                      0x55, 0x66, 0x77, 0x88,
                      0x00, 0x0F, 0x42, 0x40,
                      0x00, 0x1E, 0x84, 0x80,
                      0x00, 0x00, 0x75, 0x30,
    };

    boost::scoped_ptr<ControlPacket> packet(ParseControlPacket(buf, sizeof(buf)));
    EXPECT_NE(static_cast<ControlPacket*>(NULL), packet.get());

    EXPECT_EQ(kNoDiagnostic, packet->diagnostic);
    EXPECT_EQ(kInit, packet->state);
    EXPECT_EQ(1, packet->poll);
    EXPECT_EQ(0, packet->final);
    EXPECT_EQ(0, packet->control_plane_independent);
    EXPECT_EQ(0, packet->authentication_present);
    EXPECT_EQ(0, packet->demand);
    EXPECT_EQ(0, packet->multipoint);
    EXPECT_EQ(2, packet->detection_time_multiplier);
    EXPECT_EQ(0x11223344U, packet->sender_discriminator);
    EXPECT_EQ(0x55667788U, packet->receiver_discriminator);
    EXPECT_EQ(boost::posix_time::seconds(1), packet->desired_min_tx_interval);
    EXPECT_EQ(boost::posix_time::seconds(2), packet->required_min_rx_interval);
    EXPECT_EQ(boost::posix_time::milliseconds(30), packet->required_min_echo_rx_interval);

    uint8_t buf2[kMinimalPacketLength];
    EXPECT_EQ(kMinimalPacketLength, EncodeControlPacket(packet.get(), buf2, sizeof(buf2)));
    EXPECT_EQ(0, bcmp(buf, buf2, kMinimalPacketLength));
}

TEST_F(BFDTest, Test2) {
    uint8_t buf[] = { 0x25, 0x52, 0xFF, 0x18,
                      0xFF, 0x22, 0x33, 0x44,
                      0xFF, 0x66, 0x77, 0x88,
                      0x00, 0x0F, 0x42, 0x40,
                      0x00, 0x1E, 0x84, 0x80,
                      0x00, 0x00, 0x00, 0x00,
    };

    boost::scoped_ptr<ControlPacket> packet(ParseControlPacket(buf, sizeof(buf)));
    EXPECT_NE(static_cast<ControlPacket*>(NULL), packet.get());

    EXPECT_EQ(kPathDown, packet->diagnostic);
    EXPECT_EQ(kDown, packet->state);
    EXPECT_EQ(0, packet->poll);
    EXPECT_EQ(1, packet->final);
    EXPECT_EQ(0, packet->control_plane_independent);
    EXPECT_EQ(0, packet->authentication_present);
    EXPECT_EQ(1, packet->demand);
    EXPECT_EQ(0, packet->multipoint);
    EXPECT_EQ(255, packet->detection_time_multiplier);
    EXPECT_EQ(0xFF223344, packet->sender_discriminator);
    EXPECT_EQ(0xFF667788, packet->receiver_discriminator);
    EXPECT_EQ(boost::posix_time::seconds(1), packet->desired_min_tx_interval);
    EXPECT_EQ(boost::posix_time::seconds(2), packet->required_min_rx_interval);
    EXPECT_EQ(boost::posix_time::seconds(0), packet->required_min_echo_rx_interval);
    EXPECT_EQ(kMinimalPacketLength, packet->length);

    uint8_t buf2[kMinimalPacketLength];
    EXPECT_EQ(kMinimalPacketLength, EncodeControlPacket(packet.get(), buf2, sizeof(buf2)));
    EXPECT_EQ(0, bcmp(buf, buf2, kMinimalPacketLength));
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
