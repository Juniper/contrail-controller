/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include <fstream>

#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_xmpp_channel.h"
#include "xml/xml_pugi.h"
#include "testing/gunit.h"

using std::auto_ptr;
using std::ifstream;
using std::istreambuf_iterator;
using std::string;
using pugi::xml_node;

class XmppChannelMock : public XmppChannel {
public:
    XmppChannelMock() { }
    virtual ~XmppChannelMock() { }
    void Close() { }
    void CloseComplete() { }
    bool IsCloseInProgress() const { return false; }
    bool Send(const uint8_t *, size_t, xmps::PeerId, SendReadyCb) {
        return true;
    }
    void RegisterReceive(xmps::PeerId id, ReceiveCb callback) { }
    void UnRegisterReceive(xmps::PeerId id) { }
    void UnRegisterWriteReady(xmps::PeerId id) { }
    string ToString() const { return string("fake"); }
    string StateName() const { return string("Established"); }

    xmps::PeerState GetPeerState() const { return xmps::READY; }
    string FromString() const  { return string("fake-from"); }
    string AuthType() const { return "NIL"; }
    string PeerAddress() const { return "127.0.0.1"; }
    const XmppConnection *connection() const { return NULL; }

    virtual string LastStateName() const {
        return "";
    }
    virtual string LastStateChangeAt() const {
        return "";
    }
    virtual string LastEvent() const {
        return "";
    }
    virtual uint32_t rx_open() const {
        return 0;
    }
    virtual uint32_t rx_close() const {
        return 0;
    }
    virtual uint32_t rx_update() const {
        return 0;
    }
    virtual uint32_t rx_keepalive() const {
        return 0;
    }
    virtual uint32_t tx_open() const {
        return 0;
    }
    virtual uint32_t tx_close() const {
        return 0;
    }
    virtual uint32_t tx_update() const {
        return 0;
    }
    virtual uint32_t tx_keepalive() const {
        return 0;
    }
    virtual uint32_t FlapCount() const {
        return 0;
    }
    virtual string LastFlap() const {
        return "";
    }
    virtual void RegisterRxMessageTraceCallback(RxMessageTraceCb cb) {
        return;
    }
};

class BgpXmppChannelMock : public BgpXmppChannel {
public:
    BgpXmppChannelMock(XmppChannel *channel, BgpServer *server)
        : BgpXmppChannel(channel, server) {
    }

    virtual boost::asio::ip::tcp::endpoint endpoint() const {
        return boost::asio::ip::tcp::endpoint();
    }

private:
    virtual bool GetMembershipInfo(const string &vrf_name, int *instance_id) {
        *instance_id = 1;
        return true;
    }
};

class BgpXmppParseTest : public ::testing::Test {
protected:
    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    BgpXmppParseTest()
        : server_(&evm_),
          impl_(XmppXmlImplFactory::Instance()->GetXmlImpl()),
          pugi_(static_cast<XmlPugi *>(impl_.get())),
          channel_(new XmppChannelMock()),
          bx_channel_(new BgpXmppChannelMock(channel_.get(), &server_)) {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
        bx_channel_->set_peer_closed(true);
    }

    bool ProcessItem(const xml_node &item) {
        return bx_channel_->ProcessItem("blue", item, true);
    }

    bool ProcessMcastItem(const xml_node &item) {
        return bx_channel_->ProcessMcastItem("blue", item, true);
    }

    bool ProcessInet6Item(const xml_node &item) {
        return bx_channel_->ProcessInet6Item("blue", item, true);
    }

    bool ProcessEnetItem(const xml_node &item) {
        return bx_channel_->ProcessEnetItem("blue", item, true);
    }

    EventManager evm_;
    BgpServer server_;
    auto_ptr<XmlBase> impl_;
    XmlPugi *pugi_;
    auto_ptr<XmppChannel> channel_;
    auto_ptr<BgpXmppChannel> bx_channel_;
};

// Error in parsing message, XML document is fine.
TEST_F(BgpXmppParseTest, InetItemError1) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet_item_1.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessItem(item));
}

// Error in NLRI address family.
TEST_F(BgpXmppParseTest, InetItemError2) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet_item_2.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessItem(item));
}

// Error in NLRI address string.
TEST_F(BgpXmppParseTest, InetItemError3) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet_item_3.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessItem(item));
}

// Error in nexthop address.
TEST_F(BgpXmppParseTest, InetItemError4) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet_item_4.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessItem(item));
}

// Error in nexthop address - address is 0.0.0.0.
TEST_F(BgpXmppParseTest, InetItemError5) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet_item_5.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessItem(item));
}

// Error in nexthop - list is empty.
TEST_F(BgpXmppParseTest, InetItemError6) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet_item_6.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessItem(item));
}

// Error in parsing message, XML document is fine.
TEST_F(BgpXmppParseTest, Inet6ItemError1) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet6_item_1.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessInet6Item(item));
}

// Error in NLRI address family.
TEST_F(BgpXmppParseTest, Inet6ItemError2) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet6_item_2.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessInet6Item(item));
}

// Error in NLRI address string.
TEST_F(BgpXmppParseTest, Inet6ItemError3) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet6_item_3.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessInet6Item(item));
}

// Error in nexthop address.
TEST_F(BgpXmppParseTest, Inet6ItemError4) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet6_item_4.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessInet6Item(item));
}

// Error in nexthop address - address is 0.0.0.0.
TEST_F(BgpXmppParseTest, Inet6ItemError5) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet6_item_5.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessInet6Item(item));
}

// Error in nexthop - list is empty.
TEST_F(BgpXmppParseTest, Inet6ItemError6) {
     string data = FileRead("controller/src/bgp/testdata/bad_inet6_item_6.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessInet6Item(item));
}

// Error in parsing message, XML document is fine.
TEST_F(BgpXmppParseTest, McastItemError1) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_1.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in NLRI address family.
TEST_F(BgpXmppParseTest, McastItemError2) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_2.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in NLRI subsequent address family.
TEST_F(BgpXmppParseTest, McastItemError3) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_3.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in NLRI group string.
TEST_F(BgpXmppParseTest, McastItemError4) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_4.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in NLRI source string.
TEST_F(BgpXmppParseTest, McastItemError5) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_5.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in nexthop list - more than 1 element.
TEST_F(BgpXmppParseTest, McastItemError6) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_6.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in nexthop label.
TEST_F(BgpXmppParseTest, McastItemError7) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_7.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in nexthop address.
TEST_F(BgpXmppParseTest, McastItemError8) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_8.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in NLRI group string - address is 0.0.0.0.
TEST_F(BgpXmppParseTest, McastItemError9) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_9.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in nexthop address - address is 0.0.0.0.
TEST_F(BgpXmppParseTest, McastItemError10) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_10.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in nexthop list - 0 elements.
TEST_F(BgpXmppParseTest, McastItemError11) {
     string data = FileRead("controller/src/bgp/testdata/bad_mcast_item_11.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessMcastItem(item));
}

// Error in parsing message, XML document is fine.
TEST_F(BgpXmppParseTest, EnetItemError1) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_1.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in NLRI address family.
TEST_F(BgpXmppParseTest, EnetItemError2) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_2.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in NLRI subsequent address family.
TEST_F(BgpXmppParseTest, EnetItemError3) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_3.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in NLRI mac string.
TEST_F(BgpXmppParseTest, EnetItemError4) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_4.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in NLRI address string - missing /.
TEST_F(BgpXmppParseTest, EnetItemError5) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_5.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in NLRI address string - bad IPv4 address.
TEST_F(BgpXmppParseTest, EnetItemError6) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_6.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in NLRI address string - bad IPv6 address.
TEST_F(BgpXmppParseTest, EnetItemError7) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_7.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in NLRI address string - bad other address.
TEST_F(BgpXmppParseTest, EnetItemError8) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_8.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in nexthop address.
TEST_F(BgpXmppParseTest, EnetItemError9) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_9.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in replicator address.
TEST_F(BgpXmppParseTest, EnetItemError10) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_10.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in nexthop address - address is 0.0.0.0.
TEST_F(BgpXmppParseTest, EnetItemError11) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_11.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in replicator address - address is 0.0.0.0.
TEST_F(BgpXmppParseTest, EnetItemError12) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_12.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

// Error in nexthop - list is empty.
TEST_F(BgpXmppParseTest, EnetItemError13) {
     string data = FileRead("controller/src/bgp/testdata/bad_enet_item_13.xml");
     impl_->LoadDoc(data);
     xml_node item = pugi_->FindNode("item");
     EXPECT_FALSE(ProcessEnetItem(item));
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
