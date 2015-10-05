/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/load_balance.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class LoadBalanceTest : public ::testing::Test {
};

// Use load-balance options which are set by default
TEST_F(LoadBalanceTest, Default_1) {
    LoadBalance lb;
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lb.Type());
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lb.Subtype());

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lba.type);
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lba.sub_type);

    EXPECT_FALSE(lba.l2_source_address);
    EXPECT_FALSE(lba.l2_destination_address);
    EXPECT_TRUE(lba.l3_source_address);
    EXPECT_TRUE(lba.l3_destination_address);
    EXPECT_TRUE(lba.l4_protocol);
    EXPECT_TRUE(lba.l4_source_port);
    EXPECT_TRUE(lba.l4_destination_port);
    EXPECT_FALSE(lba.reserved1);
    EXPECT_EQ(0, lba.reserved2);
    EXPECT_FALSE(lba.source_bias);
    EXPECT_EQ(0, lba.reserved3);
    EXPECT_EQ(0, lba.reserved4);
    EXPECT_EQ(0, lba.reserved5);
    EXPECT_EQ(0, lba.reserved6);

    autogen::LoadBalanceType item;
    lba.encode(item);

    EXPECT_FALSE(item.load_balance_fields.l2_source_address);
    EXPECT_FALSE(item.load_balance_fields.l2_destination_address);
    EXPECT_TRUE(item.load_balance_fields.l3_source_address);
    EXPECT_TRUE(item.load_balance_fields.l3_destination_address);
    EXPECT_TRUE(item.load_balance_fields.l4_protocol);
    EXPECT_TRUE(item.load_balance_fields.l4_source_port);
    EXPECT_TRUE(item.load_balance_fields.l4_destination_port);
    EXPECT_EQ("field-hash", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    EXPECT_EQ(lba, LoadBalance(item).ToAttribute());
    EXPECT_EQ("loadbalance: L3SA L3DA L4PR L4SP L4DP",
              LoadBalance(item).ToString());
}

// Set all boolean options
TEST_F(LoadBalanceTest, AllBooleanSet_1) {
    LoadBalance::bytes_type data =
        { { BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE,
            BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE,
            0xFE, 0x00, 0x80, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lb.Type());
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lb.Subtype());

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lba.type);
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lba.sub_type);

    EXPECT_TRUE(lba.l2_source_address);
    EXPECT_TRUE(lba.l2_destination_address);
    EXPECT_TRUE(lba.l3_source_address);
    EXPECT_TRUE(lba.l3_destination_address);
    EXPECT_TRUE(lba.l4_protocol);
    EXPECT_TRUE(lba.l4_source_port);
    EXPECT_TRUE(lba.l4_destination_port);

    EXPECT_FALSE(lba.reserved1);
    EXPECT_EQ(0, lba.reserved2);

    EXPECT_TRUE(lba.source_bias);
    EXPECT_EQ(0, lba.reserved3);

    EXPECT_EQ(0, lba.reserved4);
    EXPECT_EQ(0, lba.reserved5);
    EXPECT_EQ(0, lba.reserved6);

    // Reconstruct community from the attribute and verify data
    EXPECT_EQ(data, LoadBalance(lba).GetExtCommunity());

    autogen::LoadBalanceType item;
    lba.encode(item);

    EXPECT_TRUE(item.load_balance_fields.l2_source_address);
    EXPECT_TRUE(item.load_balance_fields.l2_destination_address);
    EXPECT_TRUE(item.load_balance_fields.l3_source_address);
    EXPECT_TRUE(item.load_balance_fields.l3_destination_address);
    EXPECT_TRUE(item.load_balance_fields.l4_protocol);
    EXPECT_TRUE(item.load_balance_fields.l4_source_port);
    EXPECT_TRUE(item.load_balance_fields.l4_destination_port);

    EXPECT_EQ("source-bias", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    EXPECT_EQ(lba, LoadBalance(item).ToAttribute());
    EXPECT_EQ(data, LoadBalance(item).GetExtCommunity());
    EXPECT_EQ("loadbalance: L2SA L2DA L3SA L3DA L4PR L4SP L4DP, SB",
              LoadBalance(item).ToString());
}

// Reset all boolean options
TEST_F(LoadBalanceTest, AllBooleanReset_1) {
    LoadBalance::bytes_type data =
        { { BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE,
            BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lb.Type());
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lb.Subtype());

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lba.type);
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lba.sub_type);
    EXPECT_FALSE(lba.l2_source_address);
    EXPECT_FALSE(lba.l2_destination_address);
    EXPECT_FALSE(lba.l3_source_address);
    EXPECT_FALSE(lba.l3_destination_address);
    EXPECT_FALSE(lba.l4_protocol);
    EXPECT_FALSE(lba.l4_source_port);
    EXPECT_FALSE(lba.l4_destination_port);
    EXPECT_FALSE(lba.reserved1);

    EXPECT_EQ(0, lba.reserved2);

    EXPECT_FALSE(lba.source_bias);
    EXPECT_EQ(0, lba.reserved3);

    EXPECT_EQ(0, lba.reserved4);
    EXPECT_EQ(0, lba.reserved5);
    EXPECT_EQ(0, lba.reserved6);

    // Reconstruct community from the attribute and verify data
    EXPECT_EQ(data, LoadBalance(lba).GetExtCommunity());

    autogen::LoadBalanceType item;
    lba.encode(item);
    EXPECT_FALSE(item.load_balance_fields.l2_source_address);
    EXPECT_FALSE(item.load_balance_fields.l2_destination_address);
    EXPECT_FALSE(item.load_balance_fields.l3_source_address);
    EXPECT_FALSE(item.load_balance_fields.l3_destination_address);
    EXPECT_FALSE(item.load_balance_fields.l4_protocol);
    EXPECT_FALSE(item.load_balance_fields.l4_source_port);
    EXPECT_FALSE(item.load_balance_fields.l4_destination_port);
    EXPECT_EQ("field-hash", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    EXPECT_EQ(lba, LoadBalance(item).ToAttribute());
    EXPECT_EQ(data, LoadBalance(item).GetExtCommunity());
    EXPECT_EQ("loadbalance:", LoadBalance(item).ToString());
}

// Set all boolean options alternately
TEST_F(LoadBalanceTest, AlternateBooleanSet_1) {
    LoadBalance::bytes_type data =
        { { BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE,
            BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE,
            0xaa, 0x00, 0x80, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lb.Type());
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lb.Subtype());

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lba.type);
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lba.sub_type);

    EXPECT_TRUE(lba.l2_source_address);
    EXPECT_FALSE(lba.l2_destination_address);
    EXPECT_TRUE(lba.l3_source_address);
    EXPECT_FALSE(lba.l3_destination_address);
    EXPECT_TRUE(lba.l4_protocol);
    EXPECT_FALSE(lba.l4_source_port);
    EXPECT_TRUE(lba.l4_destination_port);

    EXPECT_FALSE(lba.reserved1);
    EXPECT_EQ(0, lba.reserved2);

    EXPECT_TRUE(lba.source_bias);
    EXPECT_EQ(0, lba.reserved3);

    EXPECT_EQ(0, lba.reserved4);
    EXPECT_EQ(0, lba.reserved5);
    EXPECT_EQ(0, lba.reserved6);

    // Reconstruct community from the attribute and verify data
    EXPECT_EQ(data, LoadBalance(lba).GetExtCommunity());

    autogen::LoadBalanceType item;
    lba.encode(item);

    EXPECT_TRUE(item.load_balance_fields.l2_source_address);
    EXPECT_FALSE(item.load_balance_fields.l2_destination_address);
    EXPECT_TRUE(item.load_balance_fields.l3_source_address);
    EXPECT_FALSE(item.load_balance_fields.l3_destination_address);
    EXPECT_TRUE(item.load_balance_fields.l4_protocol);
    EXPECT_FALSE(item.load_balance_fields.l4_source_port);
    EXPECT_TRUE(item.load_balance_fields.l4_destination_port);
    EXPECT_EQ("source-bias", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    EXPECT_EQ(lba, LoadBalance(item).ToAttribute());
    EXPECT_EQ(data, LoadBalance(item).GetExtCommunity());
    EXPECT_EQ("loadbalance: L2SA L3SA L4PR L4DP, SB",
              LoadBalance(item).ToString());
}

// Set all boolean options alternately
TEST_F(LoadBalanceTest, AlternateBooleanSet_2) {
    LoadBalance::bytes_type data =
        { { BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE,
            BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE,
            0x54, 0x00, 0x00, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lb.Type());
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lb.Subtype());

    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_TYPE_OPAQUE, lba.type);
    EXPECT_EQ(BGP_EXTENDED_COMMUNITY_OPAQUE_LOAD_BALANCE, lba.sub_type);
    EXPECT_FALSE(lba.l2_source_address);
    EXPECT_TRUE(lba.l2_destination_address);
    EXPECT_FALSE(lba.l3_source_address);
    EXPECT_TRUE(lba.l3_destination_address);
    EXPECT_FALSE(lba.l4_protocol);
    EXPECT_TRUE(lba.l4_source_port);
    EXPECT_FALSE(lba.l4_destination_port);
    EXPECT_FALSE(lba.reserved1);

    EXPECT_EQ(0, lba.reserved2);

    EXPECT_FALSE(lba.source_bias);
    EXPECT_EQ(0, lba.reserved3);

    EXPECT_EQ(0, lba.reserved4);
    EXPECT_EQ(0, lba.reserved5);
    EXPECT_EQ(0, lba.reserved6);

    // Reconstruct community from the attribute and verify data
    EXPECT_EQ(data, LoadBalance(lba).GetExtCommunity());

    autogen::LoadBalanceType item;
    lba.encode(item);
    EXPECT_FALSE(item.load_balance_fields.l2_source_address);
    EXPECT_TRUE(item.load_balance_fields.l2_destination_address);
    EXPECT_FALSE(item.load_balance_fields.l3_source_address);
    EXPECT_TRUE(item.load_balance_fields.l3_destination_address);
    EXPECT_FALSE(item.load_balance_fields.l4_protocol);
    EXPECT_TRUE(item.load_balance_fields.l4_source_port);
    EXPECT_FALSE(item.load_balance_fields.l4_destination_port);
    EXPECT_EQ("field-hash", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    EXPECT_EQ(lba, LoadBalance(item).ToAttribute());
    EXPECT_EQ(data, LoadBalance(item).GetExtCommunity());
    EXPECT_EQ("loadbalance: L2DA L3DA L4SP", LoadBalance(item).ToString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
