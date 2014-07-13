/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/dns.h>
#include <bind/bind_util.h>
#include <mgr/dns_mgr.h>
#include <mgr/dns_oper.h>
#include <bind/bind_resolver.h>
#include <bind/named_config.h>
#include <agent/agent_xmpp_channel.h>

uint16_t DnsManager::g_trans_id_;

DnsManager::DnsManager() 
    : bind_status_(boost::bind(&DnsManager::BindEventHandler, this, _1)),
      pending_done_queue_(TaskScheduler::GetInstance()->GetTaskId("dns::Config"), 0,
                          boost::bind(&DnsManager::PendingDone, this, _1)) {
    std::vector<BindResolver::DnsServer> bind_servers;
    bind_servers.push_back(BindResolver::DnsServer("127.0.0.1",
                                                   Dns::GetDnsPort()));
    BindResolver::Init(*Dns::GetEventManager()->io_service(), bind_servers,
                       boost::bind(&DnsManager::HandleUpdateResponse, 
                                   this, _1));

    Dns::SetDnsConfigManager(&config_mgr_);    
    DnsConfigManager::Observers obs;
    obs.virtual_dns = boost::bind(&DnsManager::ProcessConfig<VirtualDnsConfig>,
                                  this, _1, _2, _3);
    obs.virtual_dns_record = 
        boost::bind(&DnsManager::ProcessConfig<VirtualDnsRecordConfig>,
                    this, _1, _2, _3);
    obs.ipam = boost::bind(&DnsManager::ProcessConfig<IpamConfig>, this, _1, _2, _3);
    obs.vnni = boost::bind(&DnsManager::ProcessConfig<VnniConfig>, this, _1, _2, _3);
    Dns::GetDnsConfigManager()->RegisterObservers(obs);

    DnsConfig::VdnsCallback = boost::bind(&DnsManager::DnsView, this, _1, _2);
    DnsConfig::VdnsRecordCallback = boost::bind(&DnsManager::DnsRecord, this,
                                                _1, _2);
    DnsConfig::VdnsZoneCallback = boost::bind(&DnsManager::DnsPtrZone, this,
                                              _1, _2, _3);
    pending_timer_ =
        TimerManager::CreateTimer(*Dns::GetEventManager()->io_service(),
              "DnsRetransmitTimer",
              TaskScheduler::GetInstance()->GetTaskId("dns::Config"), 0);
    StartPendingTimer();
}

void DnsManager::Initialize(DB *config_db, DBGraph *config_graph) {
    NamedConfig::Init();
    // bind_status_.SetTrigger();
    config_mgr_.Initialize(config_db, config_graph);
}

DnsManager::~DnsManager() {
    pending_timer_->Cancel();
    TimerManager::DeleteTimer(pending_timer_);
    pending_done_queue_.Shutdown();
}

void DnsManager::Shutdown() {
    config_mgr_.Terminate();
    NamedConfig::Shutdown();
    BindResolver::Shutdown();
}

template <typename ConfigType>
void DnsManager::ProcessConfig(IFMapNodeProxy *proxy,
                               const std::string &name,
                               DnsConfigManager::EventType event) {
    IFMapNode *node = proxy->node();
    ConfigType *config = ConfigType::Find(name);
    switch (event) {
        case DnsConfigManager::CFG_ADD:
            if (!config) {
                config = new ConfigType(node);
            }
            config->OnAdd(node);
            break;

        case DnsConfigManager::CFG_CHANGE:
            if (config)
                config->OnChange(node);
            break;

        case DnsConfigManager::CFG_DELETE:
            if (config) {
                config->OnDelete();
                delete config;
            }
            break;

        default:
            assert(0);
    }
}

                                    
void DnsManager::ProcessAgentUpdate(BindUtil::Operation event,
                                    const std::string &name,
                                    const std::string &vdns_name,
                                    const DnsItem &item) {
    tbb::mutex::scoped_lock lock(mutex_);
    VirtualDnsRecordConfig *config = VirtualDnsRecordConfig::Find(name);
    switch (event) {
        case BindUtil::ADD_UPDATE:
            if (!config) {
                config = new VirtualDnsRecordConfig(name, vdns_name, item);
            }
            config->OnAdd();
            break;

        case BindUtil::CHANGE_UPDATE:
            if (config)
                config->OnChange(item);
            break;

        case BindUtil::DELETE_UPDATE:
            if (config) {
                config->OnDelete();
                delete config;
            }
            break;

        default:
            assert(0);
    }
}

void DnsManager::DnsView(const DnsConfig *cfg, DnsConfig::DnsConfigEvent ev) {
    if (!bind_status_.IsUp())
        return;

    const VirtualDnsConfig *config = static_cast<const VirtualDnsConfig *>(cfg);
    DNS_BIND_TRACE(DnsBindTrace, "Virtual DNS <" << config->GetName() <<
                   "> " << DnsConfig::ToEventString(ev));
    config->ClearNotified();
    std::string dns_domain = config->GetDomainName();
    if (dns_domain.empty()) {
        DNS_BIND_TRACE(DnsBindTrace, "Virtual DNS <" << config->GetName() << 
                       "> doesnt have domain; ignoring event : " <<
                       DnsConfig::ToEventString(ev));
        return;
    }

    NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
    if (ev == DnsConfig::CFG_ADD) {
        if (!CheckName(config->GetName(), dns_domain)) {
            return;
        }
        config->MarkNotified();
        ncfg->AddView(config);
    } else if (ev == DnsConfig::CFG_CHANGE) {
        if (CheckName(config->GetName(), dns_domain)) {
            config->MarkNotified();
        }
        ncfg->ChangeView(config);
        if (dns_domain != config->GetOldDomainName()) {
            for (VirtualDnsConfig::VDnsRec::const_iterator it = 
                 config->virtual_dns_records_.begin();
                 it != config->virtual_dns_records_.end(); ++it) {
                DnsRecord(*it, ev);
            }
        }
    } else {
        ncfg->DelView(config);
        PendingListViewDelete(config);
    }
}

void DnsManager::DnsPtrZone(const Subnet &subnet, const VirtualDnsConfig *vdns,
                            DnsConfig::DnsConfigEvent ev) {
    if (!bind_status_.IsUp())
        return;

    std::string dns_domain = vdns->GetDomainName();
    if (dns_domain.empty()) {
        DNS_BIND_TRACE(DnsBindTrace, "Ptr Zone <" << vdns->GetName() << 
                       "> doesnt have domain; ignoring event : " <<
                       DnsConfig::ToEventString(ev));
        return;
    }

    NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
    if (ev == DnsConfig::CFG_DELETE) {
        ncfg->DelZone(subnet, vdns);
        PendingListZoneDelete(subnet, vdns);
    } else {
        ncfg->AddZone(subnet, vdns);
    }
    DNS_BIND_TRACE(DnsBindTrace, "Virtual DNS <" << vdns->GetName() << "> " <<
                   subnet.prefix.to_string() << "/" << subnet.plen << " " <<
                   DnsConfig::ToEventString(ev));
}

void DnsManager::DnsRecord(const DnsConfig *cfg, DnsConfig::DnsConfigEvent ev) {
    if (!bind_status_.IsUp())
        return;

    const VirtualDnsRecordConfig *config = 
                static_cast<const VirtualDnsRecordConfig *>(cfg);
    config->ClearNotified();
    const DnsItem &item = config->GetRecord();
    if (item.name == "" || item.data == "") {
        DNS_BIND_TRACE(DnsBindError, "Virtual DNS Record <" <<
                       config->GetName() << 
                       "> doesnt have name / data; ignoring event : " <<
                       DnsConfig::ToEventString(ev));
        return;
    }

    if (config->GetViewName() == "" || !config->GetVirtualDns()->IsValid()) {
        return;
    }

    switch (ev) {
        case DnsConfig::CFG_ADD:
        case DnsConfig::CFG_CHANGE:
            if (SendRecordUpdate(BindUtil::ADD_UPDATE, config))
                config->MarkNotified();
            break;

        case DnsConfig::CFG_DELETE:
            if (SendRecordUpdate(BindUtil::DELETE_UPDATE, config))
                config->ClearNotified();
            break;

        default:
            assert(0);
    }
}

bool DnsManager::SendRecordUpdate(BindUtil::Operation op, 
                                  const VirtualDnsRecordConfig *config) {
    DnsItem item = config->GetRecord();
    const autogen::VirtualDnsType vdns = config->GetVDns();

    std::string zone;
    if (item.type == DNS_PTR_RECORD) {
        uint32_t addr;
        if (BindUtil::IsIPv4(item.name, addr)) {
            BindUtil::GetReverseZone(addr, 32, item.name);
        } else {
            if (!BindUtil::GetAddrFromPtrName(item.name, addr)) {
                DNS_BIND_TRACE(DnsBindError, "Virtual DNS Record <" <<
                               config->GetName() << "> invalid PTR name " <<
                               item.name << "; ignoring");
                return false;
            }
        }
        if (!CheckName(config->GetName(), item.data)) {
            return false;
        }
        Subnet net;
        if (!config->GetVirtualDns()->GetSubnet(addr, net)) {
            DNS_BIND_TRACE(DnsBindError, "Virtual DNS Record <" << 
                           config->GetName() << 
                           "> doesnt belong to a known subnet; ignoring");
            return false;
        }
        BindUtil::GetReverseZone(addr, net.plen, zone);
        item.data = BindUtil::GetFQDN(item.data, vdns.domain_name, ".");
    } else {
        // Bind allows special chars in CNAME, NS names and in CNAME data
        if ((item.type == DNS_A_RECORD || item.type == DNS_AAAA_RECORD) &&
            !CheckName(config->GetName(), item.name)) {
            return false;
        }
        zone = config->GetVDns().domain_name;
        item.name = BindUtil::GetFQDN(item.name, 
                                      vdns.domain_name, vdns.domain_name);
        if (item.type == DNS_CNAME_RECORD)
            item.data = BindUtil::GetFQDN(item.data, vdns.domain_name, ".");
        // In case of NS record, ensure that there are no special characters in data.
        // When it is virtual dns name, we could have chars like ':'
        if (item.type == DNS_NS_RECORD)
            BindUtil::RemoveSpecialChars(item.data);
    }
    DnsItems items;
    items.push_back(item);
    std::string view_name = config->GetViewName();
    SendUpdate(op, view_name, zone, items);
    return true;
}

void DnsManager::SendUpdate(BindUtil::Operation op, const std::string &view,
                            const std::string &zone, DnsItems &items) {
    uint8_t *pkt = new uint8_t[BindResolver::max_pkt_size];
    uint16_t xid = GetTransId();
    int len = BindUtil::BuildDnsUpdate(pkt, op, xid, view, zone, items);
    if (BindResolver::Resolver()->DnsSend(pkt, 0, len)) {
        DNS_BIND_TRACE(DnsBindTrace, "DNS Update sent for DNS record; xid = " <<
                   xid << "; View = " << view << "; Zone = " << zone << "; " << 
                   DnsItemsToString(items));
        AddPendingList(xid, view, zone, items, op);
    }
}

void DnsManager::SendRetransmit(uint16_t xid, BindUtil::Operation op,
                                const std::string &view,
                                const std::string &zone, DnsItems &items) {
    uint8_t *pkt = new uint8_t[BindResolver::max_pkt_size];
    int len = BindUtil::BuildDnsUpdate(pkt, op, xid, view, zone, items);
    if (BindResolver::Resolver()->DnsSend(pkt, 0, len)) {
        DNS_BIND_TRACE(DnsBindTrace, "DNS retransmit sent for DNS record; xid = " <<
                   xid << "; View = " << view << "; Zone = " << zone << "; " <<
                   DnsItemsToString(items));
    }
}

void DnsManager::UpdateAll() {
    VirtualDnsConfig::DataMap vmap = VirtualDnsConfig::GetVirtualDnsMap();
    for (VirtualDnsConfig::DataMap::iterator it = vmap.begin();
         it != vmap.end(); ++it) {
        VirtualDnsConfig *vdns = it->second;
        if (!vdns->GetDomainName().empty() &&
            CheckName(vdns->GetName(), vdns->GetDomainName())) {
            vdns->MarkNotified();
            DNS_BIND_TRACE(DnsBindTrace, "Virtual DNS <" << vdns->GetName() <<
                           "> Add");
        } else {
            vdns->ClearNotified();
        }
    }
    NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
    ncfg->AddAllViews();
    for (VirtualDnsConfig::DataMap::iterator it = vmap.begin();
         it != vmap.end(); ++it) {
        VirtualDnsConfig *vdns = it->second;
        if (!vdns->IsNotified())
            continue;
        for (VirtualDnsConfig::VDnsRec::iterator vit = 
             vdns->virtual_dns_records_.begin();
             vit != vdns->virtual_dns_records_.end(); ++vit) {
            if ((*vit)->IsValid())
                DnsRecord(*vit, DnsConfig::CFG_ADD);
        }
    }
}

void DnsManager::HandleUpdateResponse(uint8_t *pkt) {
    dns_flags flags;
    uint16_t xid;
    DnsItems ques, ans, auth, add;
    BindUtil::ParseDnsQuery(pkt, xid, flags, ques, ans, auth, add);
    if (flags.ret) {
        DNS_BIND_TRACE(DnsBindError, "Update failed : " <<
                       BindUtil::DnsResponseCode(flags.ret) << 
                       "; xid = " << xid << ";");
        // ResendRecord(xid);
    } else {
        DNS_BIND_TRACE(DnsBindTrace, "Update successful; xid = " << xid << ";");
        pending_done_queue_.Enqueue(xid);
    }
    delete [] pkt;
}

bool DnsManager::PendingDone(uint16_t xid) {
    DeletePendingList(xid);
    return true;
}

void DnsManager::ResendAllRecords() {
    for (PendingListMap::iterator it = pending_map_.begin();
         it != pending_map_.end(); ) {
        SendRetransmit(it->first, it->second.op, it->second.view,
                       it->second.zone, it->second.items);
        it->second.retransmit_count++;
        if (it->second.retransmit_count > kMaxRetransmitCount) {
            DNS_BIND_TRACE(DnsBindTrace, "DNS records max retransmits reached;"
                           << "no more retransmission; xid = " << it->first);
            pending_map_.erase(it++);
        } else {
            it++;
        }
    }
}

void DnsManager::AddPendingList(uint16_t xid, const std::string &view,
                                const std::string &zone, const DnsItems &items,
                                BindUtil::Operation op) {
    // delete earlier entries for the same items
    UpdatePendingList(view, zone, items);

    PendingListMap::iterator it = pending_map_.find(xid);
    if (it != pending_map_.end()) {
        it->second.view = view;
        it->second.zone = zone;
        it->second.items = items;
        it->second.op = op;
        it->second.retransmit_count = 0;
        return;
    }
    pending_map_.insert(PendingListPair(xid, PendingList(xid, view, zone,
                                                         items, op)));
}

// if there is an update for an item which is already in pending list,
// remove it from the pending list
void DnsManager::UpdatePendingList(const std::string &view,
                                   const std::string &zone,
                                   const DnsItems &items) {
    for (PendingListMap::iterator it = pending_map_.begin();
         it != pending_map_.end(); ) {
        if (it->second.view == view &&
            it->second.zone == zone &&
            it->second.items == items)
            pending_map_.erase(it++);
        else
            it++;
    }
}

void DnsManager::DeletePendingList(uint16_t xid) {
    pending_map_.erase(xid);
}

void DnsManager::ClearPendingList() {
    pending_map_.clear();
}

// Remove entries from pending list, upon a view delete
void DnsManager::PendingListViewDelete(const VirtualDnsConfig *config) {
    for (PendingListMap::iterator it = pending_map_.begin();
         it != pending_map_.end(); ) {
        if (it->second.view == config->GetViewName())
            pending_map_.erase(it++);
        else
            it++;
    }
}

bool DnsManager::CheckZoneDelete(ZoneList &zones, PendingList &pend) {
    for (uint32_t i = 0; i < zones.size(); i++) {
        if (zones[i] == pend.zone)
            return true;
    }
    return false;
}

// Remove entries from pending list, upon a zone delete
void DnsManager::PendingListZoneDelete(const Subnet &subnet,
                                       const VirtualDnsConfig *config) {
    ZoneList zones;
    BindUtil::GetReverseZones(subnet, zones);

    for (PendingListMap::iterator it = pending_map_.begin();
         it != pending_map_.end(); ) {
        if (it->second.view == config->GetViewName() &&
            CheckZoneDelete(zones, it->second))
            pending_map_.erase(it++);
        else
            it++;
    }
}

void DnsManager::StartPendingTimer() {
    if (!pending_timer_->running()) {
        pending_timer_->Start(kPendingRecordRetransmitTime,
                              boost::bind(&DnsManager::PendingTimerExpiry, this));
    }
}

void DnsManager::CancelPendingTimer() {
    pending_timer_->Cancel();
}

bool DnsManager::PendingTimerExpiry() {
    // TODO: change timer to be per record & resend only on individual timeout
    ResendAllRecords();
    return true;
}

inline uint16_t DnsManager::GetTransId() {
    return (++g_trans_id_ == 0 ? ++g_trans_id_ : g_trans_id_);
}

inline bool DnsManager::CheckName(std::string rec_name, std::string name) {
    if (BindUtil::HasSpecialChars(name)) {
        DNS_CONFIGURATION_LOG(
            g_vns_constants.CategoryNames.find(Category::DNSAGENT)->second,
            SandeshLevel::SYS_ERR, 
            "Invalid DNS Name - cannot use special characters in DNS name",
            rec_name, name);
        return false;
    }
    return true;
}

void DnsManager::BindEventHandler(BindStatus::Event event) {
    switch (event) {
        case BindStatus::Up: {
            DNS_OPERATIONAL_LOG(
                g_vns_constants.CategoryNames.find(Category::DNSAGENT)->second,
                SandeshLevel::SYS_NOTICE, "BIND named up; DNS is operational");
            UpdateAll();
            break;
        }

        case BindStatus::Down: {
            DNS_OPERATIONAL_LOG(
                g_vns_constants.CategoryNames.find(Category::DNSAGENT)->second,
                SandeshLevel::SYS_NOTICE, "BIND named down; DNS is not operational");
            NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
            ncfg->Reset();
            ClearPendingList();
            break;
        }

        default:
            assert(0);
    }
}

void ShowDnsConfig::HandleRequest() const {
    DnsConfigResponse *resp = new DnsConfigResponse();
    resp->set_context(context());
    VirtualDnsConfig::DataMap vdns = VirtualDnsConfig::GetVirtualDnsMap();

    std::vector<VirtualDnsSandesh> vdns_list_sandesh;
    for (VirtualDnsConfig::DataMap::iterator vdns_it = vdns.begin();
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

void ShowVirtualDnsServers::HandleRequest() const {
    VirtualDnsServersResponse *resp = new VirtualDnsServersResponse();
    resp->set_context(context());
    VirtualDnsConfig::DataMap vdns = VirtualDnsConfig::GetVirtualDnsMap();

    std::vector<VirtualDnsServersSandesh> vdns_list_sandesh;
    for (VirtualDnsConfig::DataMap::iterator vdns_it = vdns.begin();
         vdns_it != vdns.end(); ++vdns_it) {
        VirtualDnsConfig *vdns_config = vdns_it->second;
        VirtualDnsServersSandesh vdns_sandesh;
        VirtualDnsTraceData vdns_trace_data;
        vdns_config->VirtualDnsTrace(vdns_trace_data);

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
        vdns_sandesh.set_records(vdns_config->GetName());
        vdns_sandesh.set_num_records(vdns_config->virtual_dns_records_.size());
        vdns_sandesh.set_subnets(net_list_sandesh);
        vdns_list_sandesh.push_back(vdns_sandesh);
    }

    resp->set_virtual_dns_servers(vdns_list_sandesh);
    resp->Response();
}

void ShowVirtualDnsRecords::HandleRequest() const {
    VirtualDnsRecordsResponse *resp = new VirtualDnsRecordsResponse();
    VirtualDnsConfig::DataMap vdns = VirtualDnsConfig::GetVirtualDnsMap();

    std::stringstream ss(get_virtual_dns_server());
    std::string vdns_server, next_iterator;
    VirtualDnsRecordConfig *next_iterator_key = NULL;
    std::getline(ss, vdns_server, '@');
    std::getline(ss, next_iterator);
    if (!next_iterator.empty()) {
        std::stringstream next_iter(next_iterator);
        uint64_t value = 0;
        next_iter >> value;
        next_iterator_key = (VirtualDnsRecordConfig *) value;
    }
    std::vector<VirtualDnsRecordTraceData> rec_list_sandesh;
    VirtualDnsConfig::DataMap::iterator vdns_it = vdns.find(vdns_server);
    if (vdns_it != vdns.end()) {
        VirtualDnsConfig *vdns_config = vdns_it->second;
        int count = 0;
        for (VirtualDnsConfig::VDnsRec::iterator rec_it = 
             vdns_config->virtual_dns_records_.lower_bound(next_iterator_key);
             rec_it != vdns_config->virtual_dns_records_.end(); ++rec_it) {
            VirtualDnsRecordTraceData rec_trace_data;
            (*rec_it)->VirtualDnsRecordTrace(rec_trace_data);
            rec_list_sandesh.push_back(rec_trace_data);
            if (++count == DnsManager::max_records_per_sandesh) {
                if (++rec_it == vdns_config->virtual_dns_records_.end())
                    break;
                std::stringstream str;
                uint64_t value = (uint64_t)(*rec_it);
                str << vdns_server << "@" << value;
                resp->set_getnext_record_set(str.str());
                break;
            }
        }
    }

    resp->set_context(context());
    resp->set_virtual_dns_server(vdns_server);
    resp->set_records(rec_list_sandesh);
    resp->Response();
}
