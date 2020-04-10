/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <test_cmn_util.h>
#include <filter/packet_header.h>
#include <oper/tag.h>
#include <algorithm>
#include <iterator>
using namespace std;

TestTag src[] = {
    {"Tag1", 1, (1 << TagEntry::kTagTypeBitShift) | 1},
    {"Tag2", 2, (2 << TagEntry::kTagTypeBitShift) | 2},
    {"Tag13", 13, 13}
};

TestTag dst[] = {
    {"Tag3", 3, (1 << TagEntry::kTagTypeBitShift) | 3},
    {"Tag4", 4, (2 << TagEntry::kTagTypeBitShift) | 4}
};

TestTag label[] = {
    {"label1", 100, 100},
};

class FirewallPolicy : public ::testing::Test {
public:
    virtual void SetUp()  {
        CreateTags(src, 3);
        CreateTags(dst, 2);
        CreateTags(label, 1);
        AddNode("config-root", "root1", 1);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelNode("config-root", "root1");
        DeleteTags(src, 3);
        DeleteTags(dst, 2);
        DeleteTags(label, 1);
        client->WaitForIdle();
    }

protected:
    void CreateTags(TestTag *tag, uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            AddTag(tag[i].name_.c_str(), tag[i].uuid_, tag[i].id_);
        }
    }

    void DeleteTags(TestTag *tag, uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            DelNode("tag", tag[i].name_.c_str());
        }
    }

    TagList BuildTagList(TestTag *tag, uint32_t count) {
        TagList t;
        for (uint32_t i = 0; i < count; i++) {
            t.push_back(tag[i].id_);
        }
        std::sort(t.begin(), t.end());
        return t;
    }
};


TEST_F(FirewallPolicy, Test1) {
    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle(120);

    EXPECT_TRUE(AclFind(1));

    DelNode("firewall-policy", "app1");
    client->WaitForIdle(120);

    EXPECT_FALSE(AclFind(1));
}

//Check addition of firewall ACL with
//tags
TEST_F(FirewallPolicy, Test2) {
    std::vector<std::string> match;

    AddFirewall("rule1", 1, match, src, 3, dst, 2, "pass");
    client->WaitForIdle();

    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle();

    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    AddFwRuleTagLink("rule1", src, 3);
    AddFwRuleTagLink("rule1", dst, 2);
    client->WaitForIdle();

    EXPECT_TRUE(AclFind(1));
    AclDBEntry *acl = AclGet(1);
    EXPECT_EQ(acl->Size(), 2U);

    const AddressMatch *am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags(), BuildTagList(src, 3));

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(1));
    EXPECT_EQ(am->tags(), BuildTagList(dst, 2));

    client->Reset();
    AddFirewall("rule1", 1, match, src, 3, dst, 2, "pass");
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 0U);

    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    client->WaitForIdle();
    EXPECT_EQ(acl->Size(), 0U);

    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    DelFwRuleTagLink("rule1", src, 3);
    DelFwRuleTagLink("rule1", dst, 2);
    client->WaitForIdle();
}

//Check if ACE entry gets modified once dst tags list
//is modified and same ACL change is notified
TEST_F(FirewallPolicy, Test3) {
    std::vector<std::string> match;

    AddFirewall("rule1", 1, match, src, 3, dst, 2, "pass");
    client->WaitForIdle();

    AddFwRuleTagLink("rule1", src, 3);
    AddFwRuleTagLink("rule1", dst, 2);

    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle();

    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    client->WaitForIdle();

    EXPECT_TRUE(AclFind(1));
    AclDBEntry *acl = AclGet(1);
    EXPECT_EQ(acl->Size(), 2U);

    const AddressMatch *am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags(), BuildTagList(src, 3));

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(1));
    EXPECT_EQ(am->tags(), BuildTagList(dst, 2));

    client->Reset();
    AddFirewall("rule1", 1, match, src, 3, dst, 1, "pass");
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 1U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags(), BuildTagList(src, 3));

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(1));
    EXPECT_EQ(am->tags(), BuildTagList(dst, 1));

    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    client->WaitForIdle();
    EXPECT_EQ(acl->Size(), 0U);

    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    DelFwRuleTagLink("rule1", src, 3);
    DelFwRuleTagLink("rule1", dst, 2);
    client->WaitForIdle();
}

//Check if service group config gets parsed correctly
TEST_F(FirewallPolicy, Test4) {
    std::vector<std::string> protocol;
    protocol.push_back("any");
    protocol.push_back("icmp");

    std::vector<uint16_t> port;
    port.push_back(1000);
    port.push_back(2000);

    AddServiceGroup("sg1", 1, protocol, port);

    AddNode("firewall-policy", "app1", 1);
    AddNode("firewall-rule", "rule1", 1);
    client->WaitForIdle();

    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    AddLink("firewall-rule", "rule1", "service-group", "sg1");
    client->WaitForIdle();

    AclDBEntry *acl = AclGet(1);
    EXPECT_EQ(acl->Size(), 1U);

    const ServiceGroupMatch *sg = dynamic_cast<const ServiceGroupMatch*>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(sg->size(), 2U);

    client->Reset();
    protocol.clear();
    protocol.push_back("all");
    AddServiceGroup("sg1", 1, protocol, port);
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 1U);

    sg = dynamic_cast<const ServiceGroupMatch*>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(sg->size(), 1U);

    DelNode("service-group", "sg1");
    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    DelLink("firewall-rule", "rule1", "service-group", "sg1");
    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    client->WaitForIdle();
}

//Check upon change of service-group ACE entry changes
//and also that ACL entry is notified
TEST_F(FirewallPolicy, Test5) {
    std::vector<std::string> protocol;
    protocol.push_back("any");
    protocol.push_back("17");

    std::vector<uint16_t> port;
    port.push_back(1000);
    port.push_back(2000);

    AddServiceGroup("sg1", 1, protocol, port);

    AddNode("firewall-policy", "app1", 1);
    AddNode("firewall-rule", "rule1", 1);
    client->WaitForIdle();

    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    AddLink("firewall-rule", "rule1", "service-group", "sg1");
    client->WaitForIdle();

    AclDBEntry *acl = AclGet(1);
    EXPECT_EQ(acl->Size(), 1U);

    const ServiceGroupMatch *sg = dynamic_cast<const ServiceGroupMatch*>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(sg->size(), 2U);

    client->Reset();
    //Dummy change
    AddServiceGroup("sg1", 1, protocol, port);
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 0U);

    sg = dynamic_cast<const ServiceGroupMatch*>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(sg->size(), 2U);

    DelNode("service-group", "sg1");
    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    DelLink("firewall-rule", "rule1", "service-group", "sg1");
    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    client->WaitForIdle();
}

//Packet match for ACL with combination of
//Tags and service-group
TEST_F(FirewallPolicy, Test6) {
    std::vector<std::string> match;

    AddFirewall("rule1", 1, match, src, 3, dst, 2, "pass");
    client->WaitForIdle();

    std::vector<std::string> protocol;
    protocol.push_back("Udp");
    protocol.push_back("uDp");

    std::vector<uint16_t> port;
    port.push_back(1000);
    port.push_back(2000);

    AddServiceGroup("sg1", 1, protocol, port);
    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle();

    AddFwRuleTagLink("rule1", src, 3);
    AddFwRuleTagLink("rule1", dst, 2);

    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    AddLink("firewall-rule", "rule1", "service-group", "sg1");
    client->WaitForIdle();

    AclDBEntry *acl = AclGet(1);

    PacketHeader *packet1 = new PacketHeader();
    packet1->protocol = 18;
    MatchAclParams m_acl;
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->protocol = 17;
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_port = 10;
    packet1->dst_port = 1000;
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_tags_ = BuildTagList(src, 3);
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->dst_tags_ = BuildTagList(dst, 2);
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->dst_tags_ = BuildTagList(dst, 2);
    packet1->dst_tags_.push_back(20);
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_tags_ = BuildTagList(src, 3);
    packet1->src_tags_.push_back(25);
    std::sort(packet1->src_tags_.begin(), packet1->src_tags_.end());
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->dst_port = 3000;
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    //Service group helper API adds port and port + 1
    //in list
    packet1->dst_port = 2001;
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->dst_port = 1000;
    packet1->src_tags_[2] = 100;
    std::sort(packet1->src_tags_.begin(), packet1->src_tags_.end());
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    //Swap the field and reverse ACL should match it
    packet1->src_port = 10;
    packet1->dst_port = 2000;
    packet1->src_tags_ = BuildTagList(dst, 2);
    packet1->dst_tags_ = BuildTagList(src, 3);
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    protocol.clear();
    port.clear();
    //Clear service group
    AddServiceGroup("sg1", 1, protocol, port);
    client->WaitForIdle();
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    delete packet1;
    DelNode("service-group", "sg1");
    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    DelLink("firewall-rule", "rule1", "service-group", "sg1");
    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    DelFwRuleTagLink("rule1", src, 3);
    DelFwRuleTagLink("rule1", dst, 2);
    client->WaitForIdle();
}

//Check if "match" tags gets built for ACE based on
//config
TEST_F(FirewallPolicy, Test7) {
    std::vector<std::string> match;
    match.push_back("application");

    AddFirewall("rule1", 1, match, src, 0, dst, 0, "pass");
    client->WaitForIdle();

    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle();

    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    client->WaitForIdle();

    EXPECT_TRUE(AclFind(1));
    AclDBEntry *acl = AclGet(1);
    EXPECT_EQ(acl->Size(), 2U);

    const TagsMatch *tm = dynamic_cast<const TagsMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(tm->size(), 1U);
    TagList tag_match;
    tag_match.push_back(TagTable::APPLICATION);
    EXPECT_EQ(tm->tag_list(), tag_match);

    client->Reset();
    AddFirewall("rule1", 1, match, src, 0, dst, 0, "pass");
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 0U);

    PacketHeader *packet1 = new PacketHeader();
    packet1->protocol = 18;
    MatchAclParams m_acl;
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_tags_.push_back(1);
    packet1->dst_tags_.push_back(2);
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_tags_.push_back(1 << TagEntry::kTagTypeBitShift | 0x2);
    packet1->dst_tags_.push_back(1 << TagEntry::kTagTypeBitShift | 0x3);
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_tags_.clear();
    packet1->dst_tags_.clear();
    packet1->src_tags_.push_back(1 << TagEntry::kTagTypeBitShift | 0x2);
    packet1->dst_tags_.push_back(1 << TagEntry::kTagTypeBitShift | 0x2);
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    //Change the tag match type
    match.push_back("tier");
    AddFirewall("rule1", 1, match, src, 0, dst, 0, "pass");
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 1U);

    tm = dynamic_cast<const TagsMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));

    tag_match.push_back(TagTable::TIER);
    EXPECT_EQ(tm->tag_list(), tag_match);

    packet1->src_tags_.push_back(2 << TagEntry::kTagTypeBitShift | 0x1);
    packet1->dst_tags_.push_back(2 << TagEntry::kTagTypeBitShift | 0x1);
    std::sort(packet1->src_tags_.begin(), packet1->src_tags_.end());
    std::sort(packet1->dst_tags_.begin(), packet1->dst_tags_.end());
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_tags_[1] = (2 << TagEntry::kTagTypeBitShift | 0x2);
    packet1->dst_tags_[1] = (2 << TagEntry::kTagTypeBitShift | 0x1);
    packet1->src_tags_.push_back(2 << TagEntry::kTagTypeBitShift | 0x3);
    packet1->dst_tags_.push_back(2 << TagEntry::kTagTypeBitShift | 0x3);
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    TagList swap = packet1->src_tags_;
    packet1->src_tags_ = packet1->dst_tags_;
    packet1->dst_tags_ = swap;
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    client->WaitForIdle();
    EXPECT_EQ(acl->Size(), 0U);

    delete packet1;
    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    client->WaitForIdle();
}

TEST_F(FirewallPolicy, Test8) {
    std::vector<std::string> match;

    AddFirewall("rule1", 1, match, src, 0, dst, 0, "pass");
    client->WaitForIdle();

    AddFirewall("rule2", 2, match, src, 0, dst, 0, "pass");
    client->WaitForIdle();

    AddFirewall("rule3", 3, match, src, 0, dst, 0, "pass");
    client->WaitForIdle();

    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle();

    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    AddFirewallPolicyRuleLink("fpfr2", "app1", "rule2", "abcd");
    AddFirewallPolicyRuleLink("fpfr3", "app1", "rule3", "1.0");
    client->WaitForIdle();

    EXPECT_TRUE(AclFind(1));
    AclDBEntry *acl = AclGet(1);
    EXPECT_EQ(acl->Size(), 6U);

    AddFirewallPolicyRuleLink("fpfr2", "app1", "rule2", "abc");
    client->WaitForIdle();
    EXPECT_EQ(acl->Size(), 4U);

    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    client->WaitForIdle();
    EXPECT_EQ(acl->Size(), 4U);

    DelFirewallPolicyRuleLink("fpfr2", "app1", "rule2");
    client->WaitForIdle();
    EXPECT_EQ(acl->Size(), 2U);

    DelFirewallPolicyRuleLink("fpfr3", "app1", "rule3");
    client->WaitForIdle();
    EXPECT_EQ(acl->Size(), 0U);

    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    DelNode("firewall-rule", "rule2");
    DelNode("firewall-rule", "rule3");
    client->WaitForIdle();
}

//Check tag value, upon explicit add of link from tag to fw_rule
TEST_F(FirewallPolicy, Test9) {
    std::vector<std::string> match;

    AddFirewall("rule1", 1, match, src, 3, dst, 2, "pass");
    client->WaitForIdle();

    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle();

    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    client->WaitForIdle();

    EXPECT_TRUE(AclFind(1));
    AclDBEntry *acl = AclGet(1);

    const AddressMatch *am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags().size(), 3U);

    client->Reset();
    AddLink("firewall-rule", "rule1", "tag", "Tag1");
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 0U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags(), BuildTagList(src, 3));

    client->Reset();
    AddLink("firewall-rule", "rule1", "tag", "Tag2");
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 0U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags(), BuildTagList(src, 3));

    client->Reset();
    DelLink("firewall-rule", "rule1", "tag", "Tag2");
    client->WaitForIdle();
    EXPECT_EQ(client->acl_notify(), 0U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags(), BuildTagList(src, 3));

    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    DelFwRuleTagLink("rule1", src, 3);
    client->WaitForIdle();
}

TEST_F(FirewallPolicy, Test10) {
    std::vector<std::string> match;

    AddFirewall("rule1", 1, "SrcAg", "DstAg", "pass");
    client->WaitForIdle();

    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle();

    struct TestIp4Prefix src_prefix[] = {
        { Ip4Address::from_string("24.1.1.0"), 24},
        { Ip4Address::from_string("16.1.1.0"), 16},
    };

    struct TestIp4Prefix dst_prefix[] = {
        { Ip4Address::from_string("24.1.1.0"), 24},
        { Ip4Address::from_string("16.1.1.0"), 16},
        { Ip4Address::from_string("8.1.1.0"), 8},
    };
    AddAddressGroup("SrcAg", 1, src_prefix, 2);
    AddAddressGroup("DstAg", 2, dst_prefix, 3);
    client->WaitForIdle();

    AddLink("firewall-rule", "rule1", "address-group", "SrcAg");
    AddLink("firewall-rule", "rule1", "address-group", "DstAg");
    client->WaitForIdle();

    AddFwRuleTagLink("rule1", label, 1);
    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    client->WaitForIdle();

    EXPECT_TRUE(AclFind(1));
    AclDBEntry *acl = AclGet(1);

    const AddressMatch *am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags().size(), 0U);
    EXPECT_EQ(am->ip_list_size(), 2U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(1));
    EXPECT_EQ(am->tags().size(), 0U);
    EXPECT_EQ(am->ip_list_size(), 3U);

    client->Reset();
    AddLink("address-group", "SrcAg", "tag", "label1");
    AddLink("address-group", "DstAg", "tag", "label1");
    client->WaitForIdle();
    EXPECT_GE(client->acl_notify(), 1U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags().size(), 1U);
    EXPECT_EQ(am->ip_list_size(), 2U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(1));
    EXPECT_EQ(am->tags().size(), 1U);
    EXPECT_EQ(am->ip_list_size(), 3U);

    PacketHeader *packet1 = new PacketHeader();
    packet1->src_ip = Ip4Address::from_string("17.1.1.1");
    packet1->dst_ip = Ip4Address::from_string("1.8.8.8");
    packet1->src_tags_.push_back(100);
    packet1->dst_tags_.push_back(100);
    MatchAclParams m_acl;
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_ip = Ip4Address::from_string("16.1.1.1");
    packet1->dst_ip = Ip4Address::from_string("8.8.8.8");
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_ip = Ip4Address::from_string("17.1.1.1");
    packet1->dst_ip = Ip4Address::from_string("1.8.8.8");
    packet1->src_tags_.clear();
    packet1->dst_tags_.clear();
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    DelLink("address-group", "SrcAg", "tag", "label1");
    DelLink("address-group", "DstAg", "tag", "label1");
    client->WaitForIdle();

    packet1->src_ip = Ip4Address::from_string("7.8.8.8");
    packet1->dst_ip = Ip4Address::from_string("7.8.8.8");
    packet1->src_tags_.clear();
    packet1->dst_tags_.clear();
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    DelNode("address-group", "SrcAg");
    DelNode("address-group", "DstAg");
    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    DelFwRuleTagLink("rule1", label, 1);
    DelLink("firewall-rule", "rule1", "address-group", "SrcAg");
    DelLink("firewall-rule", "rule1", "address-group", "DstAg");
    DelLink("address-group", "SrcAg", "tag", "label1");
    DelLink("address-group", "DstAg", "tag", "label1");
    delete packet1;
    client->WaitForIdle();
}

TEST_F(FirewallPolicy, Test11) {
    std::vector<std::string> match;

    AddFirewall("rule1", 1, "SrcAg", "DstAg", "pass");
    client->WaitForIdle();

    AddNode("firewall-policy", "app1", 1);
    client->WaitForIdle();

    struct TestIp4Prefix src_prefix[] = {
    };

    AddAddressGroup("SrcAg", 1, src_prefix, 0);
    AddAddressGroup("DstAg", 2, src_prefix, 0);
    client->WaitForIdle();

    AddLink("firewall-rule", "rule1", "address-group", "SrcAg");
    AddLink("firewall-rule", "rule1", "address-group", "DstAg");
    client->WaitForIdle();

    AddFwRuleTagLink("rule1", label, 1);
    AddFirewallPolicyRuleLink("fpfr1", "app1", "rule1", "abc");
    client->WaitForIdle();

    EXPECT_TRUE(AclFind(1));
    AclDBEntry *acl = AclGet(1);

    const AddressMatch *am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags().size(), 0U);
    EXPECT_EQ(am->ip_list_size(), 0U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(1));
    EXPECT_EQ(am->tags().size(), 0U);
    EXPECT_EQ(am->ip_list_size(), 0U);

    client->Reset();
    AddLink("address-group", "SrcAg", "tag", "label1");
    AddLink("address-group", "DstAg", "tag", "label1");
    client->WaitForIdle();
    EXPECT_GE(client->acl_notify(), 1U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(0));
    EXPECT_EQ(am->tags().size(), 1U);
    EXPECT_EQ(am->ip_list_size(), 0U);

    am = dynamic_cast<const AddressMatch *>(
            acl->GetAclEntryAtIndex(0)->Get(1));
    EXPECT_EQ(am->tags().size(), 1U);
    EXPECT_EQ(am->ip_list_size(), 0U);

    PacketHeader *packet1 = new PacketHeader();
    packet1->src_tags_.push_back(100);
    packet1->dst_tags_.push_back(100);
    MatchAclParams m_acl;
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_ip = Ip4Address::from_string("16.1.1.1");
    packet1->dst_ip = Ip4Address::from_string("8.8.8.8");
    EXPECT_TRUE(acl->PacketMatch(*packet1, m_acl, NULL));

    packet1->src_tags_.clear();
    packet1->dst_tags_.clear();
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    DelLink("address-group", "SrcAg", "tag", "label1");
    DelLink("address-group", "DstAg", "tag", "label1");
    client->WaitForIdle();

    //Address group is empty shouldnt match any traffic
    packet1->src_tags_.push_back(100);
    packet1->dst_tags_.push_back(100);
    EXPECT_FALSE(acl->PacketMatch(*packet1, m_acl, NULL));

    DelNode("firewall-policy", "app1");
    DelNode("firewall-rule", "rule1");
    DelNode("address-group", "SrcAg");
    DelNode("address-group", "DstAg");
    DelFirewallPolicyRuleLink("fpfr1", "app1", "rule1");
    DelFwRuleTagLink("rule1", label, 1);
    DelLink("firewall-rule", "rule1", "address-group", "SrcAg");
    DelLink("firewall-rule", "rule1", "address-group", "DstAg");
    DelLink("address-group", "SrcAg", "tag", "label1");
    DelLink("address-group", "DstAg", "tag", "label1");
    delete packet1;
    client->WaitForIdle();
}

int main (int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
