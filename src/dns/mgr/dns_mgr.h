/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __dns_manager_h__
#define __dns_manager_h__

#include <tbb/mutex.h>
#include <base/index_allocator.h>
#include <mgr/dns_oper.h>
#include <bind/named_config.h>
#include <cfg/dns_config.h>
#include <ifmap/client/ifmap_manager.h>

class DB;
class DBGraph;
struct VirtualDnsConfig;
struct VirtualDnsRecordConfig;

class DnsManager {
public:
    static const int max_records_per_sandesh = 200;
    static const int kEndOfConfigCheckTime = 3000; // msec
    static const uint16_t kMaxRetransmitCount = 6;
    static const uint16_t kPendingRecordReScheduleTime = 1000; //msec
    static const uint16_t kNamedLoWaterMark = 8192; //pow(2,13);
    static const uint16_t kNamedHiWaterMark = 32768;  //pow(2,15);
    static const uint16_t kMaxIndexAllocator = 65535;

    struct PendingList {
        uint16_t xid;
        std::string view;
        std::string zone;
        DnsItems items;
        BindUtil::Operation op;
        uint32_t retransmit_count;

        PendingList(uint16_t id, const std::string &v, const std::string &z,
                    const DnsItems &it, BindUtil::Operation o, 
                    uint32_t recount = 0) {
            xid = id;
            view = v;
            zone = z;
            items = it;
            op = o;
            retransmit_count = recount;
        }
    };
    typedef std::map<uint16_t, PendingList> PendingListMap;
    typedef std::pair<uint16_t, PendingList> PendingListPair;

    typedef std::map<uint16_t, PendingList> DeportedPendingListMap;
    typedef std::pair<uint16_t, PendingList> DeportedPendingListPair;

    DnsManager();
    virtual ~DnsManager();
    void Initialize(DB *config_db, DBGraph *config_graph,
                    const std::string& named_config_dir,
                    const std::string& named_config_file,
                    const std::string& named_log_file,
                    const std::string& rndc_config_file,
                    const std::string& rndc_secret,
                    const std::string& named_max_cache_size,
                    const uint16_t named_max_retransmissions,
                    const uint16_t named_retransmission_interval);
    void Shutdown();
    void DnsView(const DnsConfig *config, DnsConfig::DnsConfigEvent ev);
    void DnsPtrZone(const Subnet &subnet, const VirtualDnsConfig *vdns,
                    DnsConfig::DnsConfigEvent ev);
    void DnsRecord(const DnsConfig *config, DnsConfig::DnsConfigEvent ev);
    void HandleUpdateResponse(uint8_t *pkt, std::size_t length);
    DnsConfigManager &GetConfigManager() { return config_mgr_; }
    bool SendUpdate(BindUtil::Operation op, const std::string &view,
                    const std::string &zone, DnsItems &items);
    void SendRetransmit(uint16_t xid, BindUtil::Operation op,
                        const std::string &view, const std::string &zone,
                        DnsItems &items, uint32_t retranmit_count);
    void UpdateAll();
    void BindEventHandler(BindStatus::Event ev);

    template <typename ConfigType>
    void ProcessConfig(IFMapNodeProxy *proxy, const std::string &name,
                       DnsConfigManager::EventType event);
    void ProcessAgentUpdate(BindUtil::Operation event, const std::string &name,
                            const std::string &vdns_name, const DnsItem &item);
    bool IsBindStatusUp() { return bind_status_.IsUp(); }

    void set_ifmap_manager(IFMapManager *ifmap_mgr) { ifmap_mgr_ = ifmap_mgr; }
    IFMapManager* get_ifmap_manager() { return ifmap_mgr_; }
    bool IsEndOfConfig() {
        if (ifmap_mgr_) return (ifmap_mgr_->GetEndOfRibComputed());
        return (true);
    }
    PendingListMap GetDeportedPendingListMap() { return dp_pending_map_; }
    void NotifyThrottledDnsRecords();

private:
    friend class DnsBindTest;
    friend class DnsManagerTest;

    bool SendRecordUpdate(BindUtil::Operation op, 
                          const VirtualDnsRecordConfig *config);
    bool PendingDone(uint16_t xid);
    bool ResendRecordsinBatch();
    bool AddPendingList(uint16_t xid, const std::string &view,
                                    const std::string &zone, const DnsItems &items,
                                    BindUtil::Operation op);
    void UpdatePendingList(const std::string &view,
                                       const std::string &zone,
                                       const DnsItems &items);
    void DeletePendingList(uint16_t xid);
    void ClearPendingList();
    void PendingListViewDelete(const VirtualDnsConfig *config);
    bool CheckZoneDelete(ZoneList &zones, PendingList &pend);
    void PendingListZoneDelete(const Subnet &subnet,
                               const VirtualDnsConfig *config);
    /* Pending Record List transmitted to named */
    void StartPendingTimer(int);
    void CancelPendingTimer();
    bool PendingTimerExpiry();

    void StartEndofConfigTimer();
    void CancelEndofConfigTimer();
    bool EndofConfigTimerExpiry();

    void NotifyAllDnsRecords(const VirtualDnsConfig *config,
                             DnsConfig::DnsConfigEvent ev);
    void NotifyReverseDnsRecords(const VirtualDnsConfig *config,
                                 DnsConfig::DnsConfigEvent ev, bool notify);
    inline uint16_t GetTransId();
    void ResetTransId(uint16_t);
    inline bool CheckName(std::string rec_name, std::string name);

    tbb::mutex mutex_;
    BindStatus bind_status_;
    DnsConfigManager config_mgr_;    
    IFMapManager *ifmap_mgr_;
    static uint16_t g_trans_id_;
    PendingListMap pending_map_;
    DeportedPendingListMap dp_pending_map_;
    Timer *pending_timer_;
    Timer *end_of_config_check_timer_;
    bool end_of_config_;
    uint32_t record_send_count_;
    uint16_t named_max_retransmissions_;
    uint16_t named_retransmission_interval_;
    uint16_t named_lo_watermark_;
    uint16_t named_hi_watermark_;
    bool named_send_throttled_;
    WorkQueue<uint16_t> pending_done_queue_;
    IndexAllocator idx_;

    DISALLOW_COPY_AND_ASSIGN(DnsManager);
};

#endif // __dns_manager_h__
