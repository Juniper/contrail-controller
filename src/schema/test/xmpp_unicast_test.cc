/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "schema/xmpp_unicast_types.h"

#include <fstream>
#include <sstream>
#include <boost/algorithm/string/erase.hpp>
#include <pugixml/pugixml.hpp>
#include "ifmap/autogen.h"

#include "base/logging.h"
#include "base/util.h"
#include "testing/gunit.h"

using namespace std;
using namespace autogen;

class QueryTest : public ::testing::Test {
  protected:

    QueryTest() :xparser_(NULL) {
    }

    virtual void SetUp()  {
        xparser_.reset(new AutogenProperty());
    }

    pugi::xml_document xdoc_;
    auto_ptr<AutogenProperty> xparser_;
};

TEST_F(QueryTest, Decode) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/schema/testdata/bgp_l3vpn_unicast_1.xml");
    EXPECT_TRUE(result);

    pugi::xml_node node = xdoc_.first_child();
    if (node.type() != pugi::node_null) {
        EntryType::XmlParseProperty(node, &xparser_);

        EntryType *entryp_;
        entryp_ = (static_cast<EntryType *>(xparser_.get()));

        EXPECT_EQ(entryp_->nlri.af, 1);
        ASSERT_STREQ(entryp_->nlri.address.c_str(), "10.1.2.1/32");

        EXPECT_EQ(entryp_->next_hops.next_hop[0].af, 1);
        ASSERT_STREQ(entryp_->next_hops.next_hop[0].address.c_str(),
                     "infrastructure-ip-address");
        EXPECT_EQ(entryp_->next_hops.next_hop[0].label, 10000);
        EXPECT_EQ(entryp_->version, 1);
    }
    xdoc_.reset();
}

TEST_F(QueryTest, Encode) {
    EntryType entryp_;

    entryp_.version = 2;
    entryp_.nlri.af = 1;
    entryp_.nlri.address = "10.2.2.2";

    autogen::NextHopType item_nexthop;

    item_nexthop.af = 1;
    item_nexthop.address = "20.2.2.2";
    item_nexthop.label = 1000;
    entryp_.next_hops.next_hop.push_back(item_nexthop);

    pugi::xml_node node = xdoc_.root();
    pugi::xml_node node_p = node.append_child("entry");
    entryp_.Encode(&node_p);

    node = xdoc_.first_child();
    if (node.type() != pugi::node_null) {
        EntryType::XmlParseProperty(node, &xparser_);

        EntryType *entryp2_;
        entryp2_ = (static_cast<EntryType *>(xparser_.get()));

        EXPECT_EQ(entryp2_->nlri.af, 1);
        ASSERT_STREQ(entryp2_->nlri.address.c_str(), "10.2.2.2");
        EXPECT_EQ(entryp2_->next_hops.next_hop[0].af, 1);
        ASSERT_STREQ(entryp2_->next_hops.next_hop[0].address.c_str(),
                     "20.2.2.2");
        EXPECT_EQ(entryp2_->version, 2);
        EXPECT_EQ(entryp2_->next_hops.next_hop[0].label, 1000);
    }
    xdoc_.reset();
}

TEST_F(QueryTest, Decode2) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/schema/testdata/bgp_l3vpn_unicast_2.xml");
    EXPECT_TRUE(result);

    pugi::xml_node node = xdoc_.first_child();
    if (node.type() != pugi::node_null) {
        ItemsType::XmlParseProperty(node, &xparser_);

        ItemsType *items;
        ItemType *item;
        EntryType entry;
        items = (static_cast<ItemsType *>(xparser_.get()));

        int count=0;
        for (vector<ItemType>::iterator iter =items->item.begin();
                                        iter != items->item.end();
                                        ++iter)
        {
            item = &*iter;
            entry = item->entry;

            count++;
            if (count == 1) { 
                EXPECT_EQ(entry.nlri.af, 1);
                ASSERT_STREQ(entry.nlri.address.c_str(), "10.1.2.1/32");
                EXPECT_EQ(entry.next_hops.next_hop[0].af, 1);
                ASSERT_STREQ(
                    entry.next_hops.next_hop[0].address.c_str(), 
                    "infrastructure-ip-address");
                EXPECT_EQ(entry.version, 1);
                EXPECT_EQ(entry.next_hops.next_hop[0].label, 10000);
            }
            if (count == 2) { 
                EXPECT_EQ(entry.nlri.af, 1);
                ASSERT_STREQ(entry.nlri.address.c_str(), "11.1.2.1/32");
                EXPECT_EQ(entry.next_hops.next_hop[0].af, 1);
                ASSERT_STREQ(
                    entry.next_hops.next_hop[0].address.c_str(),
                    "20.2.2.2");
                EXPECT_EQ(entry.version, 1);
                EXPECT_EQ(entry.next_hops.next_hop[0].label, 2222);
            }
        }
    }
    xdoc_.reset();
}

TEST_F(QueryTest, Encode2) {
    ItemsType items_;
    ItemType item1_, item2_;

    item1_.entry.version = 2;
    item1_.entry.nlri.af = 1;
    item1_.entry.nlri.address = "10.2.2.2";

    autogen::NextHopType item1_nexthop;
    item1_nexthop.af = 1;
    item1_nexthop.address = "20.2.2.2";
    item1_nexthop.label = 1000;
    item1_.entry.next_hops.next_hop.push_back(item1_nexthop);

    item2_.entry.version = 1;
    item2_.entry.nlri.af = 2;
    item2_.entry.nlri.address = "11.1.1.1";

    autogen::NextHopType item2_nexthop;
    item2_nexthop.af = 1;
    item2_nexthop.address = "12.2.2.2";
    item2_nexthop.label = 2000;
    item2_.entry.next_hops.next_hop.push_back(item2_nexthop);

    items_.item.push_back(item1_);   
    items_.item.push_back(item2_);   

    pugi::xml_node node = xdoc_.root();
    pugi::xml_node node_p = node.append_child("items");
    items_.Encode(&node_p);

    node = xdoc_.first_child();
    if (node.type() != pugi::node_null) {
        ItemsType::XmlParseProperty(node, &xparser_);

        ItemsType *itemsg;
        ItemType *item;

        itemsg = (static_cast<ItemsType *>(xparser_.get()));
  
        int count=0;
        for (vector<ItemType>::iterator iter =itemsg->item.begin();
                                        iter != itemsg->item.end();
                                        ++iter)
        {
            item = &*iter;
            count++;

            if (count == 1) {
                EXPECT_EQ(item->entry.nlri.af, 1);
                ASSERT_STREQ(item->entry.nlri.address.c_str(), "10.2.2.2");
                EXPECT_EQ(item->entry.next_hops.next_hop[0].af, 1);
                ASSERT_STREQ(
                    item->entry.next_hops.next_hop[0].address.c_str(),
                    "20.2.2.2");
                EXPECT_EQ(item->entry.version, 2);
                EXPECT_EQ(item->entry.next_hops.next_hop[0].label,
                          1000);
            }
            if (count == 2) {
                EXPECT_EQ(item->entry.nlri.af, 2);
                ASSERT_STREQ(item->entry.nlri.address.c_str(), "11.1.1.1");
                EXPECT_EQ(item->entry.next_hops.next_hop[0].af, 1);
                ASSERT_STREQ(
                    item->entry.next_hops.next_hop[0].address.c_str(),
                    "12.2.2.2");
                EXPECT_EQ(item->entry.version, 1);
                EXPECT_EQ(item->entry.next_hops.next_hop[0].label,
                          2000);
            }
        }
    }
}

TEST_F(QueryTest, Decode_NextHopType) {
    pugi::xml_parse_result result =
            xdoc_.load_file("controller/src/schema/testdata/bgp_l3vpn_unicast_nh.xml");
    EXPECT_TRUE(result);

    pugi::xml_node node = xdoc_.first_child();
    if (node.type() != pugi::node_null) {
        NextHopType::XmlParseProperty(node, &xparser_);

        NextHopType *nh_;
        nh_ = (static_cast<NextHopType *>(xparser_.get()));

        EXPECT_EQ(nh_->af, 1);
        ASSERT_STREQ(nh_->address.c_str(), "10.1.2.2");
        EXPECT_EQ(nh_->label, 10000);
        ASSERT_STREQ(nh_->tunnel_encapsulation_list.tunnel_encapsulation[0].c_str(), "gre");
        ASSERT_STREQ(nh_->tunnel_encapsulation_list.tunnel_encapsulation[1].c_str(), "udp");
    }
    xdoc_.reset();
}


TEST_F(QueryTest, Encode_NextHopType) {

    NextHopType nh_;
    nh_.af = 1;
    nh_.address = "10.2.2.2";
    nh_.label = 2000;
    nh_.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");

    pugi::xml_parse_result result = xdoc_.load_buffer("", 0);
    EXPECT_TRUE(result);
    pugi::xml_node node = xdoc_.root();
    pugi::xml_node node_p = node.append_child("next-hop");
    nh_.Encode(&node_p);

    node = xdoc_.first_child();
    if (node.type() != pugi::node_null) {
        NextHopType::XmlParseProperty(node, &xparser_);

        NextHopType *nh2_;
        nh2_ = (static_cast<NextHopType *>(xparser_.get()));

        EXPECT_EQ(nh2_->af, 1);
        ASSERT_STREQ(nh2_->address.c_str(), "10.2.2.2");
        EXPECT_EQ(nh2_->label, 2000);
        ASSERT_STREQ(nh2_->tunnel_encapsulation_list.tunnel_encapsulation[0].c_str(), "gre");
    }
    xdoc_.reset();
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
