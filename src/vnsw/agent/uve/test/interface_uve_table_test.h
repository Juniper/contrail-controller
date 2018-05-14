/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_uve_table_test_h
#define vnsw_agent_interface_uve_table_test_h

#include <uve/interface_uve_stats_table.h>

class InterfaceUveTableTest : public InterfaceUveStatsTable {
public:
    InterfaceUveTableTest(Agent *agent, uint32_t default_intvl);
    virtual void DispatchInterfaceMsg(const UveVMInterfaceAgent &uve);
    virtual void DispatchVMIStatsMsg(const VMIStats  &uve);
    uint32_t send_count() const { return send_count_; }
    uint32_t delete_count() const { return delete_count_; }
    uint32_t vmi_stats_send_count() const { return vmi_stats_send_count_; }
    uint32_t vmi_stats_delete_count() const { return vmi_stats_delete_count_; }
    uint32_t InterfaceUveCount() const { return interface_tree_.size(); }
    void ClearCount();
    L4PortBitmap* GetVmIntfPortBitmap(const VmInterface* intf);
    VMIStats* InterfaceUveObject(const VmInterface *itf);
    uint32_t GetVmIntfFipCount(const VmInterface* intf);
    const InterfaceUveTable::FloatingIp *GetVmIntfFip(const VmInterface* intf,
        const string &fip, const string &vn);
    const UveVMInterfaceAgent &last_sent_uve() const { return uve_; }
    const VMIStats &last_sent_uve_stats() const { return uve_stats_; }
    InterfaceUveTable::UveInterfaceEntry* GetUveInterfaceEntry
        (const std::string &name);
private:
    uint32_t send_count_;
    uint32_t delete_count_;
    uint32_t vmi_stats_send_count_;
    uint32_t vmi_stats_delete_count_;
    UveVMInterfaceAgent uve_;
    VMIStats uve_stats_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceUveTableTest);
};

#endif // vnsw_agent_interface_uve_table_test_h
