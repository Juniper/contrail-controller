/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_uve_table_base_h
#define vnsw_agent_vn_uve_table_base_h

#include <string>
#include <set>
#include <map>
#include <vector>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_network_types.h>
#include <oper/vn.h>
#include <uve/vn_uve_entry_base.h>

//The container class for objects representing VirtualNetwork UVEs
//Defines routines for storing and managing (add, delete, change and send)
//VirtualNetwork UVEs
class VnUveTableBase {
public:
    struct VnUveInterfaceState :public DBState {
        VnUveInterfaceState(const std::string &vm, const std::string &vn)
            : vm_name_(vm), vn_name_(vn) {
        }
        std::string vm_name_;
        std::string vn_name_;
    };

    typedef boost::shared_ptr<VnUveEntryBase> VnUveEntryPtr;
    typedef std::map<std::string, VnUveEntryPtr> UveVnMap;
    typedef std::pair<std::string, VnUveEntryPtr> UveVnPair;
    VnUveTableBase(Agent *agent, uint32_t default_intvl);
    virtual ~VnUveTableBase();

    void RegisterDBClients();
    void Shutdown(void);
    void SendVnAclRuleCount();
    bool TimerExpiry();

protected:
    void Delete(const std::string &name);
    VnUveEntryBase* UveEntryFromVn(const VnEntry *vn);
    //The following API is made protected for UT.
    virtual void DispatchVnMsg(const UveVirtualNetworkAgent &uve);
    virtual void SendVnAceStats(VnUveEntryBase *entry, const VnEntry *vn) {
    }

    UveVnMap uve_vn_map_;
    Agent *agent_;
    /* For exclusion between kTaskFlowStatsCollector and kTaskDBExclude */
    tbb::mutex uve_vn_map_mutex_;
private:
    VnUveEntryBase* Add(const VnEntry *vn);
    void Add(const std::string &vn);
    virtual VnUveEntryPtr Allocate(const VnEntry *vn);
    virtual VnUveEntryPtr Allocate();
    void VnNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void SendDeleteVnMsg(const std::string &vn);
    void MarkChanged(const VnEntry *vn);
    void InterfaceDeleteHandler(const std::string &vm, const std::string &vn,
                                const Interface* intf);
    void InterfaceAddHandler(const VnEntry* vn, const Interface* intf,
                             const std::string &vm_name,
                             VnUveInterfaceState *state);
    void set_expiry_time(int time);
    void SendVnMsg(VnUveEntryBase *entry, const VnEntry *vn);

    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;

    // Last visited VmEntry by timer
    std::string timer_last_visited_;
    Timer *timer_;
    int expiry_time_;

    DISALLOW_COPY_AND_ASSIGN(VnUveTableBase);
};

#endif // vnsw_agent_vn_uve_table_base_h
