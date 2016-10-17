/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_cfg_listener_hpp
#define vnsw_agent_interface_cfg_listener_hpp

#include <cmn/agent_cmn.h>

using namespace boost::uuids;

class AgentConfig;
class VmInterface;
class SandeshVmiLabelTableResp;

class InterfaceCfgClient {
public:
    // Map from intf-uuid to intf-name
    typedef std::map<uuid, IFMapNode *> UuidToIFNodeTree;
    typedef std::pair<uuid, IFMapNode *> UuidIFNodePair;

    struct LabelKey {
        std::string vm_label_;
        std::string network_label_;

        LabelKey() : vm_label_(), network_label_() { }
        LabelKey(const std::string &vm_label, const std::string &nw_label) :
            vm_label_(vm_label), network_label_(nw_label) {
        }
        virtual ~LabelKey() { }
        bool operator()(const LabelKey &lhs, const LabelKey &rhs) const {
            if (lhs.vm_label_ != rhs.vm_label_)
                return lhs.vm_label_ < rhs.vm_label_;
            return lhs.network_label_ < rhs.network_label_;
        }
    };
    typedef std::map<LabelKey, const VmInterface *, LabelKey> VmiLabelTree;
    typedef std::pair<LabelKey, const VmInterface *> VmiLabelPair;
    typedef VmiLabelTree::iterator VmiLabelTreeIterator;
    typedef VmiLabelTree::const_iterator VmiLabelTreeConstIterator;

    InterfaceCfgClient(AgentConfig *cfg) : agent_cfg_(cfg) { }
    virtual ~InterfaceCfgClient() { }

    void Init();
    void Shutdown();
    void FillIntrospectData(SandeshVmiLabelTableResp *resp);

    const VmInterface *LabelToVmi(const std::string &vm_label,
                                  const std::string &vn_label) const;

    void OperVmiNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceConfigNotify(DBTablePartBase *partition, DBEntryBase *e);
    void IfMapVmiNotify(DBTablePartBase *partition, DBEntryBase *e);
    void IfMapInterfaceRouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    IFMapNode *UuidToIFNode(const uuid &u) const;
    uint32_t vmi_label_tree_size() const { return vmi_label_tree_.size(); }

private:
    struct InterfaceConfigState : DBState {
        boost::uuids::uuid vmi_uuid_;
        std::string vm_label_;
        std::string nw_label_;

        InterfaceConfigState(const boost::uuids::uuid &u) :
            DBState(), vmi_uuid_(u), vm_label_(), nw_label_() {
        }
        InterfaceConfigState(const std::string &vm_label,
                             const std::string nw_label) :
            DBState(), vmi_uuid_(boost::uuids::nil_uuid()),
            vm_label_(vm_label), nw_label_(nw_label) {
        }
        virtual ~InterfaceConfigState() { }
    };

    struct IfMapVmiState : DBState {
        boost::uuids::uuid uuid_;
        IfMapVmiState(const boost::uuids::uuid &u) : uuid_(u) { }
        virtual ~IfMapVmiState() { }
    };

    struct OperVmiState : DBState {
        std::string vm_label_;
        std::string network_label_;

        OperVmiState(const std::string &vm_label,
                     const std::string &network_label) :
            vm_label_(vm_label), network_label_(network_label) {
        }
        virtual ~OperVmiState() { }
    };

    bool VmiToLabels(const VmInterface *vmi, std::string *vm_label,
                     std::string *nw_label) const;
    void NotifyUuidAdd(Agent *agent, IFMapNode *node,
                       const boost::uuids::uuid &u) const;
    void NotifyUuidDel(Agent *agent, const boost::uuids::uuid &u) const;
    void TryAddVmiFromLabel(const LabelKey &key) const;
    void DeleteVmiFromLabel(const LabelKey &key) const;

    AgentConfig *agent_cfg_;
    DBTableBase::ListenerId intf_cfg_listener_id_;
    DBTableBase::ListenerId ifmap_vmi_listener_id_;
    DBTableBase::ListenerId ifmap_intf_route_listener_id_;
    DBTableBase::ListenerId oper_interface_listener_id_;
    UuidToIFNodeTree uuid_ifnode_tree_;
    VmiLabelTree vmi_label_tree_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceCfgClient);
};

#endif // vnsw_agent_interface_cfg_listener_hpp
