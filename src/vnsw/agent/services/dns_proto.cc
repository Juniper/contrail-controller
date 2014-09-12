/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#if defined(__FreeBSD__)
#include <sys/types.h>
#endif
#include "oper/interface_common.h"
#include "services/dns_proto.h"
#include "bind/bind_resolver.h"
#include "cmn/agent_cmn.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_table.h"
#include "pkt/pkt_init.h"
#include "controller/controller_dns.h"
#include "oper/vn.h"

void DnsProto::Shutdown() {
}

void DnsProto::IoShutdown() {
    BindResolver::Shutdown();

    for (DnsBindQueryMap::iterator it = dns_query_map_.begin();
         it != dns_query_map_.end(); ) {
        DnsBindQueryMap::iterator next = it++;
        delete it->second;
        it = next;
    }

    curr_vm_requests_.clear();
    // Following tables should be deleted when all VMs are gone
    assert(update_set_.empty());
    assert(all_vms_.empty());
}

void DnsProto::ConfigInit() {
    std::vector<BindResolver::DnsServer> dns_servers;
    for (int i = 0; i < MAX_XMPP_SERVERS; i++) {
        std::string server = agent()->dns_server(i);
        if (server != "")
            dns_servers.push_back(BindResolver::DnsServer(
                                  server, agent()->dns_server_port(i)));
    }
    BindResolver::Init(*agent()->event_manager()->io_service(), dns_servers,
                       boost::bind(&DnsProto::SendDnsIpc, this, _1));
}

DnsProto::DnsProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::DNS, io),
    xid_(0), timeout_(kDnsTimeout), max_retries_(kDnsMaxRetries) {
    lid_ = agent->interface_table()->Register(
                  boost::bind(&DnsProto::InterfaceNotify, this, _2));
    Vnlid_ = agent->vn_table()->Register(
                  boost::bind(&DnsProto::VnNotify, this, _2));
    agent->domain_config_table()->RegisterIpamCb(
                  boost::bind(&DnsProto::IpamNotify, this, _1));
    agent->domain_config_table()->RegisterVdnsCb(
                  boost::bind(&DnsProto::VdnsNotify, this, _1));
    agent->interface_table()->set_update_floatingip_cb
        (boost::bind(&DnsProto::UpdateFloatingIp, this, _1, _2, _3, _4));

    AgentDnsXmppChannel::set_dns_message_handler_cb
        (boost::bind(&DnsProto::SendDnsUpdateIpc, this, _1, _2, _3, _4));
    AgentDnsXmppChannel::set_dns_xmpp_event_handler_cb
        (boost::bind(&DnsProto::SendDnsUpdateIpc, this, _1));
}

DnsProto::~DnsProto() {
    agent_->interface_table()->Unregister(lid_);
    agent_->vn_table()->Unregister(Vnlid_);
}

ProtoHandler *DnsProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new DnsHandler(agent(), info, io);
}

void DnsProto::InterfaceNotify(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (itf->type() != Interface::VM_INTERFACE)
        return;

    const VmInterface *vmitf = static_cast<VmInterface *>(entry);
    if (entry->IsDeleted()) {
        SendDnsUpdateIpc(NULL, DnsAgentXmpp::Update, vmitf, false);
        SendDnsUpdateIpc(NULL, DnsAgentXmpp::Update, vmitf, true);
        all_vms_.erase(vmitf);
        DNS_BIND_TRACE(DnsBindTrace, "Vm Interface deleted : " <<
                       vmitf->vm_name());
        // remove floating ip entries from fip list
        const VmInterface::FloatingIpList &fip = vmitf->floating_ip_list();
        for (VmInterface::FloatingIpSet::iterator it = fip.list_.begin(),
             next = fip.list_.begin(); it != fip.list_.end(); it = next) {
            ++next;
            UpdateFloatingIp(vmitf, it->vn_.get(), it->floating_ip_, true);
        }
    } else {
        autogen::VirtualDnsType vdns_type;
        std::string vdns_name;
        GetVdnsData(vmitf->vn(), vmitf->ip_addr(),
                    vdns_name, vdns_type);
        VmDataMap::iterator it = all_vms_.find(vmitf);
        if (it == all_vms_.end()) {
            if (!UpdateDnsEntry(vmitf, vmitf->vn(), vmitf->vm_name(), vdns_name,
                                vmitf->ip_addr(), false, false))
                vdns_name = "";
            IpVdnsMap vmdata;
            vmdata.insert(IpVdnsPair(vmitf->ip_addr().to_ulong(), vdns_name));
            all_vms_.insert(VmDataPair(vmitf, vmdata));
            DNS_BIND_TRACE(DnsBindTrace, "Vm Interface added : " <<
                           vmitf->vm_name());
        } else {
            CheckForUpdate(it->second, vmitf, vmitf->vn(),
                           vmitf->ip_addr(), vdns_name, vdns_type);
        }

        // floating ip activate & de-activate are updated via the callback to
        // UpdateFloatingIp; If tenant name is also required in the fip name,
        // it may not be available during activate if the vm interface doesnt
        // have vn at that time. Invoke Update here to handle that case.
        if (vmitf->vn()) {
            const VmInterface::FloatingIpList &fip = vmitf->floating_ip_list();
            for (VmInterface::FloatingIpSet::iterator it = fip.list_.begin();
                 it != fip.list_.end(); ++it) {
                if (it->installed_) {
                    UpdateFloatingIp(vmitf, it->vn_.get(),
                                     it->floating_ip_, false);
                }
            }
        }
    }
}

void DnsProto::VnNotify(DBEntryBase *entry) {
    const VnEntry *vn = static_cast<const VnEntry *>(entry);
    if (vn->IsDeleted()) {
        // remove floating ip entries from this vn in floating ip list
        Ip4Address ip_key(0);
        DnsFipEntryPtr key(new DnsFipEntry(vn, ip_key, NULL));
        DnsFipSet::iterator it = fip_list_.upper_bound(key);
        while (it != fip_list_.end()) {
            DnsFipEntry *entry = (*it).get();
            if (entry->vn_ != vn) {
                break;
            }
            ++it;
            UpdateFloatingIp(entry->interface_, entry->vn_,
                             entry->floating_ip_, true);
        }
        return;
    }
    if (vn->GetVnIpam().size() == 0)
        return;
    DNS_BIND_TRACE(DnsBindTrace, "Vn Notify : " << vn->GetName());
    for (VmDataMap::iterator it = all_vms_.begin(); it != all_vms_.end(); ++it) {
        if (it->first->vn() == vn) {
            std::string vdns_name;
            autogen::VirtualDnsType vdns_type;
            GetVdnsData(vn, it->first->ip_addr(), vdns_name, vdns_type);
            CheckForUpdate(it->second, it->first, it->first->vn(),
                           it->first->ip_addr(), vdns_name, vdns_type);
        }
    }
    Ip4Address ip_key(0);
    DnsFipEntryPtr key(new DnsFipEntry(vn, ip_key, NULL));
    DnsFipSet::iterator it = fip_list_.upper_bound(key);
    while (it != fip_list_.end()) {
        std::string fip_vdns_name;
        DnsFipEntry *entry = (*it).get();
        if (entry->vn_ != vn) {
            break;
        }
        autogen::VirtualDnsType fip_vdns_type;
        GetVdnsData(entry->vn_, entry->floating_ip_,
                    fip_vdns_name, fip_vdns_type);
        CheckForFipUpdate(entry, fip_vdns_name, fip_vdns_type);
        ++it;
    }
}

void DnsProto::IpamNotify(IFMapNode *node) {
    if (node->IsDeleted())
        return;
    DNS_BIND_TRACE(DnsBindTrace, "Ipam Notify : " << node->name());
    ProcessNotify(node->name(), node->IsDeleted(), true);
}

void DnsProto::VdnsNotify(IFMapNode *node) {
    DNS_BIND_TRACE(DnsBindTrace, "Vdns Notify : " << node->name());
    // Update any existing records prior to checking for new ones
    if (!node->IsDeleted()) {
        autogen::VirtualDns *virtual_dns =
                 static_cast <autogen::VirtualDns *> (node->GetObject());
        autogen::VirtualDnsType vdns_type = virtual_dns->data();
        std::string name = node->name();
        MoveVDnsEntry(NULL, name, name, vdns_type, false);
    }
    ProcessNotify(node->name(), node->IsDeleted(), false);
}

void DnsProto::ProcessNotify(std::string name, bool is_deleted, bool is_ipam) {
    for (VmDataMap::iterator it = all_vms_.begin(); it != all_vms_.end(); ++it) {
        std::string vdns_name;
        autogen::VirtualDnsType vdns_type;
        GetVdnsData(it->first->vn(), it->first->ip_addr(),
                    vdns_name, vdns_type);
        // in case of VDNS delete, clear the name
        if (!is_ipam && is_deleted && vdns_name == name)
            vdns_name.clear();
        CheckForUpdate(it->second, it->first, it->first->vn(),
                       it->first->ip_addr(), vdns_name, vdns_type);
    }
    DnsFipSet::iterator it = fip_list_.begin();
    while (it != fip_list_.end()) {
        std::string fip_vdns_name;
        DnsFipEntry *entry = (*it).get();
        autogen::VirtualDnsType fip_vdns_type;
        GetVdnsData(entry->vn_, entry->floating_ip_,
                    fip_vdns_name, fip_vdns_type);
        CheckForFipUpdate(entry, fip_vdns_name, fip_vdns_type);
        ++it;
    }
}

void DnsProto::CheckForUpdate(IpVdnsMap &ipvdns, const VmInterface *vmitf,
                              const VnEntry *vn, const Ip4Address &ip,
                              std::string &vdns_name,
                              const autogen::VirtualDnsType &vdns_type) {
    IpVdnsMap::iterator vmdata_it = ipvdns.find(ip.to_ulong());
    if (vmdata_it == ipvdns.end()) {
        if (UpdateDnsEntry(vmitf, vn, vmitf->vm_name(),
                           vdns_name, ip, false, false))
            ipvdns.insert(IpVdnsPair(ip.to_ulong(), vdns_name));
        else
            ipvdns.insert(IpVdnsPair(ip.to_ulong(), ""));
        return;
    }
    if (vmdata_it->second.empty() && vmdata_it->second != vdns_name) {
        if (UpdateDnsEntry(vmitf, vn, vmitf->vm_name(),
                           vdns_name, ip, false, false))
            vmdata_it->second = vdns_name;
    } else if (vmdata_it->second != vdns_name) {
        if (MoveVDnsEntry(vmitf, vdns_name, vmdata_it->second,
                          vdns_type, false))
            vmdata_it->second = vdns_name;
    }
}

void DnsProto::CheckForFipUpdate(DnsFipEntry *entry, std::string &vdns_name,
                                 const autogen::VirtualDnsType &vdns_type) {
    if (entry->vdns_name_.empty() && entry->vdns_name_ != vdns_name) {
        std::string fip_name;
        if (!GetFipName(entry->interface_, vdns_type,
                        entry->floating_ip_, fip_name))
            vdns_name = "";
        if (UpdateDnsEntry(entry->interface_, entry->vn_, fip_name,
                           vdns_name, entry->floating_ip_, true, false)) {
            entry->vdns_name_.assign(vdns_name);
            entry->fip_name_ = fip_name;
        }
    } else if (entry->vdns_name_ != vdns_name) {
        if (MoveVDnsEntry(entry->interface_, vdns_name, entry->vdns_name_,
                          vdns_type, true)) {
            entry->vdns_name_.assign(vdns_name);
        }
    }
}

// Send A & PTR record entries
bool DnsProto::SendUpdateDnsEntry(const VmInterface *vmitf,
                                  const std::string &name,
                                  const Ip4Address &ip, uint32_t plen,
                                  const std::string &vdns_name,
                                  const autogen::VirtualDnsType &vdns_type,
                                  bool is_floating, bool is_delete) {
    if (!name.size() || !vdns_type.dynamic_records_from_client) {
        DNS_BIND_TRACE(DnsBindTrace, "Not adding DNS entry; Name = " <<
                       name << "Dynamic records allowed = " <<
                       (vdns_type.dynamic_records_from_client ? "yes" : "no"));
        return false;
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
    SendDnsUpdateIpc(data, DnsAgentXmpp::Update, vmitf, is_floating);

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
    SendDnsUpdateIpc(ptr_data, DnsAgentXmpp::Update, vmitf, is_floating);
    return true;
}

// Update the floating ip entries
bool DnsProto::UpdateFloatingIp(const VmInterface *vmitf, const VnEntry *vn,
                                const Ip4Address &ip, bool is_deleted) {
    bool is_floating = true;
    std::string vdns_name;
    autogen::VirtualDnsType vdns_type;
    GetVdnsData(vn, ip, vdns_name, vdns_type);
    DnsFipEntryPtr key(new DnsFipEntry(vn, ip, vmitf));
    DnsFipSet::iterator it = fip_list_.find(key);
    if (it == fip_list_.end()) {
        if (is_deleted)
            return true;
        std::string fip_name;
        if (!GetFipName(vmitf, vdns_type, ip, fip_name))
            vdns_name = "";
        if (!UpdateDnsEntry(vmitf, vn, fip_name,
                            vdns_name, ip, is_floating, false))
            vdns_name = "";
        key.get()->vdns_name_ = vdns_name;
        key.get()->fip_name_ = fip_name;
        fip_list_.insert(key);
    } else {
        if (is_deleted) {
            std::string fip_name;
            UpdateDnsEntry(vmitf, vn, (*it)->fip_name_,
                           (*it)->vdns_name_, ip, is_floating, true);
            fip_list_.erase(key);
        } else {
            DnsFipEntry *entry = (*it).get();
            CheckForFipUpdate(entry, vdns_name, vdns_type);
        }
    }
    return true;
}

bool DnsProto::UpdateDnsEntry(const VmInterface *vmitf, const VnEntry *vn,
                              const std::string &vm_name,
                              const std::string &vdns_name,
                              const Ip4Address &ip,
                              bool is_floating, bool is_deleted) {
    if (!vdns_name.empty()) {
        uint32_t plen = 0;
        const std::vector<VnIpam> &ipam = vn->GetVnIpam();
        unsigned int i;
        for (i = 0; i < ipam.size(); ++i) {
            if (IsIp4SubnetMember(ip, ipam[i].ip_prefix, ipam[i].plen)) {
                plen = ipam[i].plen;
                break;
            }
        }
        if (i == ipam.size()) {
            DNS_BIND_TRACE(DnsBindTrace, "UpdateDnsEntry for VM <" <<
                           vm_name << "> not done for vn <" <<
                           vn->GetName() << "> IPAM doesnt have the subnet; " <<
                           ((is_deleted)? "delete" : "update"));
            return false;
        }

        autogen::VirtualDnsType vdns_type;
        if (agent_->domain_config_table()->GetVDns(vdns_name, &vdns_type)) {
            return SendUpdateDnsEntry(vmitf, vm_name, ip, plen,
                                      vdns_name, vdns_type, is_floating,
                                      is_deleted);
        } else {
            DNS_BIND_TRACE(DnsBindTrace, "UpdateDnsEntry for VM <" <<
                           vm_name << "> entry not " <<
                           ((is_deleted)? "deleted; " : "updated; ") <<
                           "VDNS info for " << vdns_name << " not present");
            return false;
        }
    }

    return true;
}

// Move DNS entries for a VM from one VDNS to another
bool DnsProto::MoveVDnsEntry(const VmInterface *vmitf,
                             std::string &new_vdns_name,
                             std::string &old_vdns_name,
                             const autogen::VirtualDnsType &vdns_type,
                             bool is_floating) {
    std::string name = (vmitf ? vmitf->vm_name() : "All Vms");
    DNS_BIND_TRACE(DnsBindTrace, "VDNS modify for VM <" << name <<
                   " old VDNS : " << old_vdns_name <<
                   " new VDNS : " << new_vdns_name <<
                   " ttl : " << vdns_type.default_ttl_seconds <<
                   " floating : " << (is_floating ? "yes" : "no"));
    SendDnsUpdateIpc(vmitf, new_vdns_name,
                     old_vdns_name, vdns_type.domain_name,
                     (uint32_t)vdns_type.default_ttl_seconds, is_floating);
    return true;
}

bool DnsProto::GetVdnsData(const VnEntry *vn, const Ip4Address &vm_addr,
                           std::string &vdns_name,
                           autogen::VirtualDnsType &vdns_type) {
    if (!vn)
        return false;

    autogen::IpamType ipam_type;
    if (!vn->GetIpamVdnsData(vm_addr, &ipam_type, &vdns_type)) {
        DNS_BIND_TRACE(DnsBindTrace, "Unable to retrieve VDNS data; VN : " <<
                   vn->GetName() << " IP : " << vm_addr.to_string());
        return false;
    }

    vdns_name = ipam_type.ipam_dns_server.virtual_dns_server_name;
    return true;
}

bool DnsProto::GetFipName(const VmInterface *vmitf,
                          const  autogen::VirtualDnsType &vdns_type,
                          const Ip4Address &ip, std::string &fip_name) const {
    std::string fip_name_notation =
        boost::to_lower_copy(vdns_type.floating_ip_record);

    std::string name;
    if (fip_name_notation == "" ||
        fip_name_notation == "dashed-ip" ||
        fip_name_notation == "dashed-ip-tenant-name") {
        name = ip.to_string();
        boost::replace_all(name, ".", "-");
    } else {
        name = vmitf->vm_name();
    }

    if (fip_name_notation == "" ||
        fip_name_notation == "dashed-ip-tenant-name" ||
        fip_name_notation == "vm-name-tenant-name") {
        if (!vmitf->vn())
            return false;
        name += "." + vmitf->vn()->GetProject();
    }

    fip_name = name;
    return true;
}

uint16_t DnsProto::GetTransId() {
    return (++xid_ == 0 ? ++xid_ : xid_);
}

void DnsProto::SendDnsIpc(uint8_t *pkt) {
    DnsIpc *ipc = new DnsIpc(pkt, 0, NULL, DnsProto::DNS_BIND_RESPONSE);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::DNS, ipc);
}

void DnsProto::SendDnsIpc(InterTaskMessage cmd, uint16_t xid, uint8_t *msg,
                          DnsHandler *handler) {
    DnsIpc *ipc = new DnsIpc(msg, xid, handler, cmd);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::DNS, ipc);
}

void DnsProto::SendDnsUpdateIpc(DnsUpdateData *data,
                                DnsAgentXmpp::XmppType type,
                                const VmInterface *vm, bool floating) {
    DnsUpdateIpc *ipc = new DnsUpdateIpc(type, data, vm, floating);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::DNS, ipc);
}

void DnsProto::SendDnsUpdateIpc(const VmInterface *vm,
                                const std::string &new_vdns,
                                const std::string &old_vdns,
                                const std::string &new_dom,
                                uint32_t ttl, bool is_floating) {
    DnsUpdateIpc *ipc = new DnsUpdateIpc(vm, new_vdns, old_vdns, new_dom,
                                         ttl, is_floating);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::DNS, ipc);
}

void DnsProto::SendDnsUpdateIpc(AgentDnsXmppChannel *channel) {
    DnsUpdateAllIpc *ipc = new DnsUpdateAllIpc(channel);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::DNS, ipc);
}

void DnsProto::AddDnsQuery(uint16_t xid, DnsHandler *handler) {
    dns_query_map_.insert(DnsBindQueryPair(xid, handler));
}

void DnsProto::DelDnsQuery(uint16_t xid) {
    dns_query_map_.erase(xid);
}

bool DnsProto::IsDnsQueryInProgress(uint16_t xid) {
    return dns_query_map_.find(xid) != dns_query_map_.end();
}

DnsHandler *DnsProto::GetDnsQueryHandler(uint16_t xid) {
    DnsBindQueryMap::iterator it = dns_query_map_.find(xid);
    if (it != dns_query_map_.end())
        return it->second;
    return NULL;
}

void DnsProto::AddVmRequest(DnsHandler::QueryKey *key) {
    curr_vm_requests_.insert(*key);
}

void DnsProto::DelVmRequest(DnsHandler::QueryKey *key) {
    curr_vm_requests_.erase(*key);
}

bool DnsProto::IsVmRequestDuplicate(DnsHandler::QueryKey *key) {
    return curr_vm_requests_.find(*key) != curr_vm_requests_.end();
}

DnsProto::DnsFipEntry::DnsFipEntry(const VnEntry *vn, const Ip4Address &fip,
                                   const VmInterface *itf)
    : vn_(vn), floating_ip_(fip), interface_(itf) {
}

DnsProto::DnsFipEntry::~DnsFipEntry() {
}

bool DnsProto::DnsFipEntryCmp::operator() (const DnsFipEntryPtr &lhs,
                                           const DnsFipEntryPtr &rhs) const {
    return lhs->IsLess(rhs.get());
}

bool DnsProto::DnsFipEntry::IsLess(const DnsFipEntry *rhs) const {
    if (vn_ != rhs->vn_) {
        return vn_ < rhs->vn_;
    }
    if (floating_ip_ != rhs->floating_ip_) {
        return floating_ip_ < rhs->floating_ip_;
    }
    return interface_ < rhs->interface_;
}
