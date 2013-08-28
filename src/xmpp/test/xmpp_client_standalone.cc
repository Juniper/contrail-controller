/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/test/xmpp_sample_peer.h"
#include <fstream>
#include <boost/asio.hpp>
#include "base/logging.h"
#include "base/timer.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"


using namespace std;
using namespace boost::asio;

static EventManager evm_;

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

class XmppBgpMockPeer : public XmppSamplePeer {
public:
    XmppBgpMockPeer(XmppChannelMux *channel)
        : XmppSamplePeer(channel) , count_(0) {
    }
 
    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *) {
        count_ ++;
    }
 
    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }

private:
    size_t count_;
};

class ClientTest {
public:
    explicit ClientTest(XmppClient *client, XmppInit *init) 
        : client_(client), mock_peer_(NULL), count_(0), init_(init),
          timer_(TimerManager::CreateTimer(
                      *client->event_manager()->io_service(), "Client timer")) {
    }

    ~ClientTest() {
        TimerManager::DeleteTimer(timer_);
    }

    void Run() {
        StartPeriodicMessageTimer();
    }

private:
    bool SendTimerExpire();
    void StartPeriodicMessageTimer() {
        timer_->Start(1000, boost::bind(&ClientTest::SendTimerExpire, this));
    }

    XmppClient *client_;
    XmppBgpMockPeer *mock_peer_;
    int count_;
    XmppInit *init_;
    Timer *timer_;
};

#define XMPP_CONTROL_SERV   "bgp.contrail.com"
#define SUB_ADDR            "agent@vnsw.contrailsystems.com"
#define XMPP_SERVER_PORT    5288

bool ClientTest::SendTimerExpire() {
    count_++;
    if (mock_peer_ == NULL) {
        XmppConnection *connection =
            client_->FindConnection(XMPP_CONTROL_SERV);
        if (connection) {
            mock_peer_ = new XmppBgpMockPeer(connection->ChannelMux());
        }
    } else {
        string data = FileRead("src/xmpp/testdata/pubsub_sub.xml");
        uint8_t buf[4096];
        memcpy(buf, data.data(), data.size()); 
        bool ret = mock_peer_->SendUpdate(buf, data.size());
        if (ret == false) {
            LOG(DEBUG, "send failed");
        }
    }
    LOG(DEBUG, "Timer expired "<< count_ << "th times.");
    if (count_ > 10) {
        client_->Shutdown();
        delete init_;
        exit(0);
    }
    return true;
}


int main() {
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    XmppClient *client = new XmppClient(&evm_);
    XmppChannelConfig *cfg = new XmppChannelConfig(true);
    XmppInit *init = new XmppInit();
 
    cfg->endpoint.address(ip::address::from_string("127.0.0.1"));
    cfg->endpoint.port(XMPP_SERVER_PORT);
    cfg->ToAddr = XMPP_CONTROL_SERV;
    cfg->FromAddr = SUB_ADDR;

    init->AddXmppChannelConfig(cfg);
    init->InitClient(client);

    ClientTest test(client, init);
    test.Run();

    evm_.Run();
    return 0;
}
