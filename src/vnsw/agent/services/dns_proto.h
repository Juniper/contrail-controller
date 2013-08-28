/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dns_proto_hpp
#define vnsw_agent_dns_proto_hpp

#include <vector>
#include "pkt/proto.h"
#include "vnc_cfg_types.h"
#include "bind/bind_util.h"
#include "bind/xmpp_dns_agent.h"

#define DEFAULT_DNS_TTL 120

typedef boost::asio::ip::udp boost_udp;
typedef boost::system::error_code error_code;
typedef boost::function<void(const error_code&, 
                             boost_udp::resolver::iterator)> ResolveHandler;

class AgentDnsXmppChannel;
class VmPortInterface;
class Timer;

class DnsHandler : public ProtoHandler {
public:
    typedef std::vector<boost_udp::resolver *> ResolvList;
    typedef std::map<uint32_t, DnsHandler *> DnsMap;
    typedef std::pair<uint32_t, DnsHandler *> DnsPair;
    static const uint32_t max_items_per_xmpp_msg = 20;

    struct QueryKey {
        const Interface *itf;
        uint16_t xid;

        QueryKey(const Interface *i, uint16_t x) : itf(i), xid(x) {}
        bool operator<(const QueryKey &rhs) const {
            if (itf != rhs.itf)
                return itf < rhs.itf;
            return xid < rhs.xid;
        }
    };
    typedef std::map<QueryKey, DnsHandler *> DnsRequestMap;
    typedef std::pair<QueryKey, DnsHandler *> DnsRequestPair;

    enum Action {
        NONE,
        DNS_QUERY,
        DNS_UPDATE
    };

    enum IpcCommand {
        DNS_NONE,
        DNS_DEFAULT_RESPONSE,
        DNS_BIND_RESPONSE,
        DNS_TIMER_EXPIRED,
        DNS_XMPP_SEND_UPDATE,
        DNS_XMPP_SEND_UPDATE_ALL,
        DNS_XMPP_UPDATE_RESPONSE,
    };

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
        void IncrStatsReq() { requests++; }
        void IncrStatsRetransmitReq() { retransmit_reqs++; }
        void IncrStatsRes() { resolved++; }
        void IncrStatsUnsupp() { unsupported++; }
        void IncrStatsFail() { fail++; }
        void IncrStatsDrop() { drop++; }
    };

    struct DnsIpc : IpcMsg {
        uint8_t *resp;
        uint16_t xid;
        DnsHandler *handler;

        DnsIpc(uint8_t *msg, uint16_t id, DnsHandler *h,
               DnsHandler::IpcCommand cmd)
            : IpcMsg(cmd), resp(msg), xid(id), handler(h) {}

        virtual ~DnsIpc() {
            if (resp)
                delete [] resp;
            if (handler)
                delete handler;
        }
    };

    struct DnsUpdateIpc : IpcMsg {
        DnsUpdateData *xmpp_data;
        const VmPortInterface *itf;
        std::string domain;

        DnsUpdateIpc(DnsAgentXmpp::XmppType type, DnsUpdateData *data,
                     const VmPortInterface *vm, const std::string &dom)
                   : IpcMsg(DnsHandler::DNS_NONE), itf(vm), domain(dom) {
            if (type == DnsAgentXmpp::Update)
                cmd = DnsHandler::DNS_XMPP_SEND_UPDATE;
            else if (type == DnsAgentXmpp::UpdateResponse)
                cmd = DnsHandler::DNS_XMPP_UPDATE_RESPONSE;

            xmpp_data = data;
        }

        virtual ~DnsUpdateIpc() {
            if (xmpp_data)
                delete xmpp_data;
        }
    };

    struct UpdateCompare {
        bool operator() (DnsUpdateIpc *const &lhs, DnsUpdateIpc *const &rhs) {
            if (!lhs || !rhs)
                return lhs < rhs;
            if (lhs->itf != rhs->itf)
                return lhs->itf < rhs->itf;
            DnsUpdateData::Compare tmp;
            return tmp(lhs->xmpp_data, rhs->xmpp_data);
        }
    };
    typedef std::set<DnsUpdateIpc *, UpdateCompare> UpdateMap;

    struct DnsUpdateAllIpc : IpcMsg {
        AgentDnsXmppChannel *channel;

        DnsUpdateAllIpc(AgentDnsXmppChannel *ch) 
            : IpcMsg(DnsHandler::DNS_XMPP_SEND_UPDATE_ALL), channel(ch) {}
    };

    DnsHandler(PktInfo *info, boost::asio::io_service &io);
    virtual ~DnsHandler();
    bool Run();
    bool TimerExpiry(uint16_t xid);
    void DefaultDnsResolveHandler(const error_code& error,
                                  boost_udp::resolver::iterator it,
                                  uint32_t index);

    static void SendDnsIpc(uint8_t *pkt);
    static void SendDnsIpc(IpcCommand cmd, uint16_t xid,
                           uint8_t *msg = NULL, DnsHandler *handler = NULL);
    static void SendDnsUpdateIpc(DnsUpdateData *data,
                                 DnsAgentXmpp::XmppType type,
                                 const VmPortInterface *vm,
                                 const std::string &domain);
    static void SendDnsUpdateIpc(AgentDnsXmppChannel *channel);
    static DnsStats GetStats() { return stats_; }
    static void ClearStats() { stats_.Reset(); }

private:
    bool HandleRequest();
    bool HandleDefaultDnsRequest(const VmPortInterface *vmitf);
    void DefaultDnsSendResponse();
    bool HandleVirtualDnsRequest(const VmPortInterface *vmitf);
    bool HandleMessage();
    bool HandleDefaultDnsResponse();
    bool HandleBindResponse();
    bool HandleUpdateResponse();
    bool HandleRetryExpiry();
    bool HandleUpdate();
    bool UpdateAll();
    void SendXmppUpdate(AgentDnsXmppChannel *channel, DnsUpdateData *xmpp_data);
    uint16_t GetTransId();
    void ParseQuery();
    void Resolve(dns_flags flags, std::vector<DnsItem> &ques, 
                 std::vector<DnsItem> &ans, std::vector<DnsItem> &auth, 
                 std::vector<DnsItem> &add);
    bool SendDnsQuery();
    void SendDnsResponse();
    void UpdateQueryNames();
    void UpdateOffsets(DnsItem &item, bool name_update_required);
    void UpdateNames(DnsUpdateData *data, std::string &domain);
    void UpdateGWAddress(DnsItem &item);
    void Update(DnsUpdateIpc *update);
    void DelUpdate(DnsUpdateIpc *update);
    void UpdateStats();
    std::string DnsItemsToString(std::vector<DnsItem> &items);

    dnshdr  *dns_;
    uint8_t *resp_ptr_;
    uint16_t dns_resp_size_;
    uint16_t xid_;
    uint32_t retries_;
    Action action_;
    QueryKey *rkey_;
    Timer *timer_;
    autogen::IpamType ipam_type_;
    autogen::VirtualDnsType vdns_type_;
    std::vector<DnsItem> items_;
    DnsNameEncoder name_encoder_;
    bool query_name_update_;
    uint16_t query_name_update_len_;   // num bytes added in the query section
    uint16_t pend_req_;
    ResolvList resolv_list_;
    tbb::mutex mutex_;

    static uint16_t g_xid_;
    static DnsMap dns_map_;
    static DnsStats stats_;
    static UpdateMap update_map_;
    static DnsRequestMap req_map_;
    DISALLOW_COPY_AND_ASSIGN(DnsHandler);
};

class DnsProto : public Proto<DnsHandler> {
public:
    static void Init(boost::asio::io_service &io);
    static void ConfigInit();
    static void Shutdown();
    static DnsProto *GetInstance() { 
        return static_cast<DnsProto *>(instance_); 
    }
    virtual ~DnsProto();
    void UpdateDnsEntry(const VmPortInterface *vmitf,
                        const std::string &name, uint32_t plen,
                        const autogen::IpamType &ipam_type,
                        const autogen::VirtualDnsType &vdns_type);
    void UpdateDnsEntry(const VmPortInterface *vmitf, const std::string &name);

    static uint32_t GetTimeout() { return timeout_; }
    static void SetTimeout(uint32_t timeout) { timeout_ = timeout; }
    static uint32_t GetMaxRetries() { return max_retries_; }
    static void SetMaxRetries(uint32_t retries) { max_retries_ = retries; }

private:
    DnsProto(boost::asio::io_service &io);
    void ItfUpdate(DBEntryBase *entry);

    DBTableBase::ListenerId lid_;
    static uint32_t timeout_;   // milli seconds
    static uint32_t max_retries_;

    DISALLOW_COPY_AND_ASSIGN(DnsProto);
};

#endif // vnsw_agent_dns_proto_hpp
