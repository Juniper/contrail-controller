/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/evpn/evpn_route.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using std::string;

class EvpnPrefixTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        bs_.reset(new BgpServerTest(&evm_, "Local"));
    }
    virtual void TearDown() {
        bs_->Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServerTestPtr bs_;
};

// No dash.
TEST_F(EvpnPrefixTest, ParseType_Error1) {
    boost::system::error_code ec;
    string prefix_str("1+10.1.1.1:65535+00:01:02:03:04:05:06:07:08:09+0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad type.
TEST_F(EvpnPrefixTest, ParseType_Error2) {
    boost::system::error_code ec;
    string prefix_str("x-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Out of range type.
TEST_F(EvpnPrefixTest, ParseType_Error3) {
    boost::system::error_code ec;
    string prefix_str("0-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Out of range type.
TEST_F(EvpnPrefixTest, ParseType_Error4) {
    boost::system::error_code ec;
    string prefix_str("5-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

TEST_F(EvpnPrefixTest, FromProtoPrefix_Error) {
    BgpProtoPrefix proto_prefix;
    for (uint16_t type = 0; type < 255; ++type) {
        if (type >= EvpnPrefix::AutoDiscoveryRoute &&
            type <= EvpnPrefix::SegmentRoute)
            continue;
        proto_prefix.type = type;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

class EvpnAutoDiscoveryPrefixTest : public EvpnPrefixTest {
};

TEST_F(EvpnAutoDiscoveryPrefixTest, BuildPrefix) {
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    EthernetSegmentId esi(
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:09"));

    string temp("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnPrefix prefix(rd, esi, tag);
        string prefix_str = temp + integerToString(tag);
        EXPECT_EQ(prefix_str, prefix.ToString());
        EXPECT_EQ(EvpnPrefix::AutoDiscoveryRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ("00:01:02:03:04:05:06:07:08:09", prefix.esi().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ(Address::UNSPEC, prefix.family());
    }
}

TEST_F(EvpnAutoDiscoveryPrefixTest, ParsePrefix) {
    string temp("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp + integerToString(tag);
        boost::system::error_code ec;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());
        EXPECT_EQ(prefix_str, prefix.ToString());
        EXPECT_EQ(EvpnPrefix::AutoDiscoveryRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ("00:01:02:03:04:05:06:07:08:09", prefix.esi().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ(Address::UNSPEC, prefix.family());
    }
}

// No dashes.
TEST_F(EvpnAutoDiscoveryPrefixTest, ParsePrefix_Error1) {
    boost::system::error_code ec;
    string prefix_str("1+10.1.1.1:65535+00:01:02:03:04:05:06:07:08:09+0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No dashes after type delimiter.
TEST_F(EvpnAutoDiscoveryPrefixTest, ParsePrefix_Error2) {
    boost::system::error_code ec;
    string prefix_str("1-10.1.1.1:65535+00:01:02:03:04:05:06:07:08:09+0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad RD.
TEST_F(EvpnAutoDiscoveryPrefixTest, ParsePrefix_Error3) {
    boost::system::error_code ec;
    string prefix_str("1-10.1.1.1:65536-00:01:02:03:04:05:06:07:08:09-0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No dashes after RD delimiter.
TEST_F(EvpnAutoDiscoveryPrefixTest, ParsePrefix_Error4) {
    boost::system::error_code ec;
    string prefix_str("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09+0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad ESI.
TEST_F(EvpnAutoDiscoveryPrefixTest, ParsePrefix_Error5) {
    boost::system::error_code ec;
    string prefix_str("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08-0");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad tag.
TEST_F(EvpnAutoDiscoveryPrefixTest, ParsePrefix_Error6) {
    boost::system::error_code ec;
    string prefix_str("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-0x");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Build and parse BgpProtoPrefix for reach.
TEST_F(EvpnAutoDiscoveryPrefixTest, FromProtoPrefix1) {
    string temp("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp + integerToString(tag);
        boost::system::error_code ec;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpAttr attr1;
        uint32_t label1 = 10000;
        BgpProtoPrefix proto_prefix;
        prefix1.BuildProtoPrefix(&attr1, label1, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::AutoDiscoveryRoute, proto_prefix.type);
        size_t expected_size =
            EvpnPrefix::kMinAutoDiscoveryRouteSize + EvpnPrefix::kLabelSize;
        EXPECT_EQ(expected_size * 8, proto_prefix.prefixlen);
        EXPECT_EQ(expected_size, proto_prefix.prefix.size());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_in2(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in2.get(), &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(attr_out2->esi().IsZero());
        EXPECT_EQ(attr_in2.get(), attr_out2.get());
        EXPECT_EQ(label1, label2);
    }
}

// Build and parse (w/ and w/o label) BgpProtoPrefix for unreach.
TEST_F(EvpnAutoDiscoveryPrefixTest, FromProtoPrefix2) {
    string temp("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp + integerToString(tag);
        boost::system::error_code ec;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpProtoPrefix proto_prefix;
        prefix1.BuildProtoPrefix(NULL, 0, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::AutoDiscoveryRoute, proto_prefix.type);
        size_t expected_size =
            EvpnPrefix::kMinAutoDiscoveryRouteSize + EvpnPrefix::kLabelSize;
        EXPECT_EQ(expected_size * 8, proto_prefix.prefixlen);
        EXPECT_EQ(expected_size, proto_prefix.prefix.size());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result;

        prefix2 = EvpnPrefix::kNullPrefix;
        result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(attr_out2.get() == NULL);
        EXPECT_EQ(0, label2);

        proto_prefix.prefix.resize(EvpnPrefix::kMinAutoDiscoveryRouteSize);
        prefix2 = EvpnPrefix::kNullPrefix;
        label2 = EvpnPrefix::kInvalidLabel;
        result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(attr_out2.get() == NULL);
        EXPECT_EQ(0, label2);
    }
}

// Smaller than minimum size for reach.
TEST_F(EvpnAutoDiscoveryPrefixTest, FromProtoPrefix_Error1) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::AutoDiscoveryRoute;

    for (size_t nlri_size = 0;
         nlri_size < EvpnPrefix::kMinAutoDiscoveryRouteSize + EvpnPrefix::kLabelSize;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum size for unreach.
TEST_F(EvpnAutoDiscoveryPrefixTest, FromProtoPrefix_Error2) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::AutoDiscoveryRoute;

    for (size_t nlri_size = 0;
         nlri_size < EvpnPrefix::kMinAutoDiscoveryRouteSize;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        EvpnPrefix prefix;
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

class EvpnMacAdvertisementPrefixTest : public EvpnPrefixTest {
};

TEST_F(EvpnMacAdvertisementPrefixTest, BuildPrefix1) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    MacAddress mac_addr(MacAddress::FromString("11:12:13:14:15:16", &ec));
    Ip4Address ip4_addr;

    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,0.0.0.0");
    string temp3("11:12:13:14:15:16,0.0.0.0/32");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnPrefix prefix;
        if (tag == EvpnPrefix::kNullTag) {
            prefix = EvpnPrefix(rd, mac_addr, ip4_addr);
        } else {
            prefix = EvpnPrefix(rd, tag, mac_addr, ip4_addr);
        }
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EXPECT_EQ(prefix_str, prefix.ToString());
        string xmpp_id_str =
            (tag == 0) ? temp3 : integerToString(tag) + "-" + temp3;
        EXPECT_EQ(xmpp_id_str, prefix.ToXmppIdString());
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ("11:12:13:14:15:16", prefix.mac_addr().ToString());
        EXPECT_EQ(Address::UNSPEC, prefix.family());
        EXPECT_EQ("0.0.0.0", prefix.ip_address().to_string());
    }
}

TEST_F(EvpnMacAdvertisementPrefixTest, BuildPrefix2) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    MacAddress mac_addr(MacAddress::FromString("11:12:13:14:15:16", &ec));
    Ip4Address ip4_addr = Ip4Address::from_string("192.1.1.1", ec);

    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,192.1.1.1");
    string temp3("11:12:13:14:15:16,192.1.1.1/32");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnPrefix prefix;
        if (tag == EvpnPrefix::kNullTag) {
            prefix = EvpnPrefix(rd, mac_addr, ip4_addr);
        } else {
            prefix = EvpnPrefix(rd, tag, mac_addr, ip4_addr);
        }
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EXPECT_EQ(prefix_str, prefix.ToString());
        string xmpp_id_str =
            (tag == 0) ? temp3 : integerToString(tag) + "-" + temp3;
        EXPECT_EQ(xmpp_id_str, prefix.ToXmppIdString());
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ("11:12:13:14:15:16", prefix.mac_addr().ToString());
        EXPECT_EQ(Address::INET, prefix.family());
        EXPECT_EQ("192.1.1.1", prefix.ip_address().to_string());
    }
}

TEST_F(EvpnMacAdvertisementPrefixTest, BuildPrefix3) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    MacAddress mac_addr(MacAddress::FromString("11:12:13:14:15:16", &ec));
    Ip6Address ip6_addr = Ip6Address::from_string("2001:db8:0:9::1", ec);

    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,2001:db8:0:9::1");
    string temp3("11:12:13:14:15:16,2001:db8:0:9::1/128");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnPrefix prefix;
        if (tag == EvpnPrefix::kNullTag) {
            prefix = EvpnPrefix(rd, mac_addr, ip6_addr);
        } else {
            prefix = EvpnPrefix(rd, tag, mac_addr, ip6_addr);
        }
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EXPECT_EQ(prefix_str, prefix.ToString());
        string xmpp_id_str =
            (tag == 0) ? temp3 : integerToString(tag) + "-" + temp3;
        EXPECT_EQ(xmpp_id_str, prefix.ToXmppIdString());
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ("11:12:13:14:15:16", prefix.mac_addr().ToString());
        EXPECT_EQ(Address::INET6, prefix.family());
        EXPECT_EQ("2001:db8:0:9::1", prefix.ip_address().to_string());
    }
}

TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix1) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,0.0.0.0");
    string temp3("11:12:13:14:15:16,0.0.0.0/32");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        boost::system::error_code ec;
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());
        EXPECT_EQ(prefix_str, prefix.ToString());
        string xmpp_id_str =
            (tag == 0) ? temp3 : integerToString(tag) + "-" + temp3;
        EXPECT_EQ(xmpp_id_str, prefix.ToXmppIdString());
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ("11:12:13:14:15:16", prefix.mac_addr().ToString());
        EXPECT_EQ(Address::UNSPEC, prefix.family());
        EXPECT_EQ("0.0.0.0", prefix.ip_address().to_string());
    }
}

TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix2) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,192.1.1.1");
    string temp3("11:12:13:14:15:16,192.1.1.1/32");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        boost::system::error_code ec;
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());
        EXPECT_EQ(prefix_str, prefix.ToString());
        string xmpp_id_str =
            (tag == 0) ? temp3 : integerToString(tag) + "-" + temp3;
        EXPECT_EQ(xmpp_id_str, prefix.ToXmppIdString());
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ("11:12:13:14:15:16", prefix.mac_addr().ToString());
        EXPECT_EQ(Address::INET, prefix.family());
        EXPECT_EQ("192.1.1.1", prefix.ip_address().to_string());
    }
}

TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix3) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,2001:db8:0:9::1");
    string temp3("11:12:13:14:15:16,2001:db8:0:9::1/128");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        boost::system::error_code ec;
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());
        EXPECT_EQ(prefix_str, prefix.ToString());
        string xmpp_id_str =
            (tag == 0) ? temp3 : integerToString(tag) + "-" + temp3;
        EXPECT_EQ(xmpp_id_str, prefix.ToXmppIdString());
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ("11:12:13:14:15:16", prefix.mac_addr().ToString());
        EXPECT_EQ(Address::INET6, prefix.family());
        EXPECT_EQ("2001:db8:0:9::1", prefix.ip_address().to_string());
    }
}

// No dashes.
TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix_Error1) {
    boost::system::error_code ec;
    string prefix_str("2+10.1.1.1:65535+65536+11:12:13:14:15:16,192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No dashes after type delimiter.
TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix_Error2) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65535+65536+11:12:13:14:15:16,192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad RD.
TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix_Error3) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65536-65536-11:12:13:14:15:16,192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No dashes after RD delimiter.
TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix_Error4) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65535-65536+11:12:13:14:15:16,192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad tag.
TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix_Error5) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65535-65536x-11:12:13:14:15:16,192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No comma.
TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix_Error6) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65535-65536-11:12:13:14:15:16+192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad MAC.
TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix_Error7) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65535-0-11:12:13:14:15,192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad IP address.
TEST_F(EvpnMacAdvertisementPrefixTest, ParsePrefix_Error8) {
    boost::system::error_code ec;
    string prefix_str("2-10.1.1.1:65535-0-11:12:13:14:15:16,192.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Build and parse BgpProtoPrefix for reach, w/o ip.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix1) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,0.0.0.0");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        boost::system::error_code ec;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpAttr attr1;
        uint32_t label1 = 10000;
        BgpProtoPrefix proto_prefix;
        EthernetSegmentId esi1 =
            EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:09");
        attr1.set_esi(esi1);
        prefix1.BuildProtoPrefix(&attr1, label1, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, proto_prefix.type);
        size_t expected_size =
            EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize;
        EXPECT_EQ(expected_size * 8, proto_prefix.prefixlen);
        EXPECT_EQ(expected_size, proto_prefix.prefix.size());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_in2(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in2.get(), &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_EQ(esi1, attr_out2->esi());
        EXPECT_NE(attr_in2.get(), attr_out2.get());
        EXPECT_EQ(label1, label2);
    }
}

// Build and parse BgpProtoPrefix for reach w/ ipv4.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix2) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        boost::system::error_code ec;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpAttr attr1;
        uint32_t label1 = 10000;
        BgpProtoPrefix proto_prefix;
        EthernetSegmentId esi1 =
            EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:09");
        attr1.set_esi(esi1);
        prefix1.BuildProtoPrefix(&attr1, label1, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, proto_prefix.type);
        size_t expected_size =
            EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize + 4;
        EXPECT_EQ(expected_size * 8, proto_prefix.prefixlen);
        EXPECT_EQ(expected_size, proto_prefix.prefix.size());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_in2(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in2.get(), &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_EQ(esi1, attr_out2->esi());
        EXPECT_NE(attr_in2.get(), attr_out2.get());
        EXPECT_EQ(label1, label2);
    }
}

// Build and parse BgpProtoPrefix for reach w/ ipv6.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix3) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,2001:db8:0:9::1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        boost::system::error_code ec;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpAttr attr1;
        uint32_t label1 = 10000;
        BgpProtoPrefix proto_prefix;
        EthernetSegmentId esi1 =
            EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:09");
        attr1.set_esi(esi1);
        prefix1.BuildProtoPrefix(&attr1, label1, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, proto_prefix.type);
        size_t expected_size =
            EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize + 16;
        EXPECT_EQ(expected_size * 8, proto_prefix.prefixlen);
        EXPECT_EQ(expected_size, proto_prefix.prefix.size());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_in2(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in2.get(), &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_EQ(esi1, attr_out2->esi());
        EXPECT_NE(attr_in2.get(), attr_out2.get());
        EXPECT_EQ(label1, label2);
    }
}

// Build and parse (w/ and w/o label) BgpProtoPrefix for unreach, no ip.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix4) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,0.0.0.0");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        boost::system::error_code ec;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpProtoPrefix proto_prefix;
        prefix1.BuildProtoPrefix(NULL, 0, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, proto_prefix.type);
        size_t expected_size =
            EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize;
        EXPECT_EQ(expected_size * 8, proto_prefix.prefixlen);
        EXPECT_EQ(expected_size, proto_prefix.prefix.size());
        size_t esi_offset = EvpnPrefix::kRdSize;
        EthernetSegmentId esi(&proto_prefix.prefix[esi_offset]);
        EXPECT_TRUE(esi.IsZero());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result;

        result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_TRUE(attr_out2.get() == NULL);
        EXPECT_EQ(0, label2);

        proto_prefix.prefix.resize(EvpnPrefix::kMinMacAdvertisementRouteSize);
        prefix2 = EvpnPrefix::kNullPrefix;
        label2 = EvpnPrefix::kInvalidLabel;
        result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_TRUE(attr_out2.get() == NULL);
        EXPECT_EQ(0, label2);
    }
}

// Build and parse (w/ and w/o label) BgpProtoPrefix for unreach, w/ ipv4.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix5) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        boost::system::error_code ec;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpProtoPrefix proto_prefix;
        prefix1.BuildProtoPrefix(NULL, 0, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, proto_prefix.type);
        size_t expected_size =
            EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize + 4;
        EXPECT_EQ(expected_size * 8, proto_prefix.prefixlen);
        EXPECT_EQ(expected_size, proto_prefix.prefix.size());
        size_t esi_offset = EvpnPrefix::kRdSize;
        EthernetSegmentId esi(&proto_prefix.prefix[esi_offset]);
        EXPECT_TRUE(esi.IsZero());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result;

        result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_TRUE(attr_out2.get() == NULL);
        EXPECT_EQ(0, label2);

        proto_prefix.prefix.resize(EvpnPrefix::kMinMacAdvertisementRouteSize + 4);
        prefix2 = EvpnPrefix::kNullPrefix;
        label2 = EvpnPrefix::kInvalidLabel;
        result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_TRUE(attr_out2.get() == NULL);
        EXPECT_EQ(0, label2);
    }
}

// Build and parse (w/ and w/o label) BgpProtoPrefix for unreach, w/ ipv6.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix6) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,2001:db8:0:9::1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        boost::system::error_code ec;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpProtoPrefix proto_prefix;
        prefix1.BuildProtoPrefix(NULL, 0, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::MacAdvertisementRoute, proto_prefix.type);
        size_t expected_size =
            EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize + 16;
        EXPECT_EQ(expected_size * 8, proto_prefix.prefixlen);
        EXPECT_EQ(expected_size, proto_prefix.prefix.size());
        size_t esi_offset = EvpnPrefix::kRdSize;
        EthernetSegmentId esi(&proto_prefix.prefix[esi_offset]);
        EXPECT_TRUE(esi.IsZero());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result;

        result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_TRUE(attr_out2.get() == NULL);
        EXPECT_EQ(0, label2);

        proto_prefix.prefix.resize(EvpnPrefix::kMinMacAdvertisementRouteSize + 16);
        prefix2 = EvpnPrefix::kNullPrefix;
        label2 = EvpnPrefix::kInvalidLabel;
        result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(prefix2.esi().IsZero());
        EXPECT_TRUE(attr_out2.get() == NULL);
        EXPECT_EQ(0, label2);
    }
}

// Smaller than minimum size for reach.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error1) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;

    for (size_t nlri_size = 0;
         nlri_size < EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum size for unreach.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error2) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;

    for (size_t nlri_size = 0;
         nlri_size < EvpnPrefix::kMinMacAdvertisementRouteSize;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        EvpnPrefix prefix;
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Bad MAC Address length in reach.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error3) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;
    size_t nlri_size =
        EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize;
    proto_prefix.prefix.resize(nlri_size, 0);
    size_t mac_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize +
        EvpnPrefix::kTagSize;

    for (uint16_t mac_len = 0; mac_len <= 255; ++mac_len) {
        if (mac_len == 48)
            continue;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        proto_prefix.prefix[mac_len_offset] = mac_len;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Bad MAC Address length in unreach.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error4) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;
    size_t nlri_size =
        EvpnPrefix::kMinMacAdvertisementRouteSize;
    proto_prefix.prefix.resize(nlri_size, 0);
    size_t mac_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize +
        EvpnPrefix::kTagSize;

    for (uint16_t mac_len = 0; mac_len <= 255; ++mac_len) {
        if (mac_len == 48)
            continue;
        EvpnPrefix prefix;
        BgpAttrPtr attr_out;
        uint32_t label;
        proto_prefix.prefix[mac_len_offset] = mac_len;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Bad IP Address length in reach.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error5) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;
    size_t nlri_size =
        EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize;
    proto_prefix.prefix.resize(nlri_size, 0);
    size_t mac_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize +
        EvpnPrefix::kTagSize;
    size_t ip_len_offset = mac_len_offset + 1 + EvpnPrefix::kMacSize;

    proto_prefix.prefix[mac_len_offset] = 48;
    for (uint16_t ip_len = 0; ip_len <= 255; ++ip_len) {
        if (ip_len == 0 || ip_len == 32 || ip_len == 128)
            continue;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        proto_prefix.prefix[ip_len_offset] = ip_len;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Bad IP Address length in unreach.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error6) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;
    size_t nlri_size =
        EvpnPrefix::kMinMacAdvertisementRouteSize;
    proto_prefix.prefix.resize(nlri_size, 0);
    size_t mac_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize +
        EvpnPrefix::kTagSize;
    size_t ip_len_offset = mac_len_offset + 1 + EvpnPrefix::kMacSize;

    proto_prefix.prefix[mac_len_offset] = 48;
    for (uint16_t ip_len = 0; ip_len <= 255; ++ip_len) {
        if (ip_len == 0 || ip_len == 32 || ip_len == 128)
            continue;
        EvpnPrefix prefix;
        BgpAttrPtr attr_out;
        uint32_t label;
        proto_prefix.prefix[ip_len_offset] = ip_len;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum reach size for ipv4 address.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error7) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;
    size_t mac_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize +
        EvpnPrefix::kTagSize;
    size_t ip_len_offset = mac_len_offset + 1 + EvpnPrefix::kMacSize;

    for (size_t nlri_size = EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize;
         nlri_size < EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize + 4;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        proto_prefix.prefix[mac_len_offset] = 48;
        proto_prefix.prefix[ip_len_offset] = 32;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum reach size for ipv4 address.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error8) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;
    size_t mac_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize +
        EvpnPrefix::kTagSize;
    size_t ip_len_offset = mac_len_offset + 1 + EvpnPrefix::kMacSize;

    for (size_t nlri_size = EvpnPrefix::kMinMacAdvertisementRouteSize;
         nlri_size < EvpnPrefix::kMinMacAdvertisementRouteSize + 4;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        proto_prefix.prefix[mac_len_offset] = 48;
        proto_prefix.prefix[ip_len_offset] = 32;
        EvpnPrefix prefix;
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum reach size for ipv6 address.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error9) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;
    size_t mac_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize +
        EvpnPrefix::kTagSize;
    size_t ip_len_offset = mac_len_offset + 1 + EvpnPrefix::kMacSize;

    for (size_t nlri_size = EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize;
         nlri_size < EvpnPrefix::kMinMacAdvertisementRouteSize + EvpnPrefix::kLabelSize + 16;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        proto_prefix.prefix[mac_len_offset] = 48;
        proto_prefix.prefix[ip_len_offset] = 128;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum unreach size for ipv6 address.
TEST_F(EvpnMacAdvertisementPrefixTest, FromProtoPrefix_Error10) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::MacAdvertisementRoute;
    size_t mac_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize +
        EvpnPrefix::kTagSize;
    size_t ip_len_offset = mac_len_offset + 1 + EvpnPrefix::kMacSize;

    for (size_t nlri_size = EvpnPrefix::kMinMacAdvertisementRouteSize;
         nlri_size < EvpnPrefix::kMinMacAdvertisementRouteSize + 16;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        proto_prefix.prefix[mac_len_offset] = 48;
        proto_prefix.prefix[ip_len_offset] = 128;
        EvpnPrefix prefix;
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, NULL, &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

class EvpnInclusiveMulticastPrefixTest : public EvpnPrefixTest {
};

TEST_F(EvpnInclusiveMulticastPrefixTest, BuildPrefix1) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address ip4_addr = Ip4Address::from_string("192.1.1.1", ec);

    string temp1("3-10.1.1.1:65535-");
    string temp2("-192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnPrefix prefix(rd, tag, ip4_addr);
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EXPECT_EQ(prefix_str, prefix.ToString());
        EXPECT_EQ(EvpnPrefix::InclusiveMulticastRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ(Address::INET, prefix.family());
        EXPECT_EQ("192.1.1.1", prefix.ip_address().to_string());
    }
}

TEST_F(EvpnInclusiveMulticastPrefixTest, BuildPrefix2) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip6Address ip6_addr = Ip6Address::from_string("2001:db8:0:9::1", ec);

    string temp1("3-10.1.1.1:65535-");
    string temp2("-2001:db8:0:9::1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnPrefix prefix(rd, tag, ip6_addr);
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EXPECT_EQ(prefix_str, prefix.ToString());
        EXPECT_EQ(EvpnPrefix::InclusiveMulticastRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ(Address::INET6, prefix.family());
        EXPECT_EQ("2001:db8:0:9::1", prefix.ip_address().to_string());
    }
}

TEST_F(EvpnInclusiveMulticastPrefixTest, ParsePrefix1) {
    string temp1("3-10.1.1.1:65535-");
    string temp2("-192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        boost::system::error_code ec;
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());
        EXPECT_EQ(prefix_str, prefix.ToString());
        EXPECT_EQ(EvpnPrefix::InclusiveMulticastRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ(Address::INET, prefix.family());
        EXPECT_EQ("192.1.1.1", prefix.ip_address().to_string());
    }
}

TEST_F(EvpnInclusiveMulticastPrefixTest, ParsePrefix2) {
    string temp1("3-10.1.1.1:65535-");
    string temp2("-2001:db8:0:9::1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        boost::system::error_code ec;
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());
        EXPECT_EQ(prefix_str, prefix.ToString());
        EXPECT_EQ(EvpnPrefix::InclusiveMulticastRoute, prefix.type());
        EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
        EXPECT_EQ(tag, prefix.tag());
        EXPECT_EQ(Address::INET6, prefix.family());
        EXPECT_EQ("2001:db8:0:9::1", prefix.ip_address().to_string());
    }
}

// No dashes.
TEST_F(EvpnInclusiveMulticastPrefixTest, ParsePrefix_Error1) {
    boost::system::error_code ec;
    string prefix_str("3+10.1.1.1:65535+65536+192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No dashes after type delimiter.
TEST_F(EvpnInclusiveMulticastPrefixTest, ParsePrefix_Error2) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65535+65536+192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad RD.
TEST_F(EvpnInclusiveMulticastPrefixTest, ParsePrefix_Error3) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65536-65536-192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No dashes after RD delimiter.
TEST_F(EvpnInclusiveMulticastPrefixTest, ParsePrefix_Error4) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65535-65536+192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad tag.
TEST_F(EvpnInclusiveMulticastPrefixTest, ParsePrefix_Error5) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65535-65536x-192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad IP address.
TEST_F(EvpnInclusiveMulticastPrefixTest, ParsePrefix_Error6) {
    boost::system::error_code ec;
    string prefix_str("3-10.1.1.1:65535-65536-192.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

TEST_F(EvpnInclusiveMulticastPrefixTest, FromProtoPrefix1) {
    string temp1("3-10.1.1.1:65535-");
    string temp2("-192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        boost::system::error_code ec;
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpAttr attr1;
        BgpProtoPrefix proto_prefix;
        prefix1.BuildProtoPrefix(&attr1, 0, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::InclusiveMulticastRoute, proto_prefix.type);
        EXPECT_EQ((EvpnPrefix::kMinInclusiveMulticastRouteSize + 4) * 8,
            proto_prefix.prefixlen);
        EXPECT_EQ(EvpnPrefix::kMinInclusiveMulticastRouteSize + 4,
            proto_prefix.prefix.size());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_in2(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in2.get(), &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(attr_out2->esi().IsZero());
        EXPECT_EQ(attr_in2.get(), attr_out2.get());
        EXPECT_EQ(0, label2);
    }
}

TEST_F(EvpnInclusiveMulticastPrefixTest, FromProtoPrefix2) {
    string temp1("3-10.1.1.1:65535-");
    string temp2("-2001:db8:0:9::1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        boost::system::error_code ec;
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
        EXPECT_EQ(0, ec.value());

        BgpAttr attr1;
        BgpProtoPrefix proto_prefix;
        prefix1.BuildProtoPrefix(&attr1, 0, &proto_prefix);
        EXPECT_EQ(EvpnPrefix::InclusiveMulticastRoute, proto_prefix.type);
        EXPECT_EQ((EvpnPrefix::kMinInclusiveMulticastRouteSize + 16) * 8,
            proto_prefix.prefixlen);
        EXPECT_EQ(EvpnPrefix::kMinInclusiveMulticastRouteSize + 16,
            proto_prefix.prefix.size());

        EvpnPrefix prefix2;
        BgpAttrPtr attr_in2(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out2;
        uint32_t label2;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in2.get(), &prefix2, &attr_out2, &label2);
        EXPECT_EQ(0, result);
        EXPECT_EQ(prefix1, prefix2);
        EXPECT_TRUE(attr_out2->esi().IsZero());
        EXPECT_EQ(attr_in2.get(), attr_out2.get());
        EXPECT_EQ(0, label2);
    }
}

// Smaller than minimum size.
TEST_F(EvpnInclusiveMulticastPrefixTest, FromProtoPrefix_Error1) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::InclusiveMulticastRoute;
    size_t nlri_size = EvpnPrefix::kMinInclusiveMulticastRouteSize;

    for (size_t nlri_size = 0;
         nlri_size < EvpnPrefix::kMinInclusiveMulticastRouteSize;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Bad IP Address length.
TEST_F(EvpnInclusiveMulticastPrefixTest, FromProtoPrefix_Error2) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::InclusiveMulticastRoute;
    size_t nlri_size = EvpnPrefix::kMinInclusiveMulticastRouteSize;
    proto_prefix.prefix.resize(nlri_size, 0);
    size_t ip_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kTagSize;

    for (uint16_t ip_len = 0; ip_len <= 255; ++ip_len) {
        if (ip_len == 32 || ip_len == 128)
            continue;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        proto_prefix.prefix[ip_len_offset] = ip_len;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum size for ipv4 address.
TEST_F(EvpnInclusiveMulticastPrefixTest, FromProtoPrefix_Error3) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::InclusiveMulticastRoute;
    size_t ip_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kTagSize;

    for (size_t nlri_size = EvpnPrefix::kMinInclusiveMulticastRouteSize;
         nlri_size < EvpnPrefix::kMinInclusiveMulticastRouteSize + 4;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        proto_prefix.prefix[ip_len_offset] = 32;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum size for ipv6 address.
TEST_F(EvpnInclusiveMulticastPrefixTest, FromProtoPrefix_Error4) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::InclusiveMulticastRoute;
    size_t ip_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kTagSize;

    for (size_t nlri_size = EvpnPrefix::kMinInclusiveMulticastRouteSize;
         nlri_size < EvpnPrefix::kMinInclusiveMulticastRouteSize + 16;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        proto_prefix.prefix[ip_len_offset] = 128;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

class EvpnSegmentPrefixTest : public EvpnPrefixTest {
};

TEST_F(EvpnSegmentPrefixTest, BuildPrefix1) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    EthernetSegmentId esi(
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:09"));
    Ip4Address ip4_addr = Ip4Address::from_string("192.1.1.1", ec);
    EvpnPrefix prefix(rd, esi, ip4_addr);
    EXPECT_EQ("4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1",
        prefix.ToString());
    EXPECT_EQ(EvpnPrefix::SegmentRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("00:01:02:03:04:05:06:07:08:09", prefix.esi().ToString());
    EXPECT_EQ(EvpnPrefix::kNullTag, prefix.tag());
    EXPECT_EQ(Address::INET, prefix.family());
    EXPECT_EQ("192.1.1.1", prefix.ip_address().to_string());
}

TEST_F(EvpnSegmentPrefixTest, BuildPrefix2) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    EthernetSegmentId esi(
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:09"));
    Ip6Address ip6_addr = Ip6Address::from_string("2001:db8:0:9::1", ec);
    EvpnPrefix prefix(rd, esi, ip6_addr);
    EXPECT_EQ("4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-2001:db8:0:9::1",
        prefix.ToString());
    EXPECT_EQ(EvpnPrefix::SegmentRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("00:01:02:03:04:05:06:07:08:09", prefix.esi().ToString());
    EXPECT_EQ(EvpnPrefix::kNullTag, prefix.tag());
    EXPECT_EQ(Address::INET6, prefix.family());
    EXPECT_EQ("2001:db8:0:9::1", prefix.ip_address().to_string());
}

TEST_F(EvpnSegmentPrefixTest, ParsePrefix1) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ("4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1",
        prefix.ToString());
    EXPECT_EQ(EvpnPrefix::SegmentRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("00:01:02:03:04:05:06:07:08:09", prefix.esi().ToString());
    EXPECT_EQ(EvpnPrefix::kNullTag, prefix.tag());
    EXPECT_EQ(Address::INET, prefix.family());
    EXPECT_EQ("192.1.1.1", prefix.ip_address().to_string());
}

TEST_F(EvpnSegmentPrefixTest, ParsePrefix2) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-2001:db8:0:9::1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ("4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-2001:db8:0:9::1",
        prefix.ToString());
    EXPECT_EQ(EvpnPrefix::SegmentRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("00:01:02:03:04:05:06:07:08:09", prefix.esi().ToString());
    EXPECT_EQ(EvpnPrefix::kNullTag, prefix.tag());
    EXPECT_EQ(Address::INET6, prefix.family());
    EXPECT_EQ("2001:db8:0:9::1", prefix.ip_address().to_string());
}

// No dashes.
TEST_F(EvpnSegmentPrefixTest, ParsePrefix_Error1) {
    boost::system::error_code ec;
    string prefix_str(
        "4+10.1.1.1:65535+00:01:02:03:04:05:06:07:08:09+192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No dashes after type delimiter.
TEST_F(EvpnSegmentPrefixTest, ParsePrefix_Error2) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65535+00:01:02:03:04:05:06:07:08:09+192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad RD.
TEST_F(EvpnSegmentPrefixTest, ParsePrefix_Error3) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65536-00:01:02:03:04:05:06:07:08:09-192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// No dashes after RD delimiter.
TEST_F(EvpnSegmentPrefixTest, ParsePrefix_Error4) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09+192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad ESI.
TEST_F(EvpnSegmentPrefixTest, ParsePrefix_Error5) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08-192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

// Bad IP address.
TEST_F(EvpnSegmentPrefixTest, ParsePrefix_Error6) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_NE(0, ec.value());
}

TEST_F(EvpnSegmentPrefixTest, FromProtoPrefix1) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1");
    EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_EQ(0, ec.value());

    BgpAttr attr1;
    BgpProtoPrefix proto_prefix;
    prefix1.BuildProtoPrefix(&attr1, 0, &proto_prefix);
    EXPECT_EQ(EvpnPrefix::SegmentRoute, proto_prefix.type);
    EXPECT_EQ((EvpnPrefix::kMinSegmentRouteSize + 4) * 8,
        proto_prefix.prefixlen);
    EXPECT_EQ(EvpnPrefix::kMinSegmentRouteSize + 4,
        proto_prefix.prefix.size());

    EvpnPrefix prefix2;
    BgpAttrPtr attr_in2(new BgpAttr(bs_->attr_db()));
    BgpAttrPtr attr_out2;
    uint32_t label2;
    int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
        proto_prefix, attr_in2.get(), &prefix2, &attr_out2, &label2);
    EXPECT_EQ(0, result);
    EXPECT_EQ(prefix1, prefix2);
    EXPECT_TRUE(attr_out2->esi().IsZero());
    EXPECT_EQ(attr_in2.get(), attr_out2.get());
    EXPECT_EQ(0, label2);
}

TEST_F(EvpnSegmentPrefixTest, FromProtoPrefix2) {
    boost::system::error_code ec;
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-2001:db8:0:9::1");
    EvpnPrefix prefix1(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_EQ(0, ec.value());

    BgpAttr attr1;
    BgpProtoPrefix proto_prefix;
    prefix1.BuildProtoPrefix(&attr1, 0, &proto_prefix);
    EXPECT_EQ(EvpnPrefix::SegmentRoute, proto_prefix.type);
    EXPECT_EQ((EvpnPrefix::kMinSegmentRouteSize + 16) * 8,
        proto_prefix.prefixlen);
    EXPECT_EQ(EvpnPrefix::kMinSegmentRouteSize + 16,
        proto_prefix.prefix.size());

    EvpnPrefix prefix2;
    BgpAttrPtr attr_in2(new BgpAttr(bs_->attr_db()));
    BgpAttrPtr attr_out2;
    uint32_t label2;
    int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
        proto_prefix, attr_in2.get(), &prefix2, &attr_out2, &label2);
    EXPECT_EQ(0, result);
    EXPECT_EQ(prefix1, prefix2);
    EXPECT_TRUE(attr_out2->esi().IsZero());
    EXPECT_EQ(attr_in2.get(), attr_out2.get());
    EXPECT_EQ(0, label2);
}

// Smaller than minimum size.
TEST_F(EvpnSegmentPrefixTest, FromProtoPrefix_Error1) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::SegmentRoute;
    size_t nlri_size = EvpnPrefix::kMinSegmentRouteSize;

    for (size_t nlri_size = 0;
         nlri_size < EvpnPrefix::kMinSegmentRouteSize;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Bad IP Address length.
TEST_F(EvpnSegmentPrefixTest, FromProtoPrefix_Error2) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::SegmentRoute;
    size_t nlri_size = EvpnPrefix::kMinSegmentRouteSize;
    proto_prefix.prefix.resize(nlri_size, 0);
    size_t ip_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize;

    for (uint16_t ip_len = 0; ip_len <= 255; ++ip_len) {
        if (ip_len == 32 || ip_len == 128)
            continue;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        proto_prefix.prefix[ip_len_offset] = ip_len;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum size for ipv4 address.
TEST_F(EvpnSegmentPrefixTest, FromProtoPrefix_Error3) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::SegmentRoute;
    size_t ip_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize;

    for (size_t nlri_size = EvpnPrefix::kMinSegmentRouteSize;
         nlri_size < EvpnPrefix::kMinSegmentRouteSize + 4;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        proto_prefix.prefix[ip_len_offset] = 32;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

// Smaller than minimum size for ipv6 address.
TEST_F(EvpnSegmentPrefixTest, FromProtoPrefix_Error4) {
    BgpProtoPrefix proto_prefix;
    proto_prefix.type = EvpnPrefix::SegmentRoute;
    size_t ip_len_offset = EvpnPrefix::kRdSize + EvpnPrefix::kEsiSize;

    for (size_t nlri_size = EvpnPrefix::kMinSegmentRouteSize;
         nlri_size < EvpnPrefix::kMinSegmentRouteSize + 16;
         ++nlri_size) {
        proto_prefix.prefix.clear();
        proto_prefix.prefix.resize(nlri_size, 0);
        proto_prefix.prefix[ip_len_offset] = 128;
        EvpnPrefix prefix;
        BgpAttrPtr attr_in(new BgpAttr(bs_->attr_db()));
        BgpAttrPtr attr_out;
        uint32_t label;
        int result = EvpnPrefix::FromProtoPrefix(bs_.get(),
            proto_prefix, attr_in.get(), &prefix, &attr_out, &label);
        EXPECT_NE(0, result);
    }
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
