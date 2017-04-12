/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __dns_oper_h__
#define __dns_oper_h__

#include <list>
#include <map>
#include <set>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_node_proxy.h"
#include "vnc_cfg_types.h"
#include "cmn/dns_types.h"

class IFMapNodeProxy;
struct IpamConfig;
struct VirtualDnsConfig;
struct VirtualDnsRecordConfig;

struct DnsConfig {
    std::string name_;
    mutable uint8_t flags_;

    enum DnsConfigEvent {
        CFG_ADD,
        CFG_CHANGE,
        CFG_DELETE,
    };
    
    enum DnsConfigFlags {
        DnsConfigValid = 1 << 0,        // DnsManager received the config
        DnsConfigNotified = 1 << 1,     // DnsManager installed the config
        DnsConfigDeleteMarked = 1 << 2, // Config is deleted
        DnsConfigErrorLogged = 1 << 3,  // Config error logged
    };

    void MarkValid() { flags_ |= DnsConfigValid; }
    bool IsValid() const { return (flags_ & DnsConfigValid); }
    void ClearValid() { flags_ &= ~DnsConfigValid; }
    void MarkNotified() const { flags_ |= DnsConfigNotified; }
    bool IsNotified() const { return (flags_ & DnsConfigNotified); }
    void ClearNotified() const { flags_ &= ~DnsConfigNotified; }
    void MarkDelete() { flags_ |= DnsConfigDeleteMarked; }
    bool IsDeleted() const { return (flags_ & DnsConfigDeleteMarked); }
    void ClearDelete() { flags_ &= ~DnsConfigDeleteMarked; }
    void MarkErrorLogged() { flags_ |= DnsConfigErrorLogged; }
    bool IsErrorLogged() const { return (flags_ & DnsConfigErrorLogged); }

    DnsConfig(const std::string &name) : name_(name), flags_(0) {}
    const std::string &GetName() const { return name_; }

    typedef boost::function<void(const DnsConfig *,
                                 DnsConfig::DnsConfigEvent)> Callback;
    typedef boost::function<void(const Subnet &, const VirtualDnsConfig *,
                                 DnsConfig::DnsConfigEvent)> ZoneCallback;
    static Callback VdnsCallback;
    static Callback VdnsRecordCallback;
    static ZoneCallback VdnsZoneCallback;
    static const std::string EventString[];
    static const std::string &ToEventString(DnsConfigEvent ev) { 
        return EventString[ev];
    }

};

struct VnniConfig : public DnsConfig {
    typedef std::map<std::string, VnniConfig *> DataMap;
    typedef std::pair<std::string, VnniConfig *> DataPair;

    Subnets subnets_;
    IpamConfig *ipam_;
    static DataMap vnni_config_;

    VnniConfig(IFMapNode *node);
    ~VnniConfig();
    bool operator <(VnniConfig &rhs) const {
        return (GetName() < rhs.GetName());
    }

    void OnAdd(IFMapNode *node);
    void OnDelete();
    void OnChange(IFMapNode *node);

    void UpdateIpam(IFMapNode *node);
    Subnets &GetSubnets() { return subnets_; }
    void FindSubnets(IFMapNode *node, Subnets &subnets);
    bool NotifySubnets(Subnets &old_nets, Subnets &new_nets,
                       VirtualDnsConfig *vdns);

    static VnniConfig *Find(std::string name);
};

struct IpamConfig : public DnsConfig {
    typedef std::set<VnniConfig *> VnniList;
    typedef std::map<std::string, IpamConfig *> DataMap;
    typedef std::pair<std::string, IpamConfig *> DataPair;

    autogen::IpamType rec_;
    VirtualDnsConfig *virtual_dns_;
    VnniList vnni_list_;
    static DataMap ipam_config_;

    IpamConfig(IFMapNode *node);
    ~IpamConfig();

    void OnAdd(IFMapNode *node);
    void OnDelete();
    void OnChange(IFMapNode *node);

    void Add(VirtualDnsConfig *vdns);
    void Delete();
    void Notify(DnsConfig::DnsConfigEvent ev);

    bool GetObject(IFMapNode *node, autogen::IpamType &data);
    void AddVnni(VnniConfig *vnni) { vnni_list_.insert(vnni); }
    void DelVnni(VnniConfig *vnni) { vnni_list_.erase(vnni); }
    std::string &GetVirtualDnsName() { 
        return rec_.ipam_dns_server.virtual_dns_server_name; 
    }
    VirtualDnsConfig *GetVirtualDns() { return virtual_dns_; }
    const VnniList &GetVnniList() const { return vnni_list_; }

    void Trace(const std::string &ev);

    static IpamConfig *Find(std::string name);
    static void AssociateIpamVdns(VirtualDnsConfig *vdns);
};

struct VirtualDnsConfig : public DnsConfig {
    typedef std::set<IpamConfig *> IpamList;
    typedef std::set<VirtualDnsRecordConfig *> VDnsRec;
    typedef std::map<std::string, VirtualDnsConfig *> DataMap;
    typedef std::pair<std::string, VirtualDnsConfig *> DataPair;

    autogen::VirtualDnsType rec_;
    autogen::VirtualDnsType old_rec_;
    VDnsRec virtual_dns_records_;
    IpamList ipams_;
    static DataMap virt_dns_config_;

    VirtualDnsConfig(IFMapNode *node);
    VirtualDnsConfig(const std::string &name);
    ~VirtualDnsConfig();
    void OnAdd(IFMapNode *node);
    void OnDelete();
    void OnChange(IFMapNode *node);

    void AddRecord(VirtualDnsRecordConfig *record);
    void DelRecord(VirtualDnsRecordConfig *record);
    void AddIpam(IpamConfig *ipam) { ipams_.insert(ipam); }
    void DelIpam(IpamConfig *ipam) { ipams_.erase(ipam); }
    const IpamList &GetIpamList() const { return ipams_; }
    autogen::VirtualDnsType GetVDns() const { return rec_; }

    bool GetObject(IFMapNode *node, autogen::VirtualDnsType &data);
    bool GetSubnet(uint32_t addr, Subnet &subnet) const;
    void NotifyPendingDnsRecords();
    bool HasChanged();

    void VirtualDnsTrace(VirtualDnsTraceData &rec);
    void Trace(const std::string &ev);

    std::string GetViewName() const;
    std::string GetNextDns() const;
    std::string GetDomainName() const { return rec_.domain_name; }
    std::string GetOldDomainName() const { return old_rec_.domain_name; }
    std::string GetRecordOrder() const { return rec_.record_order; }
    bool IsExternalVisible() const { return rec_.external_visible; }
    bool IsReverseResolutionEnabled() const { return rec_.reverse_resolution; }
    bool HasReverseResolutionChanged() const {
        return rec_.reverse_resolution != old_rec_.reverse_resolution;
    }
    bool DynamicUpdatesEnabled() const;
    int GetTtl() const { return rec_.default_ttl_seconds; }

    static VirtualDnsConfig *Find(std::string name);
    static DataMap &GetVirtualDnsMap() { return virt_dns_config_; }
};

struct VirtualDnsRecordConfig : public DnsConfig {
    typedef std::map<std::string, VirtualDnsRecordConfig *> DataMap;
    typedef std::pair<std::string, VirtualDnsRecordConfig *> DataPair;

    enum Source {
        Config,
        Agent
    };

    DnsItem rec_;
    VirtualDnsConfig *virt_dns_;
    std::string virtual_dns_name_;
    Source src_;
    static DataMap virt_dns_rec_config_;

    VirtualDnsRecordConfig(IFMapNode *node);
    VirtualDnsRecordConfig(const std::string &name,
                           const std::string &vdns_name, const DnsItem &item);
    ~VirtualDnsRecordConfig();
    void OnAdd(IFMapNode *node = NULL);
    void OnDelete();
    void OnChange(IFMapNode *node);
    void OnChange(const DnsItem &new_rec);
    bool UpdateVdns(IFMapNode *node);

    bool CanNotify();
    bool HasChanged(DnsItem &rhs);
    bool OnlyTtlChange(DnsItem &rhs);
    autogen::VirtualDnsType GetVDns() const;
    const DnsItem &GetRecord() const { return rec_; }
    std::string GetViewName() const;
    VirtualDnsConfig *GetVirtualDns() const { return virt_dns_; }
    bool GetObject(IFMapNode *node, DnsItem &item);

    void VirtualDnsRecordTrace(VirtualDnsRecordTraceData &rec);
    void Trace(const std::string &ev);

    static VirtualDnsRecordConfig *Find(std::string name);
    static void UpdateVirtualDns(VirtualDnsConfig *vdns);
};

struct GlobalQosConfig : public DnsConfig {
    GlobalQosConfig(IFMapNode *node);
    ~GlobalQosConfig();

    void OnAdd(IFMapNode *node);
    void OnDelete();
    void OnChange(IFMapNode *node);
    void SetDscp();
    static GlobalQosConfig *Find(const std::string &name);

    uint8_t control_dscp_;
    uint8_t analytics_dscp_;
    static GlobalQosConfig *singleton_;
};
#endif // __dns_oper_h__
