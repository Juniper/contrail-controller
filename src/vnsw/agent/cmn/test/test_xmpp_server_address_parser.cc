/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <string>
#include <vector>
#include <testing/gunit.h>
#include <cmn/xmpp_server_address_parser.h>

TEST(XmppServerAddressParserTest, ParseAddressWithPort) {
    std::string out_ip;
    uint16_t out_port;
    XmppServerAddressParser parser(1234, 3);

    parser.ParseAddress("1.2.3.4:4321", &out_ip, &out_port);

    EXPECT_EQ("1.2.3.4", out_ip);
    EXPECT_EQ(4321, out_port);
}

TEST(XmppServerAddressParserTest, ParseAddressWithoutPort) {
    std::string out_ip;
    uint16_t out_port;
    XmppServerAddressParser parser(1234, 3);

    parser.ParseAddress("1.2.3.4", &out_ip, &out_port);

    EXPECT_EQ("1.2.3.4", out_ip);
    EXPECT_EQ(1234, out_port);
}

TEST(XmppServerAddressParserTest, ParseAddressWithEmptyPort) {
    std::string out_ip;
    uint16_t out_port;
    XmppServerAddressParser parser(1234, 3);

    parser.ParseAddress("1.2.3.4:", &out_ip, &out_port);

    EXPECT_EQ("1.2.3.4", out_ip);
    EXPECT_EQ(1234, out_port);
}

TEST(XmppServerAddressParserTest, ParseAddressWithInvalidPort) {
    std::string out_ip;
    uint16_t out_port;
    XmppServerAddressParser parser(1234, 3);

    parser.ParseAddress("1.2.3.4:abcd", &out_ip, &out_port);

    EXPECT_EQ("1.2.3.4", out_ip);
    EXPECT_EQ(1234, out_port);
}

TEST(XmppServerAddressParserTest, ParseAddressesMoreThanMaxServers) {
    std::string out_ips[] = {
        "",
        "",
        "unused",
        ""
    };
    uint16_t out_ports[4] = {1, 2, 3};

    std::vector<std::string> addresses;
    addresses.push_back("1.2.3.4:1234");
    addresses.push_back("5.6.7.8:5678");
    addresses.push_back("9.8.7.6:9876");
    addresses.push_back("4.3.2.1:4321");

    XmppServerAddressParser parser(1111, 2);
    parser.ParseAddresses(addresses, out_ips, out_ports);

    EXPECT_EQ("1.2.3.4", out_ips[0]);
    EXPECT_EQ("5.6.7.8", out_ips[1]);
    EXPECT_EQ("unused", out_ips[2]);
    EXPECT_EQ("", out_ips[3]);

    EXPECT_EQ(1234, out_ports[0]);
    EXPECT_EQ(5678, out_ports[1]);
    EXPECT_EQ(3, out_ports[2]);
    EXPECT_EQ(0, out_ports[3]);
}

TEST(XmppServerAddressParserTest, ParseAddressesMisc) {
    std::string out_ips[4];
    uint16_t out_ports[4];

    std::vector<std::string> addresses;
    addresses.push_back("1.2.3.4:1234");
    addresses.push_back("5.6.7.8");
    addresses.push_back("9.8.7.6:");
    addresses.push_back("4.3.2.1:asdf");

    XmppServerAddressParser parser(1111, 4);
    parser.ParseAddresses(addresses, out_ips, out_ports);

    EXPECT_EQ("1.2.3.4", out_ips[0]);
    EXPECT_EQ("5.6.7.8", out_ips[1]);
    EXPECT_EQ("9.8.7.6", out_ips[2]);
    EXPECT_EQ("4.3.2.1", out_ips[3]);

    EXPECT_EQ(1234, out_ports[0]);
    EXPECT_EQ(1111, out_ports[1]);
    EXPECT_EQ(1111, out_ports[2]);
    EXPECT_EQ(1111, out_ports[3]);
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
