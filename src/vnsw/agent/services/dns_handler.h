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
        QueryKey(const Interface *i, uint16_t x) : itf(i), xid(x) {}
        bool operator<(const QueryKey &rhs) const {
            if (itf != rhs.itf)
                return itf < rhs.itf;
            return xid < rhs.xid;
        }

        const Interface *itf;
        uint16_t xid;
    };

    enum Action {
        NONE,
        DNS_QUERY,
        DNS_UPDATE
    };

    DnsHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
               boost::asio::io_service &io);
    virtual ~DnsHandler();
    bool Run();
    bool TimerExpiry(uint16_t xid);
    void DefaultDnsResolveHandler(const boost::system::error_code &error,
                                  boost_udp::resolver::iterator it,
                                  uint32_t index);

private:
    friend class DnsTest;

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
    void Update(InterTaskMsg *msg);
    void DelUpdate(InterTaskMsg *msg);
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
