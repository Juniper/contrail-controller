/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

//  
//  xmpp_xml_test.cc
//  Test code for xml_base.h implementation

#include "xml/xml_base.h"
#include <fstream>
#include <sstream>
#include <boost/algorithm/string/erase.hpp>

#include "base/util.h"
#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class XmlBaseTest : public ::testing::Test {
protected:
    string FileRead(const string &filename) {
        string content;
        fstream file(filename.c_str(), fstream::in);
        while (!file.eof()) {
            char piece[256];
            file.read(piece, sizeof(piece));
            content.append(piece, file.gcount());
        }
        file.close();
        return content;
    }
 
    virtual void SetUp() {
        doc_ = XmppXmlImplFactory::Instance()->GetXmlImpl();
        data_doc_ = XmppXmlImplFactory::Instance()->GetXmlImpl();
        xmls_="";
    }
    virtual void TearDown() {
        delete doc_;
        delete data_doc_;
    }

    std::string xmls_;
    XmlBase *doc_, *data_doc_;
    uint8_t tt[256];
};

namespace {


TEST_F(XmlBaseTest, XmlDecode) {
     EXPECT_FALSE(doc_== NULL);
     xmls_ = FileRead("src/xml/testdata/xmpp_l3_vpn.xml");
     doc_->LoadDoc(xmls_);

     std::string key("iq");
     const char *val = doc_->ReadNode(key);
     val = doc_->ReadFirstAttrib(); 
     ASSERT_STREQ(val, "set");
     val = doc_->ReadNextAttrib();
     ASSERT_STREQ(val, "01020304abcd@domain.org");

     key = "to";
     val = doc_->ReadAttrib(key);
     ASSERT_STREQ(val, "network-control.domain.org");
     val = doc_->ReadNextAttrib();
     ASSERT_STREQ(val, "request1");

     doc_->RewindAttrib();
     val = doc_->ReadFirstAttrib();
     ASSERT_STREQ(val, "set");
     val = doc_->ReadNextAttrib();
     ASSERT_STREQ(val, "01020304abcd@domain.org");
     val = doc_->ReadNextAttrib();
     ASSERT_STREQ(val, "network-control.domain.org");
     val = doc_->ReadNextAttrib();
     ASSERT_STREQ(val, "request1");
     // check no more attributes
     val = doc_->ReadNextAttrib();
     ASSERT_STREQ(val, "");

     doc_->ReadNextNode();
     val = doc_->ReadFirstAttrib();
     ASSERT_STREQ(val, "");

     doc_->RewindDoc();
     val = doc_->ReadChildNode();
     val = doc_->ReadFirstAttrib(); 
     ASSERT_STREQ(val, "set");

     doc_->ReadChildNode();
     val = doc_->ReadFirstAttrib();
     ASSERT_STREQ(val, "http://jabber.org/protocol/pubsub");
     doc_->RewindNode();
     val = doc_->ReadFirstAttrib();
     ASSERT_STREQ(val, "http://jabber.org/protocol/pubsub");

     doc_->ReadChildNode();
     val = doc_->ReadFirstAttrib();
     ASSERT_STREQ(val, "01020304abcd:vpn-ip-address/32");
     
     key = "nlri";
     val = doc_->ReadNode(key);
     ASSERT_STREQ(val, "10.1.2.1/32");
     key = "label";
     val = doc_->ReadNode(key);
     ASSERT_STREQ(val, "10000");

     doc_->RewindDoc();
     val = doc_->ReadChildNodeName();
     ASSERT_STREQ(val, "iq");
     
     val = doc_->ReadChildNodeName();
     ASSERT_STREQ(val, "pubsub");
    
     val = doc_->ReadChildNodeName();
     ASSERT_STREQ(val, "publish");
 
     val = doc_->ReadChildNodeName();
     ASSERT_STREQ(val, "item");
   
     val = doc_->ReadChildNodeName();
     ASSERT_STREQ(val, "entry");
   
     val = doc_->ReadChildNodeName();
     ASSERT_STREQ(val, "nlri");
     val = doc_->ReadNodeValue();
     ASSERT_STREQ(val, "10.1.2.1/32");
     val = doc_->ReadFirstAttrib();
     ASSERT_STREQ(val, "1");
    
     val = doc_->ReadChildNode();
     ASSERT_STREQ(val, "10.1.2.1/32");
    
     val = doc_->ReadParentName();
     ASSERT_STREQ(val, "nlri");
   
     val = doc_->ReadNextNodeName();
     ASSERT_STREQ(val, "next-hop");

     val = doc_->ReadNextNodeName();
     ASSERT_STREQ(val, "version");
   
     val = doc_->ReadNextNodeName();
     ASSERT_STREQ(val, "label");

     val = doc_->ReadChildNode();
     ASSERT_STREQ(val, "10000");
};


TEST_F (XmlBaseTest, XmlEncode) {
    EXPECT_FALSE(doc_ == NULL);
    string result = "<node1 attrib1=\"ex1\" attrib2=\"ex2\"><child1 attrib3=\"ex3\" /> <child2>10.1.1.1</child2></node1>";

    doc_->LoadDoc(xmls_);
    doc_->AddNode("node1", "");

    doc_->AddAttribute("attrib1", "ex1");
    doc_->AddAttribute("attrib2", "ex2");
    doc_->AddChildNode("child1", "");
    doc_->AddAttribute("attrib3", "ex3");
    doc_->AddNode("child2", "10.1.1.1");

    stringstream ss; 
    doc_->PrintDoc(ss);
    string encode = ss.str();

    boost::algorithm::erase_all(result, " ");
    boost::algorithm::erase_all(encode, " ");
    ASSERT_STREQ(result.c_str(), encode.c_str());
};


TEST_F (XmlBaseTest, XmlIqEncode) {
    EXPECT_FALSE(doc_ == NULL);
    string result = "<iq type=\"set\" from=\"01020304abcd@domain.org\" to=\"network-control.domain.org\" id=\"sub1\"><pubsub xmlns=\"http://jabber.org/protocol/pubsub\"><subscribe node=\"vpn-customer-name\"/></pubsub></iq>";  

    doc_->LoadDoc(xmls_);
    doc_->AddNode("iq", "");
    doc_->AddAttribute("type", "set");
    doc_->AddAttribute("from", "01020304abcd@domain.org");
    doc_->AddAttribute("to", "network-control.domain.org");
    doc_->AddAttribute("id", "sub1");

    doc_->AddChildNode("pubsub", "");
    doc_->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");

    doc_->AddChildNodeAfter("pubsub", "subscribe", "");
    doc_->AddAttribute("node", "vpn-customer-name");

    stringstream ss; 
    doc_->PrintDoc(ss);
    string encode = ss.str();

    boost::algorithm::erase_all(result, " ");
    boost::algorithm::erase_all(encode, " ");
    ASSERT_STREQ(result.c_str(), encode.c_str());
};

TEST_F (XmlBaseTest, XmlAppendDoc) {
    EXPECT_FALSE(doc_ == NULL);
    string result = "<iq type=\"set\" from=\"01020304abcd@domain.org\" to=\"network-control.domain.org\" id=\"sub1\"><pubsub xmlns=\"http://jabber.org/protocol/pubsub\" /><publish node=\"01020304abcd:vpn-ip-address/32\"><item><entry xmlns=\"http://ietf.org/protocol/bgpvpn\" /></item></publish></iq>";  
    string msg1 = "<iq type=\"set\" from=\"01020304abcd@domain.org\" to=\"network-control.domain.org\" id=\"sub1\"><pubsub xmlns=\"http://jabber.org/protocol/pubsub\"></pubsub></iq>";  
    string msg2 = "<publish node=\"01020304abcd:vpn-ip-address/32\"><item><entry xmlns=\"http://ietf.org/protocol/bgpvpn\"></entry></item></publish>";

    doc_->LoadDoc(msg1);
    data_doc_->LoadDoc(msg2);

    doc_->AppendDoc("pubsub", data_doc_);

    stringstream ss; 
    doc_->PrintDoc(ss);
    string encode = ss.str();

    boost::algorithm::erase_all(result, " ");
    boost::algorithm::erase_all(encode, " ");
    ASSERT_STREQ(result.c_str(), encode.c_str());
};

} // namespace
int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
