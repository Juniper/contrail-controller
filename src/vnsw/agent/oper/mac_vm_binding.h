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
                                      uint32_t vrf_id) const;
    void AddMacVmBinding(uint32_t vrf_id, const VmInterface *vm_interface);
    void DeleteMacVmBinding(uint32_t vrf_id, const VmInterface *vm_interface);

private:
    struct MacVmBindingKey {
        MacAddress mac;
        uint32_t vrf_id;
        InterfaceConstRef interface;

        MacVmBindingKey(const MacAddress &m, uint32_t v,
                        InterfaceConstRef intf) :
            mac(m), vrf_id(v), interface(intf) {}
        bool operator<(const MacVmBindingKey &rhs) const {
            if (mac != rhs.mac)
                return mac < rhs.mac;

            return vrf_id < rhs.vrf_id;
        }
    };
    typedef std::set<MacVmBindingKey> MacVmBindingSet;
    void UpdateBinding(const VmInterface *vm_interface, bool del, uint32_t vrf_id);
    MacVmBindingSet::iterator FindInterface(const Interface *interface);

    MacVmBindingSet mac_vm_binding_set_;
};

#endif // vnsw_agent_mac_vm_binding_h_
