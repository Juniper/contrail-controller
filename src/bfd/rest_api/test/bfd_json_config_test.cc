/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/rest_api/bfd_json_config.h"

#include <testing/gunit.h>
#include <base/logging.h>

using namespace BFD;

class BFDJsonConfigTest : public ::testing::Test {
 protected:
    REST::JsonConfig config;
};

TEST_F(BFDJsonConfigTest, Test1) {
    EXPECT_TRUE(config.ParseFromJsonString(
        "{ \"Address\":\"10.5.3.168\", "
        "\"DesiredMinTxInterval\" : 50000, "
        "\"RequiredMinRxInterval\" : 500000, "
        "\"DetectionMultiplier\" : 3 \n}"));
    EXPECT_EQ(boost::asio::ip::address::from_string("10.5.3.168"),
              config.address);
    EXPECT_EQ(boost::posix_time::milliseconds(50),
              config.desired_min_tx_interval);
    EXPECT_EQ(boost::posix_time::milliseconds(500),
              config.required_min_rx_interval);
    EXPECT_EQ(3, config.detection_time_multiplier);

    REST::JsonConfig config2;
    std::string json;
    config.EncodeJsonString(&json);
    EXPECT_TRUE(config2.ParseFromJsonString(json));
    EXPECT_EQ(boost::asio::ip::address::from_string("10.5.3.168"),
              config2.address);
    EXPECT_EQ(boost::posix_time::milliseconds(50),
              config2.desired_min_tx_interval);
    EXPECT_EQ(boost::posix_time::milliseconds(500),
              config2.required_min_rx_interval);
    EXPECT_EQ(3, config2.detection_time_multiplier);
}

TEST_F(BFDJsonConfigTest, Test2) {
    EXPECT_FALSE(config.ParseFromJsonString(
        " \"Address\":\"10.5.3.168\", "
        "\"DesiredMinTxInterval\" : 50000 , "
        "\"RequiredMinRxInterval\" : 500000, "
        "\"DetectionMultiplier\" : 3 "));
}

TEST_F(BFDJsonConfigTest, Test3) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"AddressX\":\"10.5.3.168\", "
        "\"DesiredMinTxInterval\" : 50000 , \n"
        "\"RequiredMinRxInterval\" : 500000, "
        "\"DetectionMultiplier\" : 3 \n}"));
}

TEST_F(BFDJsonConfigTest, Test4) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"Address\":\"10.5.3.168\", "
        "\"DesiredMinTxIntervalX\" : 50000 , \n "
        "\"RequiredMinRxInterval\" : 500000, "
        "\"DetectionMultiplier\" : 3 \n}"));
}

TEST_F(BFDJsonConfigTest, Test5) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"AddressX\":\"10.5.3.168\", "
        "\"DesiredMinTxInterval\" : 50000 , \n"
        "\"RequiredMinRxIntervalX\" : 500000, "
        "\"DetectionMultiplier\" : 3 \n}"));
}

TEST_F(BFDJsonConfigTest, Test6) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"AddressX\":\"10.5.3.168\", "
        "\"DesiredMinTxInterval\" : 50000 , \n "
        "\"RequiredMinRxInterval\" : 500000, "
        "\"DetectionMultiplierX\" : 3 \n}"));
}

TEST_F(BFDJsonConfigTest, Test7) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"Address\":\"10.5.300.168\", "
        "\"DesiredMinTxInterval\" : 50000 , \n "
        "\"RequiredMinRxInterval\" : 500000, "
        "\"DetectionMultiplier\" : 3 \n}"));
}

TEST_F(BFDJsonConfigTest, Test8) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"Address\": 0, "
        "\"DesiredMinTxInterval\" : 50000 , \n "
        "\"RequiredMinRxInterval\" : 500000, "
        "\"DetectionMultiplier\" : 3 \n}"));
}

TEST_F(BFDJsonConfigTest, Test9) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"Address\":\"10.5.3.168\", "
        "\"DesiredMinTxInterval\" : \"50000\" , \n"
        "\"RequiredMinRxInterval\" : 500000,"
        "\"DetectionMultiplier\" : 3 \n}"));
}

TEST_F(BFDJsonConfigTest, Test10) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"Address\":\"10.5.3.168\", "
        "\"DesiredMinTxInterval\" : 50000 , \n "
        "\"RequiredMinRxInterval\" : \"500000\", "
        "\"DetectionMultiplier\" : 3 \n}"));
}

TEST_F(BFDJsonConfigTest, Test11) {
    EXPECT_FALSE(config.ParseFromJsonString(
        "{ \"Address\":\"10.5.3.168\", "
        "\"DesiredMinTxInterval\" : 50000 , \n "
        "\"RequiredMinRxInterval\" : 500000, "
        "\"DetectionMultiplier\" : {} \n}"));
}

TEST_F(BFDJsonConfigTest, Test12) {
    EXPECT_FALSE(config.ParseFromJsonString("{}"));
}

TEST_F(BFDJsonConfigTest, Test13) {
    EXPECT_FALSE(config.ParseFromJsonString(""));
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
