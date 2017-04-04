/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/contrail_ports.h>
#include <cmn/dns.h>
#include <bind/bind_util.h>
#include <mgr/dns_mgr.h>
#include <mgr/dns_oper.h>
#include <bind/bind_resolver.h>
#include <bind/named_config.h>
#include <agent/agent_xmpp_channel.h>

DnsManager::DnsManager()
    : bind_status_(boost::bind(&DnsManager::BindEventHandler, this, _1)),
      end_of_config_(false),
      record_send_count_(TaskScheduler::GetInstance()->HardwareThreadCount()),
      named_max_retransmissions_(kMaxRetransmitCount),
      named_retransmission_interval_(kPendingRecordReScheduleTime),
      named_lo_watermark_(kNamedLoWaterMark),
      named_hi_watermark_(kNamedHiWaterMark),
      named_send_throttled_(false),
      pending_done_queue_(TaskScheduler::GetInstance()->GetTaskId("dns::NamedSndRcv"), 0,
                          boost::bind(&DnsManager::PendingDone, this, _1)),
      idx_(kMaxIndexAllocator) {
    std::vector<BindResolver::DnsServer> bind_servers;
    bind_servers.push_back(BindResolver::DnsServer("127.0.0.1",
                                                   Dns::GetDnsPort()));
    BindResolver::Init(*Dns::GetEventManager()->io_service(), bind_servers,
                       ContrailPorts::ContrailDnsClientUdpPort(),
                       boost::bind(&DnsManager::HandleUpdateResponse,
                                   this, _1, _2), 0);

    Dns::SetDnsConfigManager(&config_mgr_);
    DnsConfigManager::Observers obs;
    obs.virtual_dns = boost::bind(&DnsManager::ProcessConfig<VirtualDnsConfig>,
                                  this, _1, _2, _3);
    obs.virtual_dns_record =
        boost::bind(&DnsManager::ProcessConfig<VirtualDnsRecordConfig>,
                    this, _1, _2, _3);
    obs.ipam = boost::bind(&DnsManager::ProcessConfig<IpamConfig>, this, _1, _2, _3);
    obs.vnni = boost::bind(&DnsManager::ProcessConfig<VnniConfig>, this, _1, _2, _3);
    obs.global_qos = boost::bind(&DnsManager::ProcessConfig<GlobalQosConfig>,
                                 this, _1, _2, _3);
    Dns::GetDnsConfigManager()->RegisterObservers(obs);

    DnsConfig::VdnsCallback = boost::bind(&DnsManager::DnsView, this, _1, _2);
    DnsConfig::VdnsRecordCallback = boost::bind(&DnsManager::DnsRecord, this,
                                                _1, _2);
    DnsConfig::VdnsZoneCallback = boost::bind(&DnsManager::DnsPtrZone, this,
                                              _1, _2, _3);
    pending_timer_ =
        TimerManager::CreateTimer(*Dns::GetEventManager()->io_service(),
              "DnsRetransmitTimer",
              TaskScheduler::GetInstance()->GetTaskId("dns::NamedSndRcv"), 0);

    end_of_config_check_timer_ =
        TimerManager::CreateTimer(*Dns::GetEventManager()->io_service(),
              "Check_EndofConfig_Timer",
              TaskScheduler::GetInstance()->GetTaskId("dns::Config"), 0);
    StartEndofConfigTimer();

    // PreAllocate index 0 as it cannot be used as transaction id.
    idx_.AllocIndex();
}

void DnsManager::Initialize(DB *config_db, DBGraph *config_graph,
                            const std::string& named_config_dir,
                            const std::string& named_config_file,
                            const std::string& named_log_file,
                            const std::string& rndc_config_file,
                            const std::string& rndc_secret,
                            const std::string& named_max_cache_size,
                            const uint16_t named_max_retransmissions,
                            const uint16_t named_retranmission_interval) {
    NamedConfig::Init(named_config_dir, named_config_file,
                      named_log_file, rndc_config_file, rndc_secret,
                      named_max_cache_size);
    config_mgr_.Initialize(config_db, config_graph);
    named_max_retransmissions_ = named_max_retransmissions;
    named_retransmission_interval_ = named_retranmission_interval;
}

DnsManager::~DnsManager() {
    pending_timer_->Cancel();
    TimerManager::DeleteTimer(pending_timer_);
    end_of_config_check_timer_->Cancel();
    TimerManager::DeleteTimer(end_of_config_check_timer_);
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

    if (!end_of_config_)
        return;

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
            NotifyAllDnsRecords(config, ev);
        } else if (config->HasReverseResolutionChanged()) {
            bool reverse_resolution = config->IsReverseResolutionEnabled();
            // if reverse resolution is enabled now, add the reverse records
            // if reverse resolution is disabled now, mark them as not notified
            NotifyReverseDnsRecords(config, ev, reverse_resolution);
        }
    } else {
        ncfg->DelView(config);
        PendingListViewDelete(config);
    }
}

void DnsManager::DnsPtrZone(const Subnet &subnet, const VirtualDnsConfig *vdns,
                            DnsConfig::DnsConfigEvent ev) {

    if (!end_of_config_)
        return;

    if (!bind_status_.IsUp())
        return;

    std::string dns_domain = vdns->GetDomainName();
    if (dns_domain.empty()) {
        DNS_BIND_TRACE(DnsBindTrace, "Ptr Zone <" << vdns->GetName() <<
                       "> ; ignoring event: " << DnsConfig::ToEventString(ev) <<
                       " Domain: " << dns_domain << " Reverse Resolution: " <<
                       (vdns->IsReverseResolutionEnabled()? "enabled" : 
                       "disabled"));
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

    if (!end_of_config_)
        return;

    if (!bind_status_.IsUp())
        return;

    if (named_send_throttled_)
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
        if (!vdns.reverse_resolution) {
            DNS_BIND_TRACE(DnsBindTrace, "Virtual DNS Record <" <<
                           config->GetName() << "> PTR name " << item.name <<
                           " not added - reverse resolution is not enabled");
            return false;
        }
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
        if (item.type == DNS_CNAME_RECORD || item.type == DNS_MX_RECORD)
            item.data = BindUtil::GetFQDN(item.data, vdns.domain_name, ".");
        // In case of NS record, ensure that there are no special characters in data.
        // When it is virtual dns name, we could have chars like ':'
        if (item.type == DNS_NS_RECORD)
            BindUtil::RemoveSpecialChars(item.data);
    }
    DnsItems items;
    items.push_back(item);
    std::string view_name = config->GetViewName();
    return (SendUpdate(op, view_name, zone, items));
}

bool DnsManager::SendUpdate(BindUtil::Operation op, const std::string &view,
                            const std::string &zone, DnsItems &items) {

    if (pending_map_.size() >= named_hi_watermark_) {
        DNS_OPERATIONAL_LOG(
            g_vns_constants.CategoryNames.find(Category::DNSAGENT)->second,
            SandeshLevel::SYS_NOTICE, "Bind named Send Throttled");

        named_send_throttled_ = true;
        return false;
    }

    uint16_t xid = GetTransId();
    return (AddPendingList(xid, view, zone, items, op));
}

void DnsManager::SendRetransmit(uint16_t xid, BindUtil::Operation op,
                                const std::string &view,
                                const std::string &zone, DnsItems &items,
                                uint32_t retransmit_count) {

    uint8_t *pkt = new uint8_t[BindResolver::max_pkt_size];
    int len = BindUtil::BuildDnsUpdate(pkt, op, xid, view, zone, items);
    if (BindResolver::Resolver()->DnsSend(pkt, 0, len)) {
        DNS_BIND_TRACE(DnsBindTrace, 
            "DNS transmit sent for DNS record; xid = " <<
             xid << "; View = " << view << "; Zone = " << zone << "; " <<
             DnsItemsToString(items) << " Retry = " <<
             retransmit_count);
    }
}

void DnsManager::UpdateAll() {

    if (!bind_status_.IsUp()) {
        return;
    }

    if (!end_of_config_) {
        return;
    }

    // Start dumping records to named
    named_send_throttled_ = false;

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
        NotifyAllDnsRecords(vdns, DnsConfig::CFG_ADD);
    }
}

void DnsManager::HandleUpdateResponse(uint8_t *pkt, std::size_t length) {
    dns_flags flags;
    uint16_t xid;
    DnsItems ques, ans, auth, add;
    if (BindUtil::ParseDnsResponse(pkt, length, xid, flags,
                                   ques, ans, auth, add)) {
        if (flags.ret) {
            DNS_BIND_TRACE(DnsBindError, "Update failed : " <<
                           BindUtil::DnsResponseCode(flags.ret) <<
                           "; xid = " << xid);
        } else {
            DNS_BIND_TRACE(DnsBindTrace, "Update successful; xid = " << xid);
            pending_done_queue_.Enqueue(xid);
        }
    }
    delete [] pkt;
}

bool DnsManager::PendingDone(uint16_t xid) {
    DeletePendingList(xid);
    return true;
}

bool DnsManager::ResendRecordsinBatch() {
    static uint16_t start_index = 0;
    uint16_t sent_count = 0;

    PendingListMap::iterator it;
    if (start_index == 0) {
        it = pending_map_.upper_bound(start_index);
    } else {
        it = pending_map_.lower_bound(start_index);
    }

    for (; it != pending_map_.end() ; ) {
         if (it->second.retransmit_count > named_max_retransmissions_) {
             DNS_BIND_TRACE(DnsBindTrace, "DNS records max retransmits reached;"
                            << "no more retransmission; xid = " << it->first);

             dp_pending_map_.insert(PendingListPair(it->first,
                                    PendingList(it->first, it->second.view,
                                                it->second.zone, it->second.items,
                                                it->second.op, 
                                                it->second.retransmit_count)));
             ResetTransId(it->first);
             pending_map_.erase(it++);
         } else {
             sent_count++;
             it->second.retransmit_count++;
             SendRetransmit(it->first, it->second.op, it->second.view,
                            it->second.zone, it->second.items,
                            it->second.retransmit_count);
             it++;
         }
         if (sent_count >= record_send_count_) break;
    }

    if (it != pending_map_.end()) {
        start_index = it->first;
        pending_timer_->Reschedule(named_retransmission_interval_);
        /* Return true to trigger auto-restart of timer */
        return true;
    } else {
        if (pending_map_.size() == 0) {
            start_index = 0;
            return false;
        }
        start_index = pending_map_.begin()->first;
        pending_timer_->Reschedule(named_retransmission_interval_);
    }

    return true;
}

bool DnsManager::AddPendingList(uint16_t xid, const std::string &view,
                                const std::string &zone, const DnsItems &items,
                                BindUtil::Operation op) {
    // delete earlier entries for the same items
    UpdatePendingList(view, zone, items);

    std::pair<PendingListMap::iterator,bool> status;
    status = pending_map_.insert(PendingListPair(xid, PendingList(xid, view,
                                                 zone, items, op)));
    if (status.second == false) {
        dp_pending_map_.insert(PendingListPair(xid, PendingList(xid, view,
                                               zone, items, op)));
        return true;
    } else {
       StartPendingTimer(named_retransmission_interval_*3);
       return true;
    }
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
            it->second.items == items) {
            ResetTransId(it->first);
            pending_map_.erase(it++);
        } else {
            it++;
        }
    }
}

void DnsManager::DeletePendingList(uint16_t xid) {
    ResetTransId(xid);
    pending_map_.erase(xid);
    if (pending_map_.size() <= named_lo_watermark_) {
        if (named_send_throttled_) {
            DNS_OPERATIONAL_LOG(
                g_vns_constants.CategoryNames.find(Category::DNSAGENT)->second,
                SandeshLevel::SYS_NOTICE, "BIND named Send UnThrottled");

            named_send_throttled_ = false;
            NotifyThrottledDnsRecords();
        }
    }
}

void DnsManager::ClearPendingList() {
    pending_map_.clear();
}

// Remove entries from pending list, upon a view delete
void DnsManager::PendingListViewDelete(const VirtualDnsConfig *config) {
    for (PendingListMap::iterator it = pending_map_.begin();
         it != pending_map_.end(); ) {
        if (it->second.view == config->GetViewName()) {
            ResetTransId(it->first);
            pending_map_.erase(it++);
        } else {
            it++;
        }
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
            CheckZoneDelete(zones, it->second)) {
            ResetTransId(it->first);
            pending_map_.erase(it++);
        } else {
            it++;
        }
    }
}

void DnsManager::StartPendingTimer(int msec) {
    if (!pending_timer_->running()) {
        pending_timer_->Start(msec,
            boost::bind(&DnsManager::PendingTimerExpiry, this));
    }
}

void DnsManager::CancelPendingTimer() {
    pending_timer_->Cancel();
}

bool DnsManager::PendingTimerExpiry() {
    return ResendRecordsinBatch();
}

void DnsManager::NotifyThrottledDnsRecords() {

    if (!end_of_config_)
        return;

    if (!bind_status_.IsUp())
        return;

    VirtualDnsConfig::DataMap vmap = VirtualDnsConfig::GetVirtualDnsMap();
    for (VirtualDnsConfig::DataMap::iterator it = vmap.begin();
         it != vmap.end(); ++it) {
        VirtualDnsConfig *vdns = it->second;
        if (!vdns->IsNotified())
            continue;

        for (VirtualDnsConfig::VDnsRec::const_iterator it =
            vdns->virtual_dns_records_.begin();
            it != vdns->virtual_dns_records_.end(); ++it) {
            if ((*it)->IsValid()) {
                if (!((*it)->IsNotified())) {
                    DnsRecord(*it, DnsConfig::CFG_ADD);
                }
            }
        }
    }
}

void DnsManager::NotifyAllDnsRecords(const VirtualDnsConfig *config,
                                     DnsConfig::DnsConfigEvent ev) {

    if (!end_of_config_)
        return;

    if (!bind_status_.IsUp())
        return;

    for (VirtualDnsConfig::VDnsRec::const_iterator it =
         config->virtual_dns_records_.begin();
         it != config->virtual_dns_records_.end(); ++it) {
        // ClearNotified() to all DnsRecords
        (*it)->ClearNotified();
        if ((*it)->IsValid())
            DnsRecord(*it, ev);
    }
}

void DnsManager::NotifyReverseDnsRecords(const VirtualDnsConfig *config,
                                         DnsConfig::DnsConfigEvent ev,
                                         bool notify) {

    if (!end_of_config_)
        return;

    if (!bind_status_.IsUp())
        return;

    for (VirtualDnsConfig::VDnsRec::const_iterator it =
         config->virtual_dns_records_.begin();
         it != config->virtual_dns_records_.end(); ++it) {
        DnsItem item = (*it)->GetRecord();
        if (item.type == DNS_PTR_RECORD) {
            (*it)->ClearNotified();
            if ((*it)->IsValid() && notify) {
                DnsRecord(*it, ev);
            }
        }
    }
}

inline uint16_t DnsManager::GetTransId() {
    return (idx_.AllocIndex());
}

inline void DnsManager::ResetTransId(uint16_t xid) {
    idx_.FreeIndex(xid);
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
            ClearDeportedPendingList();
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

void DnsManager::StartEndofConfigTimer() {
    if (!end_of_config_check_timer_->running()) {
        end_of_config_check_timer_->Start(kEndOfConfigCheckTime,
                              boost::bind(&DnsManager::EndofConfigTimerExpiry, this));
    }
}

void DnsManager::CancelEndofConfigTimer() {
    end_of_config_check_timer_->Cancel();
}

bool DnsManager::EndofConfigTimerExpiry() {
    bool current_config_state = IsEndOfConfig();
    if ((current_config_state != end_of_config_) && current_config_state) {
        end_of_config_ = current_config_state;
        UpdateAll();
    } else {
        end_of_config_ = current_config_state;
    }
    return true;
}

void SandeshError(const std::string &msg, const std::string &context) {
    ErrorResp *resp = new ErrorResp();
    resp->set_resp(msg);
    resp->set_context(context);
    resp->Response();
}

void ShowVirtualDnsServers::HandleRequest() const {
    DnsManager *dns_manager = Dns::GetDnsManager();
    if(dns_manager) {
        dns_manager->VdnsServersMsgHandler("", context());
    } else {
        SandeshError("Invalid Request No DnsManager Object", context());
    }
}

void DnsManager::MakeSandeshPageReq(PageReqData *req, VirtualDnsConfig::DataMap &vdns,
                                    VirtualDnsConfig::DataMap::iterator vdns_it,
                                    VirtualDnsConfig::DataMap::iterator vdns_iter,
                                    const std::string &key, const std::string &req_name) const {
    // Set table size
    req->set_table_size(vdns.size());

    //Next page link
    if(vdns_iter != vdns.end()) {
        req->set_next_page(vdns_iter->first + req_name);
    }

    // First page link
    if(vdns.size() != 0) {
        req->set_first_page((vdns.begin())->first + req_name);
    }

    // Set Entries
    uint16_t start_entry=0, end_entry=0;
    std::stringstream ss1, ss2;
    if(vdns.size() != 0) {
        start_entry = std::distance(vdns.begin(), vdns_it);
        start_entry++;
        if(start_entry != vdns.size()) {
            end_entry = std::distance(vdns.begin(), --vdns_iter);
            end_entry++;
        } else {
            end_entry = start_entry;
        }
    }
    ss1 << start_entry;
    ss2 << end_entry;
    req->set_entries(ss1.str() + " - " + ss2.str());

    // Previous page link
    if(vdns_it != vdns.begin()) {
        for (int i=0; i < (DnsManager::max_records_per_sandesh); i++) {
            vdns_it--;
            if(vdns_it == vdns.begin())
                break;
        }
        req->set_prev_page(vdns_it->first + req_name);
    }

    // Set link to show all entries in a single page
    req->set_all("AllEntries" + req_name);
}

void DnsManager::VdnsServersMsgHandler(const std::string &key,
                                       const std::string &context) const {
    VirtualDnsServersResponse *resp;
    uint16_t count = 0;
    uint16_t sandesh_msg_limit = DnsManager::max_records_per_sandesh;
    VirtualDnsConfig::DataMap vdns = VirtualDnsConfig::GetVirtualDnsMap();
    VirtualDnsConfig::DataMap::iterator vdns_it, vdns_iter;
    std::vector<VirtualDnsServersSandesh> vdns_list_sandesh;
    if(key == "AllEntries") {
        sandesh_msg_limit = vdns.size();
        vdns_it = vdns.begin();
    } else if(key != "") {
        vdns_it = vdns.lower_bound(key);
    } else {
        vdns_it = vdns.begin();
    }

    for (vdns_iter= vdns_it; vdns_iter != vdns.end(); ++vdns_iter) {

        if (count++ == sandesh_msg_limit) {
            break;
        }
        VirtualDnsConfig *vdns_config = vdns_iter->second;
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

    resp = new VirtualDnsServersResponse();
    resp->set_context(context);
    resp->set_virtual_dns_servers(vdns_list_sandesh);
    resp->set_more(true);
    resp->Response();

    Pagination *page = new Pagination();
    PageReqData req;
    MakeSandeshPageReq(&req, vdns, vdns_it, vdns_iter, key, " VdnsServersReq");
    page->set_context(context);
    page->set_req(req);
    page->Response();

}

void ShowDnsConfig::HandleRequest() const {
    DnsManager *dns_manager = Dns::GetDnsManager();
    if(dns_manager) {
        dns_manager->DnsConfigMsgHandler("", context());
    } else {
        SandeshError("Invalid Request No DnsManager Object", context());
    }
}

void DnsManager::DnsConfigMsgHandler(const std::string &key,
                                     const std::string &context) const {
    DnsConfigResponse *resp = new DnsConfigResponse();
    uint16_t count = 0;
    uint16_t sandesh_msg_limit = DnsManager::max_records_per_sandesh;
    VirtualDnsConfig::DataMap vdns = VirtualDnsConfig::GetVirtualDnsMap();
    VirtualDnsConfig::DataMap::iterator vdns_it, vdns_iter;
    std::vector<VirtualDnsSandesh> vdns_list_sandesh;
    if(key == "AllEntries") {
        sandesh_msg_limit = vdns.size();
        vdns_it = vdns.begin();
    } else if(key != "") {
        vdns_it = vdns.lower_bound(key);
    } else {
        vdns_it = vdns.begin();
    }

    for (vdns_iter = vdns_it; vdns_iter != vdns.end(); ++vdns_iter) {
        if (count++ == sandesh_msg_limit) {
            break;
        }
        VirtualDnsConfig *vdns_config = vdns_iter->second;
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

    resp->set_context(context);
    resp->set_virtual_dns(vdns_list_sandesh);
    resp->set_more(true);
    resp->Response();

    Pagination *page = new Pagination();
    PageReqData req;
    MakeSandeshPageReq(&req, vdns, vdns_it, vdns_iter, key, " DnsConfigReq");
    page->set_context(context);
    page->set_req(req);
    page->Response();
}

void ShowVirtualDnsRecords::HandleRequest() const {
    DnsManager *dns_manager = Dns::GetDnsManager();
    if(dns_manager) {
        dns_manager->VdnsRecordsMsgHandler(get_virtual_dns_server(), context());
    } else {
        SandeshError("Invalid Request No DnsManager Object", context());
    }
}

void DnsManager::VdnsRecordsMsgHandler(const std::string &key,
                                       const std::string &context, bool show_all) const {
    VirtualDnsRecordsResponse *resp = new VirtualDnsRecordsResponse();
    VirtualDnsConfig::DataMap vdns = VirtualDnsConfig::GetVirtualDnsMap();

    std::stringstream ss(key);
    std::stringstream str;
    std::string vdns_server, next_iterator;
    VirtualDnsRecordConfig *next_iterator_key = NULL;
    std::getline(ss, vdns_server, '@');
    std::getline(ss, next_iterator);
    uint16_t sandesh_msg_limit = DnsManager::max_records_per_sandesh;
    if (!next_iterator.empty()) {
        std::stringstream next_iter(next_iterator);
        uint64_t value = 0;
        next_iter >> value;
        next_iterator_key = (VirtualDnsRecordConfig *) value;
    }
    std::vector<VirtualDnsRecordTraceData> rec_list_sandesh;
    VirtualDnsConfig::VDnsRec::iterator rec_it, rec_it1;
    VirtualDnsConfig::DataMap::iterator vdns_it = vdns.find(vdns_server);
    if (vdns_it != vdns.end()) {
        VirtualDnsConfig *vdns_config = vdns_it->second;
        uint16_t count = 0;
        uint16_t size = vdns_config->virtual_dns_records_.size();
        if(show_all) {
            rec_it1 = vdns_config->virtual_dns_records_.begin();
            sandesh_msg_limit = size;
        } else {
            rec_it1 = vdns_config->virtual_dns_records_.lower_bound(next_iterator_key);
        }
        for(rec_it = rec_it1;
             rec_it != vdns_config->virtual_dns_records_.end(); ++rec_it) {
            VirtualDnsRecordTraceData rec_trace_data;
            (*rec_it)->VirtualDnsRecordTrace(rec_trace_data);
            rec_list_sandesh.push_back(rec_trace_data);
            if (++count == sandesh_msg_limit) {
                if (++rec_it == vdns_config->virtual_dns_records_.end())
                    break;
                uint64_t value = (uint64_t)(*rec_it);
                str << vdns_server << "@" << value;
                break;
            }
        }

        resp->set_context(context);
        resp->set_virtual_dns_server(vdns_server);
        resp->set_records(rec_list_sandesh);
        resp->set_more(true);
        resp->Response();

        Pagination *page = new Pagination();
        PageReqData req;

        // Set table size
        req.set_table_size(size);

        //Next page link
        if(rec_it != vdns_config->virtual_dns_records_.end()) {
            req.set_next_page(str.str() + " VdnsRecordsReq");
        }

        // First page link
        if(size != 0) {
            std::stringstream str;
            uint64_t value = (uint64_t)(*(vdns_config->virtual_dns_records_.begin()));
            str << vdns_server << "@" << value;
            req.set_first_page(str.str() + " VdnsRecordsReq");
        }

        // Set Entries
        int start_entry=0, end_entry=0;
        std::stringstream ss1, ss2;
        if( size != 0) {
            start_entry = std::distance(vdns_config->virtual_dns_records_.begin(), rec_it1);
            start_entry++;
            if(start_entry != size) {
                end_entry = std::distance(vdns_config->virtual_dns_records_.begin(), --rec_it);
                end_entry++;
            } else {
                end_entry = start_entry;
            }
        }

        ss1 << start_entry;
        ss2 << end_entry;
        req.set_entries(ss1.str() + " - " + ss2.str());

        // Previous page link
        if(rec_it1 != vdns_config->virtual_dns_records_.begin()) {
            for (int i=0; i < (sandesh_msg_limit); i++) {
                rec_it1--;
                if(rec_it1 == vdns_config->virtual_dns_records_.begin())
                    break;
            }
            std::stringstream str;
            uint64_t value = (uint64_t)(*rec_it1);
            str << vdns_server << "@" << value;
            req.set_prev_page(str.str() + " VdnsRecordsReq");
        }

        // Set link to show all entries in a single page
        std::stringstream str;
        uint64_t value = 0;
        str << vdns_server << "@" << value;
        req.set_all(str.str() + " AllEntriesVdnsRecordsReq");

        page->set_context(context);
        page->set_req(req);
        page->Response();

    } else {
        SandeshError("Invalid Request Enter Vdns Server Name", context);
    }
}

void ShowBindPendingList::HandleRequest() const {
    DnsManager *dns_manager = Dns::GetDnsManager();
    if(dns_manager) {
        dns_manager->BindPendingMsgHandler("", context());
    } else {
        SandeshError("Invalid Request No DnsManager Object", context());
    }
}

void DnsManager::BindPendingMsgHandler(const std::string &key,
                                       const std::string &context) const {
    BindPendingListResponse *resp = new BindPendingListResponse();
    DnsManager *dns_manager = Dns::GetDnsManager();
    if (dns_manager) {
        uint16_t count =0;
        uint16_t index=0;
        stringToInteger(key, index);
        uint16_t sandesh_msg_limit = DnsManager::max_records_per_sandesh;
        DnsManager::PendingListMap map =
            dns_manager->GetDeportedPendingListMap();
        uint16_t size = map.size();
        std::vector<PendingListEntry> &pending_list =
            const_cast<std::vector<PendingListEntry>&>(resp->get_data());
        DnsManager::PendingListMap::iterator map_it, map_iter;
        if(key == "AllEntries") {
            sandesh_msg_limit = size;
            map_it = map.begin();
        }
        else if(key != "") {
            map_it = map.lower_bound(index);
        }
        else {
            map_it = map.begin();
        }

        for (map_iter = map_it; map_iter!= map.end(); ++map_iter) {
            if (count++ == sandesh_msg_limit) {
                break;
            }
            PendingListEntry entry;
            entry.set_xid(map_iter->second.xid);
            entry.set_view(map_iter->second.view);
            entry.set_zone(map_iter->second.zone);
            entry.set_retry_count(map_iter->second.retransmit_count);
            entry.set_items(DnsItemsToString(map_iter->second.items));

            pending_list.push_back(entry);
        }

        resp->set_context(context);
        resp->set_more(true);
        resp->Response();

        Pagination *page = new Pagination();
        PageReqData req;

        // Set table size
        req.set_table_size(size);

        //Next page link
        if(map_iter != map.end()) {
            std::stringstream ss;
            ss << map_iter->first;
            req.set_next_page(ss.str() + " BindPendingListReq");
        }

        // First page link
        if(size != 0) {
            std::stringstream ss;
            ss << map.begin()->first;
            req.set_first_page(ss.str()+ " BindPendingListReq");
        }

        // Set Entries
        int start_entry=0, end_entry=0;
        std::stringstream ss1, ss2;
        if(size != 0) {
            start_entry = std::distance(map.begin(), map_it);
            start_entry++;
            if(start_entry != size) {
                end_entry = std::distance(map.begin(), --map_iter);
                end_entry++;
            } else {
                end_entry = start_entry;
            }

        }
        ss1 << start_entry;
        ss2 << end_entry;
        req.set_entries(ss1.str() + " - " + ss2.str());

        // Previous page link
        if(map_it != map.begin()) {
            for (int i=0; i < (sandesh_msg_limit); i++) {
                map_it--;
                if(map_it == map.begin())
                    break;
            }
            std::stringstream ss;
            ss << map_it->first;
            req.set_prev_page(ss.str() + " BindPendingListReq");
        }

        // Set link to show all entries in a single page
        req.set_all("AllEntries BindPendingListReq");

        page->set_context(context);
        page->set_req(req);
        page->Response();

    } else {
        SandeshError("Invalid Request No DnsManager Object ", context);
    }
}

void PageReq::HandleRequest() const {
    string req_name, search_key;
    vector<string> tokens;
    boost::split(tokens, get_key(), boost::is_any_of(" "));
    search_key = tokens[0];
    req_name = tokens[1];
    DnsManager *dns_manager = Dns::GetDnsManager();
    if(dns_manager && (tokens.size() == 2)) {
        if(req_name == "VdnsServersReq") {
            dns_manager->VdnsServersMsgHandler(search_key, context());
        } else if(req_name == "DnsConfigReq") {
            dns_manager->DnsConfigMsgHandler(search_key, context());
        } else if(req_name == "VdnsRecordsReq") {
            dns_manager->VdnsRecordsMsgHandler(search_key, context());
        } else if(req_name == "AllEntriesVdnsRecordsReq") {
            dns_manager->VdnsRecordsMsgHandler(search_key, context(), true);
        } else if (req_name == "BindPendingListReq") {
            dns_manager->BindPendingMsgHandler(search_key, context());
        } else {
            SandeshError("Invalid Request", context());
        }
    } else {
        SandeshError("Invalid Request", context());
    }
}

void ShowGlobalQosConfig::HandleRequest() const {
    GlobalQosConfigResponse *resp = new GlobalQosConfigResponse();
    GlobalQosConfig *obj = GlobalQosConfig::Find("");
    resp->set_control_dscp(obj->control_dscp_);
    resp->set_analytics_dscp(obj->analytics_dscp_);
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}
