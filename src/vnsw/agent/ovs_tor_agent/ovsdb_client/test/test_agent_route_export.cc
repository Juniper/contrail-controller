/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <io/test/event_manager_test.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "control_node_mock.h"
#include "xml/xml_pugi.h"
#include "controller/controller_peer.h"
#include "controller/controller_export.h"
#include "controller/controller_vrf_export.h"

#include "ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h"
#include "test_ovs_agent_init.h"

extern OvsPeer *test_local_peer;
OvsPeerManager *peer_manager;

int main(int argc, char *argv[]) {
    GETUSERARGS();
    ksync_init = false;
    client = OvsTestInit(init_file, ksync_init);
    TestOvsAgentInit *init = static_cast<TestOvsAgentInit *>(client->agent_init());
    peer_manager = init->ovs_peer_manager();
    IpAddress server = Ip4Address::from_string("1.1.1.1");
    test_local_peer = peer_manager->Allocate(server);
    while (true) {
        sleep(2);
    }
    return 0;
}
