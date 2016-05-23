/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __dns_manager_h__
#define __dns_manager_h__

#include <tbb/mutex.h>
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
    static const uint32_t kPendingRecordRetransmitTime = 3000; // milliseconds
    static const uint32_t kMaxRetransmitCount = 32;
    static const int kJitter = 60; //percentage

    struct PendingList {
        uint16_t xid;
        std::string view;
        std::string zone;
        DnsItems items;
        BindUtil::Operation op;
        uint32_t retransmit_count;
        Timer *retry_pending_timer;

        PendingList(uint16_t id, const std::string &v, const std::string &z,
                    const DnsItems &it, BindUtil::Operation o) {
            xid = id;
            view = v;
            zone = z;
            items = it;
            op = o;
            retransmit_count = 0;
            retry_pending_timer = TimerManager::CreateTimer
                (*(Dns::GetEventManager()->io_service()), "RetryPendingTimer",
                 TaskScheduler::GetInstance()->GetTaskId("dns::Config"), 0);

            // Timer will be in range of (4-6)secs
            int ms = kPendingRecordRetransmitTime;
            ms = (ms * (100 - kJitter)) / 100;
            ms += (ms * (rand() % (kJitter * 2))) / 100;
            retry_pending_timer->Start(ms,
                boost::bind(&PendingList::SendPendingListEntry, this,
                xid, op, view, zone, items));
        }

        bool SendPendingListEntry(uint16_t xid, BindUtil::Operation op,
                                  std::string &view, std::string &zone,
                                  DnsItems &items) {
            return (Dns::GetDnsManager()->SendRetransmit(xid, op, view,
                    zone, items));
        }
    };
    typedef std::map<uint16_t, PendingList> PendingListMap;
    typedef std::pair<uint16_t, PendingList> PendingListPair;

    DnsManager();
    virtual ~DnsManager();
    void Initialize(DB *config_db, DBGraph *config_graph,
                    const std::string& named_config_dir,
                    const std::string& named_config_file,
                    const std::string& named_log_file,
                    const std::string& rndc_config_file,
                    const std::string& rndc_secret,
                    const std::string& named_max_cache_size);
    void Shutdown();
    void DnsView(const DnsConfig *config, DnsConfig::DnsConfigEvent ev);
    void DnsPtrZone(const Subnet &subnet, const VirtualDnsConfig *vdns,
                    DnsConfig::DnsConfigEvent ev);
    void DnsRecord(const DnsConfig *config, DnsConfig::DnsConfigEvent ev);
    void HandleUpdateResponse(uint8_t *pkt, std::size_t length);
    DnsConfigManager &GetConfigManager() { return config_mgr_; }
    void SendUpdate(BindUtil::Operation op, const std::string &view,
                    const std::string &zone, DnsItems &items);
    bool SendRetransmit(uint16_t xid, BindUtil::Operation op,
                        const std::string &view, const std::string &zone,
                        DnsItems &items);
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
    PendingListMap GetPendingListMap() { return pending_map_; }

private:
    friend class DnsBindTest;

    bool SendRecordUpdate(BindUtil::Operation op, 
                          const VirtualDnsRecordConfig *config);
    bool PendingDone(uint16_t xid);
    void ResendRecord(uint16_t xid);
    void AddPendingList(uint16_t xid, const std::string &view,
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
    void StartEndofConfigTimer();
    void CancelEndofConfigTimer();
    bool EndofConfigTimerExpiry();

    void NotifyAllDnsRecords(const VirtualDnsConfig *config,
                             DnsConfig::DnsConfigEvent ev);
    void NotifyReverseDnsRecords(const VirtualDnsConfig *config,
                                 DnsConfig::DnsConfigEvent ev, bool notify);
    inline uint16_t GetTransId();
    inline bool CheckName(std::string rec_name, std::string name);

    tbb::mutex mutex_;
    BindStatus bind_status_;
    DnsConfigManager config_mgr_;    
    IFMapManager *ifmap_mgr_;
    static uint16_t g_trans_id_;
    PendingListMap pending_map_;
    Timer *end_of_config_check_timer_;
    bool end_of_config_;
    WorkQueue<uint16_t> pending_done_queue_;

    DISALLOW_COPY_AND_ASSIGN(DnsManager);
};

#endif // __dns_manager_h__
