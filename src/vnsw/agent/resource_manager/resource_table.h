/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_table_hpp
#define vnsw_agent_resource_table_hpp


using namespace boost::uuids;
using namespace std;

class ResourceManager;
class ResourceTable;
class ResourceData;

class ResourceKey {
public:
    typedef boost::shared_ptr<ResourceKey> Ptr;
    ResourceKey(ResourceManager *rm, uint16_t type);
    virtual ~ResourceKey();

    virtual const std::string ToString() {return "";}
    virtual void Copy(const ResourceKey &rhs) { }
    virtual bool IsLess(const ResourceKey &rhs) const = 0;
    virtual void Backup(ResourceData *data, bool del) = 0;

    bool operator<(const ResourceKey &rhs) const;
    bool operator!=(const ResourceKey &rhs) const;
    void set_dirty() {dirty_ = true;}
    void reset_dirty() {dirty_ = false;}
    bool dirty() const {return dirty_;}

    ResourceManager *rm_;
    bool dirty_;
    //Never deleted but makes it a candidate for reuse.
    bool delete_marked_;
    ResourceTable *resource_type_;
};

class ResourceData {
public:
    typedef boost::shared_ptr<ResourceData> Ptr;
    ResourceData(ResourceManager *rm);
    virtual ~ResourceData();
    virtual const std::string ToString() = 0;
    ResourceKey::Ptr key();

    ResourceManager *rm_;
    ResourceKey::Ptr key_;
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
    virtual void Backup(ResourceData *data, bool del) {
        assert(0);
    }
};

class ResourceTable {
public:
    typedef boost::shared_ptr<ResourceKey> KeyPtr;
    typedef boost::shared_ptr<ResourceData> DataPtr;
    typedef std::map<KeyPtr, DataPtr, KeyDataMapComparator> KeyDataMap;
    typedef KeyDataMap::iterator KeyDataMapIter;
    typedef std::list<DataPtr> FreeDataList;
    typedef std::list<DataPtr>::iterator FreeDataListIter;

    ResourceTable(ResourceManager *rm);
    virtual ~ResourceTable();

    virtual const std::string ToString() = 0;
    virtual DataPtr AllocateData(KeyPtr key) = 0;

    void InsertKey(KeyPtr key, DataPtr data);
    void DeleteKey(KeyPtr key);
    ResourceData *FindKey(KeyPtr key);
    DataPtr FindKeyPtr(KeyPtr key);
    void FlushStale();

    ResourceManager *resource_manager() const { return rm_; }
private:
    ResourceManager *rm_;
    KeyDataMap key_data_map_;
    DISALLOW_COPY_AND_ASSIGN(ResourceTable);
};

#endif //resource_type
