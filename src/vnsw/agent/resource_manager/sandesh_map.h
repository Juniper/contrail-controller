/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_sandesh_map_hpp
#define vnsw_agent_resource_sandesh_map_hpp

using namespace boost::uuids;
using namespace std;

class InterfaceIndexResource;
class VrfMplsResource;
class RouteMplsResource;

class ResourceSandeshMaps {
public:
    typedef std::map<uint32_t, InterfaceIndexResource> InterfaceIndexResourceMap;
    typedef InterfaceIndexResourceMap::iterator InterfaceIndexResourceMapIter;
    typedef std::map<uint32_t, VrfMplsResource> VrfMplsResourceMap;
    typedef VrfMplsResourceMap::iterator VrfMplsResourceMapIter;
    typedef std::map<uint32_t, RouteMplsResource> RouteMplsResourceMap;
    typedef RouteMplsResourceMap::iterator RouteMplsResourceMapIter;

    ResourceSandeshMaps();
    virtual ~ResourceSandeshMaps();

    void incr_sequence_number() {sequence_number_++;}
    void update_timer_processed_sequence_number() {
        timer_processed_sequence_number_ = sequence_number_;
    }
    uint64_t sequence_number() const {
        return sequence_number_;
    }
    uint64_t timer_processed_sequence_number() const {
        return timer_processed_sequence_number_;
    }

    void WriteToFile(ResourceBackupManager *backup_manager);
    void ReadFromFile(ResourceBackupManager *backup_manager);
    void RestoreResource(Agent *agent);

    //TODO make these private and provide add/delete routines
    std::map<uint32_t, InterfaceIndexResource> interface_mpls_index_map_;
    std::map<uint32_t, VrfMplsResource> vrf_mpls_index_map_;
    std::map<uint32_t, RouteMplsResource> route_mpls_index_map_;
private:
    uint64_t sequence_number_;
    //TODO can be granularised to per map level
    uint64_t timer_processed_sequence_number_;
    DISALLOW_COPY_AND_ASSIGN(ResourceSandeshMaps);
};

#endif
