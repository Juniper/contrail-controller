/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_type_hpp
#define vnsw_agent_resource_type_hpp


using namespace boost::uuids;
using namespace std;

class ResourceManager;
class ResourceType;
class ResourceData;

class ResourceKey {
public:
    typedef boost::shared_ptr<ResourceKey> Ptr;
    ResourceKey(ResourceManager *rm, uint16_t type);
    virtual ~ResourceKey();

    virtual const std::string ToString() {return "";}
    virtual void Copy(const ResourceKey &rhs) { }
    virtual bool IsLess(const ResourceKey &rhs) const;
    virtual void Backup(ResourceData *data, bool del) {assert(0);}

    bool operator<(const ResourceKey &rhs) const;
    bool operator!=(const ResourceKey &rhs) const;
    void set_dirty() {dirty_ = true;}
    void reset_dirty() {dirty_ = false;}
    bool dirty() const {return dirty_;}

    ResourceManager *rm_;
    bool dirty_;
    //Never deleted but makes it a candidate for reuse.
    bool delete_marked_;
    ResourceType *resource_type_;
};

class ResourceData {
public:
    typedef boost::shared_ptr<ResourceData> Ptr;
    ResourceData(ResourceManager *rm);
    virtual ~ResourceData();
    virtual const std::string ToString() = 0;

    ResourceManager *rm_;
    ResourceKey::Ptr key_;
};

struct KeyDataMapComparator {
    bool operator()(const ResourceKey *lhs, const ResourceKey *rhs) const {
        return (*lhs).IsLess(*rhs);
    }
};

class ResourceType {
public:
    typedef boost::shared_ptr<ResourceKey> KeyPtr;
    typedef boost::shared_ptr<ResourceData> DataPtr;
    typedef std::map<ResourceKey *, DataPtr, KeyDataMapComparator> KeyDataMap;
    typedef KeyDataMap::iterator KeyDataMapIter;
    typedef std::list<DataPtr> FreeDataList;
    typedef std::list<DataPtr>::iterator FreeDataListIter;

    ResourceType(ResourceManager *rm);
    virtual ~ResourceType();

    virtual const std::string ToString() = 0;
    virtual DataPtr AllocateData(KeyPtr key) = 0;
    virtual void ReleaseData(KeyPtr key) = 0;

    void InsertKey(KeyPtr key, DataPtr data);
    void DeleteKey(KeyPtr key);
    ResourceData *FindKey(KeyPtr key);
    DataPtr FindKeyPtr(KeyPtr key);
    void FlushStale();

private:
    ResourceManager *rm_;
    KeyDataMap key_data_map_;
    //FreeDataList free_data_list_;
    DISALLOW_COPY_AND_ASSIGN(ResourceType);      
};

#endif //resource_type
