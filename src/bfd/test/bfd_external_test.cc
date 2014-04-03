/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/bfd_udp_connection.h"
#include "bfd/test/bfd_test_utils.h"

#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <testing/gunit.h>
#include "base/test/task_test_util.h"


using namespace BFD;

#if 1
// Test with external system

class BFDExternalTest : public ::testing::Test {
 protected:
};

TEST_F(BFDExternalTest, Test1) {
    EventManager evm;
    UDPConnectionManager cm(&evm);

    const boost::asio::ip::address addr = boost::asio::ip::address::from_string("10.5.3.165");

        BFDServer server(&evm, &cm);
        BFDSessionConfig config1;
        config1.desiredMinTxInterval = boost::posix_time::milliseconds(1000);
        config1.requiredMinRxInterval = boost::posix_time::milliseconds(1000);
        config1.detectionTimeMultiplier = 3;
        Discriminator disc1;
        server.createSession(addr, &config1, &disc1);

        cm.RegisterCallback(boost::bind(&BFDServer::processControlPacket, &server, _1));


        EventManagerThread emt(&evm);
        for (;;) {
            boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        }
}

#endif

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
