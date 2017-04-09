/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_uve_entry_base_h
#define vnsw_agent_vn_uve_entry_base_h

#include <string>
#include <set>
#include <map>
#include <vector>
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
    bool FrameVnAclRuleCountMsg(const VnEntry *vn,
                                UveVirtualNetworkAgent *uve);

    void set_changed(bool val) { changed_ = val; }
    bool changed() const { return changed_; }
    void set_deleted(bool value) { deleted_ = value; }
    bool deleted() const { return deleted_; }
    void set_renewed(bool value) { renewed_ = value; }
    bool renewed() const { return renewed_; }
    void set_add_by_vn_notify(bool val) { add_by_vn_notify_ = val; }
    virtual void Reset();

protected:
    bool UveVnAclRuleCountChanged(int32_t size) const;
    Agent *agent_;
    const VnEntry *vn_;
    UveVirtualNetworkAgent uve_info_;
    InterfaceSet interface_tree_;
    VmSet vm_tree_;
    bool add_by_vn_notify_;

    // UVE entry is changed. Timer must generate UVE for this entry
    bool changed_;
    bool deleted_;
    bool renewed_;
private:
    bool UveVnInterfaceListChanged(const std::vector<string> &new_list) const;
    bool UveVnVmListChanged(const std::vector<string> &new_list) const;
    bool UveVnAclChanged(const std::string &name) const;
    bool UveVnMirrorAclChanged(const std::string &name) const;

    DISALLOW_COPY_AND_ASSIGN(VnUveEntryBase);
};

#endif // vnsw_agent_vn_uve_entry_base_h
