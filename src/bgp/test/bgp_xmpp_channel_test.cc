/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <sstream>


#include "base/task_annotations.h"
#include "control-node/control_node.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"

using namespace std;
using namespace boost;
using namespace test;

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

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
    int GetTaskInstance() const { return 0; }
    MOCK_METHOD2(RegisterReceive, void(xmps::PeerId, ReceiveCb));
    MOCK_METHOD1(UnRegisterReceive, void(xmps::PeerId));
    MOCK_METHOD1(UnRegisterWriteReady, void(xmps::PeerId));
    std::string ToString() const { return string("fake"); }
    std::string StateName() const { return string("Established"); }

    xmps::PeerState GetPeerState() const { return xmps::READY; }
    std::string FromString() const  { return string("fake-from"); }
    const XmppConnection *connection() const { return NULL; }

    virtual std::string LastStateName() const {
        return "";
    }
    virtual std::string LastStateChangeAt() const {
        return "";
    }
    virtual std::string LastEvent() const {
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
    virtual std::string LastFlap() const {
        return "";
    }
    virtual std::string AuthType() const {
        return "";
    }
    virtual std::string PeerAddress() const {
        return "";
    }
    virtual void RegisterRxMessageTraceCallback(RxMessageTraceCb cb) {
        return;
    }
    virtual void RegisterTxMessageTraceCallback(TxMessageTraceCb cb) {
        return;
    }
};

class BgpXmppChannelMock : public BgpXmppChannel {
public:
    BgpXmppChannelMock(XmppChannel *channel, BgpServer *server,
                       BgpXmppChannelManager *manager) :
        BgpXmppChannel(channel, server, manager) , count_(0),
        receive_updates_queue_(
            TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"), 0,
            boost::bind(&BgpXmppChannelMock::ProcessUpdate, this, _1)) {
    }

    virtual ~BgpXmppChannelMock() {
    }

    bool ProcessUpdate(const XmppStanza::XmppMessage *msg) {
        ReceiveUpdate(msg);
        return true;
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_++;
        BgpXmppChannel::ReceiveUpdate(msg);
    }

    void Enqueue(XmppStanza::XmppMessage *message) {
        receive_updates_queue_.Enqueue(message);
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }

private:
    size_t count_;
    WorkQueue<const XmppStanza::XmppMessage *> receive_updates_queue_;
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *xserver, BgpServer *server)
        : BgpXmppChannelManager(xserver,server), count(0), server_(server) { }

    virtual ~BgpXmppChannelManagerMock() { }
    virtual void XmppHandleChannelEvent(XmppChannel *ch, xmps::PeerState st) {
        count++;
        BgpXmppChannelManager::XmppHandleChannelEvent(ch, st);
    }
    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        return new BgpXmppChannelMock(channel, server_, this);
    }

private:
    int count;
    BgpServer *server_;
};

class RoutingInstanceManagerTest : public RoutingInstanceMgr {
public:
    explicit RoutingInstanceManagerTest(BgpServer *server) :
        RoutingInstanceMgr(server) {
    }
    virtual ~RoutingInstanceManagerTest() { }
private:
};

class BgpMembershipManagerTest : public BgpMembershipManager {
public:
    explicit BgpMembershipManagerTest(BgpServer *server) :
        BgpMembershipManager(server), registered(false) {
    }
    virtual ~BgpMembershipManagerTest() {
    }
    MOCK_METHOD2(Unregister, void(IPeer *, BgpTable *));
    MOCK_METHOD4(
        Register, void(IPeer *, BgpTable *, const RibExportPolicy &, int));

    void MockRegister(IPeer *peer, BgpTable *table,
                      const RibExportPolicy &policy, int inst_id) {
        BgpMembershipManager::Register(peer, table, policy, inst_id);
    }
    void MockUnregister(IPeer *peer, BgpTable *table) {
        BgpMembershipManager::Unregister(peer, table);
    }

private:
    void TimerExpired(const boost::system::error_code &error);
    bool registered;
};

class BgpXmppChannelTest : public ::testing::Test {
public:
    bool PeerRegistered(BgpXmppChannel *channel, std::string instance_name,
                        bool check_registered) {
        RoutingInstanceMgr *instance_mgr = server_->routing_instance_mgr();
        RoutingInstance *rt_instance =
            instance_mgr->GetRoutingInstance(instance_name);
        EXPECT_FALSE(rt_instance == NULL);
        BgpTable *table = rt_instance->GetTable(Address::INET);
        bool ret =
            server_->membership_mgr()->IsRegistered(channel->Peer(), table);
        if (check_registered == false) ret = !ret;
        return ret;
    }


protected:
    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        BgpObjectFactory::Register<RoutingInstanceMgr>(
                boost::factory<RoutingInstanceManagerTest *>());
        BgpObjectFactory::Register<BgpMembershipManager>(
                boost::factory<BgpMembershipManagerTest *>());
        server_.reset(new BgpServer(&evm_));
        enc_.reset(new XmppDocumentMock("agent.contrailsystems.com"));

        // Allocate 3 mock XmppChannel to simulate 3 xmpp connections.
        a.reset(new XmppChannelMock);
        b.reset(new XmppChannelMock);
        c.reset(new XmppChannelMock);

        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
                BgpConfigManager::kMasterInstance, "", ""));
        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("blue",
                "target:1.2.3.4:1", "target:1.2.3.4:1"));
        red_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
                "red", "target:1:2", "target:1:2"));
        purple_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
                "purple", "target:1:2", "target:1:2"));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_->routing_instance_mgr()->CreateRoutingInstance(
                master_cfg_.get());
        server_->routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        server_->routing_instance_mgr()->CreateRoutingInstance(red_cfg_.get());
        server_->routing_instance_mgr()->CreateRoutingInstance(
                purple_cfg_.get());
        scheduler->Start();

        mgr_.reset(new BgpXmppChannelManagerMock(NULL, server_.get()));
    }

    virtual void TearDown() {
        BGP_DEBUG_UT("TearDown");
        server_->Shutdown();
        task_util::WaitForIdle();

        evm_.Shutdown();
    }

    void CountInternal(BgpXmppChannel *channel) {
        count++;
    }

    int Count(BgpXmppChannelManagerMock *mgr) {
        count = 0;
        mgr->VisitChannels(boost::bind(&BgpXmppChannelTest::CountInternal,
                                       this, _1));
        return count;
    }

    string FileRead(const string &filename) {
        string content;
        fstream file(filename.c_str(), fstream::in);
        if (!file) {
            BGP_DEBUG_UT("File not found : " << filename);
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

    std::auto_ptr<XmppStanza::XmppMessageIq> GetSubscribe(string rt_instance_name,
                                          bool subscribe) {
        std::auto_ptr<XmppStanza::XmppMessageIq> msg(AllocIq());
        pugi::xml_document *doc = subscribe ? enc_->SubscribeXmlDoc(rt_instance_name, 1) :
            enc_->UnsubscribeXmlDoc(rt_instance_name, 1);
        msg->action = string(subscribe ? "subscribe" : "unsubscribe");
        msg->node = rt_instance_name;
        msg->dom.reset(GetXmlDoc(doc));

        return msg;
    }

    std::auto_ptr<XmppStanza::XmppMessageIq> RouteAddMsg(string rt_instance_name,
                                         const string &ipa) {
        std::auto_ptr<XmppStanza::XmppMessageIq> msg(AllocIq());
        pugi::xml_document *doc = enc_->RouteAddXmlDoc(rt_instance_name, ipa);
        msg->dom.reset(GetXmlDoc(doc));
        msg->node = rt_instance_name;
        msg->is_as_node = true;
        return msg;
    }

    std::auto_ptr<XmppStanza::XmppMessageIq> RouteDelMsg(string rt_instance_name,
                                         const string &ipa) {
        std::auto_ptr<XmppStanza::XmppMessageIq> msg(AllocIq());
        pugi::xml_document *doc = enc_->RouteDeleteXmlDoc(rt_instance_name, ipa);
        msg->dom.reset(GetXmlDoc(doc));
        msg->node = rt_instance_name;
        msg->is_as_node = false;
        return msg;
    }

    BgpServer *server() { return server_.get(); }

    BgpXmppChannel *FindChannel(XmppChannel *ch) {
        return mgr_->FindChannel(ch);
    }

    void ReceiveUpdate(XmppChannelMock *channel, XmppStanza::XmppMessage *msg) {
        BgpXmppChannelMock *tmp =
            static_cast<BgpXmppChannelMock *>(FindChannel(channel));
        ASSERT_FALSE(tmp == NULL);
        tmp->Enqueue(msg);
        task_util::WaitForIdle();
    }

    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
    scoped_ptr<BgpInstanceConfigTest> purple_cfg_;
    scoped_ptr<BgpInstanceConfigTest> blue_cfg_;
    scoped_ptr<BgpInstanceConfigTest> red_cfg_;

    EventManager evm_;
    boost::scoped_ptr<BgpServer> server_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> mgr_;

    boost::scoped_ptr<XmppChannelMock> a;
    boost::scoped_ptr<XmppChannelMock> b;
    boost::scoped_ptr<XmppChannelMock> c;

    int count;

private:
    XmppStanza::XmppMessageIq *AllocIq() {
        XmppStanza::XmppMessageIq *msg = new XmppStanza::XmppMessageIq();
        msg->type = XmppStanza::IQ_STANZA;
        msg->iq_type = string("set");
        return msg;
    }

    XmlBase *GetXmlDoc(pugi::xml_document *xdoc) {
        ostringstream oss;
        xdoc->save(oss);
        XmlBase *impl =XmppStanza::AllocXmppXmlImpl(oss.str().c_str());
        return impl;
    }
    boost::scoped_ptr<XmppDocumentMock> enc_;
};

namespace {

// Test for positive cases of XMPP peer register, route add
// peer unregister.

TEST_F(BgpXmppChannelTest, Connection) {

    EXPECT_CALL(*(a.get()), RegisterReceive(xmps::BGP, _))
                .Times(1);
    EXPECT_CALL(*(b.get()), RegisterReceive(xmps::BGP, _))
                .Times(1);
    EXPECT_CALL(*(c.get()), RegisterReceive(xmps::BGP, _))
                .Times(1);

    // Generate 3 channel READY event to BgpXmppChannelManagerMock
    mgr_->XmppHandleChannelEvent(a.get(), xmps::READY);
    mgr_->XmppHandleChannelEvent(b.get(), xmps::READY);
    mgr_->XmppHandleChannelEvent(c.get(), xmps::READY);
    EXPECT_EQ(3, Count(mgr_.get()));

    EXPECT_CALL(*(a.get()), UnRegisterReceive(xmps::BGP))
                .Times(1);
    EXPECT_CALL(*(b.get()), UnRegisterReceive(xmps::BGP))
                .Times(1);
    EXPECT_CALL(*(c.get()), UnRegisterReceive(xmps::BGP))
                .Times(1);

    // Generate channel NON_READY event and verify that.
    mgr_->XmppHandleChannelEvent(c.get(), xmps::NOT_READY);
    task_util::WaitForIdle();
    delete FindChannel(c.get());
    mgr_->RemoveChannel(c.get());
    EXPECT_EQ(2, Count(mgr_.get()));

    BgpMembershipManagerTest *mock_manager =
        static_cast<BgpMembershipManagerTest *>(server_->membership_mgr());
    EXPECT_FALSE(mock_manager == NULL);
    EXPECT_CALL(*mock_manager, Register(_, _, _, _))
        .Times(4)
        .WillRepeatedly(Invoke(mock_manager,
                         &BgpMembershipManagerTest::MockRegister))
        ;
    EXPECT_CALL(*mock_manager, Unregister(_, _))
        .Times(4)
        .WillRepeatedly(Invoke(mock_manager,
                         &BgpMembershipManagerTest::MockUnregister))
        ;

    // subscribe to routing instance purple
    std::auto_ptr<XmppStanza::XmppMessageIq> msg;
    msg = GetSubscribe("purple", true);
    this->ReceiveUpdate(a.get(), msg.get());

    BgpXmppChannel *channel = this->FindChannel(a.get());
    ASSERT_FALSE(channel == NULL);

    std::string instname = msg->node;
    task_util::WaitForCondition(&evm_,
            boost::bind(&BgpXmppChannelTest::PeerRegistered, this,
                        channel, instname, true), 1 /* seconds */);

    // Publish route to instance 'purple'
    msg = RouteAddMsg("purple", "10.1.1.2");
    this->ReceiveUpdate(a.get(), msg.get());

    // Delete route from 'purple' instance
    msg = RouteDelMsg("purple", "10.1.1.2");
    this->ReceiveUpdate(a.get(), msg.get());

    // Unsubscribe from 'purple' instance
    msg = GetSubscribe("purple", false);
    this->ReceiveUpdate(a.get(), msg.get());

    // Wait until unregsiter go through
    instname = msg->node;
    task_util::WaitForCondition(&evm_,
            boost::bind(&BgpXmppChannelTest::PeerRegistered, this,
                        channel, instname, false), 1 /* seconds */);

    mgr_->XmppHandleChannelEvent(a.get(), xmps::NOT_READY);
    task_util::WaitForIdle();
    delete FindChannel(a.get());
    mgr_->RemoveChannel(a.get());
    mgr_->XmppHandleChannelEvent(b.get(), xmps::NOT_READY);
    task_util::WaitForIdle();
    delete FindChannel(b.get());
    mgr_->RemoveChannel(b.get());
    EXPECT_EQ(0, Count(mgr_.get()));
}

}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    log4cplus::Logger logger = log4cplus::Logger::getRoot();
    logger.setLogLevel(log4cplus::INFO_LOG_LEVEL);
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
