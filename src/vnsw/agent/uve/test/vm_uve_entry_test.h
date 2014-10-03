/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_entry_test_h
#define vnsw_agent_vm_uve_entry_test_h

class VmUveEntryTest : public VmUveEntry {
public:
    VmUveEntryTest(Agent *agent) : VmUveEntry(agent) {}
    virtual ~VmUveEntryTest() {}
    int InterfaceCount() const { return interface_tree_.size(); }
    L4PortBitmap* port_bitmap() { return &port_bitmap_; }
    L4PortBitmap* InterfaceBitmap(const Interface *intf) {
        UveInterfaceEntryPtr entry(new UveInterfaceEntry(intf));
        InterfaceSet::iterator it = interface_tree_.find(entry);
        if (it != interface_tree_.end()) {
            return &((*it)->port_bitmap_);
        }
        return NULL;
    }
    uint32_t FloatingIpCount(const Interface *intf) {
        UveInterfaceEntryPtr entry(new UveInterfaceEntry(intf));
        InterfaceSet::iterator it = interface_tree_.find(entry);
        if (it != interface_tree_.end()) {
            return ((*it)->fip_tree_.size());
        }
        return 0;
    }
    const VmUveEntry::FloatingIp *IntfFloatingIp(const Interface *intf,
            const std::string &fip, const std::string &vn) {
        UveInterfaceEntryPtr entry(new UveInterfaceEntry(intf));
        InterfaceSet::iterator it = interface_tree_.find(entry);
        if (it != interface_tree_.end()) {
            boost::system::error_code ec;
            Ip4Address ip = Ip4Address::from_string(fip, ec);
            FloatingIpPtr key(new FloatingIp(ip, vn));
            FloatingIpSet::iterator fip_it = ((*it)->fip_tree_.find(key));
            if (fip_it != ((*it)->fip_tree_.end())) {
                return (*fip_it).get();
            }
        }
        return NULL;
    }
    UveVirtualMachineAgent* uve_info() { return &uve_info_; }
private:
    DISALLOW_COPY_AND_ASSIGN(VmUveEntryTest);
};

#endif // vnsw_agent_vm_uve_entry_test_h
