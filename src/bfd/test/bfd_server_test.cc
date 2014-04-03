/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/test/bfd_test_utils.h"

#include <boost/asio.hpp>
#include <testing/gunit.h>
#include "base/test/task_test_util.h"

using namespace BFD;

class ServerTest : public ::testing::Test {
 protected:
};

static void processPacketAndVerifyResult(boost::function<ResultCode(const ControlPacket *)> processPacket,
        const ControlPacket *packet) {
    ResultCode result = processPacket(packet);
    LOG(INFO, "Result code:" << result);
}

TEST_F(ServerTest, Test2) {
    EventManager em;
    TestCommunicatorManager communicationManager(em.io_service());

    const boost::asio::ip::address addr1 = boost::asio::ip::address::from_string("1.1.1.1");
    const boost::asio::ip::address addr2 = boost::asio::ip::address::from_string("2.2.2.2");

    boost::scoped_ptr<Connection> communicator1(new TestCommunicator(&communicationManager, addr1));
    BFDServer server1(&em, communicator1.get());
    BFDSessionConfig config1;
    config1.desiredMinTxInterval = boost::posix_time::milliseconds(300);
    config1.requiredMinRxInterval = boost::posix_time::milliseconds(500);
    config1.detectionTimeMultiplier = 5;
    Discriminator disc1;
    server1.createSession(addr2, &config1, &disc1);

    boost::function<ResultCode(const ControlPacket *)> c1 = boost::bind(&BFDServer::processControlPacket, &server1, _1);
    communicationManager.registerServer(addr1, boost::bind(&processPacketAndVerifyResult, c1, _1));

    boost::scoped_ptr<Connection> communicator2(new TestCommunicator(&communicationManager, addr2));
    BFDServer server2(&em, communicator2.get());
    BFDSessionConfig config2;
    config2.desiredMinTxInterval = boost::posix_time::milliseconds(300);
    config2.requiredMinRxInterval = boost::posix_time::milliseconds(800);
    config2.detectionTimeMultiplier = 2;
    Discriminator disc2;
    server2.createSession(addr1, &config2, &disc2);
    boost::function<ResultCode(const ControlPacket *)> c2 = boost::bind(&BFDServer::processControlPacket, &server2, _1);
    communicationManager.registerServer(addr2, boost::bind(&processPacketAndVerifyResult, c2, _1));

    BFDSession *s1 = server1.sessionByAddress(addr2);
    BFDSession *s2 = server2.sessionByAddress(addr1);

    ASSERT_NE(s1, static_cast<BFDSession *>(NULL));
    ASSERT_NE(s2, static_cast<BFDSession *>(NULL));

    EventManagerThread t(&em);

    TASK_UTIL_EXPECT_EQ(kUp, s1->LocalState());
    TASK_UTIL_EXPECT_EQ(kUp, s2->LocalState());

    ASSERT_EQ(s1->DetectionTime(), boost::posix_time::milliseconds(1000));
    ASSERT_EQ(s2->DetectionTime(), boost::posix_time::milliseconds(4000));
    ASSERT_LT(s1->TxInterval(), boost::posix_time::milliseconds(800));
    ASSERT_GE(s1->TxInterval(), boost::posix_time::milliseconds(800*3/4));
    ASSERT_LT(s2->TxInterval(), boost::posix_time::milliseconds(500));
    ASSERT_GE(s2->TxInterval(), boost::posix_time::milliseconds(500*3/4));

    // boost 1.48 has some problems with chrono
    boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();

    communicationManager.unregisterServer(addr1);
    communicationManager.unregisterServer(addr2);

    TASK_UTIL_EXPECT_EQ(kDown, s1->LocalState());
    boost::posix_time::ptime down1 = boost::posix_time::microsec_clock::local_time();
    TASK_UTIL_EXPECT_EQ(kDown, s2->LocalState());
    boost::posix_time::ptime down2 = boost::posix_time::microsec_clock::local_time();

    ASSERT_LT(abs((s1->DetectionTime() - (down1 - start)).total_milliseconds()), 100);
    ASSERT_LT(abs((s2->DetectionTime() - (down2 - start)).total_milliseconds()), 100);

    communicationManager.registerServer(addr1, boost::bind(&processPacketAndVerifyResult, c1, _1));
    communicationManager.registerServer(addr2, boost::bind(&processPacketAndVerifyResult, c2, _1));

    start = boost::posix_time::microsec_clock::local_time();
    TASK_UTIL_EXPECT_EQ(kUp, s1->LocalState());
    boost::posix_time::ptime up1 = boost::posix_time::microsec_clock::local_time();
    TASK_UTIL_EXPECT_EQ(kUp, s2->LocalState());
    boost::posix_time::ptime up2 = boost::posix_time::microsec_clock::local_time();

    ASSERT_LT(up1-start, boost::posix_time::milliseconds(2000));
    ASSERT_LT(up2-start, boost::posix_time::milliseconds(2000));

    communicationManager.unregisterServer(addr2);

    TASK_UTIL_EXPECT_EQ(kDown, s1->LocalState());
    TASK_UTIL_EXPECT_EQ(kDown, s2->LocalState());

    communicationManager.registerServer(addr2, boost::bind(&processPacketAndVerifyResult, c2, _1));

    TASK_UTIL_EXPECT_EQ(kUp, s1->LocalState());
    TASK_UTIL_EXPECT_EQ(kUp, s2->LocalState());

    em.Shutdown();
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
