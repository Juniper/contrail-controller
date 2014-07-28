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

    enum InterTaskMessage {
        DNS_NONE,
        DNS_DEFAULT_RESPONSE,
        DNS_BIND_RESPONSE,
        DNS_TIMER_EXPIRED,
        DNS_XMPP_SEND_UPDATE,
        DNS_XMPP_SEND_UPDATE_ALL,
        DNS_XMPP_UPDATE_RESPONSE,
        DNS_XMPP_MODIFY_VDNS,
    };

    struct DnsIpc : InterTaskMsg {
        DnsIpc(uint8_t *msg, uint16_t id, DnsHandler *h, InterTaskMessage cmd)
            : InterTaskMsg(cmd), resp(msg), xid(id), handler(h) {}

        virtual ~DnsIpc() {
            if (resp)
                delete [] resp;
            if (handler)
                delete handler;
        }

        uint8_t *resp;
        uint16_t xid;
        DnsHandler *handler;
    };

    struct DnsUpdateIpc : InterTaskMsg {
        DnsUpdateIpc(const VmInterface *vm, const std::string &nvdns,
                     const std::string &ovdns, const std::string &dom,
                     uint32_t ttl_value, bool is_floating)
                   : InterTaskMsg(DNS_XMPP_MODIFY_VDNS), xmpp_data(NULL),
                     itf(vm), floatingIp(is_floating), ttl(ttl_value),
                     new_vdns(nvdns), old_vdns(ovdns), new_domain(dom) {}

        DnsUpdateIpc(DnsAgentXmpp::XmppType type, DnsUpdateData *data,
                     const VmInterface *vm, bool floating)
                   : InterTaskMsg(DNS_NONE), xmpp_data(data), itf(vm),
                     floatingIp(floating), ttl(0) {
            if (type == DnsAgentXmpp::Update)
                cmd = DNS_XMPP_SEND_UPDATE;
            else if (type == DnsAgentXmpp::UpdateResponse)
                cmd = DNS_XMPP_UPDATE_RESPONSE;
        }

        virtual ~DnsUpdateIpc() {
            if (xmpp_data)
                delete xmpp_data;
        }

        DnsUpdateData *xmpp_data;
        const VmInterface *itf;
        bool floatingIp;
        uint32_t ttl;
        std::string new_vdns;
        std::string old_vdns;
        std::string new_domain;
    };

    struct UpdateCompare {
        bool operator() (DnsUpdateIpc *const &lhs, DnsUpdateIpc *const &rhs) {
            if (!lhs || !rhs)
                return lhs < rhs;
            if (lhs->itf != rhs->itf)
                return lhs->itf < rhs->itf;
            if (lhs->floatingIp != rhs->floatingIp)
                return lhs->floatingIp < rhs->floatingIp;
            DnsUpdateData::Compare tmp;
            return tmp(lhs->xmpp_data, rhs->xmpp_data);
        }
    };

    struct DnsUpdateAllIpc : InterTaskMsg {
        DnsUpdateAllIpc(AgentDnsXmppChannel *ch) 
            : InterTaskMsg(DNS_XMPP_SEND_UPDATE_ALL), channel(ch) {}

        AgentDnsXmppChannel *channel;
    };

    struct DnsStats {
        DnsStats() { Reset(); }
        void Reset() {
            requests = resolved = retransmit_reqs = unsupported = fail = drop = 0;
        }

        uint32_t requests;
        uint32_t resolved;
        uint32_t retransmit_reqs;
        uint32_t unsupported;
        uint32_t fail;
        uint32_t drop;
    };

    struct DnsFipEntry {
        DnsFipEntry(const VnEntry *vn, const Ip4Address &fip,
                    const VmInterface *itf);
        virtual ~DnsFipEntry();
        bool IsLess(const DnsFipEntry *rhs) const;
        const VnEntry *vn_;
        Ip4Address floating_ip_;
        const VmInterface *interface_;
        std::string vdns_name_;
    };

    typedef boost::shared_ptr<DnsFipEntry> DnsFipEntryPtr;

    class DnsFipEntryCmp {
        public:
            bool operator()(const DnsFipEntryPtr &lhs, const DnsFipEntryPtr &rhs) const;
    };
    typedef std::set<DnsFipEntryPtr, DnsFipEntryCmp> DnsFipSet;
    typedef std::map<uint32_t, DnsHandler *> DnsBindQueryMap;
    typedef std::pair<uint32_t, DnsHandler *> DnsBindQueryPair;
    typedef std::set<DnsHandler::QueryKey> DnsVmRequestSet;
    typedef std::set<DnsUpdateIpc *, UpdateCompare> DnsUpdateSet;
    typedef std::map<uint32_t, std::string> IpVdnsMap;
    typedef std::pair<uint32_t, std::string> IpVdnsPair;
    typedef std::map<const VmInterface *, IpVdnsMap> VmDataMap;
    typedef std::pair<const VmInterface *, IpVdnsMap> VmDataPair;

    void ConfigInit();
    void Shutdown();
    void IoShutdown();
    DnsProto(Agent *agent, boost::asio::io_service &io);
    virtual ~DnsProto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    bool SendUpdateDnsEntry(const VmInterface *vmitf, const std::string &name,
                            const Ip4Address &ip, uint32_t plen,
                            const std::string &vdns_name,
                            const autogen::VirtualDnsType &vdns_type,
                            bool is_floating, bool is_delete);
    bool UpdateFloatingIp(const VmInterface *vmitf, const VnEntry *vn,
                          const Ip4Address &ip, bool is_deleted);
    void IpamNotify(IFMapNode *node);
    void VdnsNotify(IFMapNode *node);
    uint16_t GetTransId();

    void SendDnsIpc(uint8_t *pkt);
    void SendDnsIpc(InterTaskMessage cmd, uint16_t xid,
                    uint8_t *msg, DnsHandler *handler);
    void SendDnsUpdateIpc(DnsUpdateData *data, DnsAgentXmpp::XmppType type,
                          const VmInterface *vm, bool floating);
    void SendDnsUpdateIpc(const VmInterface *vm, const std::string &new_vdns,
                          const std::string &old_vdns,
                          const std::string &new_dom,
                          uint32_t ttl, bool is_floating);
    void SendDnsUpdateIpc(AgentDnsXmppChannel *channel);

    const DnsUpdateSet &update_set() const { return update_set_; }
    void AddUpdateRequest(DnsUpdateIpc *ipc) { update_set_.insert(ipc); }
    void DelUpdateRequest(DnsUpdateIpc *ipc) { update_set_.erase(ipc); }
    DnsUpdateIpc *FindUpdateRequest(DnsUpdateIpc *ipc) {
        DnsUpdateSet::iterator it = update_set_.find(ipc);
        if (it != update_set_.end())
            return *it;
        return NULL;
    }

    void AddDnsQuery(uint16_t xid, DnsHandler *handler);
    void DelDnsQuery(uint16_t xid);
    bool IsDnsQueryInProgress(uint16_t xid);
    DnsHandler *GetDnsQueryHandler(uint16_t xid);

    void AddVmRequest(DnsHandler::QueryKey *key);
    void DelVmRequest(DnsHandler::QueryKey *key);
    bool IsVmRequestDuplicate(DnsHandler::QueryKey *key);

    uint32_t timeout() const { return timeout_; }
    void set_timeout(uint32_t timeout) { timeout_ = timeout; }
    uint32_t max_retries() const { return max_retries_; }
    void set_max_retries(uint32_t retries) { max_retries_ = retries; }

    void IncrStatsReq() { stats_.requests++; }
    void IncrStatsRetransmitReq() { stats_.retransmit_reqs++; }
    void IncrStatsRes() { stats_.resolved++; }
    void IncrStatsUnsupp() { stats_.unsupported++; }
    void IncrStatsFail() { stats_.fail++; }
    void IncrStatsDrop() { stats_.drop++; }
    const DnsStats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }
    const VmDataMap& all_vms() const { return all_vms_; }
    const DnsFipSet& fip_list() const { return fip_list_; }

private:
    void InterfaceNotify(DBEntryBase *entry);
    void VnNotify(DBEntryBase *entry);
    void ProcessNotify(std::string name, bool is_deleted, bool is_ipam);
    void CheckForUpdate(IpVdnsMap &ipvdns, const VmInterface *vmitf,
                        const VnEntry *vn, const Ip4Address &ip,
                        std::string &vdns_name,
                        const autogen::VirtualDnsType &vdns_type);
    void CheckForFipUpdate(DnsFipEntry *entry, std::string &vdns_name,
                           const autogen::VirtualDnsType &vdns_type);
    bool UpdateDnsEntry(const VmInterface *vmitf, const VnEntry *vn,
                        const std::string &vm_name,
                        const std::string &vdns_name, const Ip4Address &ip,
                        bool is_floating, bool is_deleted);
    bool MoveVDnsEntry(const VmInterface *vmitf,
                       std::string &new_vdns_name,
                       std::string &old_vdns_name,
                       const autogen::VirtualDnsType &vdns_type,
                       bool is_floating);
    bool GetVdnsData(const VnEntry *vn, const Ip4Address &vm_addr, 
                     std::string &vdns_name,
                     autogen::VirtualDnsType &vdns_type);
    bool GetFipName(const VmInterface *vmitf,
                    const  autogen::VirtualDnsType &vdns_type,
                    const Ip4Address &ip, std::string &fip_name) const;

    uint16_t xid_;
    DnsUpdateSet update_set_;
    DnsBindQueryMap dns_query_map_;
    DnsVmRequestSet curr_vm_requests_;
    DnsStats stats_;
    uint32_t timeout_;   // milli seconds
    uint32_t max_retries_;

    VmDataMap all_vms_;
    DnsFipSet fip_list_;
    DBTableBase::ListenerId lid_;
    DBTableBase::ListenerId Vnlid_;

    DISALLOW_COPY_AND_ASSIGN(DnsProto);
};

#endif // vnsw_agent_dns_proto_hpp
