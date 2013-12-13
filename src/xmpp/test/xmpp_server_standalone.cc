/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/test/xmpp_sample_peer.h"
#include <fstream>
#include <boost/asio.hpp>
#include "base/timer.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"

#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_server.h"
#include "base/logging.h"

using namespace std;
using namespace boost::asio;

class ServerTest;
class XmppBgpMockPeer : public XmppSamplePeer {
public:
    XmppBgpMockPeer(XmppChannel *channel) : 
        XmppSamplePeer(channel), count_(0) {
    }
    virtual ~XmppBgpMockPeer() { }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }

private:
    size_t count_;
};

static string FileRead(const string &filename) {
    string content;
    fstream file(filename.c_str(), fstream::in);
    if (!file) {
        LOG(DEBUG, "File not found : " << filename);
        return content;
    }
    while (!file.eof()) {
        char piece[256];
        file.read(piece, sizeof(piece));
        content.append(piece, file.gcount());
    }
    file.close();
    return content;
}

class XmppPeerManagerMock ;

class ServerTest {
public:
    explicit ServerTest(XmppServer *server);
    ~ServerTest();

    void Run() {
        StartPeriodicMessageTimer();
    }
    void SetPeer(XmppBgpMockPeer *peer) { mock_peer_ = peer; }

private:
    bool SendTimerExpire();
    void StartPeriodicMessageTimer() {
        timer_->Start(1000, boost::bind(&ServerTest::SendTimerExpire, this));
    }

    XmppBgpMockPeer *mock_peer_;
    int count_;
    Timer *timer_;
    std::auto_ptr<XmppPeerManagerMock> manager_;
};

class XmppPeerManagerMock : public XmppPeerManager {
public:
    XmppPeerManagerMock(XmppServer *server, ServerTest *test) : 
        XmppPeerManager(server, NULL), test_(test) { }

    virtual void XmppHandleConnectionEvent(XmppChannel *ch, xmps::PeerState st) {
        if (st == xmps::READY) {
            XmppBgpMockPeer *mock_peer = new XmppBgpMockPeer(ch);
            test_->SetPeer(mock_peer);
        } else {
            test_->SetPeer(NULL);
        }
        XmppPeerManager::XmppHandleConnectionEvent(ch, st);
    }

private:
    ServerTest *test_;
};

#define XMPP_CONTROL_SERV   "bgp.contrail.com"
#define XMPP_CONTROL_NODE   "bgp-node.contrail.com"
#define XMPP_SERVER_PORT    5288 

ServerTest::ServerTest(XmppServer *server) 
    : mock_peer_(NULL), count_(0),
      timer_(TimerManager::CreateTimer(*server->event_manager()->io_service(),
                                       "Server timer")),
    manager_(new XmppPeerManagerMock(server, this)) {
}

ServerTest::~ServerTest() {
    TimerManager::DeleteTimer(timer_);
}

bool ServerTest::SendTimerExpire() {
    count_++;
    if (mock_peer_ == NULL) {
        LOG(DEBUG, "Connection not found");
    } else {
        string data = FileRead("controller/src/xmpp/testdata/pubsub_sub.xml");
        uint8_t buf[4096];
        memcpy(buf, data.data(), data.size()); 
        bool ret = mock_peer_->SendUpdate(buf, data.size());
        if (ret == false) {
            LOG(DEBUG, "send failed");
        }
    }
    LOG(DEBUG, "Timer expired "<< count_ << "th times.");
    return true;
}

int main() {
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    EventManager evm;
    XmppServer *server = new XmppServer(&evm);
    auto_ptr<XmppInit> init(new XmppInit());

    XmppChannelConfig cfg(false);
    cfg.endpoint.address(ip::address::from_string("127.0.0.1"));
    cfg.endpoint.port(XMPP_SERVER_PORT);
    cfg.ToAddr = "";
    cfg.FromAddr = XMPP_CONTROL_SERV;
    cfg.NodeAddr = XMPP_CONTROL_NODE;

    init->AddXmppChannelConfig(&cfg);
    init->InitServer(server, XMPP_SERVER_PORT, false);

    ServerTest test(server);
    test.Run();

    evm.Run();
    return 0;
}
