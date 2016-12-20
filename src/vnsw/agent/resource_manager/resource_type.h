/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_type_hpp
#define vnsw_agent_resource_type_hpp


using namespace boost::uuids;
using namespace std;

class ResourceManager;

class ResourceKey {
public:
    ResourceKey(const ResourceManager *rm);
    virtual ~ResourceKey();

    virtual ToString() = 0;
    virtual bool operator!=(const ResourceKey &rhs) const = 0;
    virtual void Copy(const ResourceKey &rhs) = 0;

    const ResourceManager *rm_;
    //Never deleted but makes it a candidate for reuse.
    bool delete_marked_;
    ResourceType *resource_type_;
};

class ResourceData {
public:
    ResourceData(const ResourceManager *rm);
    virtual ~ResourceKey();
    virtual ToString() = 0;

    const ResourceManager *rm_;
    ResourceType::KeyPtr key_;
};

class ResourceType {
public:
    typedef boost::shared_ptr<ResourceKey *> KeyPtr;
    typedef boost::shared_ptr<ResourceData *> DataPtr;
    typedef std::map<KeyPtr, DataPtr> KeyDataMap;
    typedef std::map<KeyPtr, DataPtr>::itermtor KeyDataMapIter;
    typedef std::list<DataPtr> FreeDataList;
    typedef std::list<DataPtr>::itermtor FreeDataListIter;

    ResourceType(const ResourceManager *rm);
    virtual ~ResourceType();

    virtual ToString() = 0;
    virtual DataPtr AllocateData(KeyPtr key) = 0;
    virtual void ReleaseData(KeyPtr key) = 0;

    void InsertKey(KeyPtr key, DataPtr data);
    void DeleteKey(KeyPtr key);
    DataPtr FindKey(KeyPtr key);

private:
    const ResourceManager *rm_;
    KeyDataMap key_data_map_;
    FreeDataList free_data_list_;
    DISALLOW_COPY_AND_ASSIGN(ResourceType);      
};

#endif //resource_type
