/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <pugixml/pugixml.hpp>

#include <base/logging.h>
#include <boost/bind.hpp>
#include <tbb/task.h>
#include <base/task.h>
#include "io/test/event_manager_test.h"

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "controller/controller_peer.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "openstack/instance_service_server.h"
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
        server_shutdown[0] = server_shutdown[1] = 0;
        Agent::GetInstance()->SetEventManager(&evm_);
        thread_ = new ServerThread(Agent::GetInstance()->GetEventManager());

        xs[0].reset(new XmppServer(Agent::GetInstance()->GetEventManager(), XmppInit::kControlNodeJID));
        xs[0]->Initialize(0, false);

        xs[1].reset(new XmppServer(Agent::GetInstance()->GetEventManager(), XmppInit::kControlNodeJID));
        xs[1]->Initialize(0, false);

        thread_->Start();
        XmppConnectionSetUp();
    }

    virtual void TearDown() {
        if (server_shutdown[0] == 0)
            xs[0]->Shutdown();
        if (server_shutdown[1] == 0)
            xs[1]->Shutdown();
        Agent::GetInstance()->controller()->DisConnect();
        client->WaitForIdle();
        Agent::GetInstance()->GetEventManager()->Shutdown();
        thread_->Join();
    }

    void XmppConnectionSetUp() {

        Agent::GetInstance()->SetXmppPort(xs[0]->GetPort(), 0);
        Agent::GetInstance()->SetXmppPort(xs[1]->GetPort(), 1);

        //Ask agent to connect to us
        Agent::GetInstance()->controller()->Connect();


        for(int id = 0; id < 2; id++) {
            WAIT_FOR(100, 10000, ((sconnection[id] =
                xs[id]->FindConnection(XmppInit::kFqnPrependAgentNodeJID + 
                                       boost::asio::ip::host_name())) != NULL));

            mock_ifmap_peer[id].reset(new 
                ControlNodeMockIFMapXmppPeer(sconnection[id]->ChannelMux()));
        }

    }

    EventManager evm_;

    ServerThread *thread_;

    // 0 - primary 1 - secondary
    auto_ptr<XmppServer> xs[2];

    XmppConnection *sconnection[2];

    auto_ptr<ControlNodeMockIFMapXmppPeer> mock_ifmap_peer[2];
    int server_shutdown[2];
};

namespace {

TEST_F(AgentIFMapXmppUnitTest, LinkTest) {
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
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vm4");

    req_key->id_type = "virtual-network";
    req_key->id_name = "vn4";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vn4");

    req_key->id_type = "routing-instance";
    req_key->id_name = "vrf4";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vrf4");

    req_key->id_type = "virtual-machine-interface";
    req_key->id_name = "vnet4";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), req_key)) != NULL));
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
    id = Agent::GetInstance()->GetXmppCfgServerIdx();

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
    node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), req_key);
    EXPECT_TRUE(node == NULL);

    req_key->id_type = "virtual-network";
    req_key->id_name = "vn4";
    node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), req_key);
    EXPECT_TRUE(node == NULL);

    req_key->id_type = "routing-instance";
    req_key->id_name = "vrf4";
    node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), req_key);
    EXPECT_TRUE(node == NULL);

    req_key->id_type = "virtual-machine-interface";
    req_key->id_name = "vnet4";
    node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->GetDB(), req_key);
    EXPECT_TRUE(node == NULL);
}
}

int main(int argc, char **argv) {
    int ret;
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->SetXmppServer("127.0.0.1", 0);
    Agent::GetInstance()->SetXmppServer("127.0.0.2", 1);
    ret = RUN_ALL_TESTS();
    return ret;
}
