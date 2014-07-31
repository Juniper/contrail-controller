/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "services/dhcp_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "pkt/pkt_init.h"

using namespace boost::asio;
using boost::asio::ip::udp;

void DhcpProto::Shutdown() {
}

DhcpProto::DhcpProto(Agent *agent, boost::asio::io_service &io,
                     bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::DHCP, io),
    run_with_vrouter_(run_with_vrouter), ip_fabric_interface_(NULL),
    ip_fabric_interface_index_(-1), dhcp_server_socket_(io) {

    boost::system::error_code ec;
    dhcp_server_socket_.open(udp::v4(), ec);
    assert(ec == 0);
    dhcp_server_socket_.bind(udp::endpoint(udp::v4(), DHCP_SERVER_PORT), ec);
    if (ec) {
        DHCP_TRACE(Error, "Error creating DHCP socket : " << ec);
    }

    memset(ip_fabric_interface_mac_, 0, ETHER_ADDR_LEN);

    iid_ = agent->interface_table()->Register(
                  boost::bind(&DhcpProto::ItfNotify, this, _2));

    // For DHCP requests coming from VMs in default VRF, when the IP received
    // from Nova is 0, DHCP module acts DHCP relay and relays the request onto
    // the fabric VRF. Vrouter sends responses for these to vhost0 interface.
    // We listen on DHCP server port to receive these responses (check option 82
    // header to decide that it is a response for a relayed request) and send
    // the response to the VM.
    AsyncRead();
}

DhcpProto::~DhcpProto() {
    boost::system::error_code ec;
    dhcp_server_socket_.shutdown(udp::socket::shutdown_both, ec);
    if (ec) {
        DHCP_TRACE(Error, "Error shutting down DHCP socket : " << ec);
    }
    dhcp_server_socket_.close (ec);
    if (ec) {
        DHCP_TRACE(Error, "Error closing DHCP socket : " << ec);
    }
    agent_->interface_table()->Unregister(iid_);
    if (dhcp_server_read_buf_) delete [] dhcp_server_read_buf_;
}

void DhcpProto::AsyncRead() {
    dhcp_server_read_buf_ = new uint8_t[kDhcpMaxPacketSize];
    dhcp_server_socket_.async_receive_from(
                boost::asio::buffer(dhcp_server_read_buf_, kDhcpMaxPacketSize),
                remote_endpoint_,
                boost::bind(&DhcpProto::ReadHandler, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
}

void DhcpProto::ReadHandler(const boost::system::error_code &error,
                            std::size_t len) {
    if (!error) {
        SendDhcpIpc(dhcp_server_read_buf_, len);
    } else  {
        DHCP_TRACE(Error, "Error reading packet <" + error.message() + ">");
        if (error == boost::system::errc::operation_canceled) {
            return;
        }
        delete [] dhcp_server_read_buf_;
        dhcp_server_read_buf_ = NULL;
    }

    AsyncRead();
}

void DhcpProto::SendDhcpIpc(uint8_t *dhcp, std::size_t len) {
    DhcpVhostMsg *ipc = new DhcpVhostMsg(dhcp, len);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::DHCP, ipc);
}

ProtoHandler *DhcpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new DhcpHandler(agent(), info, io);
}

void DhcpProto::ItfNotify(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(NULL);
            set_ip_fabric_interface_index(-1);
        }
    } else {
        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(itf);
            set_ip_fabric_interface_index(itf->id());
            if (run_with_vrouter_) {
#if defined(__linux__)
                set_ip_fabric_interface_mac((char *)itf->mac().ether_addr_octet);
            } else {
                char mac[ETH_ALEN];
                memset(mac, 0, ETH_ALEN);
#elif defined(__FreeBSD__)
                set_ip_fabric_interface_mac((char *)itf->mac().octet);
            } else {
                char mac[ETHER_ADDR_LEN];
                memset(mac, 0, ETHER_ADDR_LEN);
#else
#error "Unsupported platform"
#endif
                set_ip_fabric_interface_mac(mac);
            }
        }
    }
}
