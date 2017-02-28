/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_table_hpp
#define vnsw_agent_resource_table_hpp
#include "resource_cmn.h"
class ResourceManager;
class ResourceTable;
class ResourceData;
class ResourceKey {
public:
    ResourceKey(ResourceManager *rm, Resource::Type type);
    virtual ~ResourceKey();

    virtual const std::string ToString() {return "";}
    virtual bool IsLess(const ResourceKey &rhs) const = 0;
    virtual void Backup(ResourceData *data, uint16_t op) = 0;
    bool operator<(const ResourceKey &rhs) const;
    void set_dirty() {dirty_ = true;}
    void reset_dirty() {dirty_ = false;}
    bool dirty() const {return dirty_;}
    ResourceTable * resource_table() {return resource_table_;}
    ResourceManager* rm() {return rm_;}
private:
    ResourceManager *rm_;
    bool dirty_;
    ResourceTable *resource_table_;
    DISALLOW_COPY_AND_ASSIGN(ResourceKey);
};

class ResourceData {
public:
    typedef boost::shared_ptr<ResourceData> Ptr;
    ResourceData(ResourceManager *rm);
    virtual ~ResourceData();
    virtual const std::string ToString() = 0;
private:
    ResourceManager *rm_;
};

struct KeyDataMapComparator {
    bool operator()( const boost::shared_ptr<ResourceKey> lhs,
            const boost::shared_ptr<ResourceKey> rhs) const {
        ResourceKey *left = lhs.get();
        ResourceKey *right = rhs.get();
        return (*left).IsLess(*right);
    }
};

class ResourceBackupEndKey : public ResourceKey {
public:
    ResourceBackupEndKey(ResourceManager *rm);
    virtual ~ResourceBackupEndKey();

    virtual const std::string ToString() {return "ResourceBackupEndKey";}
    virtual bool IsLess(const ResourceKey &rhs) const {
        assert(0);
    }
    virtual void Backup(ResourceData *data, uint16_t op) {
        assert(0);
    }
};
// This class is resposible for allocating & restoring the the resources.
// maintains the Resource Key and resource data value pairs.
class ResourceTable {
public:
    typedef boost::shared_ptr<ResourceKey> KeyPtr;
    typedef boost::shared_ptr<ResourceData> DataPtr;
    typedef ResourceTable::KeyPtr ResourceKeyPtr;
    typedef std::map<KeyPtr, DataPtr, KeyDataMapComparator> KeyDataMap;
    typedef KeyDataMap::iterator KeyDataMapIter;
    ResourceTable(ResourceManager *rm);
    virtual ~ResourceTable();
    virtual const std::string ToString() = 0;
    virtual DataPtr AllocateData(KeyPtr key) = 0;
    virtual void RestoreKey(KeyPtr key, DataPtr data) = 0;
    virtual void ReleaseKey(KeyPtr key, DataPtr data) = 0;
    void InsertKey(KeyPtr key, DataPtr data);
    void DeleteKey(KeyPtr key);
    ResourceData *FindKey(KeyPtr key);
    DataPtr FindKeyPtr(KeyPtr key);
    void FlushStale();
    ResourceTable::DataPtr Allocate(KeyPtr key);
    ResourceManager *resource_manager() const { return rm_; }
private:
    ResourceManager *rm_;
    KeyDataMap key_data_map_;
    DISALLOW_COPY_AND_ASSIGN(ResourceTable);
};

#endif //resource_table
