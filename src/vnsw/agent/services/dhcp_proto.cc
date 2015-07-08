/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "oper/vn.h"
#include "services/dhcp_proto.h"
#include "services/dhcp_lease_db.h"
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
    ip_fabric_interface_index_(-1), pkt_interface_index_(-1),
    dhcp_server_socket_(io), dhcp_server_read_buf_(NULL) {

    dhcp_relay_mode_ = agent->params()->dhcp_relay_mode();
    if (dhcp_relay_mode_) {
        boost::system::error_code ec;
        dhcp_server_socket_.open(udp::v4(), ec);
        assert(ec == 0);
        dhcp_server_socket_.bind(udp::endpoint(udp::v4(), DHCP_SERVER_PORT), ec);
        if (ec) {
            DHCP_TRACE(Error, "Error creating DHCP socket : " << ec);
        }

        // For DHCP requests coming from VMs in default VRF, when the IP received
        // from Nova is 0, DHCP module acts DHCP relay and relays the request onto
        // the fabric VRF. Vrouter sends responses for these to vhost0 interface.
        // We listen on DHCP server port to receive these responses (check option 82
        // header to decide that it is a response for a relayed request) and send
        // the response to the VM.
        AsyncRead();
    }

    iid_ = agent->interface_table()->Register(
                  boost::bind(&DhcpProto::ItfNotify, this, _2));
    vnid_ = agent->vn_table()->Register(
                  boost::bind(&DhcpProto::VnNotify, this, _2));
}

DhcpProto::~DhcpProto() {
    if (dhcp_relay_mode_) {
        boost::system::error_code ec;
        dhcp_server_socket_.shutdown(udp::socket::shutdown_both, ec);
        if (ec) {
            DHCP_TRACE(Error, "Error shutting down DHCP socket : " << ec);
        }
        dhcp_server_socket_.close (ec);
        if (ec) {
            DHCP_TRACE(Error, "Error closing DHCP socket : " << ec);
        }
    }
    agent_->interface_table()->Unregister(iid_);
    agent_->vn_table()->Unregister(vnid_);
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
        } else if (itf->type() == Interface::PACKET) {
            set_pkt_interface_index(-1);
        } else if (itf->type() == Interface::VM_INTERFACE) {
            VmInterface *vmi = static_cast<VmInterface *>(itf);
            if (gw_vmi_list_.erase(vmi)) {
                DHCP_TRACE(Trace, "Gateway interface deleted: " << itf->name());
            }
            DeleteLeaseDb(vmi);
        }
    } else {
        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(itf);
            set_ip_fabric_interface_index(itf->id());
            if (run_with_vrouter_) {
                set_ip_fabric_interface_mac(itf->mac());
            } else {
                set_ip_fabric_interface_mac(MacAddress());
            }
        } else if (itf->type() == Interface::PACKET) {
            set_pkt_interface_index(itf->id());
        } else if (itf->type() == Interface::VM_INTERFACE) {
            VmInterface *vmi = static_cast<VmInterface *>(itf);
            if (vmi->vmi_type() == VmInterface::GATEWAY) {
                DHCP_TRACE(Trace, "Gateway interface added: " << itf->name());
                gw_vmi_list_.insert(vmi);
                CreateLeaseDb(vmi);
            }
        }
    }
}

void DhcpProto::CreateLeaseDb(VmInterface *vmi) {
    const Ip4Address &subnet = vmi->subnet();
    if (vmi->vn() == NULL || subnet.to_ulong() == 0) {
        DeleteLeaseDb(vmi);
        DHCP_TRACE(Trace, "DHCP Lease DB not created - config not present : " <<
                   vmi->subnet().to_string());
        return;
    }

    const IpAddress address(subnet);
    const VnIpam *vn_ipam = vmi->vn()->GetIpam(address);
    if (!vn_ipam) {
        DeleteLeaseDb(vmi);
        DHCP_TRACE(Trace, "DHCP Lease DB not created for subnet " <<
                   vmi->subnet().to_string() << " - IPAM not available");
        return;
    }

    std::string res;
    std::vector<Ip4Address> reserve_list;
    if (vmi->ip_addr().to_ulong()) {
        reserve_list.push_back(vmi->ip_addr());
        res = vmi->ip_addr().to_string() + ", ";
    }
    reserve_list.push_back(vn_ipam->default_gw.to_v4());
    reserve_list.push_back(vn_ipam->dns_server.to_v4());
    res += vn_ipam->default_gw.to_v4().to_string() + ", ";
    res += vn_ipam->dns_server.to_v4().to_string();
    LeaseManagerMap::iterator it = lease_manager_.find(vmi);
    if (it == lease_manager_.end()) {
        DHCP_TRACE(Trace, "Created new DHCP Lease DB : " <<
                   vmi->name() << " " << vmi->subnet().to_string() << "/" <<
                   vmi->subnet_plen() << "; Reserved : " << res);
        DhcpLeaseDb *lease_db = new DhcpLeaseDb(this, vmi->subnet(),
                                                vmi->subnet_plen(),
                                                reserve_list,
                                                UuidToString(vmi->GetUuid()),
                                                io_);
        lease_manager_.insert(LeaseManagerPair(vmi, lease_db));
    } else {
        DHCP_TRACE(Trace, "Updated DHCP Lease DB : " <<
                   vmi->subnet().to_string() << "/" <<
                   vmi->subnet_plen() << "; Reserved : " << res);
        it->second->Update(vmi->subnet(), vmi->subnet_plen(),
                           reserve_list);
    }
}

void DhcpProto::DeleteLeaseDb(VmInterface *vmi) {
    LeaseManagerMap::iterator it = lease_manager_.find(vmi);
    if (it != lease_manager_.end()) {
        delete it->second;
        lease_manager_.erase(it);
        DHCP_TRACE(Trace, "Deleted DHCP Lease DB : " << vmi->name());
    }
}

void DhcpProto::VnNotify(DBEntryBase *entry) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    for (std::set<VmInterface *>::iterator it = gw_vmi_list_.begin();
         it != gw_vmi_list_.end(); ++it) {
        VmInterface *vmi = *it;
        if (vmi->vn() != vn)
            continue;
        CreateLeaseDb(*it);
    }
}

DhcpLeaseDb *DhcpProto::GetLeaseDb(Interface *interface) {
    LeaseManagerMap::iterator it = lease_manager_.find(interface);
    if (it != lease_manager_.end())
        return it->second;

    return NULL;
}
