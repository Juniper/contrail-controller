/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_client_h
#define vnsw_agent_uve_client_h

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_machine_types.h>
#include <virtual_network_types.h>
#include <vrouter_types.h>
#include <pkt/pkt_flow.h>
#include <uve/flow_uve.h>
#include <oper/interface_common.h>
#include <uve/agent_stats.h>

class UveClientTest;
class VmStat;
class VmStatData;

class UveState :public DBState {
    bool seen_;
public:
    bool vmport_active_;
    std::string vm_name_;
    std::string vn_name_;
};

class VmUveState: public UveState {
public:
    VmUveState() : stat_(NULL) { };
    VmStat *stat_;
};

struct UveVnVmEntry {
    UveVnVmEntry(const std::string &vn_name, const std::string vm_name) :
        vn_name_(vn_name), vm_name_(vm_name) { };

    std::string vn_name_;
    std::string vm_name_;
};

struct UveVnVmEntryCmp {
    bool operator()(const UveVnVmEntry &lhs, const UveVnVmEntry &rhs) {
        if (lhs.vn_name_.compare(rhs.vn_name_) != 0) {
            return lhs.vn_name_ < rhs.vn_name_;
        }

        return lhs.vm_name_ < rhs.vm_name_;
    }
};

struct UveVmEntry {
    L4PortBitmap port_bitmap;
    UveVirtualMachineAgent  uve_info;

    UveVmEntry() : port_bitmap(), uve_info() { }
    ~UveVmEntry() {}
    UveVmEntry(const UveVmEntry &rhs) {
        port_bitmap = rhs.port_bitmap;
        uve_info = rhs.uve_info;
    }
};

struct UveVnEntry {
    L4PortBitmap port_bitmap;
    UveVirtualNetworkAgent  uve_info;
    uint64_t prev_stats_update_time;
    uint64_t prev_in_bytes;
    uint64_t prev_out_bytes;

    UveVnEntry() : port_bitmap(), uve_info(), prev_stats_update_time(0),
                   prev_in_bytes(0), prev_out_bytes(0) { }
    ~UveVnEntry() {}
    UveVnEntry(const UveVnEntry &rhs) {
        port_bitmap = rhs.port_bitmap;
        uve_info = rhs.uve_info;
        prev_stats_update_time = rhs.prev_stats_update_time;
        prev_in_bytes = rhs.prev_in_bytes;
        prev_out_bytes = rhs.prev_out_bytes;
    }
};

struct UveIntfEntry {
    L4PortBitmap port_bitmap;
    const Interface *intf;

    UveIntfEntry(const Interface *i) : port_bitmap(), intf(i) { }
    ~UveIntfEntry() {}
    UveIntfEntry(const UveIntfEntry &rhs) {
        port_bitmap = rhs.port_bitmap;
        intf = rhs.intf;
    }
};

class UveClient {
public:
    // The below const values are dependent on value of
    // VrouterStatsCollector::VrouterStatsInterval
    static const uint8_t bandwidth_mod_1min = 2;
    static const uint8_t bandwidth_mod_5min = 10;
    static const uint8_t bandwidth_mod_10min = 20;
    UveClient(uint64_t b_intvl) : 
        vn_vmlist_updates_(0), vn_vm_set_(), vn_intf_map_(),
        vm_intf_map_(), phy_intf_set_(), vn_listener_id_(DBTableBase::kInvalidId),
        vm_listener_id_(DBTableBase::kInvalidId),
        intf_listener_id_(DBTableBase::kInvalidId), prev_stats_(),
        prev_vrouter_(), last_vm_uve_set_(), last_vn_uve_set_(),
        port_bitmap_(), bandwidth_intvl_(b_intvl), 
        signal_(*(Agent::GetInstance()->GetEventManager()->io_service())) {
            start_time_ = UTCTimestampUsec();
            AddLastVnUve(*FlowHandler::UnknownVn());
            AddLastVnUve(*FlowHandler::LinkLocalVn());
            event_queue_ = new WorkQueue<VmStatData *>
                (TaskScheduler::GetInstance()->GetTaskId("Agent::Uve"), 0,
                 boost::bind(&UveClient::Process, this, _1));
        };

    virtual ~UveClient();

    typedef std::multimap<const VnEntry *, const Interface *> VnIntfMap;
    typedef std::multimap<const VmEntry *, UveIntfEntry> VmIntfMap;
    typedef std::pair<const VmEntry *, UveIntfEntry> VmIntfPair;
    typedef std::set<UveVnVmEntry, UveVnVmEntryCmp> VnVmSet;
    typedef std::map<std::string, UveVmEntry> LastVmUveSet;
    typedef std::pair<std::string, UveVmEntry> LastVmUvePair;
    typedef std::map<std::string, UveVnEntry> LastVnUveSet;
    typedef std::pair<std::string, UveVnEntry> LastVnUvePair;
    typedef std::set<const Interface *> PhyIntfSet;

    
    bool GetUveVnEntry(const string vn_name, UveVnEntry &entry);
    void AddIntfToVm(const VmEntry *vm, const Interface *intf);
    void DelIntfFromVm(const Interface *intf);
    void AddIntfToVn(const VnEntry *vn, const Interface *intf);
    void DelIntfFromVn(const Interface *intf);
    bool FrameIntfMsg(const VmInterface *intf, VmInterfaceAgent *s_intf);
    void IntfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool FrameVmMsg(const VmEntry *vm, L4PortBitmap *vm_port_bitmap, UveVmEntry *uve);
    void SendVmMsg(const VmEntry *vm, bool stats);
    uint32_t VmIntfMapSize() { return vm_intf_map_.size();};
    void DeleteAllIntf(const VmEntry *vm);
    void SendVmStats(void);
    void VmNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool FrameVnMsg(const VnEntry *vn, L4PortBitmap *vn_port_bitmap, UveVnEntry *uve);
    void SendVnMsg(const VnEntry *vn, bool stats);
    void SendUnresolvedVnMsg(std::string vn);
    bool PopulateInterVnStats(std::string vn_name, UveVirtualNetworkAgent *s_vn);
    void DeleteAllIntf(const VnEntry *vn);
    void SendVnStats(void);
    void VnNotify(DBTablePartBase *partition, DBEntryBase *e);
    uint32_t VnIntfMapSize() { return vn_intf_map_.size();};
    void AddVmToVn(const VmInterface *intf, const std::string vm_name, const std::string vn_name);
    void DelVmFromVn(const VmInterface *intf, const std::string vm_name, const std::string vn_name);
    void VnVmListSend(const std::string &vn);
    uint32_t VnVmListSize(){return vn_vm_set_.size();};
    uint32_t VnVmListUpdateCount(){return vn_vmlist_updates_;};
    void VnVmListUpdateCountReset(){vn_vmlist_updates_ = 0;};
    void AddIntfToIfStatsTree(const Interface *intf);
    void DelIntfFromIfStatsTree(const Interface *intf);

    void VrouterObjectIntfNotify(const Interface *intf);
    void IntfWalkDone(DBTableBase *base, std::vector<std::string> *intf_list,
                      std::vector<std::string> *err_if_list,
                      std::vector<std::string> *nova_if_list);
    bool AppendIntf(DBTablePartBase *part, DBEntryBase *entry,
                    std::vector<std::string> *intf_list,
                    std::vector<std::string> *err_if_list, 
                    std::vector<std::string> *nova_if_list);

    void VrouterObjectVmNotify(const VmEntry *vm);
    void VmWalkDone(DBTableBase *base, std::vector<std::string> *vm_list);
    bool AppendVm(DBTablePartBase *part, DBEntryBase *entry,
            std::vector<std::string> *vm_list);

    void VrouterObjectVnNotify(const VnEntry *vn);
    void VnWalkDone(DBTableBase *base, std::vector<std::string> *vn_list);
    bool AppendVn(DBTablePartBase *part, DBEntryBase *entry,
            std::vector<std::string> *vn_list);
    bool SendAgentStats();

    // Update flow port bucket information
    void NewFlow(const FlowEntry *flow);
    void DeleteFlow(const FlowEntry *flow);

    static void Init();
    void Shutdown();
    void RegisterSigHandler();
    uint32_t GetCpuCount();
    bool Process(VmStatData *vm_stat_data);
    void EnqueueVmStatData(VmStatData *vm_stat_data);
    bool GetVmIntfGateway(const VmInterface *vm_intf, string &gw);
    static UveClient *GetInstance() {return singleton_;}
private:
    static UveClient *singleton_;
    friend class UvePortBitmapTest;
    void InitPrevStats();
    void FetchDropStats(AgentDropStats &ds);
    bool SetVRouterPortBitmap(VrouterStatsAgent &vr_stats);
    bool SetVnPortBitmap(L4PortBitmap *port_bitmap, UveVnEntry *uve);
    bool SetVmPortBitmap(L4PortBitmap *port_bitmap, UveVmEntry *uve);
    void AddLastVnUve(std::string vn_name);
    uint8_t CalculateBandwitdh(uint64_t bytes, int speed_mbps, int diff_seconds);
    uint8_t GetBandwidthUsage(AgentStatsCollector::IfStats *s, bool dir_in, int mins);
    uint64_t GetVmPortBandwidth(AgentStatsCollector::IfStats *s, bool dir_in);
    bool BuildPhyIfBand(std::vector<AgentIfBandwidth> &phy_if_list, uint8_t mins);
    std::string GetMacAddress(const ether_addr &mac);
    bool UveVnFipCountChanged(int32_t size, const UveVirtualNetworkAgent &s_vn);
    bool UpdateVnFipCount(LastVnUveSet::iterator &it, int count, 
                          UveVirtualNetworkAgent *s_vn);
    bool UveVnInFlowCountChanged(uint32_t size, 
                                 const UveVirtualNetworkAgent &s_vn);
    bool UveVnOutFlowCountChanged(uint32_t size, 
                                  const UveVirtualNetworkAgent &s_vn);
    bool UpdateVnFlowCount(const VnEntry *vn, LastVnUveSet::iterator &it, UveVirtualNetworkAgent *s_vn);
    bool BuildPhyIfList(std::vector<AgentIfStats> &phy_if_list);
    void BuildXmppStatsList(std::vector<AgentXmppStats> &list);
    bool UveVmVRouterChanged(std::string &new_value, const UveVirtualMachineAgent &s_vm);
    bool UveVmIfListChanged(std::vector<VmInterfaceAgent> &new_list, 
                            const UveVirtualMachineAgent &s_vm);
    bool UveVnAclChanged(std::string name, const UveVirtualNetworkAgent &s_vn);
    bool UveVnAclRuleCountChanged(int32_t size, const UveVirtualNetworkAgent &s_vn);
    bool UveVnMirrorAclChanged(std::string name,
                               const UveVirtualNetworkAgent &s_vn);
    bool UveVnIfInStatsChanged(uint64_t bytes, uint64_t pkts, 
                               const UveVirtualNetworkAgent &s_vn);
    bool UveVnIfOutStatsChanged(uint64_t bytes, uint64_t pkts, 
                                const UveVirtualNetworkAgent &s_vn);
    bool UveVnIfListChanged(vector<std::string> new_list, 
                            const UveVirtualNetworkAgent &s_vn);
    bool UveVnInBandChanged(uint64_t in_band, const UveVirtualNetworkAgent &prev_vn);
    bool UveVnOutBandChanged(uint64_t out_band, const UveVirtualNetworkAgent &prev_vn);
    bool UveVnVrfStatsChanged(std::vector<UveVrfStats> &vlist, 
                              const UveVirtualNetworkAgent &prev_vn);
    bool UveInterVnInStatsChanged(vector<UveInterVnStats> new_list, 
                                  const UveVirtualNetworkAgent &s_vn);
    bool UveInterVnOutStatsChanged(vector<UveInterVnStats> new_list, 
                                   const UveVirtualNetworkAgent &s_vn);
    bool UveInterVnStatsChanged(const vector<InterVnStats> &new_list, 
                                const UveVirtualNetworkAgent &s_vn) const;
    bool FrameVnStatsMsg(const VnEntry *vn, L4PortBitmap *vn_port_bitmap,
                         UveVnEntry *uve);
    void SendVmAndVnMsg(const VmInterface* vm_port);
    bool FrameVmStatsMsg(const VmEntry *vm, L4PortBitmap *vm_port_bitmap,
                         UveVmEntry *uve);
    bool UveVmIfStatsListChanged(vector<VmInterfaceAgentStats> &new_list, 
                                 const UveVirtualMachineAgent &s_vm);
    bool FrameIntfStatsMsg(const VmInterface *vm_intf,
                           VmInterfaceAgentStats *s_intf);
    void SendVrouterUve();
    void InitSigHandler();

    uint32_t vn_vmlist_updates_;
    VnVmSet vn_vm_set_;
    VnIntfMap vn_intf_map_;
    VmIntfMap vm_intf_map_;
    PhyIntfSet phy_intf_set_;
    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId vm_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    DBTableBase::ListenerId vrf_listener_id_;
    VrouterStatsAgent prev_stats_;
    VrouterAgent prev_vrouter_;
    LastVmUveSet last_vm_uve_set_;
    LastVnUveSet last_vn_uve_set_;
    L4PortBitmap port_bitmap_;
    uint64_t bandwidth_intvl_; //in microseconds
    boost::asio::signal_set signal_;
    WorkQueue<VmStatData *> *event_queue_;
    uint64_t start_time_;
    DISALLOW_COPY_AND_ASSIGN(UveClient);
};

class VmStat {
public:
    static const size_t kBufLen = 4098;
    static const uint32_t kTimeout = 60 * 1000;
    static const uint32_t kRetryCount = 3;
    typedef boost::function<void(void)> DoneCb;
    VmStat(const uuid &vm_uuid);
    ~VmStat();
    void Start();
    static void Stop(VmStat *vm_stat);
    static void ProcessData(VmStat *vm_stat, DoneCb &cb);
 
private:
    void ReadCpuStat();
    void ReadVcpuStat();
    void ReadMemStat();
    void GetCpuStat();
    void GetVcpuStat();
    void GetMemStat();
    void ReadData(const boost::system::error_code &ec, size_t read_bytes, DoneCb &cb);
    void ExecCmd(std::string cmd, DoneCb cb);
    void StartTimer();
    bool TimerExpiry();
    void GetPid();
    void ReadPid();
    void ReadMemoryQuota();
    void GetMemoryQuota();

    const uuid vm_uuid_;
    uint32_t mem_usage_;
    uint32_t virt_memory_;
    uint32_t virt_memory_peak_;
    uint32_t vm_memory_quota_;
    double   prev_cpu_stat_;
    double   cpu_usage_;
    time_t   prev_cpu_snapshot_time_;
    std::vector<double> prev_vcpu_usage_;
    std::vector<double> vcpu_usage_percent_;
    time_t   prev_vcpu_snapshot_time_;
    char     rx_buff_[kBufLen];
    std::stringstream data_;
    boost::asio::posix::stream_descriptor input_;
    Timer *timer_;
    bool marked_delete_;
    uint32_t pid_;
    UveVirtualMachineStats prev_stats_;
    uint32_t retry_;
    DISALLOW_COPY_AND_ASSIGN(VmStat);
};

class VmStatData {
public:
    VmStatData(VmStat *stat, VmStat::DoneCb &cb):
       vm_stat_(stat), cb_(cb) {
    }
    VmStat *GetVmStat() {
        return vm_stat_;
    }

    VmStat::DoneCb& GetVmStatCb() {
        return cb_;
    }

private:
    VmStat *vm_stat_;
    VmStat::DoneCb cb_;
};

#endif // vnsw_agent_uve_client_h
