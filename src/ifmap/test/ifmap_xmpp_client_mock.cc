/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/test/ifmap_xmpp_client_mock.h"

#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_channel.h"
#include "xmpp/xmpp_init.h"

using namespace std;

const string IFMapXmppClientMock::kDefaultXmppLocalAddr = "127.0.0.1";
const string IFMapXmppClientMock::kDefaultXmppServerName = "bgp.contrail.com";
const string IFMapXmppClientMock::kDefaultXmppServerAddr = "127.0.0.1";
const string IFMapXmppClientMock::kDefaultXmppServerConfigName =
    "bgp.contrail.com/config";
const string IFMapXmppClientMock::kDefaultOutputFile = "/tmp/output.txt";

IFMapXmppClientMock::IFMapXmppClientMock(EventManager *evm,
        int srv_port, const string &name, const string &output_file_path,
        const string &laddr, const string &xmpp_srv_name,
        const string &xmpp_srv_addr, const string &xmpp_srv_cfg_name)
    : XmppClient(evm), count_(0), os_(&fb_), name_(name),
      local_addr_(laddr), xmpp_server_name_(xmpp_srv_name),
      xmpp_server_addr_(xmpp_srv_addr),
      xmpp_server_config_name_(xmpp_srv_cfg_name) {

    XmppChannelConfig *channel_config =
        CreateXmppChannelCfg(xmpp_server_addr_.c_str(), local_addr_.c_str(),
                             srv_port, xmpp_server_name_, name_);
    XmppConfigData *config = new XmppConfigData();
    config->AddXmppChannelConfig(channel_config);
    ConfigUpdate(config);
    assert(output_file_path.size() != 0);
    fb_.open(output_file_path.c_str(), ios::out);
    assert(fb_.is_open());
}

IFMapXmppClientMock::~IFMapXmppClientMock() {
    XmppChannel *channel = FindChannel(xmpp_server_name_);
    if (channel)
        channel->UnRegisterWriteReady(xmps::CONFIG);
    fb_.close();
}

void IFMapXmppClientMock::RegisterWithXmpp() {
    XmppChannel *xmpp_channel = FindChannel(xmpp_server_name_);
    assert(xmpp_channel);
    xmpp_channel->RegisterReceive(xmps::CONFIG,
        boost::bind(&IFMapXmppClientMock::ReceiveUpdate, this, _1));
}

void IFMapXmppClientMock::UnRegisterWithXmpp() {
    XmppChannel *xmpp_channel = FindChannel(xmpp_server_name_);
    assert(xmpp_channel);
    xmpp_channel->UnRegisterReceive(xmps::CONFIG);
}

bool IFMapXmppClientMock::IsEstablished() {
    XmppConnection *xmpp_connection = FindConnection(xmpp_server_name_);
    if (xmpp_connection) {
        return (xmpp_connection->GetStateMcState() == xmsm::ESTABLISHED);
    }
    return false;
}

XmppChannelConfig *IFMapXmppClientMock::CreateXmppChannelCfg(
        const char *saddr, const char *laddr, int sport,
        const string &to, const string &from) {
    XmppChannelConfig *cfg = new XmppChannelConfig(true);

    cfg->endpoint.address(boost::asio::ip::address::from_string(saddr));
    cfg->endpoint.port(sport);
    cfg->local_endpoint.address(boost::asio::ip::address::from_string(laddr));
    cfg->ToAddr = to;
    cfg->FromAddr = from;
    return cfg;
}

void IFMapXmppClientMock::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
    ++count_;

    assert(msg->type == XmppStanza::IQ_STANZA);
    XmlBase *impl = msg->dom.get();
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);

    // Append the received message into the buffer
    stringstream ss;
    pugi->PrintDocFormatted(ss);
    recv_buffer_ += ss.str();
}

void IFMapXmppClientMock::WriteReadyCb(const boost::system::error_code &ec) {
    return;
}

void IFMapXmppClientMock::SendDocument(const pugi::xml_document &xdoc) {
    ostringstream oss;
    xdoc.save(oss);
    string msg = oss.str();

    XmppChannel *channel = FindChannel(xmpp_server_name_);
    assert(channel);
    if (channel->GetPeerState() == xmps::READY) {
        channel->Send(reinterpret_cast<const uint8_t *>(msg.data()),
            msg.length(), xmps::CONFIG,
            boost::bind(&IFMapXmppClientMock::WriteReadyCb, this, _1));
    }
}

pugi::xml_node IFMapXmppClientMock::PubSubHeader(pugi::xml_document *xdoc) {
    pugi::xml_node iq = xdoc->append_child("iq");
    iq.append_attribute("type") = "set";
    iq.append_attribute("from") = name_.c_str();
    iq.append_attribute("to") = xmpp_server_config_name_.c_str();
    pugi::xml_node pubsub = iq.append_child("pubsub");
    pubsub.append_attribute("xmlns") = XmppInit::kPubSubNS;
    return pubsub;
}

void IFMapXmppClientMock::SendConfigSubscribe() {
    pugi::xml_document xdoc;
    pugi::xml_node pubsub = PubSubHeader(&xdoc);
    pugi::xml_node subscribe = pubsub.append_child("subscribe");
    string iqnode = std::string("virtual-router:") + std::string(name_);
    subscribe.append_attribute("node") = iqnode.c_str();
    SendDocument(xdoc);
}

void IFMapXmppClientMock::SendVmConfigSubscribe(string vm_name) {
    pugi::xml_document xdoc;
    pugi::xml_node pubsub = PubSubHeader(&xdoc);
    pugi::xml_node subscribe = pubsub.append_child("subscribe");
    string iqnode = std::string("virtual-machine:") + std::string(vm_name);
    subscribe.append_attribute("node") = iqnode.c_str();
    SendDocument(xdoc);
}

void IFMapXmppClientMock::SendVmConfigUnsubscribe(string vm_name) {
    pugi::xml_document xdoc;
    pugi::xml_node pubsub = PubSubHeader(&xdoc);
    pugi::xml_node subscribe = pubsub.append_child("unsubscribe");
    string iqnode = std::string("virtual-machine:") + std::string(vm_name);
    subscribe.append_attribute("node") = iqnode.c_str();
    SendDocument(xdoc);
}

void IFMapXmppClientMock::ProcessNodeTag(pugi::xml_node xnode,
                                         ObjectSet *oset) {
    string ntype;

    // EG: <node type="virtual-router"> <name>a1s27</name>
    // Search for the 'type' attribute of the 'node' tag
    for (pugi::xml_attribute attr = xnode.first_attribute(); attr;
            attr = attr.next_attribute()) {
        string attr_name = attr.name();
        if (attr_name.compare("type") == 0) {
            ntype = attr.value();
            break;
        }
    }
    assert(ntype.size() != 0);

    // Find the child with the 'name' tag. Get the child of that child.
    // The value of this grand-child is the name of 'xnode'
    for (pugi::xml_node child = xnode.first_child(); child;
            child = child.next_sibling()) {
        string child_name = child.name();
        if (child_name.compare("name") == 0) {
            pugi::xml_node gchild = child.first_child();
            // Concatenate type and value to form unique string
            oset->insert(ntype.append(gchild.value()));
            break;
        }
    }
}

void IFMapXmppClientMock::XmlDocWalk(pugi::xml_node xnode, ObjectSet *oset) {
    string node_name = xnode.name();
    assert(node_name.compare("iq") == 0);

    pugi::xml_node cnode = xnode.first_child();
    node_name = cnode.name();
    assert(node_name.compare("config") == 0);

    cnode = cnode.first_child();
    node_name = cnode.name();
    assert((node_name.compare("update") == 0) ||
           (node_name.compare("delete") == 0));

    cnode = cnode.first_child();
    node_name = cnode.name();
    if (node_name.compare("node") == 0) {
        ProcessNodeTag(cnode, oset);
    } else if (node_name.compare("link") == 0) {
        ProcessLinkTag(cnode);
    } else {
        assert(0);
    }
}

bool IFMapXmppClientMock::OutputFileCompare(string master_file_path) {
    pugi::xml_document doc1, doc2;
    ObjectSet set1, set2;

    // Save the received contents in the user's file. This file is just for
    // reference.
    os_ << recv_buffer_;

    // Decode the contents of the received buffer into set1
    pugi::xml_parse_result result =
        doc1.load_buffer(recv_buffer_.c_str(), recv_buffer_.size());
    if (result) {
        for (pugi::xml_node child = doc1.first_child(); child;
            child = child.next_sibling()) {
            XmlDocWalk(child, &set1);
        }
    } else {
        cout << "Error loading received buffer. Buffer size is "
             << recv_buffer_.size() << endl;
        return false;
    }
    cout << "Set size is " << set1.size() << endl;
    PrintSet(set1);

    // Decode the contents of the master file into set2
    result = doc2.load_file(master_file_path.c_str());
    if (result) {
        for (pugi::xml_node child = doc2.first_child(); child;
            child = child.next_sibling()) {
            XmlDocWalk(child, &set2);
        }
    } else {
        cout << "Error loading " << master_file_path << endl;
        return false;
    }

    // == compares size and contents. Note, sets are sorted.
    if (set1 == set2) {
        return true;
    } else {
        cout << "Error: File compare mismatch. set1 is "
             << set1.size() << " set2 is " << set2.size() << endl;
        return false;
    }
}

void IFMapXmppClientMock::PrintSet(ObjectSet &oset) {
    int i = 0;
    cout << "Set size is " << oset.size() << endl;
    for (ObjectSet::iterator it = oset.begin(); it != oset.end(); ++it) {
        cout << i++ << ") " << *it << endl;
    }
}

