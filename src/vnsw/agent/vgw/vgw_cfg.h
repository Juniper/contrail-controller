/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_cfg_h
#define vnsw_agent_vgw_cfg_h

#include <net/address.h>

// Simple Virtual Gateway config class
class VGwConfig {
public:
    VGwConfig(const std::string vrf, const std::string interface,
              const Ip4Address &addr) :
        vrf_(vrf), interface_(interface), ip_(addr) {};
    ~VGwConfig() {};

    const std::string &GetVrf() const {return vrf_;};
    const std::string &GetInterface() const {return interface_;};
    const Ip4Address &GetAddr() const {return ip_;};


    static void Init(const char *init_file);
    static void Shutdown();
    static bool Add(const std::string &vrf, const std::string &interface,
                    const Ip4Address &addr);
    static VGwConfig *GetInstance() {return singleton_;};
private:
    // Public network name
    std::string vrf_;
    // Interface connecting gateway to host-os
    std::string interface_;
    // IP address of interface connecting to host-os
    Ip4Address ip_;

    static VGwConfig *singleton_;

    void OnIntfCreate(DBEntryBase *entry);
    DISALLOW_COPY_AND_ASSIGN(VGwConfig);
};
#endif //vnsw_agent_vgw_cfg_h
