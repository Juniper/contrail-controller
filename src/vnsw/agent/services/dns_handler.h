/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dns_handler_hpp
#define vnsw_agent_dns_handler_hpp

#include "pkt/proto_handler.h"
#include "vnc_cfg_types.h"
#include "bind/bind_util.h"

#define DEFAULT_DNS_TTL 120

typedef boost::asio::ip::udp boost_udp;

class AgentDnsXmppChannel;
class VmInterface;
class Timer;

class DnsHandler : public ProtoHandler {
public:
    typedef boost::function<void(const boost::system::error_code&, 
                                boost_udp::resolver::iterator)> ResolveHandler;
    typedef std::vector<boost_udp::resolver *> ResolvList;
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
        DNS_XMPP_MODIFY_VDNS,
    };

    struct DnsIpc : InterTaskMsg {
        uint8_t *resp;
        uint16_t xid;
        DnsHandler *handler;

        DnsIpc(uint8_t *msg, uint16_t id, DnsHandler *h,
               DnsHandler::IpcCommand cmd)
            : InterTaskMsg(cmd), resp(msg), xid(id), handler(h) {}

        virtual ~DnsIpc() {
            if (resp)
                delete [] resp;
            if (handler)
                delete handler;
        }
    };

    struct DnsUpdateIpc : InterTaskMsg {
        DnsUpdateData *xmpp_data;
        const VmInterface *itf;
        bool floatingIp;
        uint32_t ttl;
        std::string new_vdns;
        std::string old_vdns;
        std::string new_domain;

        DnsUpdateIpc(const VmInterface *vm, const std::string &nvdns,
                     const std::string &ovdns, const std::string &dom,
                     uint32_t ttl_value, bool is_floating)
                   : InterTaskMsg(DnsHandler::DNS_XMPP_MODIFY_VDNS), xmpp_data(NULL),
                     itf(vm), floatingIp(is_floating), ttl(ttl_value),
                     new_vdns(nvdns), old_vdns(ovdns), new_domain(dom) {}

        DnsUpdateIpc(DnsAgentXmpp::XmppType type, DnsUpdateData *data,
                     const VmInterface *vm, bool floating)
                   : InterTaskMsg(DnsHandler::DNS_NONE), xmpp_data(data), itf(vm),
                     floatingIp(floating), ttl(0) {
            if (type == DnsAgentXmpp::Update)
                cmd = DnsHandler::DNS_XMPP_SEND_UPDATE;
            else if (type == DnsAgentXmpp::UpdateResponse)
                cmd = DnsHandler::DNS_XMPP_UPDATE_RESPONSE;
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
            if (lhs->floatingIp != rhs->floatingIp)
                return lhs->floatingIp < rhs->floatingIp;
            DnsUpdateData::Compare tmp;
            return tmp(lhs->xmpp_data, rhs->xmpp_data);
        }
    };

    struct DnsUpdateAllIpc : InterTaskMsg {
        AgentDnsXmppChannel *channel;

        DnsUpdateAllIpc(AgentDnsXmppChannel *ch) 
            : InterTaskMsg(DnsHandler::DNS_XMPP_SEND_UPDATE_ALL), channel(ch) {}
    };

    DnsHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
               boost::asio::io_service &io);
    virtual ~DnsHandler();
    bool Run();
    bool TimerExpiry(uint16_t xid);
    void DefaultDnsResolveHandler(const boost::system::error_code &error,
                                  boost_udp::resolver::iterator it,
                                  uint32_t index);

    static void SendDnsIpc(uint8_t *pkt);
    static void SendDnsIpc(IpcCommand cmd, uint16_t xid,
                           uint8_t *msg = NULL, DnsHandler *handler = NULL);
    static void SendDnsUpdateIpc(DnsUpdateData *data,
                                 DnsAgentXmpp::XmppType type,
                                 const VmInterface *vm,
                                 bool floating = false);
    static void SendDnsUpdateIpc(const VmInterface *vm,
                                 const std::string &new_vdns,
                                 const std::string &old_vdns,
                                 const std::string &new_dom,
                                 uint32_t ttl, bool is_floating);
    static void SendDnsUpdateIpc(AgentDnsXmppChannel *channel);

private:
    bool HandleRequest();
    bool HandleDefaultDnsRequest(const VmInterface *vmitf);
    void DefaultDnsSendResponse();
    bool HandleVirtualDnsRequest(const VmInterface *vmitf);
    bool HandleMessage();
    bool HandleDefaultDnsResponse();
    bool HandleBindResponse();
    bool HandleUpdateResponse();
    bool HandleRetryExpiry();
    bool HandleUpdate();
    bool HandleModifyVdns();
    bool UpdateAll();
    void SendXmppUpdate(AgentDnsXmppChannel *channel, DnsUpdateData *xmpp_data);
    void ParseQuery();
    void Resolve(dns_flags flags, std::vector<DnsItem> &ques, 
                 std::vector<DnsItem> &ans, std::vector<DnsItem> &auth, 
                 std::vector<DnsItem> &add);
    bool SendDnsQuery();
    void SendDnsResponse();
    void UpdateQueryNames();
    void UpdateOffsets(DnsItem &item, bool name_update_required);
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
    std::string ipam_name_;
    autogen::IpamType ipam_type_;
    autogen::VirtualDnsType vdns_type_;
    std::vector<DnsItem> items_;
    DnsNameEncoder name_encoder_;
    bool query_name_update_;
    uint16_t query_name_update_len_;   // num bytes added in the query section
    uint16_t pend_req_;
    ResolvList resolv_list_;
    tbb::mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(DnsHandler);
};

#endif // vnsw_agent_dns_handler_hpp
