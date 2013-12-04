/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_tun.h>

#include <db/db.h>
#include <cmn/agent_cmn.h>

#include <oper/interface_common.h>
#include <oper/mirror_table.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>

#include "vr_genetlink.h"
#include "vr_interface.h"
#include "vr_types.h"
#include "nl_util.h"

#include <uve/stats_collector.h>
#include <uve/vrouter_stats.h>
#include <uve/uve_init.h>
#include <uve/uve_client.h>

bool VrouterStatsCollector::Run() {
    UveClient::GetInstance()->SendAgentStats();
    return true;
}
