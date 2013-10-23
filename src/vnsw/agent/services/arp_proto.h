/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_arp_proto_hpp
#define vnsw_agent_arp_proto_hpp

#include <map>
#include <set>
#include <vector>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <tbb/mutex.h>

#include "pkt/proto.h"
#include "oper/nexthop.h"
#include "oper/agent_route.h"
#include <oper/interface.h>
#include <oper/vrf.h>
#include "ksync/ksync_index.h"
#include "ksync/interface_ksync.h"
#include "services/services_types.h"
#include "base/timer.h"

#define GRATUITOUS_ARP 0x0100 // keep this different from standard ARP commands

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

struct ArpKey {
    in_addr_t ip;
    const VrfEntry  *vrf;

    ArpKey(in_addr_t addr, const VrfEntry *ventry) : ip(addr), vrf(ventry) {};
    ArpKey(const ArpKey &key) : ip(key.ip), vrf(key.vrf) {};
    bool operator <(const ArpKey &rhs) const { return (ip < rhs.ip); }
};

class ArpHandler : public ProtoHandler {
public:
    enum ArpMsgType {
        VRF_DELETE,
        ITF_DELETE,
        ARP_RESOLVE,
        ARP_DELETE,
        ARP_SEND_GRACIOUS,
        RETRY_TIMER_EXPIRED,
        AGING_TIMER_EXPIRED,
        GRACIOUS_TIMER_EXPIRED,
    };

    struct ArpIpc : IpcMsg {
        ArpKey key;
        ArpIpc(ArpMsgType msg, ArpKey &akey) : IpcMsg(msg), key(akey) {};
        ArpIpc(ArpMsgType msg, in_addr_t ip, const VrfEntry *vrf) : 
            IpcMsg(msg), key(ip, vrf) {};
    };

    ArpHandler(PktInfo *info, boost::asio::io_service &io) : 
               ProtoHandler(info, io), arp_(NULL), arp_tpa_(0) { }
    ArpHandler(boost::asio::io_service &io) : ProtoHandler(io), 
                                              arp_(NULL), arp_tpa_(0) { }
    virtual ~ArpHandler() {}
    bool Run();

    static void SendArpIpc(ArpHandler::ArpMsgType type,
                           in_addr_t ip, const VrfEntry *vrf);
    static void SendArpIpc(ArpHandler::ArpMsgType type, ArpKey &key);

    void SendArp(uint16_t op, const unsigned char *smac, in_addr_t sip, 
                 unsigned const char *tmac, in_addr_t tip, 
                 uint16_t itf, uint16_t vrf);
    bool EntryDelete(ArpEntry *&entry);
    void EntryDeleteWithKey(ArpKey &key);

private:
    bool HandlePacket();
    bool HandleMessage();
    bool OnVrfDelete(ArpEntry *&entry, const VrfEntry *vrf);
    uint16_t ArpHdr(const unsigned char *, in_addr_t, const unsigned char *, 
                    in_addr_t, uint16_t);
    void SendGwArpReply();

    ether_arp *arp_;
    in_addr_t arp_tpa_;
    std::vector<ArpEntry *> arp_del_list_;

    DISALLOW_COPY_AND_ASSIGN(ArpHandler);
};

class ArpProto : public Proto<ArpHandler> {
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

    static void Init(boost::asio::io_service &io, bool run_with_vrouter);
    static void Shutdown();
    virtual ~ArpProto();

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
        memcpy(ip_fabric_intf_mac_, mac, MAC_ALEN);
    }

    ArpNHClient *GetArpNHClient() { return arp_nh_client_; }
    boost::asio::io_service &GetIoService() { return Proto<ArpHandler>::io_; }

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
    ArpProto(boost::asio::io_service &io, bool run_with_vrouter);
    void VrfUpdate(DBTablePartBase *part, DBEntryBase *entry);
    void ItfUpdate(DBEntryBase *entry);
    void RouteUpdate(DBTablePartBase *part, DBEntryBase *entry);

    ArpCache arp_cache_;
    ArpStats arp_stats_;
    ArpNHClient *arp_nh_client_;
    bool run_with_vrouter_;
    uint16_t ip_fabric_intf_index_;
    unsigned char ip_fabric_intf_mac_[MAC_ALEN];
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

class ArpEntry {
public:
    enum State {
        INITING = 0x01,
        RESOLVING = 0x02,
        ACTIVE = 0x04,
        RERESOLVING  = 0x08,
    };

    ArpEntry(boost::asio::io_service &io, ArpHandler *handler, in_addr_t ip, 
             const VrfEntry *vrf,
             State state = ArpEntry::INITING) 
        : key_(ip, vrf), state_(state), retry_count_(0), handler_(handler),
        arp_timer_(NULL) {
        memset(mac_, 0, MAC_ALEN);
        arp_timer_ = TimerManager::CreateTimer(io, "Arp Entry timer");
    }
    virtual ~ArpEntry() {
        arp_timer_->Cancel();
        delete handler_;
        TimerManager::DeleteTimer(arp_timer_);
    }
    bool IsResolved() {
        return (state_ & (ArpEntry::ACTIVE | ArpEntry::RERESOLVING));
    }
    ArpKey &Key() { return key_; }
    unsigned char * Mac() { return mac_; }
    State GetState() { return state_; }

    void Delete() { UpdateNhDBEntry(DBRequest::DB_ENTRY_DELETE); }
    void HandleArpReply(uint8_t *mac);
    bool HandleArpRequest();
    void RetryExpiry();
    void AgingExpiry();
    void SendGraciousArp();

private:
    void StartTimer(uint32_t timeout, ArpHandler::ArpMsgType mtype);
    void SendArpRequest();
    void UpdateNhDBEntry(DBRequest::DBOperation op, bool resolved = false);

    ArpKey key_;
    unsigned char mac_[MAC_ALEN];
    State state_;
    int retry_count_;
    ArpHandler *handler_;
    Timer *arp_timer_;
    DISALLOW_COPY_AND_ASSIGN(ArpEntry);
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
