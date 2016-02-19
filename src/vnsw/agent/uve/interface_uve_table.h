/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_uve_table_h
#define vnsw_agent_interface_uve_table_h

#include <string>
#include <vector>
#include <set>
#include <map>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <interface_types.h>
#include <uve/l4_port_bitmap.h>
#include <oper/vm.h>
#include <oper/peer.h>
#include <cmn/index_vector.h>
#include <oper/interface_common.h>

//The container class for objects representing VMInterface UVEs
//Defines routines for storing and managing (add, delete, change and send)
//VMInterface UVEs
class InterfaceUveTable {
public:
    struct UveInterfaceState :public DBState {
        UveInterfaceState(const VmInterface *intf)
            : cfg_name_(intf->cfg_name()),
            fip_list_(intf->floating_ip_list().list_) {}
        std::string cfg_name_;
        VmInterface::FloatingIpSet fip_list_;
    };

    struct FloatingIp;

    struct FipInfo {
        uint64_t bytes_;
        uint64_t packets_;
        uint32_t fip_;
        VmInterfaceKey fip_vmi_;
        bool is_local_flow_;
        bool is_ingress_flow_;
        bool is_reverse_flow_;
        std::string vn_;
        FloatingIp *rev_fip_;
        FipInfo() : bytes_(0), packets_(0), fip_(0),
            fip_vmi_(AgentKey::ADD_DEL_CHANGE, nil_uuid(), ""),
            is_local_flow_(false), is_ingress_flow_(false),
            is_reverse_flow_(false), rev_fip_(NULL) {
        }
    };
    struct FloatingIp {
        FloatingIp(const IpAddress &ip, const std::string &vn)
            : family_(ip.is_v4() ? Address::INET : Address::INET6),
              fip_(ip), vn_(vn) {
            in_bytes_ = 0;
            in_packets_ = 0;
            out_bytes_ = 0;
            out_packets_ = 0;
        }
        FloatingIp(const IpAddress &ip, const std::string &vn, uint64_t in_b,
                   uint64_t in_p, uint64_t out_b, uint64_t out_p)
            : family_(ip.is_v4() ? Address::INET : Address::INET6),
              fip_(ip), vn_(vn), in_bytes_(in_b), in_packets_(in_p),
              out_bytes_(out_b), out_packets_(out_p) {
        }
        void UpdateFloatingIpStats(const FipInfo &fip_info);

        Address::Family family_;
        IpAddress fip_;
        std::string vn_;
        uint64_t in_bytes_;
        uint64_t in_packets_;
        uint64_t out_bytes_;
        uint64_t out_packets_;
    };
    typedef boost::shared_ptr<FloatingIp> FloatingIpPtr;

    class FloatingIpCmp {
        public:
            bool operator()(const FloatingIpPtr &lhs,
                            const FloatingIpPtr &rhs) const {
                if (lhs.get()->fip_ != rhs.get()->fip_) {
                    return lhs.get()->fip_ < rhs.get()->fip_;
                }
                return (lhs.get()->vn_ < rhs.get()->vn_);
            }
    };
    typedef std::set<FloatingIpPtr, FloatingIpCmp> FloatingIpSet;

    struct AceStats {
        const std::string ace_uuid;
        mutable uint64_t count;
        mutable uint64_t prev_count;
        AceStats(const std::string &ace) : ace_uuid(ace), count(0),
            prev_count(0) {
        }
        bool operator<(const AceStats &rhs) const {
            return ace_uuid < rhs.ace_uuid;
        }
    };
    typedef std::set<AceStats> AceStatsSet;
    struct UveInterfaceEntry {
        const VmInterface *intf_;
        boost::uuids::uuid uuid_;
        L4PortBitmap port_bitmap_;
        FloatingIpSet fip_tree_;
        FloatingIpSet prev_fip_tree_;
        bool changed_;
        bool deleted_;
        bool renewed_;
        bool ace_stats_changed_;
        UveVMInterfaceAgent uve_info_;
        AceStatsSet ace_set_;
        /* For exclusion between Agent::StatsCollector and Agent::Uve tasks */
        tbb::mutex mutex_;

        UveInterfaceEntry(const VmInterface *i) : intf_(i),
            uuid_(i->GetUuid()), port_bitmap_(),
            fip_tree_(), prev_fip_tree_(), changed_(true), deleted_(false),
            renewed_(false), uve_info_() { }
        virtual ~UveInterfaceEntry() {}
        void UpdateFloatingIpStats(const FipInfo &fip_info);
        bool FillFloatingIpStats(vector<VmFloatingIPStats> &result,
                                 vector<VmFloatingIPStats> &diff_list,
                                 bool &diff_list_send);
        void SetStats(VmFloatingIPStats &fip, uint64_t in_bytes,
            uint64_t in_pkts, uint64_t out_bytes, uint64_t out_pkts) const;
        void SetDiffStats(VmFloatingIPStats &fip, uint64_t in_bytes,
            uint64_t in_pkts, uint64_t out_bytes, uint64_t out_pkts,
            bool &diff_list_send) const;
        void RemoveFloatingIp(const VmInterface::FloatingIp &fip);
        void AddFloatingIp(const VmInterface::FloatingIp &fip);
        InterfaceUveTable::FloatingIp *FipEntry(uint32_t ip,
                                                const std::string &vn);
        bool FrameInterfaceMsg(const std::string &name,
                               UveVMInterfaceAgent *s_intf) const;
        bool FrameInterfaceAceStatsMsg(const std::string &name,
                                       UveVMInterfaceAgent *s_intf);
        bool GetVmInterfaceGateway(const VmInterface *vm_intf,
                                   std::string &gw) const;
        bool FipAggStatsChanged(const vector<VmFloatingIPStats>  &list) const;
        bool PortBitmapChanged(const PortBucketBitmap &bmap) const;
        bool InBandChanged(uint64_t in_band) const;
        bool OutBandChanged(uint64_t out_band) const;
        void SetVnVmInfo(UveVMInterfaceAgent *uve) const;
        void UpdateInterfaceAceStats(const std::string &ace_uuid);
        void Reset();
    };
    typedef boost::shared_ptr<UveInterfaceEntry> UveInterfaceEntryPtr;

    typedef std::map<std::string, UveInterfaceEntryPtr> InterfaceMap;
    typedef std::pair<std::string, UveInterfaceEntryPtr> InterfacePair;

    InterfaceUveTable(Agent *agent, uint32_t default_intvl);
    virtual ~InterfaceUveTable();
    void RegisterDBClients();
    void Shutdown(void);
    virtual void DispatchInterfaceMsg(const UveVMInterfaceAgent &uve);
    bool TimerExpiry();
    virtual void SendInterfaceAceStats(const string &name,
                                       UveInterfaceEntry *entry) {
    }

protected:
    void SendInterfaceDeleteMsg(const std::string &config_name);

    Agent *agent_;
    InterfaceMap interface_tree_;
private:
    virtual UveInterfaceEntryPtr Allocate(const VmInterface *vm);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceAddHandler(const VmInterface* intf,
                             const VmInterface::FloatingIpSet &old_list);
    void InterfaceDeleteHandler(const std::string &name);
    void set_expiry_time(int time);
    void SendInterfaceMsg(const std::string &name, UveInterfaceEntry *entry);

    DBTableBase::ListenerId intf_listener_id_;
    // Last visited Interface by timer
    std::string timer_last_visited_;
    Timer *timer_;
    int expiry_time_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceUveTable);
};

#endif // vnsw_agent_interface_uve_table_h
