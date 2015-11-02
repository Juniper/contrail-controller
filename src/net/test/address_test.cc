/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "net/address.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class AddressTest : public ::testing::Test {
};

TEST_F(AddressTest, V4PrefixParseTest) {
    Ip4Address address;
    int plen;
    boost::system::error_code ec;

    string prefix_str = "10.1.1.1/24";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.1"));
    EXPECT_EQ(plen, 24);

    prefix_str = "10.1.1.1/20";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.1"));
    EXPECT_EQ(plen, 20);

    prefix_str = "10.1.1.1/23";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.1"));
    EXPECT_EQ(plen, 23);

    prefix_str = "10.1.1.1/25";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.1"));
    EXPECT_EQ(plen, 25);

    prefix_str = "10.1.1.1/31";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.1"));
    EXPECT_EQ(plen, 31);

    prefix_str = "10.1.1.1/32";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.1"));
    EXPECT_EQ(plen, 32);

    prefix_str = "10.1.1.1/8";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.1"));
    EXPECT_EQ(plen, 8);

    prefix_str = "10.1.1.0/24";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.0"));
    EXPECT_EQ(plen, 24);

    prefix_str = "10.1.0.0/16";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.0.0"));
    EXPECT_EQ(plen, 16);

    prefix_str = "10.0.0.0/8";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.0.0.0"));
    EXPECT_EQ(plen, 8);

    // 252 is '1111 1100'. 240 is '1111 0000'.
    prefix_str = "10.1.252.1/20";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.252.1"));
    EXPECT_EQ(plen, 20);

    // 252 is '1111 1100'. 224 is '1110 0000'.
    prefix_str = "10.1.252.1/19";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.252.1"));
    EXPECT_EQ(plen, 19);

    // Tests with insufficient dots.
    prefix_str = "11/8";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.0.0.0"));
    EXPECT_EQ(plen, 8);

    prefix_str = "11.1/8";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.0.0"));
    EXPECT_EQ(plen, 8);

    prefix_str = "11.1/16";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.0.0"));
    EXPECT_EQ(plen, 16);

    prefix_str = "11.1/18";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.0.0"));
    EXPECT_EQ(plen, 18);

    prefix_str = "11.1.2/10";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.2.0"));
    EXPECT_EQ(plen, 10);

    prefix_str = "11.1.2/19";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.2.0"));
    EXPECT_EQ(plen, 19);

    prefix_str = "11.1.2/24";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.2.0"));
    EXPECT_EQ(plen, 24);
}

TEST_F(AddressTest, V4PrefixParseNegativeTest) {
    Ip4Address address;
    int plen;
    boost::system::error_code ec;

    // Invalid prefix length.
    string prefix_str = "10.1.1.1/33";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    // IP address too long.
    prefix_str = "10.1.1.1.0/16";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    // Incomplete IP address portion.
    prefix_str = "11.1.3./10";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    prefix_str = "11.1.2.3";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    prefix_str = "12.13.14.x";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    prefix_str = "12.13.14.x/16";
    ec = Ip4PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);
}


TEST_F(AddressTest, V4SubnetParseTest) {
    Ip4Address address;
    int plen;
    boost::system::error_code ec;

    string prefix_str = "10.1.1.1/24";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.0"));
    EXPECT_EQ(plen, 24);

    prefix_str = "10.1.1.1/20";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.0.0"));
    EXPECT_EQ(plen, 20);

    prefix_str = "10.1.1.1/23";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.0.0"));
    EXPECT_EQ(plen, 23);

    prefix_str = "10.1.1.1/25";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.0"));
    EXPECT_EQ(plen, 25);

    prefix_str = "10.1.1.1/31";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.0"));
    EXPECT_EQ(plen, 31);

    prefix_str = "10.1.1.1/32";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.1"));
    EXPECT_EQ(plen, 32);

    prefix_str = "10.1.1.1/8";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.0.0.0"));
    EXPECT_EQ(plen, 8);

    prefix_str = "10.1.1.0/24";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.1.0"));
    EXPECT_EQ(plen, 24);

    prefix_str = "10.1.0.0/16";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.0.0"));
    EXPECT_EQ(plen, 16);

    prefix_str = "10.0.0.0/8";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.0.0.0"));
    EXPECT_EQ(plen, 8);

    // 252 is '1111 1100'. 240 is '1111 0000'.
    prefix_str = "10.1.252.1/20";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.240.0"));
    EXPECT_EQ(plen, 20);

    // 252 is '1111 1100'. 224 is '1110 0000'.
    prefix_str = "10.1.252.1/19";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("10.1.224.0"));
    EXPECT_EQ(plen, 19);

    // Tests with insufficient dots.
    prefix_str = "11/8";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.0.0.0"));
    EXPECT_EQ(plen, 8);

    prefix_str = "11.1/8";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.0.0.0"));
    EXPECT_EQ(plen, 8);

    prefix_str = "11.1/16";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.0.0"));
    EXPECT_EQ(plen, 16);

    prefix_str = "11.1/18";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.0.0"));
    EXPECT_EQ(plen, 18);

    prefix_str = "11.1.2/10";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.0.0.0"));
    EXPECT_EQ(plen, 10);

    prefix_str = "11.1.2/19";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.0.0"));
    EXPECT_EQ(plen, 19);

    prefix_str = "11.1.2/24";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("11.1.2.0"));
    EXPECT_EQ(plen, 24);
}

TEST_F(AddressTest, V4SubnetParseNegativeTest) {
    Ip4Address address;
    int plen;
    boost::system::error_code ec;

    // Invalid prefix length.
    string prefix_str = "10.1.1.1/33";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    // IP address too long.
    prefix_str = "10.1.1.1.0/16";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    // Incomplete IP address portion.
    prefix_str = "11.1.3./10";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    prefix_str = "11.1.2.3";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    prefix_str = "12.13.14.x";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    prefix_str = "12.13.14.x/16";
    ec = Ip4SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);
}

TEST_F(AddressTest, V6PrefixParseTest) {
    Ip6Address address;
    int plen;
    boost::system::error_code ec;

    string prefix_str = "2001:db8:85a3:aaaa::b:c:d/128";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa:0:b:c:d"));
    EXPECT_EQ(plen, 128);

    prefix_str = "2001:db8:85a3:aaaa::b:c:d/64";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa:0:b:c:d"));
    EXPECT_EQ(plen, 64);

    // Last 32 bits should be ignored.
    prefix_str = "2001:db8:85a3:aaaa::b:c:d/96";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa:0:b:c:d"));
    EXPECT_EQ(plen, 96);

    // Last 16 bits should be ignored.
    prefix_str = "2001:db8:85a3:aaaa::b:c:d/112";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa:0:b:c:d"));
    EXPECT_EQ(plen, 112);

    // Bits 97 - 112 is 'dddd'. /100 covers 4 bits from the first 'd'.
    prefix_str = "2001:db8:85a3:aaaa:bbbb:cccc:dddd:eeee/100";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare(
        "2001:db8:85a3:aaaa:bbbb:cccc:dddd:eeee"));
    EXPECT_EQ(plen, 100);

    // Bits 97 - 112 is 'dddd'. /102 covers the 4 bits from the first 'd' and
    // 2 bits from the second 'd'.
    prefix_str = "2001:db8:85a3:aaaa:bbbb:cccc:dddd:eeee/102";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare(
        "2001:db8:85a3:aaaa:bbbb:cccc:dddd:eeee"));
    EXPECT_EQ(plen, 102);

    // Bits 97 - 112 is 'dddd'. /106 covers 8 bits from the first two 'd''s and
    // 2 bits from the third 'd'.
    prefix_str = "2001:db8:85a3::dddd:eeee/106";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3::dddd:eeee"));
    EXPECT_EQ(plen, 106);

    prefix_str = "2001:db8:85a3:aaaa::/64";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa::"));
    EXPECT_EQ(plen, 64);
}

TEST_F(AddressTest, V6PrefixParseNegativeTest) {
    Ip6Address address;
    int plen;
    boost::system::error_code ec;

    // Prefix length greater than 128.
    string prefix_str = "2001:db8:85a3:aaaa::b:c:d/144";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    // No prefix length.
    prefix_str = "2001:db8:85a3:aaaa::b:c:d";
    ec = Inet6PrefixParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);
}

TEST_F(AddressTest, V6SubnetParseTest) {
    Ip6Address address;
    int plen;
    boost::system::error_code ec;

    string prefix_str = "2001:db8:85a3:aaaa::b:c:d/128";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa:0:b:c:d"));
    EXPECT_EQ(plen, 128);

    prefix_str = "2001:db8:85a3:aaaa::b:c:d/64";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa::"));
    EXPECT_EQ(plen, 64);

    // Last 32 bits should be ignored.
    prefix_str = "2001:db8:85a3:aaaa::b:c:d/96";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa:0:b::"));
    EXPECT_EQ(plen, 96);

    // Last 16 bits should be ignored.
    prefix_str = "2001:db8:85a3:aaaa::b:c:d/112";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa:0:b:c:0"));
    EXPECT_EQ(plen, 112);

    // Bits 97 - 112 is 'dddd'. /100 covers 4 bits from the first 'd'.
    prefix_str = "2001:db8:85a3:aaaa:bbbb:cccc:dddd:eeee/100";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare(
        "2001:db8:85a3:aaaa:bbbb:cccc:d000:0"));
    EXPECT_EQ(plen, 100);

    // Bits 97 - 112 is 'dddd'. /102 covers the 4 bits from the first 'd' and
    // 2 bits from the second 'd'.
    prefix_str = "2001:db8:85a3:aaaa:bbbb:cccc:dddd:eeee/102";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare(
        "2001:db8:85a3:aaaa:bbbb:cccc:dc00:0"));
    EXPECT_EQ(plen, 102);

    // Bits 97 - 112 is 'dddd'. /106 covers 8 bits from the first two 'd''s and
    // 2 bits from the third 'd'.
    prefix_str = "2001:db8:85a3::dddd:eeee/106";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3::ddc0:0"));
    EXPECT_EQ(plen, 106);

    prefix_str = "2001:db8:85a3:aaaa::/64";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() == 0);
    EXPECT_EQ(0, address.to_string().compare("2001:db8:85a3:aaaa::"));
    EXPECT_EQ(plen, 64);
}

TEST_F(AddressTest, V6SubnetParseNegativeTest) {
    Ip6Address address;
    int plen;
    boost::system::error_code ec;

    // Prefix length greater than 128.
    string prefix_str = "2001:db8:85a3:aaaa::b:c:d/144";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);

    // No prefix length.
    prefix_str = "2001:db8:85a3:aaaa::b:c:d";
    ec = Inet6SubnetParse(prefix_str, &address, &plen);
    EXPECT_TRUE(ec.value() != 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
