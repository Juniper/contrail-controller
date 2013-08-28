/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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

using namespace std;

static const char *config_types[] = {
    "virtual-DNS",
    "virtual-DNS-record",
    "network-ipam",
    "virtual-network-network-ipam",
};

const std::string DnsConfigManager::EventString[] = {
    "None",
    "Add",
    "Change",
    "Delete"
};

SandeshTraceBufferPtr DnsConfigTraceBuf(SandeshTraceBufferCreate("Config", 2000));

ConfigDelta::ConfigDelta() {
}

ConfigDelta::ConfigDelta(const ConfigDelta &rhs)
    : id_type(rhs.id_type), id_name(rhs.id_name),
      node(rhs.node), obj(rhs.obj) {
}

ConfigDelta::~ConfigDelta() {
}

void DnsConfigManager::Notify(Subnet &net, 
                              const VirtualDnsConfig *vdns, 
                              EventType event) {
    if (obs_.subnet && vdns) {
        switch (event) {
            case DnsConfigManager::CFG_ADD:
            case DnsConfigManager::CFG_CHANGE:
                (obs_.subnet)(net, vdns, event);
                break;

            case DnsConfigManager::CFG_DELETE:
                (obs_.subnet)(net, vdns, event);
                break;

            default:
                assert(0);
        }
        DNS_TRACE(IpamTrace, ToEventString(event), 
                  vdns->GetName(), net.ToString());
    }
}

void DnsConfigManager::Notify(const VirtualDnsConfig *config, EventType event) {
    if (obs_.virtual_dns) {
        (obs_.virtual_dns)(config, event);
    }
}

void DnsConfigManager::Notify(const VirtualDnsRecordConfig *config, 
                              EventType event) {
    if (obs_.virtual_dns_record) {
        (obs_.virtual_dns_record)(config, event);
    }
}

int DnsConfigManager::config_task_id_ = -1;
const int DnsConfigManager::kConfigTaskInstanceId;

DnsConfigManager::DnsConfigManager()
        : db_(NULL), db_graph_(NULL),
          trigger_(boost::bind(&DnsConfigManager::ConfigHandler, this),
                   TaskScheduler::GetInstance()->GetTaskId("dns::Config"), 0),
          listener_(new ConfigListener(this)) {
    IdentifierMapInit();

    if (config_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        config_task_id_ = scheduler->GetTaskId("dns::Config");
    }
}

DnsConfigManager::~DnsConfigManager() {
}

void DnsConfigManager::Initialize(DB *db, DBGraph *db_graph) {
    db_ = db;
    db_graph_ = db_graph;
    int ntypes = sizeof(config_types) / sizeof(const char *);
    listener_->Initialize(db, ntypes, config_types);
}

void DnsConfigManager::OnChange() {
    trigger_.Set();
}

void DnsConfigManager::IdentifierMapInit() {
    id_map_.insert(make_pair("network-ipam",
            boost::bind(&DnsConfigManager::ProcessNetworkIpam, this, _1)));
    id_map_.insert(make_pair("virtual-network-network-ipam",
            boost::bind(&DnsConfigManager::ProcessVNNI, this, _1)));
    id_map_.insert(make_pair("virtual-DNS",
            boost::bind(&DnsConfigManager::ProcessVirtualDNS, this, _1)));
    id_map_.insert(make_pair("virtual-DNS-record",
            boost::bind(&DnsConfigManager::ProcessVirtualDNSRecord, this, _1)));
}

void 
VnniConfig::FindSubnets(Subnets &subnets) {
    IFMapNode *node = GetNode();
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

bool VirtualDnsRecordConfig::CanNotify() {
    if (!virt_dns_ || !virt_dns_->GetNode())
        return false;

    if (rec_.record_class == "IN" &&
        rec_.record_type == "PTR") {
        uint32_t addr;
        if (BindUtil::IsIPv4(rec_.record_name, addr) ||
            BindUtil::GetAddrFromPtrName(rec_.record_name, addr)) {
            Subnet net;
            return virt_dns_->GetSubnet(addr, net);
        }
        return false;
    }

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
                if ((addr & mask) == subnets[i].prefix.to_ulong()) {
                    subnet = subnets[i];
                    return true;
                }
            }
        }
    }
    return false;
}

bool 
DnsConfigManager::NotifySubnets(Subnets &old_nets, Subnets &new_nets,
                                VirtualDnsConfig *vdns) {
    bool change = false;
    Subnets::iterator it_old = old_nets.begin();
    Subnets::iterator it_new = new_nets.begin();
    while (it_old != old_nets.end() && it_new != new_nets.end()) {
        if (*it_old < *it_new) {
            // old entry is deleted
            it_old->MarkDelete();
            Notify(*it_old, vdns, DnsConfigManager::CFG_DELETE);
            change = true;
            it_old++;
        } else if (*it_new < *it_old) {
            // new entry
            Notify(*it_new, vdns, DnsConfigManager::CFG_ADD);
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
        Notify(*it_old, vdns, DnsConfigManager::CFG_DELETE);
        change = true;
    }   

    // add remaining new entries
    for (; it_new != new_nets.end(); ++it_new) {
        Notify(*it_new, vdns, DnsConfigManager::CFG_ADD);
        change = true;
    }

    return change;
}

void DnsConfigManager::NotifyPendingDnsRecords(VirtualDnsConfig *vdns) {
    if (!vdns)
        return;
    for (VirtualDnsConfig::VDnsRec::iterator it = 
         vdns->virtual_dns_records_.begin();
         it != vdns->virtual_dns_records_.end(); ++it) {
        VirtualDnsRecordConfig *rec = *it;
        if (!rec->IsNotified()) {
            if (rec->CanNotify()) {
                Notify(rec, DnsConfigManager::CFG_ADD);
                rec->MarkNotified();
            }
        } else {
            if (!rec->CanNotify())
                rec->ClearNotified();
        }
    }
}

void DnsConfigManager::ProcessVNNI(const ConfigDelta &delta) {
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsError, "Received VNNI without a name");
        return;
    }
    VnniConfig *config = vnni_config_.Find(delta.id_name);
    if (!config) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL)
            return;
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted())
            return;
        IFMapNode *ipam_node = FindTarget(node, "virtual-network-network-ipam", "network-ipam");
        if (ipam_node == NULL) {
            DNS_TRACE(DnsError, "VirtualNetworkNetworkIpam <" + delta.id_name + 
                                "> does not have Ipam link");
            return;
        }
        config = new VnniConfig(delta.id_name, proxy,
                                ipam_config_.Find(ipam_node->name()));
        vnni_config_.Add(delta.id_name, config);
        config->FindSubnets(config->subnets_);
        if (config->GetIpam()) {
            config->GetIpam()->AddVnni(config);
            if (config->GetIpam()->GetNode()) {
                Subnets subnets;
                NotifySubnets(subnets, config->subnets_, 
                              config->GetIpam()->GetVirtualDns());
                NotifyPendingDnsRecords(config->GetIpam()->GetVirtualDns());
            }
        } else {
            config->ipam_ = new IpamConfig(ipam_node->name(), NULL);
            ipam_config_.Add(ipam_node->name(), config->GetIpam());
            config->GetIpam()->AddVnni(config);
        }
    } else {
        if (config->GetNode()->IsDeleted() ||
            !config->GetNode()->HasAdjacencies(db_graph_))  {
            config->MarkDelete();
            if (config->GetIpam()) {
                if (config->GetIpam()->GetNode()) {
                    Subnets &subnets = config->GetSubnets();
                    for (unsigned int i = 0; i < subnets.size(); i++) {
                        Notify(subnets[i], config->GetIpam()->GetVirtualDns(), 
                               DnsConfigManager::CFG_DELETE);
                    }
                }
                config->GetIpam()->DelVnni(config);
                NotifyPendingDnsRecords(config->GetIpam()->GetVirtualDns());
            }
            vnni_config_.Del(delta.id_name);
            delete config;
        } else {
            Subnets subnets;
            config->FindSubnets(subnets);
            if (config->GetIpam() && config->GetIpam()->GetNode()) {
                config->subnets_.swap(subnets);
                if (NotifySubnets(subnets, config->subnets_,
                                  config->GetIpam()->GetVirtualDns())) {
                    NotifyPendingDnsRecords(config->GetIpam()->GetVirtualDns());
                }
            } else {
                config->subnets_ = subnets;
            }
        }
    }
}

void DnsConfigManager::ProcessNetworkIpam(const ConfigDelta &delta) {
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsError, "Received Ipam without a name");
        return;
    }
    IpamConfig *config = ipam_config_.Find(delta.id_name);
    if (!config || !config->GetNode()) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL)
            return;
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted())
            return;
        if (!config) {
            config = new IpamConfig(delta.id_name, proxy);
            ipam_config_.Add(delta.id_name, config);
        } else {
            config->SetProxy(proxy);
            config->GetObject(config->rec_);
        }
        config->Add(virt_dns_config_.Find(config->GetVirtualDnsName()),
                    boost::bind(&DnsConfigManager::Notify, this, _1, _2, _3));
        NotifyPendingDnsRecords(config->GetVirtualDns());
    } else {
        if (config->GetNode()->IsDeleted() ||
            !config->GetNode()->HasAdjacencies(db_graph_))  {
            config->Delete(boost::bind(&DnsConfigManager::Notify, 
                                       this, _1, _2, _3));
            ipam_config_.Del(config->name_);
            NotifyPendingDnsRecords(config->GetVirtualDns());
            delete config;
        } else {
            autogen::IpamType new_rec;
            config->GetObject(new_rec);
            if (new_rec.ipam_dns_server.virtual_dns_server_name != 
                config->GetVirtualDnsName()) {
                config->Delete(boost::bind(&DnsConfigManager::Notify, 
                                           this, _1, _2, _3));
                NotifyPendingDnsRecords(config->GetVirtualDns());
                config->ClearDelete();
                config->rec_ = new_rec;
                config->Add(virt_dns_config_.Find(config->GetVirtualDnsName()),
                            boost::bind(&DnsConfigManager::Notify, this, _1, _2, _3));
                NotifyPendingDnsRecords(config->GetVirtualDns());
            } else
                config->rec_ = new_rec;
        }
    }
}

void DnsConfigManager::ProcessVirtualDNS(const ConfigDelta &delta) {
    DnsConfigManager::EventType event = DnsConfigManager::CFG_CHANGE;
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsError, "Received Virtual DNS without a name");
        return;
    }
    bool update_dependency = false;
    VirtualDnsConfig *config = virt_dns_config_.Find(delta.id_name);
    if (!config || !config->GetNode()) {
        event = DnsConfigManager::CFG_ADD;
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL)
            return;
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted())
            return;
        if (!config) {
            config = new VirtualDnsConfig(delta.id_name, proxy);
            virt_dns_config_.Add(delta.id_name, config);
        } else {
            config->SetProxy(proxy);
            config->GetObject(config->rec_);
            update_dependency = true;
        }
        // Update any ipam configs dependent on this virtual dns
        for (DnsConfigData<IpamConfig *>::DataMap::iterator iter 
             = ipam_config_.data_.begin(); iter != ipam_config_.data_.end(); 
             ++iter) {
            IpamConfig *ipam = iter->second;
            if (!ipam->GetVirtualDns() && 
                ipam->GetVirtualDnsName() == config->GetName()) {
                ipam->virtual_dns_ = config;
                config->AddIpam(ipam);
            }
        }
        config->Trace(ToEventString(event));
        Notify(config, event);
        config->MarkNotified();
        // No notification for subnets is required here as the config was 
        // available in the above notify
        if (update_dependency) {
            NotifyPendingDnsRecords(config);
        }
    } else {
        if (config->GetNode()->IsDeleted() ||
            !config->GetNode()->HasAdjacencies(db_graph_))  {
            event = DnsConfigManager::CFG_DELETE;
            config->MarkDelete();
            config->Trace(ToEventString(event));
            Notify(config, event);
            for (VirtualDnsConfig::IpamList::iterator iter = config->ipams_.begin();
                 iter != config->ipams_.end(); ++iter) {
                IpamConfig *ipam = *iter;
                ipam->virtual_dns_ = NULL;
            }
            for (VirtualDnsConfig::VDnsRec::iterator it = 
                 config->virtual_dns_records_.begin();
                 it != config->virtual_dns_records_.end(); ++it) {
                (*it)->virt_dns_ = NULL;
                (*it)->ClearNotified();
            }
            virt_dns_config_.Del(delta.id_name);
            NotifyPendingDnsRecords(config);
            delete config;
        } else {
            if (!config->GetObject(config->rec_) ||
                !config->HasChanged())
                return;
            config->Trace(ToEventString(event));
            Notify(config, event);
            config->old_rec_ = config->rec_;
        }
    }
}

void DnsConfigManager::ProcessVirtualDNSRecord(const ConfigDelta &delta) {
    DnsConfigManager::EventType event = DnsConfigManager::CFG_ADD;
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsError, "Received Virtual DNS Record without a name");
        return;
    }
    VirtualDnsRecordConfig *config = virt_dns_rec_config_.Find(delta.id_name);
    if (!config) {
        event = DnsConfigManager::CFG_ADD;
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL)
            return;
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted())
            return;
        IFMapNode *vdns_node = 
            FindTarget(node, "virtual-DNS-virtual-DNS-record");
        if (vdns_node == NULL) {
            DNS_TRACE(DnsError, "Virtual DNS Record <" + delta.id_name + 
                                "> does not have virtual DNS link");
            return;
        }
        config = new VirtualDnsRecordConfig(delta.id_name, proxy,
                     virt_dns_config_.Find(vdns_node->name()));
        virt_dns_rec_config_.Add(delta.id_name, config);
        if (!config->virt_dns_) {
            config->virt_dns_ = 
                new VirtualDnsConfig(vdns_node->name(), NULL);
            virt_dns_config_.Add(vdns_node->name(), config->virt_dns_);
            config->virt_dns_->AddRecord(config);
            config->Trace(ToEventString(event));
            return;
        } else if (!config->virt_dns_->GetNode()) {
            config->virt_dns_->AddRecord(config);
            config->Trace(ToEventString(event));
            return;
        }
        config->virt_dns_->AddRecord(config);
        if (config->CanNotify()) {
            Notify(config, event);
            config->MarkNotified();
        }
        config->Trace(ToEventString(event));
    } else {
        if (config->GetNode()->IsDeleted() || 
            !config->GetNode()->HasAdjacencies(db_graph_))  {
            event = DnsConfigManager::CFG_DELETE;
            config->MarkDelete();
            if (config->virt_dns_) {
                if (config->IsNotified()) {
                    Notify(config, event);
                }
                config->virt_dns_->DelRecord(config);
            }
            virt_dns_rec_config_.Del(delta.id_name);
            config->Trace(ToEventString(event));
            delete config;
        } else {
            autogen::VirtualDnsRecordType new_rec;
            if (!config->GetObject(new_rec) ||
                config->RecordEqual(new_rec))
                return;
            // For records, notify a change with a delete of the old record
            // followed by addition of new record
            if (config->IsNotified()) {
                Notify(config, DnsConfigManager::CFG_DELETE);
                config->ClearNotified();
            }
            config->rec_ = new_rec;
            if (config->CanNotify()) {
                Notify(config, event);
                config->MarkNotified();
            }
            config->Trace(ToEventString(event));
        }
    }
}

void DnsConfigManager::ProcessChanges(const ChangeList &change_list) {
    for (ChangeList::const_iterator iter = change_list.begin();
         iter != change_list.end(); ++iter) {
        IdentifierMap::iterator loc = id_map_.find(iter->id_type);
        if (loc != id_map_.end()) {
            (loc->second)(*iter);
        }
    }
}

IFMapNode *DnsConfigManager::FindTarget(IFMapNode *node, std::string link_name) {
    for (DBGraphVertex::edge_iterator iter = node->edge_list_begin(graph());
         iter != node->edge_list_end(graph()); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        if (link->metadata() == link_name)
            return static_cast<IFMapNode *>(iter.target());
    }
    return NULL;
}

IFMapNode *DnsConfigManager::FindTarget(IFMapNode *node, std::string link_name, 
                                        std::string node_type) {
    for (DBGraphVertex::edge_iterator iter = node->edge_list_begin(graph());
         iter != node->edge_list_end(graph()); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        if (link->metadata() == link_name) {
            IFMapNode *targ_node = static_cast<IFMapNode *>(iter.target());
            if (targ_node->table()->Typename() == node_type)
                return targ_node;
        }
    }
    return NULL;
}

bool DnsConfigManager::ConfigHandler() {
    ConfigListener::ChangeList change_list;
    listener_->GetChangeList(&change_list);
    ProcessChanges(change_list);
    return true;
}

void DnsConfigManager::Terminate() {
    listener_->Terminate(db_);
}

void ShowDnsConfig::HandleRequest() const {
    DnsConfigResponse *resp = new DnsConfigResponse();
    resp->set_context(context());
    DnsConfigManager::VirtualDnsMap vdns = Dns::GetDnsConfigManager()->GetVirtualDnsMap();

    std::vector<VirtualDnsSandesh> vdns_list_sandesh;
    for (DnsConfigManager::VirtualDnsMap::iterator vdns_it = vdns.begin();
         vdns_it != vdns.end(); ++vdns_it) {
        VirtualDnsConfig *vdns_config = vdns_it->second;
        VirtualDnsSandesh vdns_sandesh;
        VirtualDnsTraceData vdns_trace_data;
        vdns_config->VirtualDnsTrace(vdns_trace_data);

        std::vector<VirtualDnsRecordTraceData> rec_list_sandesh;
        for (VirtualDnsConfig::VDnsRec::iterator rec_it = 
             vdns_config->virtual_dns_records_.begin();
             rec_it != vdns_config->virtual_dns_records_.end(); ++rec_it) {
            VirtualDnsRecordTraceData rec_trace_data;
            (*rec_it)->VirtualDnsRecordTrace(rec_trace_data);
            rec_list_sandesh.push_back(rec_trace_data);
        }

        std::vector<std::string> net_list_sandesh;
        for (VirtualDnsConfig::IpamList::iterator ipam_iter = 
             vdns_config->ipams_.begin(); 
             ipam_iter != vdns_config->ipams_.end(); ++ipam_iter) {
            IpamConfig *ipam = *ipam_iter;
            const IpamConfig::VnniList &vnni = ipam->GetVnniList();
            for (IpamConfig::VnniList::iterator vnni_it = vnni.begin();
                 vnni_it != vnni.end(); ++vnni_it) {
                Subnets &subnets = (*vnni_it)->GetSubnets();
                for (unsigned int i = 0; i < subnets.size(); i++) {
                    std::stringstream str;
                    str << subnets[i].prefix.to_string();
                    str << "/";
                    str << subnets[i].plen;
                    net_list_sandesh.push_back(str.str());
                }
            }
        }

        vdns_sandesh.set_virtual_dns(vdns_trace_data);
        vdns_sandesh.set_records(rec_list_sandesh);
        vdns_sandesh.set_subnets(net_list_sandesh);
        vdns_list_sandesh.push_back(vdns_sandesh);
    }

    resp->set_virtual_dns(vdns_list_sandesh);
    resp->Response();
}
