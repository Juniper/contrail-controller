/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mac_vm_binding_h_
#define vnsw_agent_mac_vm_binding_h_

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

class VmInterface;

class MacVmBinding {
public:
    MacVmBinding();
    virtual ~MacVmBinding();
    const Interface *FindMacVmBinding(const MacAddress &address,
                                      int vxlan) const;
    void AddMacVmBinding(const VmInterface *vm_interface);
    void DeleteMacVmBinding(const VmInterface *vm_interface);

private:
    struct MacVmBindingKey {
        MacAddress mac;
        int vxlan;
        InterfaceConstRef interface;

        MacVmBindingKey(const MacAddress &m, int v, InterfaceConstRef intf) :
            mac(m), vxlan(v), interface(intf) {}
        bool operator<(const MacVmBindingKey &rhs) const {
            if (mac != rhs.mac)
                return mac < rhs.mac;

            return vxlan < rhs.vxlan;
        }
    };
    typedef std::set<MacVmBindingKey> MacVmBindingSet;
    void UpdateBinding(const VmInterface *vm_interface, bool del);
    MacVmBindingSet::iterator FindInterfaceUsingMac(MacAddress &address,
                                                    const Interface *interface);

    MacVmBindingSet mac_vm_binding_set_;
};

#endif // vnsw_agent_mac_vm_binding_h_
