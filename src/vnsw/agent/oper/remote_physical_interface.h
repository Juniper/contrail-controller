/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef src_vnsw_agent_oper_remote_physical_interface_hpp
#define src_vnsw_agent_oper_remote_physical_interface_hpp

struct RemotePhysicalInterfaceData;

/////////////////////////////////////////////////////////////////////////////
// Remote Physical Ports represents physical ports on an external device
// UUID is the key
/////////////////////////////////////////////////////////////////////////////
class RemotePhysicalInterface : public Interface {
public:
    RemotePhysicalInterface(const std::string &name);
    virtual ~RemotePhysicalInterface();

    virtual bool CmpInterface(const DBEntry &rhs) const;
    virtual void GetOsParams(Agent *agent);
    virtual std::string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool OnChange(const InterfaceTable *table,
                          const RemotePhysicalInterfaceData *data);

    const std::string &display_name() const { return display_name_; }
    PhysicalDevice *physical_device() const {
        return physical_device_.get();
    }

    // Helper functions
    static void CreateReq(InterfaceTable *table, const std::string &fqdn,
                          const std::string &display_name,
                          const std::string &vrf_name,
                          const boost::uuids::uuid &device_uuid);
    static void Create(InterfaceTable *table, const std::string &fqdn,
                       const std::string &display_name,
                       const std::string &vrf_name,
                       const boost::uuids::uuid &device_uuid);
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
    static void Delete(InterfaceTable *table, const std::string &ifname);

    friend struct RemotePhysicalInterfaceKey;
private:
    std::string display_name_;
    PhysicalDeviceRef physical_device_;
    DISALLOW_COPY_AND_ASSIGN(RemotePhysicalInterface);
};

struct RemotePhysicalInterfaceKey : public InterfaceKey {
    RemotePhysicalInterfaceKey(const std::string &name);
    ~RemotePhysicalInterfaceKey();

    Interface *AllocEntry(const InterfaceTable *table) const;
    Interface *AllocEntry(const InterfaceTable *table,
                          const InterfaceData *data) const;
    InterfaceKey *Clone() const;
};

struct RemotePhysicalInterfaceData : public InterfaceData {
    RemotePhysicalInterfaceData(Agent *agent, IFMapNode *node,
                                const std::string &display_name,
                                const std::string &vrf_name,
                                const boost::uuids::uuid &device_uuid);
    virtual ~RemotePhysicalInterfaceData();

    std::string display_name_;
    boost::uuids::uuid device_uuid_;
};

#endif // src_vnsw_agent_oper_remote_physical_interface_hpp
