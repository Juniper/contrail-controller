/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <cmn/agent_cmn.h>
#include <pugixml/pugixml.hpp>

#include <base/logging.h>
#include <boost/bind.hpp>
#include "io/test/event_manager_test.h"

#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "vnc_cfg_types.h"
#include "bgp_schema_types.h"
#include "ifmap_node.h"
#include "ifmap_agent_table.h"

#include "xml/xml_pugi.h"

#include "controller/controller_ifmap.h" 

using namespace pugi;
const char *init_file_local;
bool ksync_init_local;

void RouterIdDepInit(Agent *agent) {

}

class ControlNodeMockIFMapXmppPeer {
public:
    ControlNodeMockIFMapXmppPeer(XmppChannel *channel) : channel_ (channel), rx_count_(0) {
        channel->RegisterReceive(xmps::CONFIG,
                                 boost::bind(&ControlNodeMockIFMapXmppPeer::ReceiveUpdate,
                                 this, _1));
    }

    void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
    }    

    bool SendUpdate(uint8_t *msg, size_t size) {
        if (channel_ && 
            (channel_->GetPeerState() == xmps::READY)) {
            return channel_->Send(msg, size, xmps::CONFIG,
                   boost::bind(&ControlNodeMockIFMapXmppPeer::WriteReadyCb, this, _1));
        }
    }

    void WriteReadyCb(const boost::system::error_code &ec) {
    }

    size_t Count() const { return rx_count_; }
    virtual ~ControlNodeMockIFMapXmppPeer() { }
private:
    XmppChannel *channel_;
    size_t rx_count_;
};


class AgentIFMapXmppUnitTest : public ::testing::Test { 
protected:

    virtual void SetUp() {
        client = TestInit(init_file_local, ksync_init_local);
        Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.1", 0);
        //Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.2", 1);

        server_shutdown[0] = server_shutdown[1] = 0;
        thread_ = new ServerThread(&evm_);

        thread_->Start();
        //Ask agent to connect to us
        Agent::GetInstance()->controller()->Connect();

        //xs[0].reset(new XmppServer(&evm_, XmppInit::kControlNodeJID));
        xs[0] = new XmppServer(&evm_, XmppInit::kControlNodeJID);
        xs[0]->Initialize(XMPP_SERVER_PORT, false);
        client->WaitForIdle();
        //xs[1].reset(new XmppServer(&evm_, XmppInit::kControlNodeJID));
        //xs[1]->Initialize(0, false);

        XmppConnectionSetUp();
        client->WaitForIdle();
        AddArp("10.1.1.254", "0a:0b:0c:0d:0e:0f",
               Agent::GetInstance()->fabric_interface_name().c_str());
    }

    virtual void TearDown() {
        XmppClient *xc = Agent::GetInstance()->controller_ifmap_xmpp_client(0);
        XmppClient *xc_dns = Agent::GetInstance()->dns_xmpp_client(0);
        if (xc)
            xc->ConfigUpdate(new XmppConfigData());
        if (xc_dns)
            xc_dns->ConfigUpdate(new XmppConfigData());
        //xc = Agent::GetInstance()->controller_ifmap_xmpp_client(1);
        //xc_dns = Agent::GetInstance()->dns_xmpp_client(1);
        //if (xc)
        //    xc->ConfigUpdate(new XmppConfigData());
        //if (xc_dns)
        //    xc_dns->ConfigUpdate(new XmppConfigData());

        mock_ifmap_peer[0].reset();
        //mock_ifmap_peer[1].reset();
        client->WaitForIdle();

        xs[0]->Shutdown();
        //xs[1]->Shutdown();
        client->WaitForIdle();

        Agent::GetInstance()->controller()->DisConnect();
        client->WaitForIdle();
        TaskScheduler::GetInstance()->Stop();
        Agent::GetInstance()->controller()->multicast_cleanup_timer().cleanup_timer_->Fire();
        Agent::GetInstance()->controller()->unicast_cleanup_timer().cleanup_timer_->Fire();
        Agent::GetInstance()->controller()->config_cleanup_timer().cleanup_timer_->Fire();
        TaskScheduler::GetInstance()->Start();
        client->WaitForIdle();
        Agent::GetInstance()->controller()->Cleanup();
        client->WaitForIdle();

        TcpServerManager::DeleteServer(xs[0]);
        //TcpServerManager::DeleteServer(xs[0].get());
        //TcpServerManager::DeleteServer(xs[1].get());
        TcpServerManager::DeleteServer(Agent::GetInstance()->controller_ifmap_xmpp_client(0));
        //TcpServerManager::DeleteServer(Agent::GetInstance()->controller_ifmap_xmpp_client(1));
        TcpServerManager::DeleteServer(Agent::GetInstance()->dns_xmpp_client(0));
        //TcpServerManager::DeleteServer(Agent::GetInstance()->dns_xmpp_client(1));
        xs[0] = NULL;
        //xs[0].reset();
        //xs[1].reset();
        client->WaitForIdle();

        evm_.Shutdown();
        client->WaitForIdle();
        thread_->Join();
        client->WaitForIdle();

        DelArp("10.1.1.254", "0a:0b:0c:0d:0e:0f",
               Agent::GetInstance()->fabric_interface_name().c_str());
        TestShutdown();
        client->WaitForIdle();
        delete client;
        delete thread_;
    }

    void XmppConnectionSetUp() {

        //Agent::GetInstance()->set_controller_ifmap_xmpp_port(xs[0]->GetPort(), 0);
        //Agent::GetInstance()->set_controller_ifmap_xmpp_port(xs[1]->GetPort(), 1);


        for(int id = 0; id < 1; id++) {
            WAIT_FOR(100, 10000, ((sconnection[id] =
                xs[id]->FindConnection(boost::asio::ip::host_name())) != NULL));

            mock_ifmap_peer[id].reset(new 
                ControlNodeMockIFMapXmppPeer(sconnection[id]->ChannelMux()));
        }

    }

    EventManager evm_;

    ServerThread *thread_;

    // 0 - primary 1 - secondary
    //auto_ptr<XmppServer> xs[2];
    XmppServer *xs[2];

    XmppConnection *sconnection[2];

    auto_ptr<ControlNodeMockIFMapXmppPeer> mock_ifmap_peer[2];
    int server_shutdown[2];
};

namespace {

TEST_F(AgentIFMapXmppUnitTest, LinkTest) {
#if 0
    IFMapNode *node;
    AgentXmppChannel *peer;
    IFMapAgentTable *table;
    int id;

    // Wait for the connection to be established
    WAIT_FOR(100, 10000, (sconnection[0]->GetStateMcState() == xmsm::ESTABLISHED));
    WAIT_FOR(100, 10000, (sconnection[1]->GetStateMcState() == xmsm::ESTABLISHED));

    struct PortInfo input1[] = {
        {"vnet4", 4, "4.1.1.1", "00:00:00:04:04:04", 4, 4},
        {"vnet5", 5, "5.1.1.1", "00:00:00:05:05:05", 5, 5},
        {"vnet6", 6, "6.1.1.1", "00:00:00:06:06:06", 6, 6},
        {"vnet7", 7, "7.1.1.1", "00:00:00:07:07:07", 7, 7},
    };

    struct PortInfo input2[] = {
        {"vnet4", 4, "4.1.1.1", "00:00:00:04:04:04", 4, 4},
        {"vnet5", 5, "5.1.1.1", "00:00:00:05:05:05", 5, 5},
    };

    CreateVmportEnv(input1, 4, 0);
    client->WaitForIdle();

    // Lookup in config db
    IFMapTable::RequestKey *req_key = new IFMapTable::RequestKey;

    req_key->id_type = "virtual-machine";
    req_key->id_name = "vm4";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vm4");

    req_key->id_type = "virtual-network";
    req_key->id_name = "vn4";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vn4");

    req_key->id_type = "routing-instance";
    req_key->id_name = "vrf4";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vrf4");

    req_key->id_type = "virtual-machine-interface";
    req_key->id_name = "vnet4";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vnet4");

    client->WaitForIdle();

    //Lookup in oper db
    WAIT_FOR(100, 10000, (VnFind(4) != false));
    WAIT_FOR(100, 10000, (VmFind(4) != false));
    WAIT_FOR(100, 10000, (VmPortFind(4) != false));
    WAIT_FOR(100, 10000, (VrfFind("vrf4") != false));

    //Look for VRF in Vn
    VnEntry *oper_vn = VnGet(4);
    VrfEntry *oper_vrf = VrfGet("vrf4");
    EXPECT_EQ(oper_vn->GetVrf(), oper_vrf);
    
    //DeleteVmportEnv(input2, 2, 0);
    client->WaitForIdle();

    //Bring down the primary
    id = Agent::GetInstance()->ifmap_active_xmpp_server_index();

    server_shutdown[id] = 1;
    xs[id]->Shutdown();

    client->WaitForIdle();

    //Wait for the stale cleaner to kickinn
    sleep(15);
    client->WaitForIdle();

    // Lookup in config db
    req_key = new IFMapTable::RequestKey;

    req_key->id_type = "virtual-machine";
    req_key->id_name = "vm4";
    node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key);
    EXPECT_TRUE(node == NULL);

    req_key->id_type = "virtual-network";
    req_key->id_name = "vn4";
    node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key);
    EXPECT_TRUE(node == NULL);

    req_key->id_type = "routing-instance";
    req_key->id_name = "vrf4";
    node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key);
    EXPECT_TRUE(node == NULL);

    req_key->id_type = "virtual-machine-interface";
    req_key->id_name = "vnet4";
    node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key);
    EXPECT_TRUE(node == NULL);

    //Cleanup
    DeleteVmportEnv(input1, 4, 0);
    client->WaitForIdle();
#endif
}
}

int main(int argc, char **argv) {
    int ret;
    GETUSERARGS();
    init_file_local = init_file;
    ksync_init_local = ksync_init;

    ret = RUN_ALL_TESTS();
    return ret;
}
