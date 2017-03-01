/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_test_util.h"

#include <assert.h>
#include <stdio.h>

#include "base/test/task_test_util.h"
#include "db/db.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using namespace std;

namespace bgp_util {
void NetworkConfigGenerate(DB *db,
        const vector<string> &instance_names,
        const multimap<string, string> &connections,
        const vector<string> &networks,
        const vector<int> &network_ids) {
    assert(networks.empty() || instance_names.size() == networks.size());
    assert(networks.size() == network_ids.size());

    int index;
    index = 0;
    for (vector<string>::const_iterator iter = instance_names.begin();
         iter != instance_names.end(); ++iter) {
        autogen::VirtualNetworkType *vn_property =
            new autogen::VirtualNetworkType();;
        vn_property->network_id =
            network_ids.empty() ? index + 1 : network_ids[index];
        ifmap_test_util::IFMapMsgNodeAdd(db, "virtual-network",
            networks.empty() ? *iter : networks[index], 0,
            "virtual-network-properties", vn_property);
        index++;
    }
    index = 0;
    for (vector<string>::const_iterator iter = instance_names.begin();
         iter != instance_names.end(); ++iter) {
        ostringstream target;
        target << "target:64496:" << (index + 1);
        ifmap_test_util::IFMapMsgLink(db, "routing-instance", *iter,
            "route-target", target.str(), "instance-target");
        index++;
    }
    index = 0;
    for (vector<string>::const_iterator iter = instance_names.begin();
         iter != instance_names.end(); ++iter) {
        ifmap_test_util::IFMapMsgLink(db, "virtual-network",
            networks.empty() ? *iter : networks[index], "routing-instance",
            *iter, "virtual-network-routing-instance");
        index++;
    }
    for (multimap<string, string>::const_iterator iter = connections.begin();
         iter != connections.end(); ++iter) {
        ifmap_test_util::IFMapMsgLink(db, "routing-instance", iter->first,
            "routing-instance", iter->second, "connection");
    }
    task_util::WaitForIdle();
}

}
