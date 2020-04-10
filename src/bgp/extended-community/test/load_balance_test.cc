/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/load_balance.h"

#include "testing/gunit.h"

using namespace std;

class LoadBalanceTest : public ::testing::Test {
};

// Use load-balance options which are set by default
TEST_F(LoadBalanceTest, Default_1) {
    LoadBalance lb;
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();
    vector<string> lb_fields = vector<string>();

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lb.Type());
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lb.Subtype());

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lba.type);
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lba.sub_type);

    EXPECT_TRUE(lba.l3_source_address);
    lb_fields.push_back("l3-source-address");

    EXPECT_TRUE(lba.l3_destination_address);
    lb_fields.push_back("l3-destination-address");

    EXPECT_TRUE(lba.l4_protocol);
    lb_fields.push_back("l4-protocol");

    EXPECT_TRUE(lba.l4_source_port);
    lb_fields.push_back("l4-source-port");

    EXPECT_TRUE(lba.l4_destination_port);
    lb_fields.push_back("l4-destination-port");

    EXPECT_FALSE(lba.reserved1);

    EXPECT_EQ(0, lba.reserved2);

    EXPECT_FALSE(lba.source_bias);
    EXPECT_EQ(0, lba.reserved3);

    EXPECT_EQ(0, lba.reserved4);
    EXPECT_EQ(0, lba.reserved5);
    EXPECT_EQ(0, lba.reserved6);
    EXPECT_TRUE(lb.IsDefault());
    EXPECT_TRUE(lba.IsDefault());

    autogen::LoadBalanceType item;
    lba.Encode(&item);
    EXPECT_EQ(lb_fields, item.load_balance_fields.load_balance_field_list);
    EXPECT_EQ("field-hash", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    LoadBalance lb2 = LoadBalance(item);
    EXPECT_EQ(lba, lb2.ToAttribute());
    EXPECT_EQ("load-balance:field-hash", lb2.ToString());

    ShowLoadBalance show_load_balance;
    lb2.ShowAttribute(&show_load_balance);
    EXPECT_EQ("field-hash", show_load_balance.decision_type);
    EXPECT_EQ(lb_fields, show_load_balance.fields);
}

// Set all boolean options
TEST_F(LoadBalanceTest, AllBooleanSet_1) {
    LoadBalance::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0xF8, 0x00, 0x80, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();
    EXPECT_NE(lb, LoadBalance());
    EXPECT_NE(lba, LoadBalance().ToAttribute());
    vector<string> lb_fields = vector<string>();

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lb.Type());
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lb.Subtype());

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lba.type);
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lba.sub_type);

    lb_fields.push_back("l3-source-address");
    EXPECT_TRUE(lba.l3_source_address);

    lb_fields.push_back("l3-destination-address");
    EXPECT_TRUE(lba.l3_destination_address);

    lb_fields.push_back("l4-protocol");
    EXPECT_TRUE(lba.l4_protocol);

    lb_fields.push_back("l4-source-port");
    EXPECT_TRUE(lba.l4_source_port);

    lb_fields.push_back("l4-destination-port");
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
    EXPECT_FALSE(lb.IsDefault());
    EXPECT_FALSE(lba.IsDefault());

    autogen::LoadBalanceType item;
    lba.Encode(&item);
    EXPECT_EQ(lb_fields, item.load_balance_fields.load_balance_field_list);
    EXPECT_EQ("source-bias", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    LoadBalance lb2 = LoadBalance(item);
    EXPECT_EQ(lba, lb2.ToAttribute());
    EXPECT_EQ(data, lb2.GetExtCommunity());
    EXPECT_EQ("load-balance:source-bias", lb2.ToString());

    ShowLoadBalance show_load_balance;
    lb2.ShowAttribute(&show_load_balance);
    EXPECT_EQ("source-bias", show_load_balance.decision_type);
    EXPECT_EQ(0U, show_load_balance.fields.size());
}

// Reset all boolean options
TEST_F(LoadBalanceTest, AllBooleanReset_1) {
    LoadBalance::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();
    EXPECT_NE(lb, LoadBalance());
    EXPECT_NE(lba, LoadBalance().ToAttribute());
    vector<string> lb_fields = vector<string>();

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lb.Type());
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lb.Subtype());

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lba.type);
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lba.sub_type);
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
    EXPECT_FALSE(lb.IsDefault());
    EXPECT_FALSE(lba.IsDefault());

    autogen::LoadBalanceType item;
    lba.Encode(&item);
    EXPECT_EQ(lb_fields, item.load_balance_fields.load_balance_field_list);
    EXPECT_EQ("field-hash", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    LoadBalance lb2 = LoadBalance(item);

    // Since item's load_balance_fields is empty, expect default attribute.
    EXPECT_EQ(LoadBalance().ToAttribute(), lb2.ToAttribute());
    EXPECT_EQ(LoadBalance().GetExtCommunity(), lb2.GetExtCommunity());
    EXPECT_EQ("load-balance:field-hash", lb2.ToString());

    ShowLoadBalance show_load_balance;
    lb2.ShowAttribute(&show_load_balance);
    EXPECT_EQ("field-hash", show_load_balance.decision_type);

    // Expect default values set even though ext-community is fully reset.
    EXPECT_EQ(5U, show_load_balance.fields.size());
}

// Set all boolean options alternately
TEST_F(LoadBalanceTest, AlternateBooleanSet_1) {
    LoadBalance::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0xa8, 0x00, 0x80, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();
    EXPECT_NE(lb, LoadBalance());
    EXPECT_NE(lba, LoadBalance().ToAttribute());
    vector<string> lb_fields = vector<string>();

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lb.Type());
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lb.Subtype());

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lba.type);
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lba.sub_type);

    lb_fields.push_back("l3-source-address");
    EXPECT_TRUE(lba.l3_source_address);

    EXPECT_FALSE(lba.l3_destination_address);

    lb_fields.push_back("l4-protocol");
    EXPECT_TRUE(lba.l4_protocol);

    EXPECT_FALSE(lba.l4_source_port);

    lb_fields.push_back("l4-destination-port");
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
    EXPECT_FALSE(lb.IsDefault());
    EXPECT_FALSE(lba.IsDefault());

    autogen::LoadBalanceType item;
    lba.Encode(&item);
    EXPECT_EQ(lb_fields, item.load_balance_fields.load_balance_field_list);
    EXPECT_EQ("source-bias", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    LoadBalance lb2 = LoadBalance(item);
    EXPECT_EQ(lba, lb2.ToAttribute());
    EXPECT_EQ(data, lb2.GetExtCommunity());
    EXPECT_EQ("load-balance:source-bias", lb2.ToString());

    ShowLoadBalance show_load_balance;
    lb2.ShowAttribute(&show_load_balance);
    EXPECT_EQ("source-bias", show_load_balance.decision_type);
    EXPECT_EQ(0U, show_load_balance.fields.size());
}

// Set all boolean options alternately
TEST_F(LoadBalanceTest, AlternateBooleanSet_2) {
    LoadBalance::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0x50, 0x00, 0x00, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);
    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();
    EXPECT_NE(lb, LoadBalance());
    EXPECT_NE(lba, LoadBalance().ToAttribute());
    vector<string> lb_fields = vector<string>();

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lb.Type());
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lb.Subtype());

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lba.type);
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lba.sub_type);

    EXPECT_FALSE(lba.l3_source_address);

    lb_fields.push_back("l3-destination-address");
    EXPECT_TRUE(lba.l3_destination_address);

    EXPECT_FALSE(lba.l4_protocol);

    lb_fields.push_back("l4-source-port");
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
    lba.Encode(&item);
    EXPECT_EQ(lb_fields, item.load_balance_fields.load_balance_field_list);
    EXPECT_EQ("field-hash", item.load_balance_decision);
    EXPECT_FALSE(lb.IsDefault());
    EXPECT_FALSE(lba.IsDefault());

    // Reconstruct load-balance extended community from autogen item and verify
    LoadBalance lb2 = LoadBalance(item);
    EXPECT_EQ(lba, lb2.ToAttribute());
    EXPECT_EQ(data, lb2.GetExtCommunity());
    EXPECT_EQ("load-balance:field-hash", lb2.ToString());

    ShowLoadBalance show_load_balance;
    lb2.ShowAttribute(&show_load_balance);
    EXPECT_EQ("field-hash", show_load_balance.decision_type);
    EXPECT_EQ(lb_fields, show_load_balance.fields);
}

TEST_F(LoadBalanceTest, AttributeSetters) {
    LoadBalance::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
    LoadBalance lb(data);

    lb.SetL3SourceAddress(true);
    lb.SetL3DestinationAddress(true);
    lb.SetL4Protocol(true);
    lb.SetL4SourcePort(true);
    lb.SetL4DestinationPort(true);
    lb.SetSourceBias(true);

    const LoadBalance::LoadBalanceAttribute lba = lb.ToAttribute();
    vector<string> lb_fields = vector<string>();

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lb.Type());
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lb.Subtype());

    EXPECT_EQ(BgpExtendedCommunityType::Opaque, lba.type);
    EXPECT_EQ(BgpExtendedCommunityOpaqueSubType::LoadBalance, lba.sub_type);

    lb_fields.push_back("l3-source-address");
    EXPECT_TRUE(lba.l3_source_address);

    lb_fields.push_back("l3-destination-address");
    EXPECT_TRUE(lba.l3_destination_address);

    lb_fields.push_back("l4-protocol");
    EXPECT_TRUE(lba.l4_protocol);

    lb_fields.push_back("l4-source-port");
    EXPECT_TRUE(lba.l4_source_port);

    lb_fields.push_back("l4-destination-port");
    EXPECT_TRUE(lba.l4_destination_port);

    EXPECT_FALSE(lba.reserved1);

    EXPECT_EQ(0, lba.reserved2);

    EXPECT_TRUE(lba.source_bias);
    EXPECT_EQ(0, lba.reserved3);

    EXPECT_EQ(0, lba.reserved4);
    EXPECT_EQ(0, lba.reserved5);
    EXPECT_EQ(0, lba.reserved6);
    EXPECT_FALSE(lb.IsDefault());
    EXPECT_FALSE(lba.IsDefault());

    // All options are set by set APIs above
    LoadBalance::bytes_type data2 =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::LoadBalance,
            0xF8, 0x00, 0x80, 0x00, 0x00, 0x00 } };

    // Reconstruct community from the attribute and verify data
    EXPECT_EQ(data2, LoadBalance(lba).GetExtCommunity());

    autogen::LoadBalanceType item;
    lba.Encode(&item);
    EXPECT_EQ(lb_fields, item.load_balance_fields.load_balance_field_list);
    EXPECT_EQ("source-bias", item.load_balance_decision);

    // Reconstruct load-balance extended community from autogen item and verify
    LoadBalance lb2 = LoadBalance(item);
    EXPECT_EQ(lba, lb2.ToAttribute());
    EXPECT_EQ(data2, lb2.GetExtCommunity());
    EXPECT_EQ("load-balance:source-bias", lb2.ToString());

    ShowLoadBalance show_load_balance;
    lb2.ShowAttribute(&show_load_balance);
    EXPECT_EQ("source-bias", show_load_balance.decision_type);
    EXPECT_EQ(0U, show_load_balance.fields.size());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
