/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BGP_BGP_CONFIG_YAML_H__
#define BGP_BGP_CONFIG_YAML_H__

#include <istream>

/*
 * BGP YAML configuration manager
 * Uses yaml-cpp library http://code.google.com/p/yaml-cpp
 */
#include "bgp/bgp_config.h"

class BgpYamlConfigManager : public BgpConfigManager {
public:
    class Configuration;
    static const int kMaxHoldTime = 60 * 60;

    explicit BgpYamlConfigManager(BgpServer *server);
    virtual ~BgpYamlConfigManager();

    /*
     * begin: BgpConfigManager Interface
     */
    virtual void Terminate();
    virtual const std::string &localname() const;

    virtual InstanceMapRange InstanceMapItems(
        const std::string &start_name = std::string()) const;
    virtual NeighborMapRange NeighborMapItems(
        const std::string &instance_name) const;

    virtual int NeighborCount(const std::string &instance_name) const;

    virtual const BgpInstanceConfig *FindInstance(
        const std::string &name) const;
    virtual const BgpProtocolConfig *GetProtocolConfig(
        const std::string &instance_name) const;
    virtual const BgpNeighborConfig *FindNeighbor(
        const std::string &instance_name, const std::string &name) const;
    // end: BgpConfigManager

    bool Parse(std::istream *istream, std::string *error_msg);

private:
    bool Resolve(Configuration *candidate, std::string *error_msg);
    void Update(Configuration *current, Configuration *next);
    void UpdateProtocol(Configuration *current, Configuration *next);
    void UpdateInstances(Configuration *current, Configuration *next);
    void UpdateNeighbors(Configuration *current, Configuration *next);

    void AddInstance(InstanceMap::iterator iter);
    void DeleteInstance(InstanceMap::iterator iter);
    void UpdateInstance(InstanceMap::iterator iter1,
                        InstanceMap::iterator iter2);

    void AddNeighbor(NeighborMap::iterator iter);
    void DeleteNeighbor(NeighborMap::iterator iter);
    void UpdateNeighbor(NeighborMap::iterator iter1,
                        NeighborMap::iterator iter2);

    std::auto_ptr<Configuration> data_;
};

#endif  // BGP_BGP_CONFIG_YAML_H__
