/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_next_hop_index_resource_hpp
#define vnsw_agent_next_hop_index_resource_hpp

/*
 * NEXTHOP index allocator using index_vector
 * Uses Resource:NEXTHOP_INDEX
 */
#include <resource_manager/index_resource.h>
#include <resource_manager/resource_backup.h>
class ResourceManager;
class ResourceKey;
class NextHopKey;
// Maintains the nhid to label map.
struct cnhid_label_map {
    uint32_t nh_id;
    uint32_t label;
};
//Nexthop Resource Index
class NHIndexResourceKey : public IndexResourceKey {
public:
    NHIndexResourceKey(ResourceManager *rm, uint16_t nh_type,
                       NextHopKey *nh_key);
    NHIndexResourceKey(ResourceManager *rm, uint16_t nh_type,
                       const uint16_t comp_type,
                       const std::vector<cnhid_label_map>&nh_ids, const bool policy,
                       const std::string &vrf_name);
    virtual ~NHIndexResourceKey();

    virtual const std::string ToString() { return "";}
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, uint16_t op);
    void BackupCompositeNH(ResourceData *data, uint16_t op);
    const NextHopKey *GetNhKey() const { return nh_key_.get(); }
    uint16_t nh_type() const {return nh_type_;}
    uint16_t comp_type() const {return comp_type_;}
    const bool policy() const {return policy_;}
    const std::string & vrf_name() const {return vrf_name_;}
private:
    std::auto_ptr<NextHopKey> nh_key_;
    uint16_t nh_type_;
    // Composite NH fileds
    uint16_t comp_type_;
    std::vector<cnhid_label_map> nh_ids_labels_;
    bool policy_;
    std::string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(NHIndexResourceKey);
};
#endif
