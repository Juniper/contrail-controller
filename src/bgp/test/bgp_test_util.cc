/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_test_util.h"

#include <stdio.h>
#include <pugixml/pugixml.hpp>

using pugi::xml_document;
using pugi::xml_node;
using pugi::node_pcdata;
using namespace std;

namespace bgp_util {
string NetworkConfigGenerate(
        const vector<string> &instance_names,
        const multimap<string, string> &connections) {
    int index;
    xml_document xdoc;
    xml_node env = xdoc.append_child("Envelope");
    xml_node update = env.append_child("Body").append_child("response").
            append_child("pollResult").append_child("updateResult");

    index = 0;
    for (vector<string>::const_iterator iter = instance_names.begin();
         iter != instance_names.end(); ++iter) {
        xml_node item = update.append_child("resultItem");
        xml_node id = item.append_child("identity");
        string vn("virtual-network:");
        vn.append(*iter);
        id.append_attribute("name") = vn.c_str();
        xml_node meta = item.append_child("metadata");
        xml_node vn_properties = meta.append_child("virtual-network-properties");
        xml_node net_id = vn_properties.append_child("network-id");
        char value[16];
        snprintf(value, sizeof(value), "%d", ++index);
        net_id.append_child(node_pcdata).set_value(value);
    }
    index = 0;
    for (vector<string>::const_iterator iter = instance_names.begin();
         iter != instance_names.end(); ++iter) {
        xml_node item = update.append_child("resultItem");
        xml_node id1 = item.append_child("identity");
        string instance("routing-instance:");
        instance.append(*iter);
        id1.append_attribute("name") = instance.c_str();
        xml_node id2 = item.append_child("identity");
        ostringstream target;
        target << "route-target:target:64496:" << ++index;
        id2.append_attribute("name") = target.str().c_str();
        xml_node meta = item.append_child("metadata");
        meta.append_child("instance-target");
    }
    for (vector<string>::const_iterator iter = instance_names.begin();
         iter != instance_names.end(); ++iter) {
        xml_node item = update.append_child("resultItem");
        xml_node id1 = item.append_child("identity");
        string vn("virtual-network:");
        vn.append(*iter);
        id1.append_attribute("name") = vn.c_str();
        xml_node id2 = item.append_child("identity");
        string instance("routing-instance:");
        instance.append(*iter);
        id2.append_attribute("name") = instance.c_str();
        xml_node meta = item.append_child("metadata");
        meta.append_child("virtual-network-routing-instance");
    }
    for (multimap<string, string>::const_iterator iter = connections.begin();
         iter != connections.end(); ++iter) {
        xml_node item = update.append_child("resultItem");
        xml_node id1 = item.append_child("identity");
        string instance1("routing-instance:");
        instance1.append(iter->first);
        id1.append_attribute("name") = instance1.c_str();
        xml_node id2 = item.append_child("identity");
        string instance2("routing-instance:");
        instance2.append(iter->second);
        id2.append_attribute("name") = instance2.c_str();
        xml_node meta = item.append_child("metadata");
        meta.append_child("connection");
    }
    ostringstream oss;
    xdoc.save(oss);
    return oss.str();
}

}
