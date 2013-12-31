/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vr_defs.h"
#include "base/logging.h"
#include "oper/interface_common.h"
#include "oper/mirror_table.h"
#include "dns_proto.h"
#include "services/services_types.h"
#include "services_init.h"
#include "bind/bind_resolver.h"
#include "xmpp/xmpp_channel.h"
#include "cmn/agent_cmn.h"
#include "pugixml/pugixml.hpp"
#include "xml/xml_pugi.h"
#include "bind/xmpp_dns_agent.h"
#include "controller/controller_dns.h"
#include "base/timer.h"
#include "pkt/pkt_init.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_table.h"

////////////////////////////////////////////////////////////////////////////////

void DnsProto::Init(boost::asio::io_service &io) {
}

void DnsProto::Shutdown() {
    BindResolver::Shutdown();
}

void DnsProto::ConfigInit() {
    std::vector<std::string> dns_servers;
    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        std::string server = Agent::GetInstance()->GetDnsXmppServer(i);    
        if (server != "")
            dns_servers.push_back(server);
    }
    BindResolver::Init(*Agent::GetInstance()->GetEventManager()->io_service(), dns_servers,
                       boost::bind(&DnsHandler::SendDnsIpc, _1));
}

DnsProto::DnsProto(boost::asio::io_service &io) :
    Proto("Agent::Services", PktHandler::DNS, io),
    xid_(0), timeout_(kDnsTimeout), max_retries_(kDnsMaxRetries) {
    lid_ = Agent::GetInstance()->GetInterfaceTable()->Register(
                  boost::bind(&DnsProto::ItfUpdate, this, _2));
    Vnlid_ = Agent::GetInstance()->GetVnTable()->Register(
                  boost::bind(&DnsProto::VnUpdate, this, _2));
    Agent::GetInstance()->GetDomainConfigTable()->RegisterIpamCb(
                  boost::bind(&DnsProto::IpamUpdate, this, _1));
    Agent::GetInstance()->GetDomainConfigTable()->RegisterVdnsCb(
                  boost::bind(&DnsProto::VdnsUpdate, this, _1));
}

DnsProto::~DnsProto() {
    Agent::GetInstance()->GetInterfaceTable()->Unregister(lid_);
    Agent::GetInstance()->GetVnTable()->Unregister(Vnlid_);
}

ProtoHandler *DnsProto::AllocProtoHandler(PktInfo *info,
                                          boost::asio::io_service &io) {
    return new DnsHandler(info, io);
}

void DnsProto::ItfUpdate(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (itf->type() != Interface::VM_INTERFACE)
        return;

    const VmInterface *vmitf = static_cast<VmInterface *>(entry);
    if (entry->IsDeleted()) {
        DnsHandler::SendDnsUpdateIpc(NULL, DnsAgentXmpp::Update, vmitf);
        DnsHandler::SendDnsUpdateIpc(NULL, DnsAgentXmpp::Update, vmitf, true);
        all_vms_.erase(vmitf);
    } else {
        uint32_t ttl = kDnsDefaultTtl;
        std::string vdns_name, domain;
        GetVdnsData(vmitf->vn(), vmitf->ip_addr(),
                    vdns_name, domain, ttl);
        VmDataMap::iterator it = all_vms_.find(vmitf);
        if (it == all_vms_.end()) {
            if (!UpdateDnsEntry(vmitf, vmitf->vn(), vdns_name,
                                vmitf->ip_addr(), false, false))
                vdns_name = "";
            IpVdnsMap vmdata;
            vmdata.insert(IpVdnsPair(vmitf->ip_addr().to_ulong(), vdns_name));
            all_vms_.insert(VmDataPair(vmitf, vmdata));
        } else {
            CheckForUpdate(it->second, vmitf, vmitf->vn(),
                           vmitf->ip_addr(), vdns_name, domain, ttl, false);
        }
    }
}

void DnsProto::VnUpdate(DBEntryBase *entry) {
    const VnEntry *vn = static_cast<const VnEntry *>(entry);
    if (vn->IsDeleted() || vn->GetVnIpam().size() == 0)
        return;
    for (VmDataMap::iterator it = all_vms_.begin(); it != all_vms_.end(); ++it) {
        if (it->first->vn() == vn) {
            uint32_t ttl = kDnsDefaultTtl;
            std::string vdns_name, domain;
            GetVdnsData(vn, it->first->ip_addr(), vdns_name, domain, ttl);
            CheckForUpdate(it->second, it->first, it->first->vn(),
                           it->first->ip_addr(), vdns_name, domain, ttl, false);
        }
        const VmInterface::FloatingIpSet &fip_list =
            it->first->floating_ip_list().list_;
        for (VmInterface::FloatingIpSet::iterator fip = fip_list.begin();
             fip != fip_list.end(); ++fip) {
            if (fip->vn_.get() == vn) {
                uint32_t ttl = kDnsDefaultTtl;
                std::string vdns_name, domain;
                GetVdnsData(vn, fip->floating_ip_, vdns_name, domain, ttl);
                CheckForUpdate(it->second, it->first, vn,
                               fip->floating_ip_, vdns_name, domain, ttl, true);
            }
        }
    }
}

void DnsProto::IpamUpdate(IFMapNode *node) {
    if (node->IsDeleted())
        return;
    ProcessUpdate(node->name(), node->IsDeleted(), true);
}

void DnsProto::VdnsUpdate(IFMapNode *node) {
    // Update any existing records prior to checking for new ones
    if (!node->IsDeleted()) {
        autogen::VirtualDns *virtual_dns =
                 static_cast <autogen::VirtualDns *> (node->GetObject());
        autogen::VirtualDnsType vdns_type = virtual_dns->data();
        std::string name = node->name();
        UpdateDnsEntry(NULL, name, name, vdns_type.domain_name,
                       (uint32_t)vdns_type.default_ttl_seconds, false);
    }
    ProcessUpdate(node->name(), node->IsDeleted(), false);
}

void DnsProto::ProcessUpdate(std::string name, bool is_deleted, bool is_ipam) {
    for (VmDataMap::iterator it = all_vms_.begin(); it != all_vms_.end(); ++it) {
        uint32_t ttl = kDnsDefaultTtl;
        std::string vdns_name, domain;
        GetVdnsData(it->first->vn(), it->first->ip_addr(),
                    vdns_name, domain, ttl);
        // in case of VDNS delete, clear the name
        if (!is_ipam && is_deleted && vdns_name == name)
            vdns_name.clear();
        CheckForUpdate(it->second, it->first, it->first->vn(),
                       it->first->ip_addr(), vdns_name, domain, ttl, false);

        const VmInterface::FloatingIpSet &fip_list =
            it->first->floating_ip_list().list_;
        for (VmInterface::FloatingIpSet::iterator fip = fip_list.begin();
             fip != fip_list.end(); ++fip) {
            VnEntry *vn = fip->vn_.get();
            ttl = kDnsDefaultTtl;
            GetVdnsData(vn, fip->floating_ip_, vdns_name, domain, ttl);
            CheckForUpdate(it->second, it->first, vn,
                           fip->floating_ip_, vdns_name, domain, ttl, true);
        }
    }
}

void DnsProto::CheckForUpdate(IpVdnsMap &ipvdns, const VmInterface *vmitf,
                              const VnEntry *vn, const Ip4Address &ip,
                              std::string &vdns_name, std::string &domain, 
                              uint32_t ttl, bool is_floating) {
    IpVdnsMap::iterator vmdata_it = ipvdns.find(ip.to_ulong());
    if (vmdata_it == ipvdns.end()) {
	if (UpdateDnsEntry(vmitf, vn, vdns_name, ip, is_floating, false))
            ipvdns.insert(IpVdnsPair(ip.to_ulong(), vdns_name));
        else
            ipvdns.insert(IpVdnsPair(ip.to_ulong(), ""));
	return;
    }
    if (vmdata_it->second.empty() && vmdata_it->second != vdns_name) {
	if (UpdateDnsEntry(vmitf, vn, vdns_name, ip, is_floating, false))
	    vmdata_it->second = vdns_name;
    } else if (vmdata_it->second != vdns_name) {
	if (UpdateDnsEntry(vmitf, vdns_name, vmdata_it->second, domain,
                           ttl, is_floating))
	    vmdata_it->second = vdns_name;
    }
}

// Send A & PTR record entries
void DnsProto::UpdateDnsEntry(const VmInterface *vmitf,
                              const std::string &name,
                              const Ip4Address &ip, uint32_t plen,
                              const std::string &vdns_name,
                              const autogen::VirtualDnsType &vdns_type,
                              bool is_floating, bool is_delete) {
    if (!name.size() || !vdns_type.dynamic_records_from_client) {
        DNS_BIND_TRACE(DnsBindTrace, "Not adding DNS entry; Name = " <<
                       name << "Dynamic records allowed = " <<
                       (vdns_type.dynamic_records_from_client ? "yes" : "no"));
        return;
    }

    DnsUpdateData *data = new DnsUpdateData();
    data->virtual_dns = vdns_name;
    data->zone = vdns_type.domain_name;

    // Add a DNS record
    DnsItem item;
    item.eclass = is_delete ? DNS_CLASS_NONE : DNS_CLASS_IN;
    item.type = DNS_A_RECORD;
    item.ttl = is_delete ? 0 : vdns_type.default_ttl_seconds;
    item.name = name;
    boost::system::error_code ec;
    item.data = ip.to_string(ec);
    data->items.push_back(item);

    DNS_BIND_TRACE(DnsBindTrace, "DNS update sent for : " << item.ToString() <<
                   " VDNS : " << data->virtual_dns << " Zone : " << data->zone);
    DnsHandler::SendDnsUpdateIpc(data, DnsAgentXmpp::Update, vmitf, is_floating);

    // Add a PTR record as well
    DnsUpdateData *ptr_data = new DnsUpdateData();
    ptr_data->virtual_dns = vdns_name;
    BindUtil::GetReverseZone(ip.to_ulong(), plen, ptr_data->zone);

    item.type = DNS_PTR_RECORD;
    item.data.swap(item.name);
    std::stringstream str;
    for (int i = 0; i < 4; i++) {
        str << ((ip.to_ulong() >> (i * 8)) & 0xFF);
        str << ".";
    }
    item.name = str.str() + "in-addr.arpa";
    ptr_data->items.push_back(item);

    DNS_BIND_TRACE(DnsBindTrace, "DNS update sent for : " << item.ToString() <<
                   " VDNS : " << ptr_data->virtual_dns <<
                   " Zone : " << ptr_data->zone);
    DnsHandler::SendDnsUpdateIpc(ptr_data, DnsAgentXmpp::Update, vmitf, is_floating);
}

// Update the floating ip entries
bool DnsProto::UpdateDnsEntry(const VmInterface *vmitf, const VnEntry *vn,
                              const Ip4Address &ip, bool is_deleted) {
    bool is_floating = true;
    uint32_t ttl = kDnsDefaultTtl;
    std::string vdns_name, domain;
    GetVdnsData(vn, ip, vdns_name, domain, ttl);
    VmDataMap::iterator it = all_vms_.find(vmitf);
    if (it == all_vms_.end()) {
        if (is_deleted)
            return true;
        if (!UpdateDnsEntry(vmitf, vn, vdns_name, ip, is_floating, false))
            vdns_name = "";
        IpVdnsMap vmdata;
        vmdata.insert(IpVdnsPair(ip.to_ulong(), vdns_name));
        all_vms_.insert(VmDataPair(vmitf, vmdata));
    } else {
        if (is_deleted) {
            UpdateDnsEntry(vmitf, vn, vdns_name, ip, is_floating, true);
            it->second.erase(ip.to_ulong());
        } else {
            CheckForUpdate(it->second, vmitf, vn, ip, vdns_name, domain,
                           ttl, is_floating);
        }
    }
    return true;
}

bool DnsProto::UpdateDnsEntry(const VmInterface *vmitf, const VnEntry *vn,
                              const std::string &vdns_name, const Ip4Address &ip,
                              bool is_floating, bool is_deleted) {
    if (!vdns_name.empty()) {
        uint32_t plen = 0;
        const std::vector<VnIpam> &ipam = vn->GetVnIpam();
        unsigned int i;
        for (i = 0; i < ipam.size(); ++i) {
            uint32_t mask = ipam[i].plen ? (0xFFFFFFFF << (32 - ipam[i].plen)) : 0;
            if ((ip.to_ulong() & mask) == (ipam[i].ip_prefix.to_ulong() & mask)) {
                plen = ipam[i].plen;
                break;
            }
        }
        if (i == ipam.size()) {
            DNS_BIND_TRACE(DnsBindTrace, "UpdateDnsEntry for VM <" <<
                           vmitf->vm_name() << "> not done for vn <" <<
                           vn->GetName() << "> IPAM doesnt have the subnet; " <<
                           ((is_deleted)? "delete" : "update"));
            return false;
        }

        autogen::VirtualDnsType vdns_type;
        if (Agent::GetInstance()->GetDomainConfigTable()->
            GetVDns(vdns_name, &vdns_type)) {
            UpdateDnsEntry(vmitf, vmitf->vm_name(), ip, plen,
                           vdns_name, vdns_type, is_floating, is_deleted);
        } else {
            DNS_BIND_TRACE(DnsBindTrace, "UpdateDnsEntry for VM <" <<
                           vmitf->vm_name() << "> entry not " <<
                           ((is_deleted)? "deleted; " : "updated; ") <<
                           "VDNS info for " << vdns_name << " not present");
            return false;
        }
    }

    return true;
}

// Move DNS entries for a VM from one VDNS to another
bool DnsProto::UpdateDnsEntry(const VmInterface *vmitf,
                              std::string &new_vdns_name, std::string &old_vdns_name,
                              std::string &new_domain, uint32_t ttl, bool is_floating) {
    std::string name = (vmitf ? vmitf->vm_name() : "All Vms");
    DNS_BIND_TRACE(DnsBindTrace, "VDNS modify for VM <" << name <<
                   " old VDNS : " << old_vdns_name <<
                   " new VDNS : " << new_vdns_name <<
                   " ttl : " << ttl <<
                   " floating : " << (is_floating ? "yes" : "no"));
    DnsHandler::SendDnsUpdateIpc(vmitf, new_vdns_name,
                                 old_vdns_name, new_domain, ttl, is_floating);
    return true;
}

bool DnsProto::GetVdnsData(const VnEntry *vn, const Ip4Address &vm_addr, 
                           std::string &vdns_name, std::string &domain,
                           uint32_t &ttl) {
    if (!vn)
        return false;

    autogen::IpamType ipam_type;
    autogen::VirtualDnsType vdns_type;
    if (!vn->GetIpamVdnsData(vm_addr, &ipam_type, &vdns_type)) {
        DNS_BIND_TRACE(DnsBindTrace, "Unable to retrieve VDNS data; VN : " <<
                   vn->GetName() << " IP : " << vm_addr.to_string());
        return false;
    }

    vdns_name = ipam_type.ipam_dns_server.virtual_dns_server_name;
    domain = vdns_type.domain_name;
    ttl = (uint32_t)vdns_type.default_ttl_seconds;
    return true;
}

inline uint16_t DnsProto::GetTransId() {
    return (++xid_ == 0 ? ++xid_ : xid_);
}

////////////////////////////////////////////////////////////////////////////////

DnsHandler::DnsHandler(PktInfo *info, boost::asio::io_service &io) : 
    ProtoHandler(info, io), resp_ptr_(NULL), dns_resp_size_(0),
    xid_(-1), retries_(0), action_(NONE), rkey_(NULL), query_name_update_(false),
    pend_req_(0) {
    dns_ = (dnshdr *) pkt_info_->data;
    timer_ = TimerManager::CreateTimer(io, "DnsHandlerTimer");
}

DnsHandler::~DnsHandler() {
    for (ResolvList::iterator it = resolv_list_.begin();
         it != resolv_list_.end(); ++it) {
        delete *it;
    }
    if (rkey_) {
        delete rkey_;
    }
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

bool DnsHandler::Run() {
    switch(pkt_info_->type) {
        case PktType::MESSAGE:
            return HandleMessage();

       default:
            return HandleRequest();
    }
}    

bool DnsHandler::HandleRequest() {
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    dns_proto->IncrStatsReq();

    uint16_t ret = DNS_ERR_NO_ERROR;
    const Interface *itf = InterfaceTable::GetInstance()->FindInterface(GetIntf());
    if (!itf || (itf->type() != Interface::VM_INTERFACE) || 
        dns_->flags.req) {
        dns_proto->IncrStatsDrop();
        DNS_BIND_TRACE(DnsBindError, "Received Invalid DNS request - dropping"
                       << "; itf = " << itf << "; flags.req = " 
                       << dns_->flags.req << "; src addr = " 
                       << pkt_info_->ip->saddr <<";");
        return true;
    }

    const VmInterface *vmitf = static_cast<const VmInterface *>(itf);
    if (!vmitf->ipv4_forwarding()) {
        DNS_BIND_TRACE(DnsBindError, "DNS request on VM port with disabled" 
                       "ipv4 service: " << itf);
        return true;
    }
    // Handle requests (req == 0), queries (op code == 0), updates, non auth
    if ((dns_->flags.op && dns_->flags.op != DNS_OPCODE_UPDATE) || 
        (dns_->flags.cd)) {
        DNS_BIND_TRACE(DnsBindError, "Unimplemented DNS request"
                       << "; flags.op = " << dns_->flags.op << "; flags.cd = "
                       << dns_->flags.cd << ";");
        ret = DNS_ERR_NO_IMPLEMENT;
        goto error;
    }

    if (!vmitf->vn() || 
        !vmitf->vn()->GetIpamData(vmitf->ip_addr(), &ipam_name_,
                                          &ipam_type_)) {
        DNS_BIND_TRACE(DnsBindError, "Unable to find Ipam data; interface = "
                       << vmitf->name());
        ret = DNS_ERR_SERVER_FAIL;
        goto error;
    }

    if (ipam_type_.ipam_dns_method == "default-dns-server" ||
        ipam_type_.ipam_dns_method == "") {
        if (dns_->flags.op == DNS_OPCODE_UPDATE) {
            DNS_BIND_TRACE(DnsBindError, "Default DNS request : Update received, ignoring");
            ret = DNS_ERR_NO_IMPLEMENT;
            goto error;
        }
        return HandleDefaultDnsRequest(vmitf);
    } else if (ipam_type_.ipam_dns_method == "virtual-dns-server") {
        if (!Agent::GetInstance()->GetDomainConfigTable()->GetVDns(ipam_type_.ipam_dns_server.
            virtual_dns_server_name, &vdns_type_)) {
            DNS_BIND_TRACE(DnsBindError, "Unable to find domain; interface = "
                           << vmitf->vm_name());
            ret = DNS_ERR_SERVER_FAIL;
            goto error;
        }
        return HandleVirtualDnsRequest(vmitf);
    }

    ret = DNS_ERR_SERVER_FAIL;
error:
    BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE,
                             DNS_OPCODE_QUERY, 0, 1, ret,
                             ntohs(dns_->ques_rrcount));
    SendDnsResponse();
    return true;
}

bool DnsHandler::HandleDefaultDnsRequest(const VmInterface *vmitf) {
    tbb::mutex::scoped_lock lock(mutex_);
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    rkey_ = new QueryKey(vmitf, dns_->xid);
    if (dns_proto->IsVmRequestDuplicate(rkey_)) {
        DNS_BIND_TRACE(DnsBindTrace, 
                       "Retry DNS query from VM - dropping; interface = "
                       << vmitf->vm_name() << " xid = " << dns_->xid);
        dns_proto->IncrStatsRetransmitReq();
        return true;
    }
    dns_proto->AddVmRequest(rkey_);

    dns_resp_size_ = BindUtil::ParseDnsQuery((uint8_t *)dns_, items_);
    resp_ptr_ = (uint8_t *)dns_ + dns_resp_size_;
    action_ = DnsHandler::DNS_QUERY;
    BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE, 
                             DNS_OPCODE_QUERY, 0, 1, DNS_ERR_NO_ERROR, 
                             ntohs(dns_->ques_rrcount));
    for (uint32_t i = 0; i < items_.size(); i++) {
        ResolveHandler resolv_handler = 
            boost::bind(&DnsHandler::DefaultDnsResolveHandler, this, _1, _2, i);

        boost_udp::resolver *resolv = new boost_udp::resolver(io_);
        switch(items_[i].type) {
            case DNS_A_RECORD:
                resolv->async_resolve(
                        boost_udp::resolver::query(boost_udp::v4(), 
                        items_[i].name, "domain"), resolv_handler);
                break;

            case DNS_PTR_RECORD: {
                uint32_t addr;
                if (!BindUtil::GetAddrFromPtrName(items_[i].name, addr)) {
                    DNS_BIND_TRACE(DnsBindError, "Default DNS request:"
                                   " interface = " << vmitf->vm_name() <<
                                   " Ignoring invalid IP in PTR type : " + 
                                   items_[i].name);
                    delete resolv;
                    continue;
                }
                boost_udp::endpoint ep;
                ep.address(boost::asio::ip::address_v4(addr));
                resolv->async_resolve(ep, resolv_handler);
                break;
            }

            case DNS_AAAA_RECORD:
                resolv->async_resolve(
                        boost_udp::resolver::query(boost_udp::v6(),
                        items_[i].name, "domain"), resolv_handler);
                break;

            default:
                DNS_BIND_TRACE(DnsBindError, "Default DNS request:"
                               " interface = " << vmitf->vm_name() <<
                               " Ignoring unsupported type : " << 
                               items_[i].type);
                continue;
        }
        resolv_list_.push_back(resolv);
        pend_req_++;
    }

    DNS_BIND_TRACE(DnsBindTrace, "Default DNS query sent to server; "
                   "interface = " << vmitf->vm_name() << " xid = " << 
                   dns_->xid << "; " << DnsItemsToString(items_) << ";");
    if (pend_req_)
        return false;

    dns_proto->DelVmRequest(rkey_);
    return true;
}

void DnsHandler::DefaultDnsResolveHandler(const error_code& error,
                                          boost_udp::resolver::iterator it,
                                          uint32_t index) {
    {
        tbb::mutex::scoped_lock lock(mutex_);
        if (error) {
            dns_->flags.ret = DNS_ERR_NO_SUCH_NAME;
        } else {
            bool resolved = true;
            items_[index].ttl = DEFAULT_DNS_TTL;
            if (items_[index].type == DNS_A_RECORD) {
                boost_udp::resolver::iterator end;
                while (it != end) {
                    boost_udp::endpoint ep = *it;
                    if (ep.address().is_v4() && 
                        !ep.address().to_v4().is_unspecified())
                        items_[index].data = ep.address().to_v4().to_string();
                    else {     
                        dns_->flags.ret = DNS_ERR_SERVER_FAIL;
                        resolved = false;
                    }
                    it++;
                }
            } else if (items_[index].type == DNS_PTR_RECORD) {
                items_[index].data = it->host_name();
            } else if (items_[index].type == DNS_AAAA_RECORD) {
                boost_udp::resolver::iterator end;
                while (it != end) {
                    boost_udp::endpoint ep = *it;
                    if (ep.address().is_v6() && 
                        !ep.address().to_v6().is_unspecified())
                        items_[index].data = ep.address().to_v6().to_string();
                    else {     
                        dns_->flags.ret = DNS_ERR_SERVER_FAIL;
                        resolved = false;
                    }
                    it++;
                }
            }

            if (resolved) {
                resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, items_[index], 
                                                       dns_resp_size_);
                dns_->ans_rrcount++;
            }
        }

        pend_req_--;
        if (pend_req_)
            return;

        dns_->ans_rrcount = htons(dns_->ans_rrcount);
    }
    SendDnsIpc(DnsHandler::DNS_DEFAULT_RESPONSE, 0, NULL, this);
}

void DnsHandler::DefaultDnsSendResponse() {
    Agent::GetInstance()->GetDnsProto()->DelVmRequest(rkey_);
    if (dns_->flags.ret) {
        DNS_BIND_TRACE(DnsBindError, "Query failed : " << 
                       BindUtil::DnsResponseCode(dns_->flags.ret) <<
                       "; xid = " << dns_->xid << "; " <<
                       DnsItemsToString(items_) << ";");
    } else {
        DNS_BIND_TRACE(DnsBindTrace, "Query successful : xid = " <<
                       dns_->xid << ";" << DnsItemsToString(items_) << ";");
    }
    SendDnsResponse();
}

bool DnsHandler::HandleVirtualDnsRequest(const VmInterface *vmitf) {
    rkey_ = new QueryKey(vmitf, dns_->xid);
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    if (dns_proto->IsVmRequestDuplicate(rkey_)) {
        DNS_BIND_TRACE(DnsBindTrace, 
                       "Retry DNS query from VM - dropping; interface = " <<
                       vmitf->vm_name() << " xid = " << dns_->xid << "; " << 
                       DnsItemsToString(items_) << ";");
        dns_proto->IncrStatsRetransmitReq();
        return true;
    }
    dns_proto->AddVmRequest(rkey_);

    BindUtil::RemoveSpecialChars(ipam_type_.ipam_dns_server.
                                 virtual_dns_server_name);

    uint16_t ret = DNS_ERR_NO_ERROR;
    switch (dns_->flags.op) {
        case DNS_OPCODE_QUERY: {
            dns_resp_size_ = BindUtil::ParseDnsQuery((uint8_t *)dns_, items_);
            resp_ptr_ = (uint8_t *)dns_ + dns_resp_size_;
            UpdateQueryNames();
            xid_ = dns_proto->GetTransId();
            action_ = DnsHandler::DNS_QUERY;
            BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE, 
                                     DNS_OPCODE_QUERY, 0, 1, ret, 
                                     ntohs(dns_->ques_rrcount));
            if (SendDnsQuery())
                return false;
            break;
        }

        case DNS_OPCODE_UPDATE: {
            if (vdns_type_.dynamic_records_from_client) {
                DnsUpdateData *update_data = new DnsUpdateData();
                DnsUpdateIpc *update = new DnsUpdateIpc(DnsAgentXmpp::Update, 
                                                        update_data, vmitf, false);
                if (BindUtil::ParseDnsUpdate((uint8_t *)dns_, *update_data)) {
                    update_data->virtual_dns = ipam_type_.ipam_dns_server.
                                               virtual_dns_server_name;
                    Update(update);
                } else {
                    delete update;
                    ret = DNS_ERR_NO_IMPLEMENT;
                }
            } else {
                DNS_BIND_TRACE(DnsBindTrace, "Client not allowed to update "
                "dynamic records : " << vmitf->vm_name() << " ;Ignoring "
                "update for " << DnsItemsToString(items_) << ";");
                ret = DNS_ERR_NOT_AUTH;
            }
            BindUtil::BuildDnsHeader(dns_, ntohs(dns_->xid), DNS_QUERY_RESPONSE, 
                                     DNS_OPCODE_UPDATE, 0, 1, ret,
                                     ntohs(dns_->ques_rrcount));
            SendDnsResponse();
            break;
        }

        default: {
            DNS_BIND_TRACE(DnsBindTrace, "Unsupported op : " << dns_->flags.op
                           << "from vm : " << vmitf->vm_name());
            break;
        }
    }
    dns_proto->DelVmRequest(rkey_);
    return true;
}

bool DnsHandler::SendDnsQuery() {
    uint8_t *pkt = NULL;
    std::size_t len = 0;
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    bool in_progress = dns_proto->IsDnsQueryInProgress(xid_);
    if (in_progress) {
        if (retries_ >= dns_proto->GetMaxRetries()) {
            DNS_BIND_TRACE(DnsBindTrace, 
                           "Max retries reached for query; xid = " << xid_ <<
                           "; " << DnsItemsToString(items_) << ";");
            goto cleanup;
        } else
            retries_++;
    } else {
        dns_proto->AddDnsQuery(xid_, this);
    }

    pkt = new uint8_t[BindResolver::max_pkt_size];
    len = BindUtil::BuildDnsQuery(pkt, xid_, 
          ipam_type_.ipam_dns_server.virtual_dns_server_name, items_);
    if (BindResolver::Resolver()->DnsSend(pkt, Agent::GetInstance()->GetXmppDnsCfgServerIdx(),
                                          len)) {
        DNS_BIND_TRACE(DnsBindTrace, "DNS query sent to server; xid = " << 
                       xid_ << "; " << DnsItemsToString(items_) << ";");
        timer_->Start(dns_proto->GetTimeout(),
                      boost::bind(&DnsHandler::TimerExpiry, this, xid_));
        return true;
    } 

cleanup:
    dns_proto->IncrStatsDrop();
    dns_proto->DelDnsQuery(xid_);
    return false;
}

bool DnsHandler::HandleMessage() {
    switch (pkt_info_->ipc->cmd) {
        case DnsHandler::DNS_DEFAULT_RESPONSE:
            return HandleDefaultDnsResponse();

        case DnsHandler::DNS_BIND_RESPONSE:
            return HandleBindResponse();

        case DnsHandler::DNS_TIMER_EXPIRED:
            return HandleRetryExpiry();

        case DnsHandler::DNS_XMPP_SEND_UPDATE:
            return HandleUpdate();

        case DnsHandler::DNS_XMPP_MODIFY_VDNS:
            return HandleModifyVdns();

        case DnsHandler::DNS_XMPP_UPDATE_RESPONSE:
            return HandleUpdateResponse();

        case DnsHandler::DNS_XMPP_SEND_UPDATE_ALL:
            return UpdateAll();

        default:
            assert(0);
    }
}

bool DnsHandler::HandleDefaultDnsResponse() {
    DnsIpc *ipc = static_cast<DnsIpc *>(pkt_info_->ipc);
    ipc->handler->DefaultDnsSendResponse();
    delete ipc;
    return true;
}

bool DnsHandler::HandleBindResponse() {
    DnsIpc *ipc = static_cast<DnsIpc *>(pkt_info_->ipc);
    uint16_t xid = ntohs(*(uint16_t *)ipc->resp);
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    DnsHandler *handler = dns_proto->GetDnsQueryHandler(xid);
    if (handler) {
        dns_flags flags;
        std::vector<DnsItem> ques, ans, auth, add;
        BindUtil::ParseDnsQuery(ipc->resp, xid, flags, ques, ans, auth, add);
        switch(handler->action_) {
            case DnsHandler::DNS_QUERY:
                handler->Resolve(flags, ques, ans, auth, add);
                if (flags.ret) {
                    DNS_BIND_TRACE(DnsBindError, "Query failed : " << 
                                   BindUtil::DnsResponseCode(flags.ret) <<
                                   "; xid = " << xid << "; " <<
                                   DnsItemsToString(items_) << ";");
                } else {
                    DNS_BIND_TRACE(DnsBindTrace, "Query successful : xid = " <<
                                   xid << ";" << DnsItemsToString(ans) << ";");
                }
                break;

            default:
                DNS_BIND_TRACE(DnsBindTrace, 
                               "Received invalid BIND response: xid = " << xid);
                break;
        }
        dns_proto->DelDnsQuery(xid);
        dns_proto->DelVmRequest(handler->rkey_);
        delete handler;
    } else
        DNS_BIND_TRACE(DnsBindError, "Invalid xid " << xid << 
                       "received from DNS server - dropping");

    delete ipc;
    return true;
}

bool DnsHandler::HandleRetryExpiry() {
    DnsIpc *ipc = static_cast<DnsIpc *>(pkt_info_->ipc);
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    DnsHandler *handler = dns_proto->GetDnsQueryHandler(ipc->xid);
    if (handler && !handler->SendDnsQuery()) {
            dns_proto->DelVmRequest(handler->rkey_);
            delete handler;
    }

    delete ipc;
    return true;
}

bool DnsHandler::HandleUpdateResponse() {
    DnsUpdateIpc *ipc = static_cast<DnsUpdateIpc *>(pkt_info_->ipc);
    delete ipc;
    return true;
}

bool DnsHandler::HandleUpdate() {
    DnsUpdateIpc *ipc = static_cast<DnsUpdateIpc *>(pkt_info_->ipc);
    if (!ipc->xmpp_data) {
        DelUpdate(ipc);
    } else {
        Update(ipc);
    }
    return true;
}

bool DnsHandler::HandleModifyVdns() {
    DnsUpdateIpc *ipc = static_cast<DnsUpdateIpc *>(pkt_info_->ipc);
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    std::vector<DnsHandler::DnsUpdateIpc *> change_list;
    const DnsProto::DnsUpdateSet &update_set = dns_proto->GetUpdateRequestSet();
    for (DnsProto::DnsUpdateSet::const_iterator it = update_set.begin();
         it != update_set.end(); ++it) {
        if ((ipc->itf &&
             ((*it)->itf != ipc->itf || (*it)->floatingIp != ipc->floatingIp)) ||
            !((*it)->xmpp_data) || (*it)->xmpp_data->virtual_dns != ipc->old_vdns)
            continue;

        change_list.push_back(*it);
        // if the server ttl changes, we only readd with the new values
        if (!ipc->itf && ipc->new_vdns == ipc->old_vdns)
            continue;

        for (DnsItems::iterator item = (*it)->xmpp_data->items.begin(); 
             item != (*it)->xmpp_data->items.end(); ++item) {
            // in case of delete, set the class to NONE and ttl to 0
            (*item).eclass = DNS_CLASS_NONE;
            (*item).ttl = 0;
        }
        for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
            AgentDnsXmppChannel *channel = 
                        Agent::GetInstance()->GetAgentDnsXmppChannel(i);
            SendXmppUpdate(channel, (*it)->xmpp_data);
        }
    }
    for (unsigned int i = 0; i < change_list.size(); i++) {
        dns_proto->DelUpdateRequest(change_list[i]);
        if (ipc->new_vdns.empty())
            delete change_list[i];
    }
    if (ipc->new_vdns.empty())
        goto done;
    for (unsigned int i = 0; i < change_list.size(); i++) {
        change_list[i]->xmpp_data->virtual_dns = ipc->new_vdns;
        if (change_list[i]->xmpp_data->zone.find(".in-addr.arpa") ==
                                                 std::string::npos)
            change_list[i]->xmpp_data->zone = ipc->new_domain;
        for (DnsItems::iterator item = change_list[i]->xmpp_data->items.begin();
             item != change_list[i]->xmpp_data->items.end(); ++item) {
            (*item).eclass = DNS_CLASS_IN;
            (*item).ttl = ipc->ttl;
        }
        Update(change_list[i]);
    }

done:
    delete ipc;
    return true;
}

bool DnsHandler::UpdateAll() {
    DnsUpdateAllIpc *ipc = static_cast<DnsUpdateAllIpc *>(pkt_info_->ipc);
    const DnsProto::DnsUpdateSet &update_set =
          Agent::GetInstance()->GetDnsProto()->GetUpdateRequestSet();
    for (DnsProto::DnsUpdateSet::const_iterator it = update_set.begin(); 
         it != update_set.end(); ++it) {
        SendXmppUpdate(ipc->channel, (*it)->xmpp_data);
    }

    delete ipc;
    return true;
}

void DnsHandler::SendXmppUpdate(AgentDnsXmppChannel *channel, 
                                DnsUpdateData *xmpp_data) {
    if (channel) {
        // Split the request in case we have more data items
        DnsItems done, store;
        DnsItems::iterator first, last;

        uint32_t size = xmpp_data->items.size();
        store.swap(xmpp_data->items);
        for (uint32_t i = 0; i <= size / max_items_per_xmpp_msg; i++) {
            if (!store.size())
                break;

            first = last = store.begin();
            if (store.size() > max_items_per_xmpp_msg)
                std::advance(last, max_items_per_xmpp_msg);
            else
                last = store.end();
            xmpp_data->items.splice(xmpp_data->items.begin(), store, first, last);

            uint8_t *data = new uint8_t[DnsAgentXmpp::max_dns_xmpp_msg_len];
            xid_ = Agent::GetInstance()->GetDnsProto()->GetTransId();
            std::size_t len = 0;
            if ((len = DnsAgentXmpp::DnsAgentXmppEncode(channel->GetXmppChannel(), 
                                                        DnsAgentXmpp::Update,
                                                        xid_, 0, xmpp_data, 
                                                        data)) > 0) {
                channel->SendMsg(data, len);
            }
            else 
                delete data;

            done.splice(done.end(), xmpp_data->items, xmpp_data->items.begin(),
                        xmpp_data->items.end());
        }
        xmpp_data->items.swap(done);
    }
}

void 
DnsHandler::Resolve(dns_flags flags, std::vector<DnsItem> &ques, 
                    std::vector<DnsItem> &ans, std::vector<DnsItem> &auth, 
                    std::vector<DnsItem> &add) {
    for(unsigned int i = 0; i < ans.size(); ++i) {
        // find the matching entry in the request
        bool name_update_required = true;
        for (unsigned int j = 0; j < items_.size(); ++j) {
            if (ans[i].name == items_[j].name &&
                ans[i].eclass == items_[j].eclass) {
                ans[i].name_plen = items_[j].name_plen;
                ans[i].name_offset = items_[j].offset;
                name_update_required = false;
                break;
            }
        }
        UpdateOffsets(ans[i], name_update_required);
        UpdateGWAddress(ans[i]);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, ans[i], 
                                               dns_resp_size_);
        dns_->ans_rrcount++;
    }

    for(unsigned int i = 0; i < auth.size(); ++i) {
        UpdateOffsets(auth[i], true);
        UpdateGWAddress(auth[i]);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, auth[i], 
                                               dns_resp_size_);
        dns_->auth_rrcount++;
    }

    for(unsigned int i = 0; i < add.size(); ++i) {
        UpdateOffsets(add[i], true);
        UpdateGWAddress(add[i]);
        resp_ptr_ = BindUtil::AddAnswerSection(resp_ptr_, add[i], 
                                               dns_resp_size_);
        dns_->add_rrcount++;
    }

    dns_->flags.ret = flags.ret;
    dns_->flags.auth = flags.auth;
    dns_->flags.ad = flags.ad;
    dns_->flags.ra = flags.ra;
    dns_->ans_rrcount = htons(dns_->ans_rrcount);
    dns_->auth_rrcount = htons(dns_->auth_rrcount);
    dns_->add_rrcount = htons(dns_->add_rrcount);
    SendDnsResponse();
}

void DnsHandler::SendDnsResponse() {
    in_addr_t src_ip = pkt_info_->ip->daddr;
    in_addr_t dest_ip = pkt_info_->ip->saddr;
    unsigned char dest_mac[ETH_ALEN];
    memcpy(dest_mac, pkt_info_->eth->h_source, ETH_ALEN);

    // fill in the response
    dns_resp_size_ += sizeof(udphdr);
    UdpHdr(dns_resp_size_, src_ip, DNS_SERVER_PORT, 
           dest_ip, ntohs(pkt_info_->transp.udp->source));
    dns_resp_size_ += sizeof(iphdr);
    IpHdr(dns_resp_size_, src_ip, dest_ip, IPPROTO_UDP);
    EthHdr(agent_vrrp_mac, dest_mac, 0x800);
    dns_resp_size_ += sizeof(ethhdr);

    PacketInterfaceKey key(nil_uuid(), Agent::GetInstance()->GetHostIfname());
    Interface *pkt_itf = static_cast<Interface *>
                         (Agent::GetInstance()->GetInterfaceTable()->FindActiveEntry(&key));
    if (pkt_itf) {
        UpdateStats();
        Send(dns_resp_size_, pkt_itf->id(), pkt_info_->vrf,
             AGENT_CMD_ROUTE, PktHandler::DNS);
    } else
        Agent::GetInstance()->GetDnsProto()->IncrStatsDrop();
}

void DnsHandler::UpdateQueryNames() {
    for (unsigned int i = 0; i < items_.size(); ++i) {
        if (items_[i].name.find('.', 0) == std::string::npos) {
            items_[i].name.append(".");
            items_[i].name.append(vdns_type_.domain_name);
            query_name_update_ = true;
        }
    }
}

// In case we added domain name to the queries, the response to the VM 
// should not have the domain name. Update the offsets in the DnsItems
// accordingly.
void DnsHandler::UpdateOffsets(DnsItem &item, bool name_update_required) {
    if (!query_name_update_)
        return;

    uint16_t msg_offset = (resp_ptr_ - (uint8_t *)dns_) | 0xC000;
    if (name_update_required) {
        name_encoder_.AddName(item.name, msg_offset, item.name_plen,
                              item.name_offset);
    }
    msg_offset += BindUtil::DataLength(item.name_plen, item.name_offset,
                                       item.name.size()) + 10;
    if (item.type == DNS_TYPE_SOA) {
        name_encoder_.AddName(item.soa.primary_ns, msg_offset, item.soa.ns_plen,
                              item.soa.ns_offset);
        msg_offset += BindUtil::DataLength(item.soa.ns_plen, item.soa.ns_offset,
                                           item.soa.primary_ns.size());
        name_encoder_.AddName(item.soa.mailbox, msg_offset,
                              item.soa.mailbox_plen, item.soa.mailbox_offset);
    } else {
        name_encoder_.AddName(item.data, msg_offset,
                              item.data_plen, item.data_offset);
    }
}

void DnsHandler::UpdateGWAddress(DnsItem &item) {
    boost::system::error_code ec;
    if (item.type == DNS_A_RECORD && 
        (item.data == Agent::GetInstance()->GetDnsXmppServer(0) ||
         item.data == Agent::GetInstance()->GetDnsXmppServer(1))) {
        boost::asio::ip::address_v4 addr(pkt_info_->ip_daddr);
        item.data = addr.to_string(ec);
    }
}

void DnsHandler::Update(DnsUpdateIpc *update) {
    bool free_update = true;
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    DnsUpdateIpc *update_req = dns_proto->FindUpdateRequest(update);
    if (update_req) {
        DnsUpdateData *data = update_req->xmpp_data;
        for (DnsItems::iterator item = update->xmpp_data->items.begin(); 
             item != update->xmpp_data->items.end();) {
            if ((*item).IsDelete()) {
                if (!data->DelItem(*item))
                    update->xmpp_data->items.erase(item++);
                else
                    ++item;
            } else {
                if (!data->AddItem(*item))
                    update->xmpp_data->items.erase(item++);
                else
                    ++item;
            }
        }
        if (!data->items.size()) {
            dns_proto->DelUpdateRequest(update_req);
            delete update_req;
        }
        if (!update->xmpp_data->items.size())
            goto done;
    } else {
        dns_proto->AddUpdateRequest(update);
        free_update = false;
    }

    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        AgentDnsXmppChannel *channel = Agent::GetInstance()->GetAgentDnsXmppChannel(i);
        SendXmppUpdate(channel, update->xmpp_data);
    }

done:
    if (free_update)
        delete update;
}

void DnsHandler::DelUpdate(DnsUpdateIpc *update) {
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    DnsUpdateIpc *update_req = dns_proto->FindUpdateRequest(update);
    while (update_req) {
        for (DnsItems::iterator item = update_req->xmpp_data->items.begin(); 
             item != update_req->xmpp_data->items.end(); ++item) {
            // in case of delete, set the class to NONE and ttl to 0
            (*item).eclass = DNS_CLASS_NONE;
            (*item).ttl = 0;
        }
        for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
            AgentDnsXmppChannel *channel = 
                        Agent::GetInstance()->GetAgentDnsXmppChannel(i);
            SendXmppUpdate(channel, update_req->xmpp_data);
        }
        dns_proto->DelUpdateRequest(update_req);
        delete update_req;
        update_req = dns_proto->FindUpdateRequest(update);
    }
    delete update;
}

void DnsHandler::UpdateStats() {
    DnsProto *dns_proto = Agent::GetInstance()->GetDnsProto();
    switch (dns_->flags.ret) {
        case DNS_ERR_NO_ERROR:
            dns_proto->IncrStatsRes();
            break;

        case DNS_ERR_NO_IMPLEMENT:
            dns_proto->IncrStatsUnsupp();
            break;

        case DNS_ERR_FORMAT_ERROR:
        case DNS_ERR_NO_SUCH_NAME:
        case DNS_ERR_SERVER_FAIL:
        case DNS_ERR_NOT_AUTH:
        default:
            dns_proto->IncrStatsFail();
            break;
    }
}

bool DnsHandler::TimerExpiry(uint16_t xid) {
    SendDnsIpc(DnsHandler::DNS_TIMER_EXPIRED, xid);
    return false;
}

std::string DnsHandler::DnsItemsToString(std::vector<DnsItem> &items) {
    std::string str;
    for (unsigned int i = 0; i < items.size(); ++i) {
        str.append(items[i].ToString());
        str.append(" ");
    }
    return str;
}

void DnsHandler::SendDnsIpc(uint8_t *pkt) {
    DnsIpc *ipc = new DnsIpc(pkt, 0, NULL, DnsHandler::DNS_BIND_RESPONSE);
    Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::DNS,
                                                            ipc);
}

void DnsHandler::SendDnsIpc(IpcCommand cmd, uint16_t xid, uint8_t *msg,
                            DnsHandler *handler) {
    DnsIpc *ipc = new DnsIpc(msg, xid, handler, cmd);
    Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::DNS,
                                                            ipc);
}

void DnsHandler::SendDnsUpdateIpc(DnsUpdateData *data,
                                  DnsAgentXmpp::XmppType type,
                                  const VmInterface *vm, bool floating) {
    DnsUpdateIpc *ipc = new DnsUpdateIpc(type, data, vm, floating);
    Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::DNS,
                                                            ipc);
}

void DnsHandler::SendDnsUpdateIpc(const VmInterface *vm,
                                  const std::string &new_vdns,
                                  const std::string &old_vdns,
                                  const std::string &new_dom,
                                  uint32_t ttl, bool is_floating) {
    DnsUpdateIpc *ipc = new DnsUpdateIpc(vm, new_vdns, old_vdns, new_dom,
                                         ttl, is_floating);
    Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::DNS,
                                                            ipc);
}

void DnsHandler::SendDnsUpdateIpc(AgentDnsXmppChannel *channel) {
    DnsUpdateAllIpc *ipc = new DnsUpdateAllIpc(channel);
    Agent::GetInstance()->pkt()->pkt_handler()->SendMessage(PktHandler::DNS,
                                                            ipc);
}

////////////////////////////////////////////////////////////////////////////////
