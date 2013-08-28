/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/test/ifmap_client_mock.h"

#include <boost/tuple/tuple.hpp>
#include <pugixml/pugixml.hpp>

#include "testing/gunit.h"

using namespace std;
using pugi::xml_document;
using pugi::xml_node;
using pugi::xml_parse_result;

IFMapClientMock::IFMapClientMock(const string &addr)
    : identifier_(addr), count_(0) {
}
    
const string &IFMapClientMock::identifier() const {
    return identifier_;
}

bool IFMapClientMock::SendUpdate(const std::string &msg) {
    xml_document xdoc;
    xml_parse_result result = xdoc.load_buffer(msg.data(), msg.size());
    EXPECT_TRUE(result);
    if (!result) {
        return true;
    }
    xml_node iq  = xdoc.child("iq");
    EXPECT_TRUE(iq);
    if (!iq) {
        return true;
    }
    xml_node config = iq.child("config");
    EXPECT_TRUE(config);
    if (!config) {
        return true;
    }
    for (xml_node node = config.first_child(); node;
         node = node.next_sibling()) {
        if (strcmp(node.name(), "update") == 0) {
            ProcessElement(node, true);
        } else if (strcmp(node.name(), "delete") == 0) {
            ProcessElement(node, false);
        }
    }
    return true;
}

bool IFMapClientMock::NodeExists(const string &type, const string &name) const {
    std::pair<ObjectMap::const_iterator, ObjectMap::const_iterator> range;
    range = object_map_.equal_range(type);
    for (ObjectMap::const_iterator iter = range.first;
         iter != range.second; ++iter) {
        if (name.compare(iter->second) == 0) {
            return true;
        }
    }
    return false;
}

size_t IFMapClientMock::NodeKeyCount(const string &key) const {
    return object_map_.count(key);
}

bool IFMapClientMock::LinkExists(const string &ltype, const string &rtype,
                          const string &lname, const string &rname) const {
    std::pair<LinkMap::const_iterator, LinkMap::const_iterator> range;
    range = link_map_.equal_range(make_pair(ltype, rtype));
    for (LinkMap::const_iterator iter = range.first;
         iter != range.second; ++iter) {
        std::pair<std::string, std::string> names = iter->second;
        if ((lname.compare(names.first) == 0) &&
            (rname.compare(names.second) == 0)) {
            return true;
        }
    }
    return false;
}

size_t IFMapClientMock::LinkKeyCount(const string &ltype,
                                     const string &rtype) const {
    return link_map_.count(make_pair(ltype, rtype));
}

pair<string, string> IFMapClientMock::LinkFind(
    const string &left, const string &right) const {
    LinkMap::const_iterator loc = link_map_.find(make_pair(left, right));
    if (loc != link_map_.end()) {
        return loc->second;
    }
    return make_pair("", "");
}

void IFMapClientMock::ProcessElement(const xml_node &parent, bool update) {
    for (xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        if (strcmp(node.name(), "node") == 0) {
            NodeParse(node, update);
        } else if (strcmp(node.name(), "link") == 0) {
            LinkParse(node, update);
        }
    }
}

static pair<string, string> NodeIdentifier(const xml_node &node) {
    string type = node.attribute("type").value();
    string name = node.child("name").child_value();
    return make_pair(type, name);
}
    
void IFMapClientMock::NodeParse(const xml_node &node, bool update) {
    string type, name;
    boost::tie(type, name) = NodeIdentifier(node);
    if (update) {
        object_map_.insert(make_pair(type, name));
    } else {
        MapErase(&object_map_, type, name);
    }
    count_++;
}

void IFMapClientMock::LinkParse(const xml_node &link, bool update) {
    xml_node first = link.first_child();
    xml_node second = first.next_sibling();
    pair<string, string> left = NodeIdentifier(first);
    pair<string, string> right = NodeIdentifier(second);

    pair<string, string> key = make_pair(left.first, right.first);
    pair<string, string> data = make_pair(left.second, right.second);

    if (update) {
        link_map_.insert(make_pair(key, data));
    } else {
        MapErase(&link_map_, key, data);
    }
    count_++;
}

void IFMapClientMock::PrintNodes() {
    std::cout << "***Printing " << object_map_.size() << " nodes:" << std::endl;
    int i = 1;
    for (ObjectMap::iterator iter = object_map_.begin();
         iter != object_map_.end(); ++iter, ++i) {
        std::cout << i << ") ";
        std::cout << "[" << iter->first << " -- " << iter->second << "]"
                  << std::endl;
    }
}

void IFMapClientMock::PrintLinks() {
    std::cout << "***Printing " << link_map_.size() << " links:" << std::endl;
    int i = 1;
    for (LinkMap::iterator iter = link_map_.begin();
         iter != link_map_.end(); ++iter, ++i) {
        std::cout << i << ") ";
        std::pair<std::string, std::string> left = iter->first;
        std::pair<std::string, std::string> right = iter->second;
        std::cout << "[" << left.first << " -- " << left.second << "]";
        std::cout << "::";
        std::cout << "[" << right.first << " -- " << right.second << "]"
                  << std::endl;
    }
}

