/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "testing/gunit.h"

#include "vnsw/agent/filter/acl_entry.h"
#include "vnsw/agent/filter/acl_entry_spec.h"
#include "vnsw/agent/filter/packet_header.h"
#include "vnsw/agent/filter/traffic_action.h"
#include "vnsw/agent/oper/mirror_table.h"

#include "net/address.h"

namespace {
class AclEntryTest : public ::testing::Test {
protected:
};

TEST_F(AclEntryTest, Basic) {
    AclEntrySpec ae_spec1;
    ae_spec1.id = 1;
    ae_spec1.src_ip_addr = IpAddress::from_string("1.1.1.1");
    ae_spec1.src_ip_mask = IpAddress::from_string("255.255.255.255");
    ae_spec1.src_addr_type = AddressMatch::IP_ADDR; 
    RangeSpec protocol;
    protocol.min = 10;
    protocol.max = 10;
    ae_spec1.protocol.push_back(protocol);
    RangeSpec port;
    port.min = 10;
    port.max = 100;
    ae_spec1.dst_port.push_back(port);
    ActionSpec action;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::PASS;
    //ae_spec1.permit_deny = true;
    ae_spec1.action_l.push_back(action);

    AclEntry *entry1 = new AclEntry();
    entry1->PopulateAclEntry(ae_spec1);

    PacketHeader *packet1 = new PacketHeader();
    packet1->src_ip = 0x01010102;
    packet1->dst_ip = 0xFFFFFF00;
    packet1->protocol = 10;
    packet1->dst_port = 99;

    AclEntry::ActionList al;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());

    packet1->src_ip = 0x01010101;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(1U, al.size());
    AclEntry::ActionList::iterator ial;
    ial = al.begin();
    EXPECT_EQ(SimpleAction::PASS, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    // EXPECT_TRUE(entry1->Match(packet1));

    packet1->src_ip = 0x01010101;
    packet1->protocol = 11;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010101;
    packet1->protocol = 10;
    packet1->dst_port = 101;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010101;
    packet1->protocol = 9;
    packet1->dst_port = 101;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010101;
    packet1->protocol = 9;
    packet1->dst_port = 100;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    delete packet1;
    delete entry1;
}

TEST_F(AclEntryTest, SubnetAddress) {
    AclEntrySpec ae_spec1;
    ae_spec1.id = 1;
    ae_spec1.src_ip_addr = IpAddress::from_string("1.1.1.0");
    ae_spec1.src_ip_mask = IpAddress::from_string("255.255.255.0");
    ae_spec1.src_addr_type = AddressMatch::IP_ADDR; 
    RangeSpec protocol;
    protocol.min = 10;
    protocol.max = 10;
    ae_spec1.protocol.push_back(protocol);
    RangeSpec port;
    port.min = 10;
    port.max = 100;
    ae_spec1.dst_port.push_back(port);
    ActionSpec action;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::PASS;
    ae_spec1.action_l.push_back(action);
    //ae_spec1.permit_deny = true;

    AclEntry *entry1 = new AclEntry();
    PacketHeader *packet1 = new PacketHeader();
    packet1->src_ip = 0x01010102;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    entry1->PopulateAclEntry(ae_spec1);
    AclEntry::ActionList al;
    al = entry1->PacketMatch(*packet1);
    AclEntry::ActionList::iterator ial;
    ial = al.begin();
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(TrafficAction::PASS, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    //EXPECT_TRUE(entry1->Match(packet1));

    packet1->src_ip = 0x01010101;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(SimpleAction::PASS, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    //EXPECT_TRUE(entry1->Match(packet1));

    packet1->src_ip = 0x01010110;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(SimpleAction::PASS, static_cast<SimpleAction *>(*ial.operator->())->GetAction());    
    //EXPECT_TRUE(entry1->Match(packet1));

    packet1->src_ip = 0x01010101;
    packet1->protocol = 11;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010102;
    packet1->protocol = 10;
    packet1->dst_port = 101;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010103;
    packet1->protocol = 9;
    packet1->dst_port = 101;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010104;
    packet1->protocol = 9;
    packet1->dst_port = 100;
    al = entry1->PacketMatch(*packet1);
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    delete packet1;
    delete entry1;
}


TEST_F(AclEntryTest, BasicAccept) {
    AclEntrySpec ae_spec1;

    ae_spec1.id = 1;
    ae_spec1.src_ip_addr = IpAddress::from_string("1.1.1.0");
    ae_spec1.src_ip_mask = IpAddress::from_string("255.255.255.0");
    ae_spec1.src_addr_type = AddressMatch::IP_ADDR; 
    RangeSpec protocol;
    protocol.min = 10;
    protocol.max = 10;
    ae_spec1.protocol.push_back(protocol);
    RangeSpec port;
    port.min = 10;
    port.max = 100;
    ae_spec1.dst_port.push_back(port);
    ae_spec1.terminal = true;
    //ae_spec1.permit_deny = true;
    ActionSpec action;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::PASS;
    ae_spec1.action_l.push_back(action);

    AclEntry *entry1 = new AclEntry();

    PacketHeader *packet1 = new PacketHeader();
    packet1->src_ip = 0x01010102;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    entry1->PopulateAclEntry(ae_spec1);

    AclEntry::ActionList al;
    al = entry1->PacketMatch(*packet1);
    AclEntry::ActionList::iterator ial;
    ial = al.begin();
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(SimpleAction::PASS, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    //entry1->FindPermitDenyAction(action);
    //EXPECT_TRUE(action);

    packet1->src_ip = 0x01010101;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(SimpleAction::PASS, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    //ASSERT_EQ(1, entry1->Match(packet1));
    //entry1->FindPermitDenyAction(action);
    //EXPECT_TRUE(action);

    packet1->src_ip = 0x01010110;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(SimpleAction::PASS, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    //EXPECT_TRUE(entry1->Match(packet1));

    packet1->src_ip = 0x01010101;
    packet1->protocol = 11;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010102;
    packet1->protocol = 10;
    packet1->dst_port = 101;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010103;
    packet1->protocol = 9;
    packet1->dst_port = 101;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010104;
    packet1->protocol = 9;
    packet1->dst_port = 100;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    delete packet1;
    delete entry1;
}



TEST_F(AclEntryTest, BasicDeny) {
    AclEntrySpec ae_spec1;

    ae_spec1.id = 1;
    ae_spec1.src_ip_addr = IpAddress::from_string("1.1.1.0");
    ae_spec1.src_ip_mask = IpAddress::from_string("255.255.255.0");
    ae_spec1.src_addr_type = AddressMatch::IP_ADDR; 
    RangeSpec protocol;
    protocol.min = 10;
    protocol.max = 10;
    ae_spec1.protocol.push_back(protocol);
    RangeSpec port;
    port.min = 10;
    port.max = 100;
    ae_spec1.dst_port.push_back(port);
    ae_spec1.terminal = true;
    ActionSpec action;
    action.ta_type = TrafficAction::SIMPLE_ACTION;
    action.simple_action = TrafficAction::DENY;
    ae_spec1.action_l.push_back(action);
    //ae_spec1.permit_deny = false;

    AclEntry *entry1 = new AclEntry();

    PacketHeader *packet1 = new PacketHeader();
    packet1->src_ip = 0x01010102;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    entry1->PopulateAclEntry(ae_spec1);
    AclEntry::ActionList al;
    al = entry1->PacketMatch(*packet1);
    AclEntry::ActionList::iterator ial;
    ial = al.begin();
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(SimpleAction::DENY, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    //EXPECT_TRUE(entry1->Match(packet1));
    //entry1->FindPermitDenyAction(action);
    //EXPECT_FALSE(action);

    packet1->src_ip = 0x01010101;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(SimpleAction::DENY, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    //EXPECT_TRUE(entry1->Match(packet1));
    //entry1->FindPermitDenyAction(action);
    //EXPECT_FALSE(action);

    packet1->src_ip = 0x01010110;
    packet1->protocol = 10;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(1U, al.size());
    EXPECT_EQ(SimpleAction::DENY, static_cast<SimpleAction *>(*ial.operator->())->GetAction());
    //EXPECT_TRUE(entry1->Match(packet1));
    //entry1->FindPermitDenyAction(action);
    //EXPECT_FALSE(action);

    packet1->src_ip = 0x01010101;
    packet1->protocol = 11;
    packet1->dst_port = 99;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010102;
    packet1->protocol = 10;
    packet1->dst_port = 101;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010103;
    packet1->protocol = 9;
    packet1->dst_port = 101;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    packet1->src_ip = 0x01010104;
    packet1->protocol = 9;
    packet1->dst_port = 100;
    al = entry1->PacketMatch(*packet1);
    ial = al.begin();
    EXPECT_EQ(0U, al.size());
    //EXPECT_FALSE(entry1->Match(packet1));

    delete packet1;
    delete entry1;
}


} // namespace

int main (int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
