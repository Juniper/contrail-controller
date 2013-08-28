/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DNS_CONFIG_H__
#define __DNS_CONFIG_H__

#include <list>
#include <map>
#include <set>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/ptr_container/ptr_map.hpp>

#include "base/util.h"
#include "base/queue_task.h"
#include "base/task_trigger.h"
#include "bgp/bgp_peer_key.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_node_proxy.h"
#include "vnc_cfg_types.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "cmn/dns_types.h"

extern SandeshTraceBufferPtr DnsConfigTraceBuf;

#define DNS_TRACE(Obj, ...)                                                   \
do {                                                                          \
    Obj::TraceMsg(DnsConfigTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);         \
} while (false)                                                               \

class ConfigListener;
class ConfigManager;
class DB;
class DBTable;
class DBGraph;
class IFMapNodeProxy;
struct IpamConfig;
struct VnniConfig;
struct VirtualDnsConfig;
struct VirtualDnsRecordConfig;
class DnsConfigManager;

typedef boost::shared_ptr<IFMapNodeProxy> IFMapNodeRef;

struct DnsConfig {
    std::string name_;
    IFMapNodeProxy node_proxy_;
    uint8_t flags_;

    enum DnsConfigFlags {
        Notified = 1 << 0,
        DeleteMarked = 1 << 1,
    };

    void MarkDelete() { flags_ |= DeleteMarked; }
    bool IsDeleted() const { return (flags_ & DeleteMarked); }
    void ClearDelete() { flags_ &= ~DeleteMarked; }
    void MarkNotified() { flags_ |= Notified; }
    bool IsNotified() const { return (flags_ & Notified); }
    void ClearNotified() { flags_ &= ~Notified; }

    DnsConfig(const std::string &name, IFMapNodeProxy *proxy) : name_(name) {
        SetProxy(proxy);
        flags_ = 0;
    }

    IFMapNode *GetNode() {
        return node_proxy_.node();
    }

    void SetProxy(IFMapNodeProxy *proxy) {
        if (proxy != NULL)
            node_proxy_.Swap(proxy);
    }

    const std::string &GetName() const { return name_; }
};

struct VnniConfig : public DnsConfig {
    Subnets subnets_;
    IpamConfig *ipam_;

    VnniConfig(const std::string &name, IFMapNodeProxy *proxy, 
               IpamConfig *ipam) : DnsConfig(name, proxy), ipam_(ipam) {}

    bool operator <(VnniConfig &rhs) const {
        return (GetName() < rhs.GetName());
    }

    void FindSubnets(Subnets &subnets);
    Subnets &GetSubnets() { return subnets_; }
    IpamConfig *GetIpam() { return ipam_; }
};

struct VirtualDnsConfig : public DnsConfig {
    typedef std::set<IpamConfig *> IpamList;
    typedef std::set<VirtualDnsRecordConfig *> VDnsRec;

    autogen::VirtualDnsType rec_;
    autogen::VirtualDnsType old_rec_;
    VDnsRec virtual_dns_records_;
    IpamList ipams_;

    VirtualDnsConfig(const std::string &name, IFMapNodeProxy *proxy)
        : DnsConfig(name, proxy) {
        GetObject(rec_);
        old_rec_ = rec_;
    }

    void AddRecord(VirtualDnsRecordConfig *record) {
        virtual_dns_records_.insert(record);
    }
    void DelRecord(VirtualDnsRecordConfig *record) {
        virtual_dns_records_.erase(record);
    }

    void AddIpam(IpamConfig *ipam) { ipams_.insert(ipam); }
    void DelIpam(IpamConfig *ipam) { ipams_.erase(ipam); }
    const IpamList &GetIpamList() const { return ipams_; }
    autogen::VirtualDnsType GetVDns() const { return rec_; }
    bool GetSubnet(uint32_t addr, Subnet &subnet) const;

    bool GetObject(autogen::VirtualDnsType &data) {
        IFMapNode *node = GetNode();
        if (!node)
            return false;

        autogen::VirtualDns *dns =
            static_cast<autogen::VirtualDns *>(GetNode()->GetObject());
        if (!dns)
            return false;

        data = dns->data();
        return true;
    }

    bool HasChanged() {
        if (rec_.domain_name == old_rec_.domain_name &&
            rec_.dynamic_records_from_client == old_rec_.dynamic_records_from_client &&
            rec_.record_order == old_rec_.record_order &&
            rec_.default_ttl_seconds == old_rec_.default_ttl_seconds &&
            rec_.next_virtual_DNS == old_rec_.next_virtual_DNS)
            return false;
        return true;
    }

    void VirtualDnsTrace(VirtualDnsTraceData &rec) {
        rec.name = name_;
        rec.dns_name = rec_.domain_name;
        rec.dns_dyn_rec = rec_.dynamic_records_from_client;
        rec.dns_order = rec_.record_order;
        rec.dns_ttl = rec_.default_ttl_seconds;
        rec.dns_next = rec_.next_virtual_DNS;
    }

    void Trace(const std::string &ev) {
        VirtualDnsTraceData rec;
        VirtualDnsTrace(rec);
        DNS_TRACE(VirtualDnsTrace, ev, rec);
    }

    std::string GetViewName() const { 
        std::string name(GetName());
        BindUtil::RemoveSpecialChars(name);
        return name; 
    }
    std::string GetDomainName() const { return rec_.domain_name; }
    std::string GetOldDomainName() const { return old_rec_.domain_name; }
    std::string GetRecordOrder() const { return rec_.record_order; }
    std::string GetNextDns() const { 
        std::string name(rec_.next_virtual_DNS);
        BindUtil::RemoveSpecialChars(name);
        return name; 
    }
    bool DynamicUpdatesEnabled() const { return rec_.dynamic_records_from_client; }
    int GetTtl() const { return rec_.default_ttl_seconds; }
};

struct VirtualDnsRecordConfig : public DnsConfig {
    autogen::VirtualDnsRecordType rec_;
    VirtualDnsConfig *virt_dns_;

    VirtualDnsRecordConfig(const std::string &name, IFMapNodeProxy *proxy,
                           VirtualDnsConfig *virt_dns) 
                         : DnsConfig(name, proxy), virt_dns_(virt_dns) {
        GetObject(rec_);
    }

    bool CanNotify();
    autogen::VirtualDnsType GetVDns() const { 
        autogen::VirtualDnsType data;
        data.dynamic_records_from_client = false;
        data.default_ttl_seconds = 0;
        if (virt_dns_)
            data = virt_dns_->GetVDns();
        return data;
    }
    const autogen::VirtualDnsRecordType &GetRecord() const { return rec_; }
    std::string GetViewName() const { 
        if (virt_dns_) {
            return virt_dns_->GetViewName(); 
        } else
            return "";
    }
    VirtualDnsConfig *GetVirtualDns() const { return virt_dns_; }

    bool GetObject(autogen::VirtualDnsRecordType &data) {
        IFMapNode *node = GetNode();
        if (!node)
            return false;

        autogen::VirtualDnsRecord *rec =
            static_cast<autogen::VirtualDnsRecord *>(node->GetObject());
        if (!rec)
            return false;

        data = rec->data();
        return true;
    }

    bool RecordEqual(autogen::VirtualDnsRecordType &rhs) {
        if (rec_.record_name == rhs.record_name &&
            rec_.record_type == rhs.record_type &&
            rec_.record_class == rhs.record_class &&
            rec_.record_data == rhs.record_data &&
            rec_.record_ttl_seconds == rhs.record_ttl_seconds)
            return true;
        return false;
    }

    void VirtualDnsRecordTrace(VirtualDnsRecordTraceData &rec) {
        rec.name = name_;
        rec.rec_name = rec_.record_name;
        rec.rec_type = rec_.record_type;
        rec.rec_class = rec_.record_class;
        rec.rec_data = rec_.record_data;
        rec.rec_ttl = rec_.record_ttl_seconds;
        if (IsNotified())
            rec.installed = "true";
        else
            rec.installed = "false";
    }

    void Trace(const std::string &ev) {
        VirtualDnsRecordTraceData rec;
        VirtualDnsRecordTrace(rec);
        std::string dns_name;
        if (virt_dns_)
            dns_name = virt_dns_->name_;
        DNS_TRACE(VirtualDnsRecordTrace, ev, dns_name, rec);
    }
};

struct ConfigDelta {
    ConfigDelta();
    ConfigDelta(const ConfigDelta &rhs);
    ~ConfigDelta();
    std::string id_type;
    std::string id_name;
    IFMapNodeRef node;
    IFMapObjectRef obj;
};

template <typename Entry>
struct DnsConfigData {
    typedef std::map<std::string, Entry> DataMap;
    typedef std::pair<std::string, Entry> DataPair;

    DataMap data_;

    void Add(std::string name, Entry entry) {
        data_.insert(DataPair(name, entry));
    }

    void Del(std::string name) {
        data_.erase(name);
    }

    Entry Find(std::string name) {
        typename DataMap::iterator iter = data_.find(name);
        if (iter != data_.end())
            return iter->second;
        return NULL;
    }
};

class DnsConfigManager {
public:
    static const int kConfigTaskInstanceId = 0;
    enum EventType {
        CFG_NONE,
        CFG_ADD,
        CFG_CHANGE,
        CFG_DELETE
    };
    static const std::string EventString[];

    typedef std::map<std::string, VirtualDnsConfig *> VirtualDnsMap;
    typedef boost::function<void(const Subnet &, const VirtualDnsConfig *, EventType)>
        IpamObserver;
    typedef boost::function<void(const VirtualDnsConfig *, EventType)>
        VirtualDnsObserver;
    typedef boost::function<void(const VirtualDnsRecordConfig *, EventType)>
        VirtualDnsRecordObserver;

    struct Observers {
        IpamObserver subnet;
        VirtualDnsObserver virtual_dns;
        VirtualDnsRecordObserver virtual_dns_record;
    };

    DnsConfigManager();
    virtual ~DnsConfigManager();
    void Initialize(DB *db, DBGraph *db_graph);

    void Notify(const VirtualDnsConfig *config, EventType event);
    void Notify(const VirtualDnsRecordConfig *config, EventType event);
    void Notify(Subnet &, const VirtualDnsConfig *, EventType);

    void RegisterObservers(const Observers &obs) { obs_ = obs; }

    DB *database() { return db_; }
    DBGraph *graph() { return db_graph_; }

    void OnChange();

    const std::string &localname() const { return localname_; }
    const VirtualDnsMap &GetVirtualDnsMap() {
        return virt_dns_config_.data_;
    }

    void Terminate();

private:
    typedef std::vector<ConfigDelta> ChangeList;
    typedef std::map<std::string,
        boost::function<void(const ConfigDelta &)> >IdentifierMap;

    void IdentifierMapInit();
    void ProcessChanges(const ChangeList &change_list);
    void ProcessNetworkIpam(const ConfigDelta &delta);
    void ProcessVNNI(const ConfigDelta &delta);
    void ProcessVirtualDNS(const ConfigDelta &delta);
    void ProcessVirtualDNSRecord(const ConfigDelta &delta);
    IFMapNode *FindTarget(IFMapNode *node, std::string link_name);
    IFMapNode *FindTarget(IFMapNode *node, std::string link_name, std::string node_type);
    bool NotifySubnets(Subnets &old_nets,
                       Subnets &new_nets, VirtualDnsConfig *vdns);
    void NotifyPendingDnsRecords(VirtualDnsConfig *vdns);

    const std::string &ToEventString(EventType ev) { return EventString[ev]; }

    bool ConfigHandler();
    static int config_task_id_;

    DB *db_;
    DBGraph *db_graph_;
    std::string localname_;
    IdentifierMap id_map_;
    DnsConfigData<IpamConfig *> ipam_config_;
    DnsConfigData<VnniConfig *> vnni_config_;
    DnsConfigData<VirtualDnsConfig *> virt_dns_config_;
    DnsConfigData<VirtualDnsRecordConfig *> virt_dns_rec_config_;
    Observers obs_;
    TaskTrigger trigger_;
    boost::scoped_ptr<ConfigListener> listener_;
    DISALLOW_COPY_AND_ASSIGN(DnsConfigManager);
};

struct IpamConfig : public DnsConfig {
    typedef std::set<VnniConfig *> VnniList;
    typedef boost::function<void(Subnet &, const VirtualDnsConfig *, 
                                 DnsConfigManager::EventType)> Callback;

    autogen::IpamType rec_;
    VirtualDnsConfig *virtual_dns_;
    VnniList vnni_list_;

    IpamConfig(const std::string &name, IFMapNodeProxy *proxy) 
        : DnsConfig(name, proxy), virtual_dns_(NULL) {
        GetObject(rec_);
    }

    ~IpamConfig() {
        for (VnniList::iterator it = vnni_list_.begin();
             it != vnni_list_.end(); ++it) {
            (*it)->ipam_ = NULL;
        }
    }

    void Add(VirtualDnsConfig *vdns, Callback cb) {
        virtual_dns_ = vdns;
        if (virtual_dns_)
            virtual_dns_->AddIpam(this);
        Notify(DnsConfigManager::CFG_ADD, cb);
    }

    void Delete(Callback cb) {
        MarkDelete();
        Notify(DnsConfigManager::CFG_DELETE, cb);
        if (virtual_dns_)
            virtual_dns_->DelIpam(this);
    }

    void Notify(DnsConfigManager::EventType ev, Callback cb) {
        for (IpamConfig::VnniList::iterator it = vnni_list_.begin();
             it != vnni_list_.end(); ++it) {
            Subnets &subnets = (*it)->GetSubnets();
            for (unsigned int i = 0; i < subnets.size(); i++) {
                cb(subnets[i], virtual_dns_, ev);
            }
        }
    }

    bool GetObject(autogen::IpamType &data) {
        IFMapNode *node = GetNode();
        if (!node)
            return false;

        autogen::NetworkIpam *ipam =
            static_cast<autogen::NetworkIpam *>(node->GetObject());
        if (!ipam)
            return false;

        data = ipam->mgmt();
        return true;
    }

    void AddVnni(VnniConfig *vnni) { vnni_list_.insert(vnni); }
    void DelVnni(VnniConfig *vnni) { vnni_list_.erase(vnni); }
    std::string &GetVirtualDnsName() { 
        return rec_.ipam_dns_server.virtual_dns_server_name; 
    }
    VirtualDnsConfig *GetVirtualDns() { return virtual_dns_; }
    const VnniList &GetVnniList() const { return vnni_list_; }

    void Trace(const std::string &ev) {
        DNS_TRACE(IpamTrace, name_, GetVirtualDnsName(), ev);
    }
};

#endif // __DNS_CONFIG_H__
