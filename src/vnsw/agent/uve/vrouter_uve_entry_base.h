/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_uve_entry_base_h
#define vnsw_agent_vrouter_uve_entry_base_h

#include <string>
#include <vector>
#include <set>
#include <sys/types.h>
#include <net/ethernet.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <vrouter_types.h>
#include <uve/l4_port_bitmap.h>
#include <pkt/flow_proto.h>
#include <cmn/agent_cmn.h>
#include <vgw/cfg_vgw.h>

//The class that defines data-structures to store VRouter information
//required for sending VRouter UVE.
class VrouterUveEntryBase {
public:
    struct VrouterUveInterfaceState : public DBState {
        VrouterUveInterfaceState(bool active) :
            vmport_active_(active) {}
        ~VrouterUveInterfaceState() {}
        bool vmport_active_;
    };
    struct VrouterPhysicalDeviceState : public DBState {
        VrouterPhysicalDeviceState() : master_(false) {}
        bool master_;
    };

    // The below const values are dependent on value of
    // VrouterStatsCollector::VrouterStatsInterval
    static const uint8_t bandwidth_mod_1min = 2;
    static const uint8_t bandwidth_mod_5min = 10;
    typedef std::set<const Interface *> PhysicalInterfaceSet;
    typedef boost::shared_ptr<std::vector<std::string> > StringVectorPtr;

    VrouterUveEntryBase(Agent *agent);
    virtual ~VrouterUveEntryBase();

    void RegisterDBClients();
    void Shutdown();

    bool AppendVm(DBTablePartBase *part, DBEntryBase *e, StringVectorPtr l);
    void VmWalkDone(DBTableBase *base, StringVectorPtr list);
    bool AppendVn(DBTablePartBase *part, DBEntryBase *e, StringVectorPtr l);
    void VnWalkDone(DBTableBase *base, StringVectorPtr list);
    bool AppendInterface(DBTablePartBase *part, DBEntryBase *entry,
                         StringVectorPtr intf_list, StringVectorPtr err_list,
                         StringVectorPtr nova_if_list,
                         StringVectorPtr unmanaged_list);
    void InterfaceWalkDone(DBTableBase *base, StringVectorPtr if_l,
                           StringVectorPtr err_if_l,
                           StringVectorPtr nova_if_l,
                           StringVectorPtr unmanaged_list);
    virtual bool SendVrouterMsg();
    void SendVrouterProuterAssociation(const std::vector<std::string> &list);
    bool TimerExpiry();
protected:
    Agent *agent_;
    PhysicalInterfaceSet phy_intf_set_;
    VrouterStatsAgent prev_stats_;
    VrouterAgent prev_vrouter_;
    uint8_t cpu_stats_count_;
    bool do_vn_walk_;
    bool do_vm_walk_;
    bool do_interface_walk_;
    DBTableWalker::WalkId vn_walk_id_;
    DBTableWalker::WalkId vm_walk_id_;
    DBTableWalker::WalkId interface_walk_id_;

    //The following Dispatch functions are not made const function because
    //in derived class they need to be non-const
    virtual void DispatchVrouterStatsMsg(const VrouterStatsAgent &uve);
    virtual void DispatchComputeCpuStateMsg(const ComputeCpuState &ccs);
private:
    //The following Dispatch functions are not made const function because
    //in derived class they need to be non-const
    virtual void DispatchVrouterMsg(const VrouterAgent &uve);
    void BuildAndSendComputeCpuStateMsg(const CpuLoadInfo &info);
    void PhysicalDeviceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void VmNotify(DBTablePartBase *partition, DBEntryBase *e);
    void VnNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool StartVnWalk();
    bool StartVmWalk();
    bool StartInterfaceWalk();
    void StartTimer();
    bool Run();
    std::string GetMacAddress(const MacAddress &mac) const;
    void SubnetToStringList(VirtualGatewayConfig::SubnetList &l1,
                            std::vector<std::string> &l2);
    void BuildAgentConfig(VrouterAgent &vrouter_agent);
    void AppendInterfaceInternal(const VmInterface *port,
        StringVectorPtr intf_list, StringVectorPtr err_if_list);
    PhysicalDevice *VmiToPhysicalDevice(const VmInterface *port);
    PhysicalDevice *InterfaceToPhysicalDevice(Interface *intf);

    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId vm_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    DBTableBase::ListenerId physical_device_listener_id_;
    Timer *timer_;
    DISALLOW_COPY_AND_ASSIGN(VrouterUveEntryBase);
};

#endif // vnsw_agent_vrouter_uve_entry_base_h
