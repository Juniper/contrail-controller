/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/dns.h>
#include <bind/bind_util.h>
#include <bind/bind_resolver.h>
#include <bind/named_config.h>
#include <mgr/dns_mgr.h>
#include <agent/agent_xmpp_channel.h>

uint16_t DnsManager::g_trans_id_;

DnsManager::DnsManager() 
    : bind_status_(boost::bind(&DnsManager::BindEventHandler, this, _1)) {
    std::vector<std::string> bind_servers;
    bind_servers.push_back("127.0.0.1");
    BindResolver::Init(*Dns::GetEventManager()->io_service(), bind_servers,
                       boost::bind(&DnsManager::HandleUpdateResponse, 
                                   this, _1));

    Dns::SetDnsConfigManager(&config_mgr_);    
    DnsConfigManager::Observers obs;
    obs.virtual_dns = boost::bind(&DnsManager::DnsView, this, _1, _2);
    obs.virtual_dns_record = boost::bind(&DnsManager::DnsRecord, this, _1, _2);
    obs.subnet = boost::bind(&DnsManager::DnsPtrZone, this, _1, _2, _3);
    Dns::GetDnsConfigManager()->RegisterObservers(obs);
}

void DnsManager::Initialize(DB *config_db, DBGraph *config_graph) {
    NamedConfig::Init();
    // bind_status_.SetTrigger();
    config_mgr_.Initialize(config_db, config_graph);
}

DnsManager::~DnsManager() {}

void DnsManager::Shutdown() {
    config_mgr_.Terminate();
    BindResolver::Shutdown();
}

void DnsManager::DnsView(const VirtualDnsConfig *config,
                         DnsConfigManager::EventType ev) {
    if (!bind_status_.IsUp())
        return;

    std::string dns_domain = config->GetDomainName();
    if (dns_domain.empty()) {
        DNS_BIND_TRACE(DnsBindError, "Virtual DNS <" <<
                       config->GetName() << 
                       "> doesnt have domain; ignoring event : " << ev);
        return;
    }

    NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
    if (ev == DnsConfigManager::CFG_ADD) {
        ncfg->AddView(config);
    } else if (ev == DnsConfigManager::CFG_CHANGE) {
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
    }
}

void DnsManager::DnsPtrZone(const Subnet &subnet, const VirtualDnsConfig *vdns,
                            DnsConfigManager::EventType ev) {
    if (!bind_status_.IsUp())
        return;

    std::string dns_domain = vdns->GetDomainName();
    if (dns_domain.empty()) {
        return;
    }

    NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
    if (ev == DnsConfigManager::CFG_DELETE) {
        ncfg->DelZone(subnet, vdns);
    } else {
        ncfg->AddZone(subnet, vdns);
    }
}

void DnsManager::DnsRecord(const VirtualDnsRecordConfig *config,
                           DnsConfigManager::EventType ev) {
    if (!bind_status_.IsUp())
        return;

    const autogen::VirtualDnsRecordType &rec = config->GetRecord();
    if (rec.record_name == "" || rec.record_data == "") {
        DNS_BIND_TRACE(DnsBindError, "Virtual DNS Record <" <<
                       config->GetName() << 
                       "> doesnt have name / data; ignoring event : " << ev);
        return;
    }

    if (config->GetViewName() == "") {
        return;
    }

    switch (ev) {
        case DnsConfigManager::CFG_ADD:
        case DnsConfigManager::CFG_CHANGE:
            SendRecordUpdate(BindUtil::ADD_UPDATE, config);
            break;

        case DnsConfigManager::CFG_DELETE:
            SendRecordUpdate(BindUtil::DELETE_UPDATE, config);
            break;

        default:
            assert(0);
    }
}

void DnsManager::SendRecordUpdate(BindUtil::Operation op, 
                                  const VirtualDnsRecordConfig *config) {
    const autogen::VirtualDnsRecordType rec = config->GetRecord();
    const autogen::VirtualDnsType vdns = config->GetVDns();

    std::string zone;
    DnsItem item;
    item.eclass = BindUtil::DnsClass(rec.record_class);
    item.type = BindUtil::DnsType(rec.record_type);
    item.ttl = rec.record_ttl_seconds;
    item.data = rec.record_data;
    if (rec.record_type == "PTR") {
        uint32_t addr;
        if (BindUtil::IsIPv4(rec.record_name, addr)) {
            BindUtil::GetReverseZone(addr, 32, item.name);
        } else {
            item.name = rec.record_name;
            if (!BindUtil::GetAddrFromPtrName(item.name, addr)) {
                DNS_BIND_TRACE(DnsBindError, "Virtual DNS Record <" <<
                               config->GetName() << "> invalid PTR name " <<
                               item.name << "; ignoring");
                return;
            }
        }
        if (!CheckName(config->GetName(), rec.record_data)) {
            return;
        }
        Subnet net;
        if (!config->GetVirtualDns()->GetSubnet(addr, net)) {
            DNS_BIND_TRACE(DnsBindError, "Virtual DNS Record <" << 
                           config->GetName() << 
                           "> doesnt belong to a known subnet; ignoring");
            return;
        }
        BindUtil::GetReverseZone(addr, net.plen, zone);
        item.data = BindUtil::GetFQDN(rec.record_data, vdns.domain_name, ".");
    } else {
        // Bind allows special chars in CNAME, NS names and in CNAME data
        if ((rec.record_type == "A" || rec.record_type == "AAAA") &&
            !CheckName(config->GetName(), rec.record_name)) {
            return;
        }
        zone = config->GetVDns().domain_name;
        item.name = BindUtil::GetFQDN(rec.record_name, 
                                      vdns.domain_name, vdns.domain_name);
        if (rec.record_type == "CNAME")
            item.data = BindUtil::GetFQDN(rec.record_data, vdns.domain_name, ".");
        else
            item.data = rec.record_data;
        // In case of NS record, ensure that there are no special characters in data.
        // When it is virtual dns name, we could have chars like ':'
        if (rec.record_type == "NS")
            BindUtil::RemoveSpecialChars(item.data);
    }
    DnsItems items;
    items.push_back(item);
    SendUpdate(op, config->GetViewName(), zone, items);
}

void DnsManager::SendUpdate(BindUtil::Operation op, const std::string &view,
                            const std::string &zone, DnsItems &items) {
    uint8_t *pkt = new uint8_t[BindResolver::max_pkt_size];
    uint16_t xid = GetTransId();
    int len = BindUtil::BuildDnsUpdate(pkt, op, xid, view, zone, items);
    if (BindResolver::Resolver()->DnsSend(pkt, 0, len))
        DNS_BIND_TRACE(DnsBindTrace, "DNS Update sent for DNS record; xid = " <<
                   xid << "; View = " << view << "; Zone = " << zone << "; " << 
                   DnsItemsToString(items));
}

void DnsManager::UpdateAll() {
    NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
    ncfg->AddAllViews();
    DnsConfigManager::VirtualDnsMap vmap = config_mgr_.GetVirtualDnsMap();
    for (DnsConfigManager::VirtualDnsMap::iterator it = vmap.begin();
         it != vmap.end(); ++it) {
        VirtualDnsConfig *vdns = it->second;
        for (VirtualDnsConfig::VDnsRec::iterator vit = 
             vdns->virtual_dns_records_.begin();
             vit != vdns->virtual_dns_records_.end(); ++vit) {
            if ((*vit)->IsNotified())
                SendRecordUpdate(BindUtil::ADD_UPDATE, *vit);
        }
    }
}

void DnsManager::HandleUpdateResponse(uint8_t *pkt) {
    dns_flags flags;
    uint16_t xid;
    std::vector<DnsItem> ques, ans, auth, add;
    BindUtil::ParseDnsQuery(pkt, xid, flags, ques, ans, auth, add);
    if (flags.ret) {
        DNS_BIND_TRACE(DnsBindError, "Update failed : " << 
                       BindUtil::DnsResponseCode(flags.ret) << 
                       "; xid = " << xid << ";");
    } else {
        DNS_BIND_TRACE(DnsBindTrace, "Update successful; xid = " << xid << ";");
    }
    delete [] pkt;
}

inline uint16_t DnsManager::GetTransId() {
    tbb::mutex::scoped_lock lock(mutex_);
    return (++g_trans_id_ == 0 ? ++g_trans_id_ : g_trans_id_);
}

inline bool DnsManager::CheckName(std::string rec_name, std::string name) {
    if (BindUtil::HasSpecialChars(name)) {
        DNS_BIND_TRACE(DnsBindError, "Virtual DNS / Record <" << rec_name <<
                       "> Special chars are not allowed in DNS name : " <<
                       name << "; ignoring");
        return false;
    }
    return true;
}

void DnsManager::BindEventHandler(BindStatus::Event event) {
    switch (event) {
        case BindStatus::Up: {
            UpdateAll();
            Dns::GetAgentXmppChannelManager()->UpdateAll();
            DNS_BIND_TRACE(DnsBindTrace, "BIND named up;");
            break;
        }

        case BindStatus::Down: {
            DNS_BIND_TRACE(DnsBindTrace, "BIND named down;");
            NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
            ncfg->Reset();
            break;
        }

        default:
            assert(0);
    }
}
