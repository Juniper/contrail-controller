/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"

void RouterIdDepInit() {
}

static void NovaIntfAdd(int id, const char *name, const char *addr,
                        const char *mac) {
    IpAddress ip = Ip4Address::from_string(addr);
    VmInterface::Add(Agent::GetInstance()->GetInterfaceTable(),
                     MakeUuid(id), name, ip.to_v4(), mac, "");
}

static void NovaDel(int id) {
    VmInterface::Delete(Agent::GetInstance()->GetInterfaceTable(),
                        MakeUuid(id));
}

static void CfgIntfSync(int id, const char *cfg_str, int vn, int vm, std::string ) {
    FloatingIpConfigList list;
    uuid intf_uuid = MakeUuid(id);
    uuid vn_uuid = MakeUuid(vn);
    uuid vm_uuid = MakeUuid(vm);

    std::string cfg_name = cfg_str;

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    VmInterfaceKey *key = new VmInterfaceKey(AgentKey::RESYNC,
                                                     intf_uuid, "");
    req.key.reset(key);

    VmInterfaceData *cfg_data = new VmInterfaceData();
    InterfaceData *data = static_cast<InterfaceData *>(cfg_data);
    data->VmPortInit();

    cfg_data->cfg_name_ = cfg_name;
    cfg_data->vn_uuid_ = vn_uuid;
    cfg_data->vm_uuid_ = vm_uuid;
    cfg_data->floating_iplist_ = list;
    req.data.reset(cfg_data);
    Agent::GetInstance()->GetInterfaceTable()->Enqueue(&req);
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
};

class VrfAssignTest : public ::testing::Test {
public:
    virtual void SetUp() {
        client->Reset();
        CreateVmportEnv(input1, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
    }

    virtual void TearDown() {
        DeleteVmportEnv(input1, 1, true);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortActive(1));
        EXPECT_FALSE(VrfFind("vrf1"));
        EXPECT_FALSE(VnFind(1));
    }
};

TEST_F(VrfAssignTest, basic_1) {
    VrfAssignTable::CreateVlanReq(MakeUuid(1), "vrf2", 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 1) != NULL);

    VrfAssignTable::DeleteVlanReq(MakeUuid(1), 1);
    client->WaitForIdle();
    EXPECT_TRUE(VrfAssignTable::FindVlanReq(MakeUuid(1), 1) == NULL);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    usleep(10000);
    return ret;
}
