/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_node__
#define __ctrlplane__ifmap_node__

#include <boost/crc.hpp>      // for boost::crc_32_type
#include <boost/intrusive/list.hpp>

#include "db/db_graph_vertex.h"
#include "ifmap/ifmap_object.h"

class IFMapNode : public DBGraphVertex {
public:
    typedef boost::crc_32_type::value_type crc32type;
    typedef boost::intrusive::member_hook<IFMapObject,
        boost::intrusive::list_member_hook<>,
        &IFMapObject::node_> MemberHook;
    
    typedef boost::intrusive::list<IFMapObject, MemberHook> ObjectList;
    typedef std::pair<std::string, std::string> Descriptor;
    
    explicit IFMapNode(IFMapTable *table);
    virtual ~IFMapNode();
    
    virtual std::string ToString() const;
    
    IFMapTable *table() { return table_; }
    const IFMapTable *table() const { return table_; }
    
    virtual KeyPtr GetDBRequestKey() const;
    
    virtual void SetKey(const DBRequestKey *genkey);
    
    virtual bool IsLess(const DBEntry &db_entry) const {
        const IFMapNode &rhs = static_cast<const IFMapNode &>(db_entry);
        return name_ < rhs.name_;
    }
    
    void EncodeNodeDetail(pugi::xml_node *parent) const;
    void EncodeNode(pugi::xml_node *parent) const;
    static void EncodeNode(const Descriptor &descriptor,
                           pugi::xml_node *parent);
    
    static IFMapNode *DescriptorLookup(DB *db, const Descriptor &descriptor);
    
    const std::string &name() const { return name_; }

    IFMapObject *Find(IFMapOrigin origin);
    void Insert(IFMapObject *obj);
    void Remove(IFMapObject *obj);
    
    IFMapObject *GetObject();
    const IFMapObject *GetObject() const;
    crc32type GetConfigCrc();
    void PrintAllObjects();
    int get_object_list_size() { return list_.size(); }

private:
    friend class IFMapNodeCopier;
    IFMapTable *table_;
    std::string name_;
    ObjectList list_;
    DISALLOW_COPY_AND_ASSIGN(IFMapNode);
};


#endif /* defined(__ctrlplane__ifmap_node__) */
