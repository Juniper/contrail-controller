/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_arp_proto_hpp
#define vnsw_agent_arp_proto_hpp

#include "pkt/proto.h"
#include "services/arp_handler.h"

#define ARP_TRACE(obj, ...)                                                 \
do {                                                                        \
    Arp##obj::TraceMsg(ArpTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (false)                                                             \

class ArpEntry;
class ArpNHClient;

template <typename CacheKey, typename CacheEntry>
class Cache {
public:
    typedef std::map<CacheKey, CacheEntry> CacheMap;
    typedef std::pair<CacheKey, CacheEntry> CachePair;
    typedef typename std::map<CacheKey, CacheEntry>::iterator CacheIter;
    // Callback for iteration; retuns true to continue, false to stop
    typedef boost::function<bool(const CacheKey &, CacheEntry &)> Callback;

    static const uint32_t max_cache_size = 1024;

    bool Add(CacheKey &key, CacheEntry &entry) {
        tbb::mutex::scoped_lock lock(mutex_);
        if (cache_.size() < max_cache_size)
            return cache_.insert(CachePair(key, entry)).second;
        return false;
    };
    bool Delete(CacheKey &key) {
        tbb::mutex::scoped_lock lock(mutex_);
        return (bool) cache_.erase(key);
    };
    CacheEntry Find(CacheKey &key) {
        tbb::mutex::scoped_lock lock(mutex_);
        CacheIter it = cache_.find(key);
        if (it == cache_.end())
            return NULL;
        return it->second;
    };
    void Iterate(Callback cb) { 
        if (cb) {
            tbb::mutex::scoped_lock lock(mutex_);
            for (CacheIter it = cache_.begin(); it != cache_.end(); it++) 
                if (!cb(it->first, it->second))
                    return;
        }
    };
    std::size_t Size() { 
        tbb::mutex::scoped_lock lock(mutex_);
        return cache_.size(); 
    };
    void Clear() { 
        tbb::mutex::scoped_lock lock(mutex_);
        cache_.clear(); 
    };

private:
    tbb::mutex mutex_;
    CacheMap cache_;
};

class ArpProto : public Proto {
public:
    static const uint16_t kGratRetries = 2;
    static const uint32_t kGratRetryTimeout = 2000;        // milli seconds
    static const uint16_t kMaxRetries = 8;
    static const uint32_t kRetryTimeout = 2000;            // milli seconds
    static const uint32_t kAgingTimeout = (5 * 60 * 1000); // milli seconds

    typedef Cache<ArpKey, ArpEntry *> ArpCache;
    typedef boost::function<bool(const ArpKey &, ArpEntry *)> Callback;

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

    bool Add(ArpKey &key, ArpEntry *ent) { return arp_cache_.Add(key, ent); }
    bool Delete(ArpKey &key) { return arp_cache_.Delete(key); }
    ArpEntry *Find(ArpKey &key) { return arp_cache_.Find(key); }
    void Iterate(Callback cb) { arp_cache_.Iterate(cb); }
    std::size_t GetArpCacheSize() { return arp_cache_.Size(); }
    const ArpCache& GetArpCache() { return arp_cache_; }

    Interface *IPFabricIntf() { return ip_fabric_intf_; }
    void IPFabricIntf(Interface *itf) { ip_fabric_intf_ = itf; }
    uint16_t IPFabricIntfIndex() { return ip_fabric_intf_index_; }
    void IPFabricIntfIndex(uint16_t ind) { ip_fabric_intf_index_ = ind; }
    unsigned char *IPFabricIntfMac() { return ip_fabric_intf_mac_; }
    void IPFabricIntfMac(char *mac) { 
        memcpy(ip_fabric_intf_mac_, mac, ETH_ALEN);
    }

    ArpNHClient *GetArpNHClient() { return arp_nh_client_; }
    boost::asio::io_service &GetIoService() { return Proto::io_; }

    ArpEntry *GraciousArpEntry() { return gracious_arp_entry_; }
    void GraciousArpEntry(ArpEntry *entry) { gracious_arp_entry_ = entry; }
    void DelGraciousArpEntry();

    void StatsPktsDropped() { arp_stats_.pkts_dropped++; }
    void StatsArpReq() { arp_stats_.arp_req++; }
    void StatsArpReplies() { arp_stats_.arp_replies++; }
    void StatsGracious() { arp_stats_.arp_gracious++; }
    void StatsResolved() { arp_stats_.resolved++; }
    void StatsMaxRetries() { arp_stats_.max_retries_exceeded++; }
    void StatsErrors() { arp_stats_.errors++; }
    ArpStats GetStats() { return arp_stats_; }
    void ClearStats() { arp_stats_.Reset(); }

    uint16_t MaxRetries() { return max_retries_; }
    void MaxRetries(uint16_t retries) { max_retries_ = retries; }
    uint32_t RetryTimeout() { return retry_timeout_; }
    void RetryTimeout(uint32_t timeout) { retry_timeout_ = timeout; }
    uint32_t AgingTimeout() { return aging_timeout_; }
    void AgingTimeout(uint32_t timeout) { aging_timeout_ = timeout; }

private:
    void VrfUpdate(DBTablePartBase *part, DBEntryBase *entry);
    void ItfUpdate(DBEntryBase *entry);
    void RouteUpdate(DBTablePartBase *part, DBEntryBase *entry);

    ArpCache arp_cache_;
    ArpStats arp_stats_;
    ArpNHClient *arp_nh_client_;
    bool run_with_vrouter_;
    uint16_t ip_fabric_intf_index_;
    unsigned char ip_fabric_intf_mac_[ETH_ALEN];
    Interface *ip_fabric_intf_;
    ArpEntry *gracious_arp_entry_;
    DBTableBase::ListenerId vid_;
    DBTableBase::ListenerId iid_;
    DBTableBase::ListenerId fabric_route_table_listener_;

    uint16_t max_retries_;
    uint32_t retry_timeout_;   // milli seconds
    uint32_t aging_timeout_;   // milli seconds

    DISALLOW_COPY_AND_ASSIGN(ArpProto);
};

class ArpNHClient {
public:
    ArpNHClient(boost::asio::io_service &io);
    virtual ~ArpNHClient();
    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void HandleArpNHmodify(ArpNH *arp_nh);
    void UpdateArp(Ip4Address &ip, struct ether_addr &mac, const string &vrf,
                   const Interface &intf, DBRequest::DBOperation op, 
                   bool resolved);

private:
    DBTableBase::ListenerId listener_id_;
    boost::asio::io_service &io_;
    DISALLOW_COPY_AND_ASSIGN(ArpNHClient);
};

struct ArpVrfState : public DBState {
    bool seen_;
};

struct ArpRouteState : public DBState {
    bool seen_;
};

#endif // vnsw_agent_arp_proto_hpp
