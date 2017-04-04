/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/dns.h>
#include <bind/bind_util.h>
#include <bind/bind_resolver.h>
#include <mgr/dns_mgr.h>
#include <mgr/dns_oper.h>
#include <bind/named_config.h>
#include <agent/agent_xmpp_channel.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/common/vns_types.h>
#include <vnc_cfg_types.h>

#include <boost/bind.hpp>
#include "base/logging.h"
#include "base/task.h"
#include "base/util.h"
#include "bind/bind_util.h"
#include "cfg/dns_config.h"
#include "cfg/config_listener.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_table.h"
#include "cmn/dns.h"
#include "xmpp/xmpp_server.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////

const std::string DnsConfig::EventString[] = {
    "Add",
    "Change",
    "Delete"
};
DnsConfig::Callback DnsConfig::VdnsCallback;
DnsConfig::Callback DnsConfig::VdnsRecordCallback;
DnsConfig::ZoneCallback DnsConfig::VdnsZoneCallback;
VnniConfig::DataMap VnniConfig::vnni_config_;
IpamConfig::DataMap IpamConfig::ipam_config_;
VirtualDnsConfig::DataMap VirtualDnsConfig::virt_dns_config_;
VirtualDnsRecordConfig::DataMap VirtualDnsRecordConfig::virt_dns_rec_config_;
GlobalQosConfig *GlobalQosConfig::singleton_;

////////////////////////////////////////////////////////////////////////////////

VnniConfig::VnniConfig(IFMapNode *node)
                     : DnsConfig(node->name()), ipam_(NULL) {
    vnni_config_.insert(DataPair(GetName(), this));
    UpdateIpam(node);
}

VnniConfig::~VnniConfig() {
    vnni_config_.erase(name_);
}

void VnniConfig::OnAdd(IFMapNode *node) {
    MarkValid();
    FindSubnets(node, subnets_);
    if (!ipam_) {
        UpdateIpam(node);
    }
    if (ipam_ && ipam_->IsValid() && ipam_->GetVirtualDns()) {
        Subnets subnets;
        NotifySubnets(subnets, subnets_, ipam_->GetVirtualDns());
        ipam_->GetVirtualDns()->NotifyPendingDnsRecords();
    }
}

void VnniConfig::OnDelete() {
    MarkDelete();
    if (ipam_) {
        if (ipam_->IsValid() && ipam_->GetVirtualDns()) {
            for (unsigned int i = 0; i < subnets_.size(); i++) {
                VdnsZoneCallback(subnets_[i], ipam_->GetVirtualDns(),
                                 DnsConfig::CFG_DELETE);
            }
            ipam_->GetVirtualDns()->NotifyPendingDnsRecords();
        }
        ipam_->DelVnni(this);
    }
}

void VnniConfig::OnChange(IFMapNode *node) {
    Subnets old_subnets;
    subnets_.swap(old_subnets);
    FindSubnets(node, subnets_);
    if (!ipam_) {
        UpdateIpam(node);
    }
    if (ipam_ && ipam_->IsValid() && ipam_->GetVirtualDns() &&
        NotifySubnets(old_subnets, subnets_, ipam_->GetVirtualDns())) {
            ipam_->GetVirtualDns()->NotifyPendingDnsRecords();
    }
}

void VnniConfig::UpdateIpam(IFMapNode *node) {
    if (node) {
        IFMapNode *ipam_node = Dns::GetDnsConfigManager()->FindTarget(node,
                               "virtual-network-network-ipam", "network-ipam");
        if (ipam_node == NULL) {
            DNS_TRACE(DnsError, "VirtualNetworkNetworkIpam <" + GetName() +
                                "> does not have Ipam link");
            return;
        }
        ipam_ = IpamConfig::Find(ipam_node->name());
        if (!ipam_) {
            ipam_ = new IpamConfig(ipam_node);
        }
        ipam_->AddVnni(this);
    }
}

void VnniConfig::FindSubnets(IFMapNode *node, Subnets &subnets) {
    if (!node || node->IsDeleted())
        return;

    autogen::VirtualNetworkNetworkIpam *vnni = 
        static_cast<autogen::VirtualNetworkNetworkIpam *> (node->GetObject());
    if (!vnni)
        return;

    const autogen::VnSubnetsType &subnets_type = vnni->data();
    for (unsigned int i = 0; i < subnets_type.ipam_subnets.size(); ++i) {
        Subnet subnet(subnets_type.ipam_subnets[i].subnet.ip_prefix, 
               subnets_type.ipam_subnets[i].subnet.ip_prefix_len);
        subnets.push_back(subnet);
    }
    std::sort(subnets.begin(), subnets.end());
}

bool VnniConfig::NotifySubnets(Subnets &old_nets, Subnets &new_nets,
                         VirtualDnsConfig *vdns) {
    bool change = false;
    Subnets::iterator it_old = old_nets.begin();
    Subnets::iterator it_new = new_nets.begin();
    while (it_old != old_nets.end() && it_new != new_nets.end()) {
        if (*it_old < *it_new) {
            // old entry is deleted
            it_old->MarkDelete();
            VdnsZoneCallback(*it_old, vdns, DnsConfig::CFG_DELETE);
            change = true;
            it_old++;
        } else if (*it_new < *it_old) {
            // new entry
            VdnsZoneCallback(*it_new, vdns, DnsConfig::CFG_ADD);
            change = true;
            it_new++;
        } else {
            // no change in entry
            it_old++;
            it_new++;
        }   
    }   

    // delete remaining old entries
    for (; it_old != old_nets.end(); ++it_old) {
        it_old->MarkDelete();
        VdnsZoneCallback(*it_old, vdns, DnsConfig::CFG_DELETE);
        change = true;
    }   

    // add remaining new entries
    for (; it_new != new_nets.end(); ++it_new) {
        VdnsZoneCallback(*it_new, vdns, DnsConfig::CFG_ADD);
        change = true;
    }

    return change;
}

VnniConfig *VnniConfig::Find(std::string name) {
    if (name.empty())
        return NULL;
    DataMap::iterator iter = vnni_config_.find(name);
    if (iter != vnni_config_.end())
        return iter->second;
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////

IpamConfig::IpamConfig(IFMapNode *node) 
    : DnsConfig(node->name()), virtual_dns_(NULL) {
    GetObject(node, rec_);
    ipam_config_.insert(DataPair(GetName(), this));
}

IpamConfig::~IpamConfig() {
    for (VnniList::iterator it = vnni_list_.begin();
         it != vnni_list_.end(); ++it) {
        (*it)->ipam_ = NULL;
    }
    ipam_config_.erase(name_);
}

void IpamConfig::OnAdd(IFMapNode *node) {
    MarkValid();
    GetObject(node, rec_);
    Add(VirtualDnsConfig::Find(GetVirtualDnsName()));
    if (GetVirtualDns())
        GetVirtualDns()->NotifyPendingDnsRecords();
}

void IpamConfig::OnDelete() {
    Delete();
    if (GetVirtualDns())
        GetVirtualDns()->NotifyPendingDnsRecords();
}

void IpamConfig::OnChange(IFMapNode *node) {
    autogen::IpamType new_rec;
    GetObject(node, new_rec);
    if (new_rec.ipam_dns_server.virtual_dns_server_name != GetVirtualDnsName()) {
        Delete();
        if (GetVirtualDns())
            GetVirtualDns()->NotifyPendingDnsRecords();
        ClearDelete();
        rec_ = new_rec;
        Add(VirtualDnsConfig::Find(GetVirtualDnsName()));
        if (GetVirtualDns())
            GetVirtualDns()->NotifyPendingDnsRecords();
    } else
        rec_ = new_rec;
}

void IpamConfig::Add(VirtualDnsConfig *vdns) {
    virtual_dns_ = vdns;
    if (virtual_dns_) {
        virtual_dns_->AddIpam(this);
        Notify(DnsConfig::CFG_ADD);
    }
}

void IpamConfig::Delete() {
    MarkDelete();
    if (virtual_dns_) {
        Notify(DnsConfig::CFG_DELETE);
        virtual_dns_->DelIpam(this);
    }
}

void IpamConfig::Notify(DnsConfigEvent ev) {
    for (IpamConfig::VnniList::iterator it = vnni_list_.begin();
         it != vnni_list_.end(); ++it) {
        Subnets &subnets = (*it)->GetSubnets();
        for (unsigned int i = 0; i < subnets.size(); i++) {
            VdnsZoneCallback(subnets[i], virtual_dns_, ev);
        }
    }
}

bool IpamConfig::GetObject(IFMapNode *node, autogen::IpamType &data) {
    if (!node)
        return false;

    autogen::NetworkIpam *ipam =
        static_cast<autogen::NetworkIpam *>(node->GetObject());
    if (!ipam)
        return false;

    data = ipam->mgmt();
    return true;
}

void IpamConfig::Trace(const std::string &ev) {
    DNS_TRACE(IpamTrace, name_, GetVirtualDnsName(), ev);
}

IpamConfig *IpamConfig::Find(std::string name) {
    if (name.empty())
        return NULL;
    DataMap::iterator iter = ipam_config_.find(name);
    if (iter != ipam_config_.end())
        return iter->second;
    return NULL;
}

void IpamConfig::AssociateIpamVdns(VirtualDnsConfig *vdns) {
    for (DataMap::iterator iter = ipam_config_.begin();
         iter != ipam_config_.end(); ++iter) {
        IpamConfig *ipam = iter->second;
        if (!ipam->GetVirtualDns() &&
            ipam->GetVirtualDnsName() == vdns->GetName()) {
            ipam->virtual_dns_ = vdns; 
            vdns->AddIpam(ipam);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

VirtualDnsConfig::VirtualDnsConfig(IFMapNode *node) : DnsConfig(node->name()) {
    GetObject(node, rec_);
    old_rec_ = rec_;
    virt_dns_config_.insert(DataPair(GetName(), this));
}

VirtualDnsConfig::VirtualDnsConfig(const std::string &name) : DnsConfig(name) {
    rec_.external_visible = false;
    rec_.reverse_resolution = false;
    old_rec_ = rec_;
    virt_dns_config_.insert(DataPair(GetName(), this));
}

VirtualDnsConfig::~VirtualDnsConfig() {
    virt_dns_config_.erase(name_);
}

void VirtualDnsConfig::OnAdd(IFMapNode *node) {
    if (!GetObject(node, rec_) || GetDomainName().empty())
        return;
    MarkValid();
    // Update any ipam configs dependent on this virtual dns
    IpamConfig::AssociateIpamVdns(this);
    // Update any records dependent on this virtual dns
    VirtualDnsRecordConfig::UpdateVirtualDns(this);
    VdnsCallback(this, DnsConfig::CFG_ADD);
    old_rec_ = rec_;
    // No notification for subnets is required here as the config was
    // available in the above notify
    NotifyPendingDnsRecords();
    Trace("Add");
}

void VirtualDnsConfig::OnDelete() {
    Trace("Delete");
    MarkDelete();
    VdnsCallback(this, DnsConfig::CFG_DELETE);
    for (VirtualDnsConfig::IpamList::iterator iter = ipams_.begin();
         iter != ipams_.end(); ++iter) {
        (*iter)->virtual_dns_ = NULL;
    }
    for (VirtualDnsConfig::VDnsRec::iterator it =
         virtual_dns_records_.begin();
         it != virtual_dns_records_.end(); ++it) {
        (*it)->virt_dns_ = NULL;
        (*it)->ClearNotified();
    }
    NotifyPendingDnsRecords();
}

void VirtualDnsConfig::OnChange(IFMapNode *node) {
    Trace("Change");
    if (!GetObject(node, rec_) || !HasChanged())
        return;

    bool notify = false;
    if (!IsValid()) {
        MarkValid();
        // Update any ipam configs dependent on this virtual dns
        IpamConfig::AssociateIpamVdns(this);
        // Update any records dependent on this virtual dns
        VirtualDnsRecordConfig::UpdateVirtualDns(this);
        notify = true;
    }

    VdnsCallback(this, DnsConfig::CFG_CHANGE);
    old_rec_ = rec_;
    if (notify) NotifyPendingDnsRecords();
}

void VirtualDnsConfig::AddRecord(VirtualDnsRecordConfig *record) {
    virtual_dns_records_.insert(record);
}

void VirtualDnsConfig::DelRecord(VirtualDnsRecordConfig *record) {
    virtual_dns_records_.erase(record);
}

bool VirtualDnsConfig::GetObject(IFMapNode *node,
                                 autogen::VirtualDnsType &data) {
    if (!node)
        return false;

    autogen::VirtualDns *dns =
        static_cast<autogen::VirtualDns *>(node->GetObject());
    // if (!dns || !dns->IsPropertySet(autogen::VirtualDns::ID_PERMS))
    if (!dns)
        return false;

    data = dns->data();
    return true;
}

bool VirtualDnsConfig::GetSubnet(uint32_t addr, Subnet &subnet) const {
    for (IpamList::iterator ipam_iter = ipams_.begin();
         ipam_iter != ipams_.end(); ++ipam_iter) {
        IpamConfig *ipam = *ipam_iter;
        const IpamConfig::VnniList &vnni = ipam->GetVnniList();
        for (IpamConfig::VnniList::iterator vnni_it = vnni.begin();
             vnni_it != vnni.end(); ++vnni_it) {
            Subnets &subnets = (*vnni_it)->GetSubnets();
            for (unsigned int i = 0; i < subnets.size(); i++) {
                uint32_t mask = 
                    subnets[i].plen ? (0xFFFFFFFF << (32 - subnets[i].plen)) : 0;
                if ((addr & mask) == (subnets[i].prefix.to_ulong() & mask)) {
                    subnet = subnets[i];
                    return true;
                }
            }
        }
    }
    return false;
}

void VirtualDnsConfig::NotifyPendingDnsRecords() {
    for (VirtualDnsConfig::VDnsRec::iterator it = virtual_dns_records_.begin();
         it != virtual_dns_records_.end(); ++it) {
        VirtualDnsRecordConfig *rec = *it;
        if (rec->CanNotify()) {
            if (!rec->IsNotified()) {
                VdnsRecordCallback(rec, DnsConfig::CFG_ADD);
            }
        } else {
            rec->ClearNotified();
        }
    }
}

bool VirtualDnsConfig::HasChanged() {
    if (rec_.domain_name == old_rec_.domain_name &&
        rec_.dynamic_records_from_client == old_rec_.dynamic_records_from_client &&
        rec_.record_order == old_rec_.record_order &&
        rec_.default_ttl_seconds == old_rec_.default_ttl_seconds &&
        rec_.next_virtual_DNS == old_rec_.next_virtual_DNS &&
        rec_.external_visible == old_rec_.external_visible &&
        rec_.reverse_resolution == old_rec_.reverse_resolution)
        return false;
    return true;
}

void VirtualDnsConfig::VirtualDnsTrace(VirtualDnsTraceData &rec) {
    rec.name = name_;
    rec.dns_name = rec_.domain_name;
    rec.dns_dyn_rec = rec_.dynamic_records_from_client;
    rec.dns_order = rec_.record_order;
    rec.dns_ttl = rec_.default_ttl_seconds;
    rec.dns_next = rec_.next_virtual_DNS;
    rec.installed = (IsNotified() ? "true" : "false");
    rec.floating_ip_record = rec_.floating_ip_record;
    rec.external_visible = (rec_.external_visible ? "yes" : "no");
    rec.reverse_resolution = (rec_.reverse_resolution ? "yes" : "no");
    rec.flags = flags_;
}

void VirtualDnsConfig::Trace(const std::string &ev) {
    VirtualDnsTraceData rec;
    VirtualDnsTrace(rec);
    DNS_TRACE(VirtualDnsTrace, ev, rec);
}

std::string VirtualDnsConfig::GetViewName() const { 
    std::string name(GetName());
    BindUtil::RemoveSpecialChars(name);
    return name; 
}

std::string VirtualDnsConfig::GetNextDns() const { 
    std::string name(rec_.next_virtual_DNS);
    BindUtil::RemoveSpecialChars(name);
    return name; 
}   

bool VirtualDnsConfig::DynamicUpdatesEnabled() const { 
    return rec_.dynamic_records_from_client;
}

VirtualDnsConfig *VirtualDnsConfig::Find(std::string name) {
    if (name.empty())
        return NULL;
    DataMap::iterator iter = virt_dns_config_.find(name);
    if (iter != virt_dns_config_.end())
        return iter->second;
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////

VirtualDnsRecordConfig::VirtualDnsRecordConfig(IFMapNode *node)
      : DnsConfig(node->name()), virt_dns_(NULL), src_(VirtualDnsRecordConfig::Config) {
    GetObject(node, rec_);
    virt_dns_rec_config_.insert(DataPair(GetName(), this));
    UpdateVdns(node);
}

VirtualDnsRecordConfig::VirtualDnsRecordConfig(const std::string &name, 
                                               const std::string &vdns_name,
                                               const DnsItem &item)
      : DnsConfig(name), rec_(item), virt_dns_(NULL),
        src_(VirtualDnsRecordConfig::Agent) {
    virt_dns_rec_config_.insert(DataPair(GetName(), this));
    virt_dns_ = VirtualDnsConfig::Find(vdns_name);
    if (!virt_dns_) {
        virt_dns_ = new VirtualDnsConfig(vdns_name);
        virt_dns_->AddRecord(this);
    }
    virtual_dns_name_ = virt_dns_->GetName();
}

VirtualDnsRecordConfig::~VirtualDnsRecordConfig() {
    virt_dns_rec_config_.erase(name_);
}

void VirtualDnsRecordConfig::OnAdd(IFMapNode *node) {
    MarkValid();
    if (!virt_dns_) {
        if (!UpdateVdns(node)) {
            ClearValid();
        }
    } else if (!virt_dns_->IsValid()) {
        virt_dns_->AddRecord(this);
    } else {
        virt_dns_->AddRecord(this);
        if (CanNotify()) {
            VdnsRecordCallback(this, DnsConfig::CFG_ADD);
        }
    }
    Trace("Add");
}

void VirtualDnsRecordConfig::OnDelete() {
    Trace("Delete");
    MarkDelete();
    if (virt_dns_) {
        if (IsNotified()) {
            VdnsRecordCallback(this, DnsConfig::CFG_DELETE);
        }
        virt_dns_->DelRecord(this);
    }
}

void VirtualDnsRecordConfig::OnChange(IFMapNode *node) {
    DnsItem new_rec;
    if (!GetObject(node, new_rec) ||
        (IsValid() && !HasChanged(new_rec))) {
        return;
    }
    if (!virt_dns_) {
        MarkValid();
        if (!UpdateVdns(node)) {
            ClearValid();
        }
    }
    // For records, notify a change with a delete of the old record
    // followed by addition of new record; If only TTL has changed,
    // do not delete, as the order of delete and add processing is
    // not controlled and we might end up with deleted record
    if (IsNotified() && !OnlyTtlChange(new_rec)) {
        VdnsRecordCallback(this, DnsConfig::CFG_DELETE);
    }
    OnChange(new_rec);
}

void VirtualDnsRecordConfig::OnChange(const DnsItem &new_rec) {
    rec_ = new_rec;
    if (CanNotify()) {
        VdnsRecordCallback(this, DnsConfig::CFG_ADD);
    }
    Trace("Change");
}

bool VirtualDnsRecordConfig::UpdateVdns(IFMapNode *node) {
    if (!node) {
        return false;
    }

    IFMapNode *vdns_node = Dns::GetDnsConfigManager()->FindTarget(node,
                           "virtual-DNS-virtual-DNS-record");
    if (vdns_node == NULL) {
        DNS_TRACE(DnsError, "Virtual DNS Record <" + GetName() +
                            "> does not have virtual DNS link");
        return false;
    }
    virt_dns_ = VirtualDnsConfig::Find(vdns_node->name());
    if (!virt_dns_) {
        virt_dns_ = new VirtualDnsConfig(vdns_node);
    }
    virt_dns_->AddRecord(this);
    virtual_dns_name_ = virt_dns_->GetName();
    return true;
}

bool VirtualDnsRecordConfig::CanNotify() {
    if (!IsValid() || !virt_dns_ || !virt_dns_->IsValid())
        return false;

    if (rec_.eclass == DNS_CLASS_IN && rec_.type == DNS_PTR_RECORD) {
        if (!virt_dns_->IsReverseResolutionEnabled()) {
            return false;
        }
        uint32_t addr;
        if (BindUtil::IsIPv4(rec_.name, addr) ||
            BindUtil::GetAddrFromPtrName(rec_.name, addr)) {
            Subnet net;
            return virt_dns_->GetSubnet(addr, net);
        }
        if (!IsErrorLogged()) {
            DNS_CONFIGURATION_LOG(
             g_vns_constants.CategoryNames.find(Category::DNSAGENT)->second,
             SandeshLevel::SYS_ERR, "Invalid PTR Name",
             GetName(), rec_.name);
            MarkErrorLogged();
        }
        return false;
    }

    return true;
}

bool VirtualDnsRecordConfig::HasChanged(DnsItem &rhs) {
    if (rec_.name == rhs.name && rec_.type == rhs.type &&
        rec_.eclass == rhs.eclass && rec_.data == rhs.data &&
        rec_.ttl == rhs.ttl && rec_.priority == rhs.priority)
        return false;
    return true;
}

bool VirtualDnsRecordConfig::OnlyTtlChange(DnsItem &rhs) {
    if (rec_.name == rhs.name && rec_.type == rhs.type &&
        rec_.eclass == rhs.eclass && rec_.data == rhs.data &&
        rec_.ttl != rhs.ttl && rec_.priority == rhs.priority)
        return true;
    return false;
}

autogen::VirtualDnsType VirtualDnsRecordConfig::GetVDns() const { 
    autogen::VirtualDnsType data;
    data.dynamic_records_from_client = false;
    data.default_ttl_seconds = 0;
    if (virt_dns_)
        data = virt_dns_->GetVDns();
    return data;
}

std::string VirtualDnsRecordConfig::GetViewName() const { 
    if (virt_dns_) {
        return virt_dns_->GetViewName(); 
    } else
        return "";
}

bool VirtualDnsRecordConfig::GetObject(IFMapNode *node, DnsItem &item) {
    if (!node)
        return false;

    autogen::VirtualDnsRecord *rec =
        static_cast<autogen::VirtualDnsRecord *>(node->GetObject());
    if (!rec)
        return false;

    autogen::VirtualDnsRecordType rec_data = rec->data();
    item.eclass = BindUtil::DnsClass(rec_data.record_class);
    item.type = BindUtil::DnsType(rec_data.record_type);
    item.name = rec_data.record_name;
    item.data = rec_data.record_data;
    item.ttl = rec_data.record_ttl_seconds;
    item.priority = rec_data.record_mx_preference;
    return true;
}

void VirtualDnsRecordConfig::VirtualDnsRecordTrace(VirtualDnsRecordTraceData &rec) {
    rec.name = name_;
    rec.rec_name = rec_.name;
    rec.rec_type = BindUtil::DnsType(rec_.type);
    if (rec_.eclass == DNS_CLASS_IN)
        rec.rec_class = "IN";
    rec.rec_data = rec_.data;
    rec.rec_ttl = rec_.ttl;
    if (src_ == VirtualDnsRecordConfig::Config)
        rec.source = "Config";
    else
        rec.source = "Agent";
    if (IsNotified())
        rec.installed = "true";
    else
        rec.installed = "false";
    rec.flags = flags_;
}

void VirtualDnsRecordConfig::Trace(const std::string &ev) {
    VirtualDnsRecordTraceData rec;
    VirtualDnsRecordTrace(rec);
    std::string dns_name;
    if (virt_dns_)
        dns_name = virt_dns_->name_;
    DNS_TRACE(VirtualDnsRecordTrace, ev, dns_name, rec);
}

VirtualDnsRecordConfig *VirtualDnsRecordConfig::Find(std::string name) {
    if (name.empty())
        return NULL;
    DataMap::iterator iter = virt_dns_rec_config_.find(name);
    if (iter != virt_dns_rec_config_.end())
        return iter->second;
    return NULL;
}

// Check virtual dns records having empty VDNS to see if they belong to
// the vdns being added now; update the reference accordingly
void VirtualDnsRecordConfig::UpdateVirtualDns(VirtualDnsConfig *vdns) {
    for (DataMap::iterator it = virt_dns_rec_config_.begin();
         it != virt_dns_rec_config_.end(); ++it) {
        if (!it->second || it->second->GetVirtualDns()) continue;
        if (it->second->virtual_dns_name_ == vdns->GetName()) {
            it->second->virt_dns_ = vdns;
            vdns->AddRecord(it->second);
        }
    }
}

GlobalQosConfig::GlobalQosConfig(IFMapNode *node)
    : DnsConfig(node->name()) {
    assert(singleton_ == NULL);
    singleton_ = this;
}

GlobalQosConfig::~GlobalQosConfig() {
    singleton_ = NULL;
}

GlobalQosConfig* GlobalQosConfig::Find(const string &name) {
    return singleton_;
}

void GlobalQosConfig::SetDscp() {
    XmppServer *server = Dns::GetXmppServer();
    if (server) {
        server->SetDscpValue(control_dscp_);
    }
}

void GlobalQosConfig::OnAdd(IFMapNode *node) {
    MarkValid();
    autogen::GlobalQosConfig *qos =
        static_cast<autogen::GlobalQosConfig *>(node->GetObject());
    if (!qos)
        return;
    const autogen::ControlTrafficDscpType &dscp = qos->control_traffic_dscp();
    if (control_dscp_ != dscp.control) {
        control_dscp_ = dscp.control;
        SetDscp();
    }
    if (analytics_dscp_ != dscp.analytics) {
        analytics_dscp_ = dscp.analytics;
    }
}

void GlobalQosConfig::OnDelete() {
    if (control_dscp_ != 0) {
        control_dscp_ = 0;
        SetDscp();
    }
    analytics_dscp_ = 0;
}

void GlobalQosConfig::OnChange(IFMapNode *node) {
    OnAdd(node);
}

////////////////////////////////////////////////////////////////////////////////
