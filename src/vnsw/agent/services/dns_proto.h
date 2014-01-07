/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dns_proto_hpp
#define vnsw_agent_dns_proto_hpp

#include "pkt/proto.h"
#include "services/dns_handler.h"
#include "vnc_cfg_types.h"

class VmInterface;
class IFMapNode;

class DnsProto : public Proto {
public:
    static const uint32_t kDnsTimeout = 2000;   // milli seconds
    static const uint32_t kDnsMaxRetries = 2;
    static const uint32_t kDnsDefaultTtl = 84600;

    typedef std::map<uint32_t, DnsHandler *> DnsBindQueryMap;
    typedef std::pair<uint32_t, DnsHandler *> DnsBindQueryPair;
    typedef std::set<DnsHandler::QueryKey> DnsVmRequestSet;
    typedef std::set<DnsHandler::DnsUpdateIpc *,
                     DnsHandler::UpdateCompare> DnsUpdateSet;
    typedef std::map<uint32_t, std::string> IpVdnsMap;
    typedef std::pair<uint32_t, std::string> IpVdnsPair;
    typedef std::map<const VmInterface *, IpVdnsMap> VmDataMap;
    typedef std::pair<const VmInterface *, IpVdnsMap> VmDataPair;

    struct DnsStats {
        uint32_t requests;
        uint32_t resolved;
        uint32_t retransmit_reqs;
        uint32_t unsupported;
        uint32_t fail;
        uint32_t drop;

        void Reset() {
            requests = resolved = retransmit_reqs = unsupported = fail = drop = 0;
        }
        DnsStats() { Reset(); }
    };

    void Init();
    void ConfigInit();
    void Shutdown();
    DnsProto(Agent *agent, boost::asio::io_service &io);
    virtual ~DnsProto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    void UpdateDnsEntry(const VmInterface *vmitf,
                        const std::string &name, const Ip4Address &ip, uint32_t plen,
                        const std::string &vdns_name,
                        const autogen::VirtualDnsType &vdns_type,
                        bool is_floating, bool is_delete);
    bool UpdateDnsEntry(const VmInterface *vmitf, const VnEntry *vn,
                        const Ip4Address &ip, bool is_deleted);
    void IpamUpdate(IFMapNode *node);
    void VdnsUpdate(IFMapNode *node);
    uint16_t GetTransId();

    const DnsUpdateSet &GetUpdateRequestSet() { return update_set_; }
    void AddUpdateRequest(DnsHandler::DnsUpdateIpc *ipc) { update_set_.insert(ipc); }
    void DelUpdateRequest(DnsHandler::DnsUpdateIpc *ipc) { update_set_.erase(ipc); }
    DnsHandler::DnsUpdateIpc *FindUpdateRequest(DnsHandler::DnsUpdateIpc *ipc) {
        DnsUpdateSet::iterator it = update_set_.find(ipc);
        if (it != update_set_.end())
            return *it;
        return NULL;
    }

    void AddDnsQuery(uint16_t xid, DnsHandler *handler) { 
        dns_query_map_.insert(DnsBindQueryPair(xid, handler));
    }
    void DelDnsQuery(uint16_t xid) { dns_query_map_.erase(xid); }
    bool IsDnsQueryInProgress(uint16_t xid) {
        return dns_query_map_.find(xid) != dns_query_map_.end();
    }
    DnsHandler *GetDnsQueryHandler(uint16_t xid) {
        DnsBindQueryMap::iterator it = dns_query_map_.find(xid);
        if (it != dns_query_map_.end())
            return it->second;
        return NULL;
    }

    void AddVmRequest(DnsHandler::QueryKey *key) { curr_vm_requests_.insert(*key); }
    void DelVmRequest(DnsHandler::QueryKey *key) { curr_vm_requests_.erase(*key); }
    bool IsVmRequestDuplicate(DnsHandler::QueryKey *key) {
        return curr_vm_requests_.find(*key) != curr_vm_requests_.end();
    }

    uint32_t GetTimeout() { return timeout_; }
    void SetTimeout(uint32_t timeout) { timeout_ = timeout; }
    uint32_t GetMaxRetries() { return max_retries_; }
    void SetMaxRetries(uint32_t retries) { max_retries_ = retries; }

    void IncrStatsReq() { stats_.requests++; }
    void IncrStatsRetransmitReq() { stats_.retransmit_reqs++; }
    void IncrStatsRes() { stats_.resolved++; }
    void IncrStatsUnsupp() { stats_.unsupported++; }
    void IncrStatsFail() { stats_.fail++; }
    void IncrStatsDrop() { stats_.drop++; }
    DnsStats GetStats() { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    void ItfUpdate(DBEntryBase *entry);
    void VnUpdate(DBEntryBase *entry);
    void ProcessUpdate(std::string name, bool is_deleted, bool is_ipam);
    void CheckForUpdate(IpVdnsMap &ipvdns, const VmInterface *vmitf,
                        const VnEntry *vn, const Ip4Address &ip,
                        std::string &vdns_name, std::string &domain,
                        uint32_t ttl, bool is_floating);
    bool UpdateDnsEntry(const VmInterface *vmitf, const VnEntry *vn,
                        const std::string &vdns_name, const Ip4Address &ip,
                        bool is_floating, bool is_deleted);
    bool UpdateDnsEntry(const VmInterface *vmitf,
                        std::string &new_vdns_name,
                        std::string &old_vdns_name,
                        std::string &new_domain,
                        uint32_t ttl, bool is_floating);
    bool GetVdnsData(const VnEntry *vn, const Ip4Address &vm_addr, 
                     std::string &vdns_name, std::string &domain, uint32_t &ttl);

    uint16_t xid_;
    DnsUpdateSet update_set_;
    DnsBindQueryMap dns_query_map_;
    DnsVmRequestSet curr_vm_requests_;
    DnsStats stats_;
    uint32_t timeout_;   // milli seconds
    uint32_t max_retries_;

    VmDataMap all_vms_;
    DBTableBase::ListenerId lid_;
    DBTableBase::ListenerId Vnlid_;

    DISALLOW_COPY_AND_ASSIGN(DnsProto);
};

#endif // vnsw_agent_dns_proto_hpp
