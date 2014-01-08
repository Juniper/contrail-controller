/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_arp_proto_hpp
#define vnsw_agent_arp_proto_hpp

#include "pkt/proto.h"
#include "services/arp_handler.h"
#include "services/arp_entry.h"

#define ARP_TRACE(obj, ...)                                                 \
do {                                                                        \
    Arp##obj::TraceMsg(ArpTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (false)                                                             \

struct ArpVrfState : public DBState {
    bool seen_;
};

struct ArpRouteState : public DBState {
    bool seen_;
};

class ArpProto : public Proto {
public:
    static const uint16_t kGratRetries = 2;
    static const uint32_t kGratRetryTimeout = 2000;        // milli seconds
    static const uint16_t kMaxRetries = 8;
    static const uint32_t kRetryTimeout = 2000;            // milli seconds
    static const uint32_t kAgingTimeout = (5 * 60 * 1000); // milli seconds

    typedef std::map<ArpKey, ArpEntry *> ArpCache;
    typedef std::pair<ArpKey, ArpEntry *> ArpCachePair;
    typedef boost::function<bool(const ArpKey &, ArpEntry *)> Callback;

    struct ArpIpc : InterTaskMsg {
        ArpKey key;
        ArpIpc(ArpHandler::ArpMsgType msg, ArpKey &akey)
            : InterTaskMsg(msg), key(akey) {}
        ArpIpc(ArpHandler::ArpMsgType msg, in_addr_t ip, const VrfEntry *vrf) : 
            InterTaskMsg(msg), key(ip, vrf) {}
    };

    struct ArpStats {
        uint32_t pkts_dropped;
        uint32_t arp_req;
        uint32_t arp_replies;
        uint32_t arp_gracious;
        uint32_t resolved;
        uint32_t max_retries_exceeded;
        uint32_t errors;

        void Reset() {
            pkts_dropped = arp_req = arp_replies = arp_gracious = 
            resolved = max_retries_exceeded = errors = 0;
        }
        ArpStats() { Reset(); }
    };

    void Init();
    void Shutdown();
    ArpProto(Agent *agent, boost::asio::io_service &io, bool run_with_vrouter);
    virtual ~ArpProto();

    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    bool TimerExpiry(ArpKey &key, ArpHandler::ArpMsgType timer_type);
    void UpdateArp(Ip4Address &ip, struct ether_addr &mac, const string &vrf,
                   const Interface &intf, DBRequest::DBOperation op, 
                   bool resolved);

    bool AddArpEntry(const ArpKey &key, ArpEntry *entry);
    bool DeleteArpEntry(const ArpKey &key);
    ArpEntry *FindArpEntry(const ArpKey &key);
    std::size_t GetArpCacheSize() { return arp_cache_.size(); }
    const ArpCache& arp_cache() { return arp_cache_; }

    Interface *ip_fabric_interface() const { return ip_fabric_interface_; }
    uint16_t ip_fabric_interface_index() const {
        return ip_fabric_interface_index_;
    }
    const unsigned char *ip_fabric_interface_mac() const { 
        return ip_fabric_interface_mac_;
    }
    void set_ip_fabric_interface(Interface *itf) { ip_fabric_interface_ = itf; }
    void set_ip_fabric_interface_index(uint16_t ind) {
        ip_fabric_interface_index_ = ind;
    }
    void set_ip_fabric_interface_mac(char *mac) { 
        memcpy(ip_fabric_interface_mac_, mac, ETH_ALEN);
    }

    ArpEntry *gracious_arp_entry() const { return gracious_arp_entry_; }
    void set_gracious_arp_entry(ArpEntry *entry) { gracious_arp_entry_ = entry; }
    void del_gracious_arp_entry();

    void StatsPktsDropped() { arp_stats_.pkts_dropped++; }
    void StatsArpReq() { arp_stats_.arp_req++; }
    void StatsArpReplies() { arp_stats_.arp_replies++; }
    void StatsGracious() { arp_stats_.arp_gracious++; }
    void StatsResolved() { arp_stats_.resolved++; }
    void StatsMaxRetries() { arp_stats_.max_retries_exceeded++; }
    void StatsErrors() { arp_stats_.errors++; }
    ArpStats GetStats() { return arp_stats_; }
    void ClearStats() { arp_stats_.Reset(); }

    uint16_t max_retries() const { return max_retries_; }
    uint32_t retry_timeout() const { return retry_timeout_; }
    uint32_t aging_timeout() const { return aging_timeout_; }
    void set_max_retries(uint16_t retries) { max_retries_ = retries; }
    void set_retry_timeout(uint32_t timeout) { retry_timeout_ = timeout; }
    void set_aging_timeout(uint32_t timeout) { aging_timeout_ = timeout; }

private:
    void VrfNotify(DBTablePartBase *part, DBEntryBase *entry);
    void InterfaceNotify(DBEntryBase *entry);
    void NextHopNotify(DBEntryBase *e);
    void RouteUpdate(DBTablePartBase *part, DBEntryBase *entry);
    void SendArpIpc(ArpHandler::ArpMsgType type,
                    in_addr_t ip, const VrfEntry *vrf);
    void SendArpIpc(ArpHandler::ArpMsgType type, ArpKey &key);

    ArpCache arp_cache_;
    ArpStats arp_stats_;
    bool run_with_vrouter_;
    uint16_t ip_fabric_interface_index_;
    unsigned char ip_fabric_interface_mac_[ETH_ALEN];
    Interface *ip_fabric_interface_;
    ArpEntry *gracious_arp_entry_;
    DBTableBase::ListenerId vid_;
    DBTableBase::ListenerId iid_;
    DBTableBase::ListenerId nhid_;
    DBTableBase::ListenerId fabric_route_table_listener_;

    uint16_t max_retries_;
    uint32_t retry_timeout_;   // milli seconds
    uint32_t aging_timeout_;   // milli seconds

    DISALLOW_COPY_AND_ASSIGN(ArpProto);
};

#endif // vnsw_agent_arp_proto_hpp
