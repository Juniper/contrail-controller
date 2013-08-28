/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

// Mock client with the ability to decode messages.
#ifndef __IFMAP__TEST__IFMAP_CLIENT_MOCK_H__
#define __IFMAP__TEST__IFMAP_CLIENT_MOCK_H__

#include <map>
#include "ifmap/ifmap_client.h"

namespace pugi {
class xml_node;
}

class IFMapClientMock : public IFMapClient {
public:
    typedef std::multimap<
                std::pair<std::string, std::string>,
                std::pair<std::string, std::string>
            > LinkMap;
    typedef std::multimap<std::string, std::string> ObjectMap;

    IFMapClientMock(const std::string &addr);

    virtual const std::string &identifier() const;
    virtual bool SendUpdate(const std::string &msg);

    bool NodeExists(const std::string &type, const std::string &name) const;
    bool LinkExists(const std::string &ltype, const std::string &rtype,
                    const std::string &lname, const std::string &rname) const;
    std::pair<std::string, std::string> LinkFind(const std::string &left,
                                            const std::string &right) const;

    const ObjectMap &object_map() const { return object_map_; }
    const LinkMap &link_map() const { return link_map_; }
    int count() const { return count_; }
    int node_count() const { return object_map_.size(); }
    int link_count() const { return link_map_.size(); }
    void PrintNodes();
    void PrintLinks();
    size_t NodeKeyCount(const std::string &key) const;
    size_t LinkKeyCount(const std::string &ltype,
                        const std::string &rtype) const;

private:
    void ProcessElement(const pugi::xml_node &parent, bool update);
    void NodeParse(const pugi::xml_node &node, bool update);
    void LinkParse(const pugi::xml_node &link, bool update);

    template <typename Map, typename Key, typename ValueType>
    void MapErase(Map *map, const Key &key, const ValueType &value) {
        for (typename Map::iterator iter = map->find(key); iter != map->end();
             ++iter) {
            if (iter->first != key) {
                break;
            }
            if (iter->second == value) {
                map->erase(iter);
                break;
            }
        }
    }

    std::string identifier_;
    ObjectMap object_map_;
    LinkMap link_map_;
    int count_;
};

#endif
