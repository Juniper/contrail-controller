/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/mvpn/mvpn_table.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using std::string;

class MvpnPrefixTest : public ::testing::Test {
};

// Invalid prefix type.
TEST_F(MvpnPrefixTest, PrefixTypeError) {
    boost::system::error_code ec;
    string prefix_str("9-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

TEST_F(MvpnPrefixTest, Type1BuildPrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address originator(Ip4Address::from_string("9.8.7.6", ec));
    MvpnPrefix prefix(MvpnPrefix::IntraASPMSIADRoute, rd, originator);
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ("1-10.1.1.1:65535,9.8.7.6", prefix.ToString());
    EXPECT_EQ(MvpnPrefix::IntraASPMSIADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("9.8.7.6", prefix.originator().to_string());
}

TEST_F(MvpnPrefixTest, Type1ParsePrefix) {
    string prefix_str("1-10.1.1.1:65535,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ(MvpnPrefix::IntraASPMSIADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("192.168.1.1", prefix.originator().to_string());
    EXPECT_EQ(prefix_str, prefix.ToString());
}

// No "-" to delineate the prefix type.
TEST_F(MvpnPrefixTest, Type1Error1) {
    boost::system::error_code ec;
    string prefix_str("1:10.1.1.1:65535,192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "," to delineate the rd.
TEST_F(MvpnPrefixTest, Type1Error2) {
    boost::system::error_code ec;
    string prefix_str("1-10.1.1.1:65535:192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad rd.
TEST_F(MvpnPrefixTest, Type1Error3) {
    boost::system::error_code ec;
    string prefix_str("1-10.1.1.1:65536,192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad originator address.
TEST_F(MvpnPrefixTest, Type1Error4) {
    boost::system::error_code ec;
    string prefix_str("1-10.1.1.1:65535,192.168.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

TEST_F(MvpnPrefixTest, Type2BuildPrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    uint32_t asn = 12345;
    MvpnPrefix prefix(MvpnPrefix::InterASPMSIADRoute, rd, asn);
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ("2-10.1.1.1:65535,12345", prefix.ToString());
    EXPECT_EQ(MvpnPrefix::InterASPMSIADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ(12345U, prefix.asn());
}

TEST_F(MvpnPrefixTest, Type2ParsePrefix) {
    string prefix_str("2-10.1.1.1:65535,23456");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ(MvpnPrefix::InterASPMSIADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ(23456U, prefix.asn());
    EXPECT_EQ(prefix_str, prefix.ToString());
}

// No "-" to delineate the prefix type.
TEST_F(MvpnPrefixTest, Type2Error1) {
    boost::system::error_code ec;
    string prefix_str("2:10.1.1.1:65535,23456");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "," to delineate the rd.
TEST_F(MvpnPrefixTest, Type2Error2) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65535:23456");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad rd.
TEST_F(MvpnPrefixTest, Type2Error3) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65536:23456");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad asn.
TEST_F(MvpnPrefixTest, Type2Error4) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65535:12334455623456");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

TEST_F(MvpnPrefixTest, Type3BuildPrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address originator(Ip4Address::from_string("9.8.7.6", ec));
    Ip4Address group(Ip4Address::from_string("224.1.2.3", ec));
    Ip4Address source(Ip4Address::from_string("192.168.1.1", ec));
    MvpnPrefix prefix(MvpnPrefix::SPMSIADRoute, rd, originator, group, source);
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ("3-10.1.1.1:65535,192.168.1.1,224.1.2.3,9.8.7.6", prefix.ToString());
    EXPECT_EQ(MvpnPrefix::SPMSIADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("9.8.7.6", prefix.originator().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
}

TEST_F(MvpnPrefixTest, Type3ParsePrefix) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ(MvpnPrefix::SPMSIADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("9.8.7.6", prefix.source().to_string());
    EXPECT_EQ(prefix_str, prefix.ToString());
}

// No "-" to delineate the prefix type.
TEST_F(MvpnPrefixTest, Type3Error1) {
    boost::system::error_code ec;
    string prefix_str("3:10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "-" to delineate the rd.
TEST_F(MvpnPrefixTest, Type3Error2) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65535:9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad rd.
TEST_F(MvpnPrefixTest, Type3Error3) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65536,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "," to delineate the group.
TEST_F(MvpnPrefixTest, Type3Error4) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3:192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad group address.
TEST_F(MvpnPrefixTest, Type3Error5) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2,192.168.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad originator address.
TEST_F(MvpnPrefixTest, Type3Error6) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

TEST_F(MvpnPrefixTest, Type4BuildPrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address originator(Ip4Address::from_string("9.8.7.6", ec));
    Ip4Address group(Ip4Address::from_string("224.1.2.3", ec));
    Ip4Address source(Ip4Address::from_string("192.168.1.1", ec));
    MvpnPrefix prefix(MvpnPrefix::SPMSIADRoute, rd, originator, group, source);
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ("3-10.1.1.1:65535,192.168.1.1,224.1.2.3,9.8.7.6", prefix.ToString());
    EXPECT_EQ(MvpnPrefix::SPMSIADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("9.8.7.6", prefix.originator().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
    Ip4Address type4_originator(Ip4Address::from_string("20.1.1.1"));
    MvpnPrefix type4_prefix(MvpnPrefix::LeafADRoute, type4_originator);
    type4_prefix.SetLeafADPrefixFromSPMSIPrefix(prefix);
    EXPECT_EQ("4-3-10.1.1.1:65535,192.168.1.1,224.1.2.3,9.8.7.6,20.1.1.1",
            type4_prefix.ToString());
}

TEST_F(MvpnPrefixTest, Type4ParsePrefix1) {
    string prefix_str("4-3-10.1.1.1:65535,192.168.1.1,224.1.2.3,9.8.7.6,20.1.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ(MvpnPrefix::LeafADRoute, prefix.type());
    EXPECT_EQ("20.1.1.1", prefix.originator().to_string());
    EXPECT_EQ(prefix_str, prefix.ToString());
}

TEST_F(MvpnPrefixTest, Type4ParsePrefix2) {
    string prefix_str("4-2-10.1.1.1:65535,23456,20.1.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ(MvpnPrefix::LeafADRoute, prefix.type());
    EXPECT_EQ("20.1.1.1", prefix.originator().to_string());
    EXPECT_EQ(prefix_str, prefix.ToString());
}

// No "-" to delineate the prefix type.
TEST_F(MvpnPrefixTest, Type4Error1) {
    boost::system::error_code ec;
    string prefix_str("4:10.1.1.1:65,192.168.1.1,224.1.2.3,9.8.7.6,20.1.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "-" to delineate the rd.
TEST_F(MvpnPrefixTest, Type4Error3) {
    boost::system::error_code ec;
    string prefix_str("4-3:10.1.1.1:65:192.168.1.1,224.1.2.3,9.8.7.6,20.1.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad rd.
TEST_F(MvpnPrefixTest, Type4Error4) {
    boost::system::error_code ec;
    string prefix_str("4-3-10.1.1.1:65536,192.168.1.1,24.1.2.3,9.8.7.6,20.1.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "," to delineate the group.
TEST_F(MvpnPrefixTest, Type4Error5) {
    boost::system::error_code ec;
    string prefix_str("4-3-10.1.1.1:65535,192.168.1.1:24.1.2.3,9.8.7.6,20.1.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad group address.
TEST_F(MvpnPrefixTest, Type4Error6) {
    boost::system::error_code ec;
    string prefix_str("4-3-10.1.1.1:65535,192.168.1.1,24.2.3,9.8.7.6,20.1.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad source address.
TEST_F(MvpnPrefixTest, Type4Error7) {
    boost::system::error_code ec;
    string prefix_str("4-3-10.1.1.1:65535,192.1.1,24.1.2.3,9.8.7,20.1.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad originator address.
TEST_F(MvpnPrefixTest, Type4Error8) {
    boost::system::error_code ec;
    string prefix_str("4-3-10.1.1.1:65535,192.1.1,24.1.2.3,9.8.7.6,20.1.1");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

TEST_F(MvpnPrefixTest, Type5BuildPrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address group(Ip4Address::from_string("224.1.2.3", ec));
    Ip4Address source(Ip4Address::from_string("192.168.1.1", ec));
    MvpnPrefix prefix(MvpnPrefix::SourceActiveADRoute, rd, group, source);
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ("5-10.1.1.1:65535,192.168.1.1,224.1.2.3", prefix.ToString());
    EXPECT_EQ(MvpnPrefix::SourceActiveADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
}

TEST_F(MvpnPrefixTest, Type5ParsePrefix) {
    string prefix_str("5-10.1.1.1:65535,9.8.7.6,224.1.2.3");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ(MvpnPrefix::SourceActiveADRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("9.8.7.6", prefix.source().to_string());
    EXPECT_EQ(prefix_str, prefix.ToString());
}

// No "-" to delineate the prefix type.
TEST_F(MvpnPrefixTest, Type5Error1) {
    boost::system::error_code ec;
    string prefix_str("5:10.1.1.1:65535,9.8.7.6,224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "-" to delineate the rd.
TEST_F(MvpnPrefixTest, Type5Error2) {
    boost::system::error_code ec;
    string prefix_str("5:10.1.1.1:65535:9.8.7.6,224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad rd.
TEST_F(MvpnPrefixTest, Type5Error3) {
    boost::system::error_code ec;
    string prefix_str("5:10.1.1.1:65536,9.8.7.6,224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "," to delineate the group.
TEST_F(MvpnPrefixTest, Type5Error4) {
    boost::system::error_code ec;
    string prefix_str("5:10.1.1.1:65535,9.8.7.6:224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad group address.
TEST_F(MvpnPrefixTest, Type5Error5) {
    boost::system::error_code ec;
    string prefix_str("5:10.1.1.1:65535,9.8.7.6,224.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad source address.
TEST_F(MvpnPrefixTest, Type5Error6) {
    boost::system::error_code ec;
    string prefix_str("5:10.1.1.1:65535,9.8.7,224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Type6 and Type7 are exactly same formats, so no need of separate tests
TEST_F(MvpnPrefixTest, Type7BuildPrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address group(Ip4Address::from_string("224.1.2.3", ec));
    Ip4Address source(Ip4Address::from_string("192.168.1.1", ec));
    MvpnPrefix prefix(MvpnPrefix::SourceTreeJoinRoute, rd, 1234, group, source);
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ("7-10.1.1.1:65535,1234,192.168.1.1,224.1.2.3", prefix.ToString());
    EXPECT_EQ(MvpnPrefix::SourceTreeJoinRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
    EXPECT_EQ(1234U, prefix.asn());
}

TEST_F(MvpnPrefixTest, Type7ParsePrefix) {
    string prefix_str("7-10.1.1.1:65535,12345,9.8.7.6,224.1.2.3");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    EXPECT_TRUE(prefix.IsValid(prefix.type()));
    EXPECT_EQ(MvpnPrefix::SourceTreeJoinRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("9.8.7.6", prefix.source().to_string());
    EXPECT_EQ(12345U, prefix.asn());
    EXPECT_EQ(prefix_str, prefix.ToString());
}

// No "-" to delineate the prefix type.
TEST_F(MvpnPrefixTest, Type7Error1) {
    boost::system::error_code ec;
    string prefix_str("7:10.1.1.1:65535,12345,9.8.7.6,224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "-" to delineate the rd.
TEST_F(MvpnPrefixTest, Type7Error2) {
    boost::system::error_code ec;
    string prefix_str("7:10.1.1.1:65535:12345,9.8.7.6,224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad rd.
TEST_F(MvpnPrefixTest, Type7Error3) {
    boost::system::error_code ec;
    string prefix_str("7:10.1.1.1:65536,12345,9.8.7.6,224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// No "," to delineate the group.
TEST_F(MvpnPrefixTest, Type7Error4) {
    boost::system::error_code ec;
    string prefix_str("7:10.1.1.1:65535,12345,9.8.7.6:224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad group address.
TEST_F(MvpnPrefixTest, Type7Error5) {
    boost::system::error_code ec;
    string prefix_str("7:10.1.1.1:65535,12345,9.8.7.6,224.1.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

// Bad source address.
TEST_F(MvpnPrefixTest, Type7Error6) {
    boost::system::error_code ec;
    string prefix_str("7:10.1.1.1:65535,12345,9.7.6,224.1.2.3");
    MvpnPrefix prefix = MvpnPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
}

class MvpnRouteTest : public ::testing::Test {
protected:
    MvpnRouteTest() : server_(&evm_) {
        attr_db_ = server_.attr_db();
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
};

TEST_F(MvpnRouteTest, InvalidRouteType) {
    MvpnPrefix prefix;
    MvpnRoute route(prefix);
    EXPECT_FALSE(route.IsValid());
}

TEST_F(MvpnRouteTest, ToString) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
    EXPECT_EQ("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1", route.ToString());
}

TEST_F(MvpnRouteTest, IsValid1) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix);
    route.MarkDelete();
}

TEST_F(MvpnRouteTest, IsValid2) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix);
}

TEST_F(MvpnRouteTest, IsValid3) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix);
    BgpAttr *attr = new BgpAttr(attr_db_);
    BgpAttrPtr attr_ptr = attr_db_->Locate(attr);
    BgpPath *path(new BgpPath(NULL, 0, BgpPath::Local, attr_ptr, 0, 0));
    route.InsertPath(path);
    route.DeletePath(path);
}

TEST_F(MvpnRouteTest, IsValid4) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix);
    EdgeForwardingSpec efspec;
    BgpAttr *attr = new BgpAttr(attr_db_);
    attr->set_edge_forwarding(&efspec);
    BgpAttrPtr attr_ptr = attr_db_->Locate(attr);
    BgpPath *path(new BgpPath(NULL, 0, BgpPath::Local, attr_ptr, 0, 0));
    route.InsertPath(path);
    route.DeletePath(path);
}

TEST_F(MvpnRouteTest, FromProtoPrefix) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    int result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_EQ(0, result);
    EXPECT_EQ(MvpnPrefix::SPMSIADRoute, proto_prefix.type);
    EXPECT_EQ(MvpnPrefix::kSPMSIADRouteSize * 8,
              static_cast<size_t>(proto_prefix.prefixlen));
    EXPECT_EQ(MvpnPrefix::kSPMSIADRouteSize, proto_prefix.prefix.size());
    EXPECT_EQ(prefix1, prefix2);
}

TEST_F(MvpnRouteTest, FromProtoPrefixType4) {
    string prefix_str(
                "4-3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1,10.10.10.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    int result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_EQ(0, result);
    EXPECT_EQ(MvpnPrefix::LeafADRoute, proto_prefix.type);
    EXPECT_EQ(28 * 8, proto_prefix.prefixlen);
    EXPECT_EQ(28U, proto_prefix.prefix.size());
    EXPECT_EQ(prefix1, prefix2);
}

TEST_F(MvpnRouteTest, SetKey) {
    MvpnPrefix null_prefix;
    MvpnRoute route(null_prefix);
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    boost::scoped_ptr<MvpnTable::RequestKey> key(
        new MvpnTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(MvpnRouteTest, GetDBRequestKey) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const MvpnTable::RequestKey *key =
        static_cast<MvpnTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}

TEST_F(MvpnRouteTest, FromProtoPrefixError1) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    int result;
    proto_prefix.type = 13;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
}

TEST_F(MvpnRouteTest, FromProtoPrefixError2) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    proto_prefix.prefix.resize(18);
    int result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
}

TEST_F(MvpnRouteTest, FromProtoPrefixError3) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    proto_prefix.prefix.resize(20);
    int result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
}

TEST_F(MvpnRouteTest, FromProtoPrefixError4) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    proto_prefix.prefix.resize(46);
    int result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
}

TEST_F(MvpnRouteTest, FromProtoPrefixError5) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    int result;
    proto_prefix.prefix[8] = 0;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
    proto_prefix.prefix[8] = 24;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
    proto_prefix.prefix[8] = 64;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
    proto_prefix.prefix[8] = 128;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
}

TEST_F(MvpnRouteTest, FromProtoPrefixError6) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    int result;
    proto_prefix.prefix[13] = 0;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
    proto_prefix.prefix[13] = 24;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
    proto_prefix.prefix[13] = 64;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
    proto_prefix.prefix[13] = 128;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
}

TEST_F(MvpnRouteTest, FromProtoPrefixType4Error) {
    string prefix_str(
                "4-3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1,10.1.1.1");
    MvpnPrefix prefix1(MvpnPrefix::FromString(prefix_str));
    MvpnRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, 0);
    MvpnPrefix prefix2;
    int result;
    proto_prefix.prefix[15] = 0;
    result = MvpnPrefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
