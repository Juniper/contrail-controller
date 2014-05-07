/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_scanner_h
#define vnsw_agent_interface_scanner_h

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "vr_types.h"
#include "vr_interface.h"
#include <cmn/agent_cmn.h>

// Scan DHCP Snoop entries from kernel interface snapshot
class InterfaceKScan {
public:
    InterfaceKScan(Agent *agent);
    virtual ~InterfaceKScan();

    void Init();
    bool Reset();
    void KernelInterfaceData(vr_interface_req *r);
private:
    Agent *agent_;
    Timer *timer_;
    static const uint32_t timeout_ = 180000; // 3 minutes
    DISALLOW_COPY_AND_ASSIGN(InterfaceKScan);
};

#endif // vnsw_agent_interface_scanner_h
