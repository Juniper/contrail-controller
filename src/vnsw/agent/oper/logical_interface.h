/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OPER_LOGICAL_PORT_H_
#define SRC_VNSW_AGENT_OPER_LOGICAL_PORT_H_

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <string>

class IFMapDependencyManager;

class VmInterface;
class SandeshLogicalInterface;

struct LogicalInterfaceKey;
struct LogicalInterfaceData;

class LogicalInterface : public Interface {
 public:
    enum Type {
        VLAN
    };

    explicit LogicalInterface(const boost::uuids::uuid &uuid,
                              const std::string &name,
                              const boost::uuids::uuid &logical_router_uuid);
    virtual ~LogicalInterface();

    virtual bool CmpInterface(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual bool Delete(const DBRequest *req);
    virtual void GetOsParams(Agent *agent);
    virtual bool OnChange(const InterfaceTable *table,
                          const LogicalInterfaceData *data);

    const std::string &display_name() const { return display_name_; }
    const std::string &phy_dev_display_name() const { return phy_dev_display_name_; }
    const std::string &phy_intf_display_name() const { return phy_intf_display_name_; }
    Interface *physical_interface() const;
    VmInterface *vm_interface() const;
    PhysicalDevice *physical_device() const;

 private:
    friend class InterfaceTable;
    std::string display_name_;
    InterfaceRef physical_interface_;
    InterfaceRef vm_interface_;
    boost::uuids::uuid vm_uuid_;
    PhysicalDeviceRef physical_device_;
    std::string phy_dev_display_name_;
    std::string phy_intf_display_name_;
    boost::uuids::uuid vn_uuid_;
    DISALLOW_COPY_AND_ASSIGN(LogicalInterface);
};

struct LogicalInterfaceKey : public InterfaceKey {
    explicit LogicalInterfaceKey(const boost::uuids::uuid &uuid,
                            const std::string &name);
    virtual ~LogicalInterfaceKey();

};

struct LogicalInterfaceData : public InterfaceData {
    LogicalInterfaceData(Agent *agent, IFMapNode *node,
                         const std::string &display_name,
                         const std::string &physical_interface,
                         const boost::uuids::uuid &vif,
                         const boost::uuids::uuid &device_uuid,
                         const std::string &phy_dev_display_name,
                         const std::string &phy_intf_display_name);
    virtual ~LogicalInterfaceData();

    std::string display_name_;
    std::string physical_interface_;
    boost::uuids::uuid vm_interface_;
    boost::uuids::uuid device_uuid_;
    std::string phy_dev_display_name_;
    std::string phy_intf_display_name_;
};

struct VlanLogicalInterfaceKey : public LogicalInterfaceKey {
    explicit VlanLogicalInterfaceKey(const boost::uuids::uuid &uuid,
                                const std::string &name);
    virtual ~VlanLogicalInterfaceKey();

    virtual LogicalInterface *AllocEntry(const InterfaceTable *table) const;
    virtual LogicalInterface *AllocEntry(const InterfaceTable *table,
                                         const InterfaceData *data) const;
    virtual InterfaceKey *Clone() const;
};

struct VlanLogicalInterfaceData : public LogicalInterfaceData {
    VlanLogicalInterfaceData(Agent *agent, IFMapNode *node,
                             const std::string &display_name,
                             const std::string &physical_interface,
                             const boost::uuids::uuid &vif,
                             const boost::uuids::uuid &device_uuid,
                             const std::string &phy_dev_display_name,
                             const std::string &phy_intf_display_name,
                             uint16_t vlan);
    virtual ~VlanLogicalInterfaceData();

    uint16_t vlan_;
};

class VlanLogicalInterface : public LogicalInterface {
 public:
    explicit VlanLogicalInterface(const boost::uuids::uuid &uuid,
                                  const std::string &name, uint16_t vlan,
                                  const boost::uuids::uuid &lr_uuid);
    virtual ~VlanLogicalInterface();
    virtual DBEntryBase::KeyPtr GetDBRequestKey() const;

    uint16_t vlan() const { return vlan_; }

 private:
    // IMP: vlan_ cannot be changed
    uint16_t vlan_;
    DISALLOW_COPY_AND_ASSIGN(VlanLogicalInterface);
};

#endif  // SRC_VNSW_AGENT_OPER_LOGICAL_PORT_H_
