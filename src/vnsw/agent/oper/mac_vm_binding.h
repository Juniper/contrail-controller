/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mac_vm_binding_h_
#define vnsw_agent_mac_vm_binding_h_

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

class MacVmBinding {
public:
    MacVmBinding(const Agent *agent);
    virtual ~MacVmBinding();
    void Register();
    const Interface *FindMacVmBinding(const MacAddress &address,
                                      int vxlan) const;
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);

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
    MacVmBindingSet::iterator FindInterfaceUsingMac(MacAddress &address,
                                                    const Interface *interface);

    const Agent *agent_;
    DBTable::ListenerId interface_listener_id_;
    MacVmBindingSet mac_vm_binding_set_;
};

#endif // vnsw_agent_mac_vm_binding_h_
