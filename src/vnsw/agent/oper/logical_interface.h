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
                              const std::string &name);
    virtual ~LogicalInterface();

    virtual bool CmpInterface(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual bool Delete(const DBRequest *req);
    virtual bool OnChange(const InterfaceTable *table,
                          const LogicalInterfaceData *data);
    virtual void ConfigEventHandler(IFMapNode *node);

    virtual bool Copy(const InterfaceTable *table,
                      const LogicalInterfaceData *data) = 0;

    const std::string &display_name() const { return display_name_; }
    Interface *physical_interface() const;
    VmInterface *vm_interface() const;

 private:
    friend class InterfaceTable;
    std::string display_name_;
    InterfaceRef physical_interface_;
    InterfaceRef vm_interface_;
    DISALLOW_COPY_AND_ASSIGN(LogicalInterface);
};

struct LogicalInterfaceKey : public InterfaceKey {
    explicit LogicalInterfaceKey(const boost::uuids::uuid &uuid,
                            const std::string &name);
    virtual ~LogicalInterfaceKey();

};

struct LogicalInterfaceData : public InterfaceData {
    LogicalInterfaceData(IFMapNode *node, const std::string &display_name,
                         const std::string &physical_interface,
                    const boost::uuids::uuid &vif);
    virtual ~LogicalInterfaceData();

    std::string display_name_;
    std::string physical_interface_;
    boost::uuids::uuid vm_interface_;
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
    VlanLogicalInterfaceData(IFMapNode *node, const std::string &display_name,
                             const std::string &physical_interface,
                             const boost::uuids::uuid &vif, uint16_t vlan);
    virtual ~VlanLogicalInterfaceData();

    uint16_t vlan_;
};

class VlanLogicalInterface : public LogicalInterface {
 public:
    explicit VlanLogicalInterface(const boost::uuids::uuid &uuid,
                                  const std::string &name);
    virtual ~VlanLogicalInterface();
    virtual DBEntryBase::KeyPtr GetDBRequestKey() const;

    bool Copy(const InterfaceTable *table, const LogicalInterfaceData *d);
    uint16_t vlan() const { return vlan_; }

 private:
    uint16_t vlan_;
    DISALLOW_COPY_AND_ASSIGN(VlanLogicalInterface);
};

#endif  // SRC_VNSW_AGENT_OPER_LOGICAL_PORT_H_
