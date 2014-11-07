/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_uve_entry_base_h
#define vnsw_agent_vn_uve_entry_base_h

#include <string>
#include <set>
#include <map>
#include <vector>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_network_types.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/vn.h>
#include <tbb/mutex.h>
#include <cmn/agent_cmn.h>

//The class that defines data-structures to store VirtualNetwork information
//required for sending VirtualNetwork UVE.
class VnUveEntryBase {
public:
    typedef std::set<const Interface *> InterfaceSet;
    typedef std::set<std::string> VmSet;

    VnUveEntryBase(Agent *agent, const VnEntry *vn);
    VnUveEntryBase(Agent *agent);
    virtual ~VnUveEntryBase();

    void set_vn(const VnEntry *vn) { vn_ = vn; }

    void InterfaceAdd(const Interface *intf);
    void InterfaceDelete(const Interface *intf);
    void VmAdd(const std::string &vm);
    void VmDelete(const std::string &vm);
    bool BuildInterfaceVmList(UveVirtualNetworkAgent &s_vn);
    bool FrameVnMsg(const VnEntry *vn, UveVirtualNetworkAgent &uve);
    const VnEntry *vn() const { return vn_; }
    void GetInStats(uint64_t *in_bytes, uint64_t *in_pkts) const;
    void GetOutStats(uint64_t *out_bytes, uint64_t *out_pkts) const;
protected:
    bool UveVnAclRuleCountChanged(int32_t size) const;
    Agent *agent_;
    const VnEntry *vn_;
    UveVirtualNetworkAgent uve_info_;
    InterfaceSet interface_tree_;
    VmSet vm_tree_;

private:
    bool UveVnInterfaceListChanged(const std::vector<string> &new_list) const;
    bool UveVnVmListChanged(const std::vector<string> &new_list) const;
    bool UveVnAclChanged(const std::string &name) const;
    bool UveVnMirrorAclChanged(const std::string &name) const;

    DISALLOW_COPY_AND_ASSIGN(VnUveEntryBase);
};

#endif // vnsw_agent_vn_uve_entry_base_h
