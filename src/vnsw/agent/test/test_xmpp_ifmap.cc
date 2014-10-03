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

#include "xml/xml_pugi.h"

#include "controller/controller_ifmap.h" 

using namespace pugi;
const char *init_file_local;
bool ksync_init_local;

void RouterIdDepInit(Agent *agent) {

    // Parse config and then connect
    Agent::GetInstance()->controller()->Connect();
}


class ControlNodeMockIFMapXmppPeer {
public:
    ControlNodeMockIFMapXmppPeer(XmppChannel *channel) :
        channel_ (channel), rx_count_(0) {
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
        return false;
    }

    void WriteReadyCb(const boost::system::error_code &ec) {
    }

    size_t Count() const { return rx_count_; }
    virtual ~ControlNodeMockIFMapXmppPeer() {
        channel_->UnRegisterReceive(xmps::CONFIG);
        channel_ = NULL;
    }
private:
    XmppChannel *channel_;
    size_t rx_count_;
};


class AgentIFMapXmppUnitTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        client = TestInit(init_file_local, ksync_init_local);
        Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.1", 0);

        //Agent::GetInstance()->set_event_manager(&evm_);
        //thread_ = new ServerThread(Agent::GetInstance()->event_manager());
        thread_ = new ServerThread(&evm_);
        RouterIdDepInit(Agent::GetInstance());
        xs = new XmppServer(&evm_,
                            XmppInit::kControlNodeJID);
        //xs.reset(new XmppServer(Agent::GetInstance()->event_manager(), XmppInit::kControlNodeJID));

        xs->Initialize(XMPP_SERVER_PORT, false);
        client->WaitForIdle();
        thread_->Start();
        client->WaitForIdle();
        XmppConnectionSetUp();
        client->WaitForIdle();
        AddArp("10.1.1.254", "0a:0b:0c:0d:0e:0f",
               Agent::GetInstance()->fabric_interface_name().c_str());
    }

    virtual void TearDown() {
        XmppClient *xc = Agent::GetInstance()->controller_ifmap_xmpp_client(0);
        XmppClient *xc_dns = Agent::GetInstance()->dns_xmpp_client(0);
        xc->ConfigUpdate(new XmppConfigData());
        xc_dns->ConfigUpdate(new XmppConfigData());

        mock_ifmap_peer.reset();
        client->WaitForIdle();

        xs->Shutdown();
        client->WaitForIdle();

        Agent::GetInstance()->controller()->DisConnect();
        client->WaitForIdle();
        ShutdownAgentController(Agent::GetInstance());

        TcpServerManager::DeleteServer(xs);
        TcpServerManager::DeleteServer(Agent::GetInstance()->controller_ifmap_xmpp_client(0));
        TcpServerManager::DeleteServer(Agent::GetInstance()->dns_xmpp_client(0));
        xs = NULL;
        client->WaitForIdle();

        //Agent::GetInstance()->event_manager()->Shutdown();
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

    void NovaIntfAdd(int id, const char *name, const char *addr,
                    const char *mac) {
        CfgIntKey *key = new CfgIntKey(MakeUuid(id));
        CfgIntData *data = new CfgIntData();
        boost::system::error_code ec;
        IpAddress ip = Ip4Address::from_string(addr, ec);
        data->Init(nil_uuid(), nil_uuid(), nil_uuid(), name, ip.to_v4(),
                   Ip6Address(), mac, "", VmInterface::kInvalidVlanId,
                   CfgIntEntry::CfgIntVMPort, 0);

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(key);
        req.data.reset(data);
        Agent::GetInstance()->interface_config_table()->Enqueue(&req);
        usleep(1000);
    }

    void NovaIntfDel(int id) {
        CfgIntKey *key = new CfgIntKey(MakeUuid(id));

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(key);
        req.data.reset(NULL);
        Agent::GetInstance()->interface_config_table()->Enqueue(&req);
        usleep(1000);
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const char *local_address,
                                            const string &from,
                                            const string &to,
                                            bool isclient) {
        XmppChannelConfig *cfg = new XmppChannelConfig(isclient);
        cfg->endpoint.address(boost::asio::ip::address::from_string(address));
        cfg->endpoint.port(port);
        cfg->local_endpoint.address(boost::asio::ip::address::from_string(local_address));
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        return cfg;
    }

    void SendDocument(const pugi::xml_document &xdoc, ControlNodeMockIFMapXmppPeer *peer) {
        ostringstream oss;
        xdoc.save(oss);
        string msg = oss.str();
        uint8_t buf[4096];
        bzero(buf, sizeof(buf));
        memcpy(buf, msg.data(), msg.size());
        peer->SendUpdate(buf, msg.size());
    }

    xml_node MessageHeader(xml_document *xdoc, bool update) {
        xml_node msg = xdoc->append_child("iq");
        msg.append_attribute("type") = "set";
        string str(XmppInit::kControlNodeJID);
        str += "/";
        str += XmppInit::kConfigPeer;
        msg.append_attribute("from") = str.c_str();
        string to_str(boost::asio::ip::host_name());
        to_str += "/";
        to_str += XmppInit::kConfigPeer;
        msg.append_attribute("to") = to_str.c_str();

        xml_node node = msg.append_child("config");
        if (update == true) {
            return node.append_child("update");
        } else {
            return node.append_child("delete");
        }
    }

    void BuildCfgLinkMessage(xml_node &node, string type1, string name1, string type2, string name2) {

        xml_node link = node.append_child("link");
        xml_node tmp = link.append_child("node");
        tmp.append_attribute("type") = type1.c_str();
        tmp.append_child("name").text().set(name1.c_str());

        tmp = link.append_child("node");
        tmp.append_attribute("type") = type2.c_str();
        tmp.append_child("name").text().set(name2.c_str());
    }

    void BuildCfgNodeMessage(xml_node &node, string type, string name, int uuid, IFMapIdentifier *cfg) {

        xml_node tmp = node.append_child("node");
        tmp.append_attribute("type") = type.c_str();
        tmp.append_child("name").text().set(name.c_str());

        autogen::IdPermsType id;
        id.uuid.uuid_mslong = 0;
        id.uuid.uuid_lslong = uuid;

        cfg->SetProperty("id-perms", static_cast<AutogenProperty *>(&id));
        cfg->EncodeUpdate(&tmp);
    }

    void ClearCfgNodeMessage(xml_node &node, string type, string name, int uuid, IFMapIdentifier *cfg) {

        xml_node tmp = node.append_child("node");
        tmp.append_attribute("type") = type.c_str();
        tmp.append_child("name").text().set(name.c_str());

        cfg->ClearProperty("id-perms");
        cfg->EncodeUpdate(&tmp);
    }

    void XmppConnectionSetUp() {
        // server connection
        WAIT_FOR(100, 10000,
            ((sconnection = xs->FindConnection(
              boost::asio::ip::host_name())) != NULL));

        //Create control-node bgp mock peer 
        mock_ifmap_peer.reset(new ControlNodeMockIFMapXmppPeer(sconnection->ChannelMux()));
    }

    EventManager evm_;

    ServerThread *thread_;

    XmppConfigData *xmpps_cfg;

    XmppServer *xs;
    //auto_ptr<XmppServer> xs;

    XmppConnection *sconnection;

    auto_ptr<ControlNodeMockIFMapXmppPeer> mock_ifmap_peer;
};

namespace {

TEST_F(AgentIFMapXmppUnitTest, Test_setup_teardown) {
    //Verify call of setup and teardown
}

TEST_F(AgentIFMapXmppUnitTest, vntest) {
    IFMapNode *node;
    xml_document xdoc;

    // Wait for the connection to be established
    WAIT_FOR(100, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));

    xml_node xitems = MessageHeader(&xdoc, true);

    // "virtual-network" "update" message
    autogen::VirtualNetwork *vn = new autogen::VirtualNetwork();
    BuildCfgNodeMessage(xitems, "virtual-network", "vn1", 1, static_cast
                        <IFMapIdentifier *> (vn));
    SendDocument(xdoc, mock_ifmap_peer.get());
    client->WaitForIdle();

    // Lookup in config db
    IFMapTable::RequestKey *req_key = new IFMapTable::RequestKey;
    req_key->id_type = "virtual-network";
    req_key->id_name = "vn1";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(
                           Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vn1");

    //Lookup in oper db
    EXPECT_TRUE(VnFind(1));

    //Delete node
    xml_document del_xdoc;
    xitems = MessageHeader(&del_xdoc, false);
    BuildCfgNodeMessage(xitems, "virtual-network", "vn1", 1, static_cast
                        <IFMapIdentifier *> (vn));
    SendDocument(del_xdoc, mock_ifmap_peer.get());
    WAIT_FOR(1000, 10000, (VnFind(1) == false));
    delete req_key;
    delete vn;
}

TEST_F(AgentIFMapXmppUnitTest, vmtest) {
    IFMapNode *node;
    xml_document xdoc;

    // Wait for the connection to be established
    WAIT_FOR(100, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));

    xml_node xitems = MessageHeader(&xdoc, true);

    // "virtual-network" "update" message
    autogen::VirtualMachine *vm = new autogen::VirtualMachine();
    BuildCfgNodeMessage(xitems,"virtual-machine", "vm1", 1, static_cast <IFMapIdentifier *> (vm));
    SendDocument(xdoc, mock_ifmap_peer.get());
    client->WaitForIdle();

    // Lookup in config db
    IFMapTable::RequestKey *req_key = new IFMapTable::RequestKey;
    req_key->id_type = "virtual-machine";
    req_key->id_name = "vm1";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vm1");

    //Lookup in oper db
    EXPECT_TRUE(VmFind(1));

    xml_document del_xdoc;
    xitems = MessageHeader(&del_xdoc, false);
    BuildCfgNodeMessage(xitems,"virtual-machine", "vm1", 1, static_cast <IFMapIdentifier *> (vm));
    SendDocument(del_xdoc, mock_ifmap_peer.get());
    WAIT_FOR(1000, 10000, (VmFind(1) == false));
    delete vm;
    delete req_key;
}

TEST_F(AgentIFMapXmppUnitTest, vn_vm_vrf_test) {
    IFMapNode *node;
    xml_document xdoc;

    // Wait for the connection to be established
    WAIT_FOR(100, 10000, (sconnection->GetStateMcState() == xmsm::ESTABLISHED));

    xml_node xitems = MessageHeader(&xdoc, true);

    // "virtual-network" "update" message
    autogen::VirtualNetwork *vn = new autogen::VirtualNetwork();
    BuildCfgNodeMessage(xitems, "virtual-network", "vn1", 1, static_cast <IFMapIdentifier *> (vn));

    // "routing-instance" "update" message
    autogen::RoutingInstance *ri = new autogen::RoutingInstance();
    BuildCfgNodeMessage(xitems,"routing-instance", "vrf2", 2, static_cast <IFMapIdentifier *> (ri));

    // "virtual-machine" "update" message
    autogen::VirtualMachine *vm = new autogen::VirtualMachine();
    BuildCfgNodeMessage(xitems,"virtual-machine", "vm3", 3, static_cast <IFMapIdentifier *> (vm));

    // "virtual-machine-interface" "update" message
    autogen::VirtualMachineInterface *vmi = new autogen::VirtualMachineInterface();
    BuildCfgNodeMessage(xitems,"virtual-machine-interface", "vnet4", 4, static_cast <IFMapIdentifier *> (vmi));

    //vn-ri link
    BuildCfgLinkMessage(xitems, "virtual-network", "vn1", "routing-instance", "vrf2");
    //vm-vmi link
    BuildCfgLinkMessage(xitems, "virtual-machine", "vm3", "virtual-machine-interface", "vnet4");
    //vn-vmi link
    BuildCfgLinkMessage(xitems, "virtual-network", "vn1", "virtual-machine-interface", "vnet4");

    NovaIntfAdd(4, "vnet4", "1.1.1.1", "00:00:00:00:00:11");
    //Send the iq message
    SendDocument(xdoc, mock_ifmap_peer.get());
    client->WaitForIdle();

    // Lookup in config db
    IFMapTable::RequestKey *req_key = new IFMapTable::RequestKey;

    req_key->id_type = "virtual-machine";
    req_key->id_name = "vm3";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vm3");

    req_key->id_type = "virtual-network";
    req_key->id_name = "vn1";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vn1");

    req_key->id_type = "routing-instance";
    req_key->id_name = "vrf2";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vrf2");

    req_key->id_type = "virtual-machine-interface";
    req_key->id_name = "vnet4";
    WAIT_FOR(100, 10000, ((node = IFMapAgentTable::TableEntryLookup(Agent::GetInstance()->db(), req_key)) != NULL));
    EXPECT_EQ(node->name(), "vnet4");

    client->WaitForIdle();

    //Lookup in oper db
    WAIT_FOR(100, 10000, (VnFind(1) != false));
    WAIT_FOR(100, 10000, (VmFind(3) != false));
    WAIT_FOR(100, 10000, (VmPortFind(4) != false));
    WAIT_FOR(100, 10000, (VrfFind("vrf2") != false));

    //Look for VRF in Vn
    VnEntry *oper_vn = VnGet(1);
    VrfEntry *oper_vrf = VrfGet("vrf2");
    EXPECT_EQ(oper_vn->GetVrf(), oper_vrf);
    
    // Delete the VN to RI linka
    xml_document doc;
    xitems = MessageHeader(&doc, false);
    //vn-ri link
    BuildCfgLinkMessage(xitems, "virtual-network", "vn1", "routing-instance", "vrf2");
    SendDocument(doc, mock_ifmap_peer.get());
    client->WaitForIdle();

    //Look for VRF in Vn
    oper_vn = VnGet(1);
    WAIT_FOR(100, 10000, (oper_vn->GetVrf() != false));
    oper_vrf = VrfGet("vrf2");
    EXPECT_TRUE((oper_vrf != NULL));

    NovaIntfDel(4);
    //Send the iq message
    SendDocument(xdoc, mock_ifmap_peer.get());
    client->WaitForIdle();
    delete vn;
    delete ri;
    delete vm;
    delete vmi;
    delete req_key;
}

}

int main(int argc, char **argv) {
    GETUSERARGS();
    init_file_local = init_file;
    ksync_init_local = ksync_init;

    int ret = RUN_ALL_TESTS();
    return ret;
}
