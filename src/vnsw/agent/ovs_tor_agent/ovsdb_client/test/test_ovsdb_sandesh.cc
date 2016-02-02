/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <base/string_util.h>
#include <oper/vn.h>
#include <ovsdb_route_peer.h>
#include <test_ovsdb_types.h>

using namespace std;

// Has to be set externally for this introspect to work
OvsPeer *test_local_peer = NULL;

class TestOvsdbRouteExportTask : public Task {
public:
    TestOvsdbRouteExportTask(string resp_ctx, string vn_uuid, string mac,
                             string dest_ip, uint16_t count, bool deleted,
                             bool use_same_dest)
        : Task((TaskScheduler::GetInstance()->GetTaskId("db::DBTable")), 0),
        resp_(new TestOvsdbRouteExportResp()), resp_data_(resp_ctx),
        vn_uuid_(vn_uuid), mac_(mac), dest_ip_(dest_ip), count_(count),
        deleted_(deleted), same_dest_(use_same_dest) {
    }
    virtual ~TestOvsdbRouteExportTask() {}
    virtual bool Run() {
        if (test_local_peer == NULL) {
            resp_->set_success(false);
            resp_->set_error("Route Peer Not Available");
        } else {
            VnKey key(StringToUuid(vn_uuid_));
            VnEntry *vn = static_cast<VnEntry *>(
                    Agent::GetInstance()->vn_table()->FindActiveEntry(&key));
            if (vn == NULL) {
                resp_->set_success(false);
                resp_->set_error("VN not Available");
            } else {
                boost::system::error_code err;
                Ip4Address dest = Ip4Address::from_string(dest_ip_, err);
                MacAddress mac_addr(mac_);
                for (uint16_t i = 0; i < count_; i++) {
                    if (deleted_) {
                        test_local_peer->DeleteOvsRoute(vn->GetVrf(),
                                                        vn->GetVxLanId(),
                                                        mac_addr);
                    } else {
                        test_local_peer->AddOvsRoute(vn->GetVrf(),
                                                     vn->GetVxLanId(),
                                                     vn->GetName(), mac_addr,
                                                     dest);
                    }
                    // increment mac
                    if (mac_addr[5] != 0xff) {
                        mac_addr[5]++;
                    } else {
                        mac_addr[5] = 0;
                        if (mac_addr[4] != 0xff) {
                            mac_addr[4]++;
                        } else {
                            mac_addr[4] = 0;
                            if (mac_addr[3] != 0xff) {
                                mac_addr[3]++;
                            } else {
                                mac_addr[3] = 0;
                                if (mac_addr[2] != 0xff) {
                                    mac_addr[2]++;
                                } else {
                                    mac_addr[2] = 0;
                                    if (mac_addr[1] != 0xff) {
                                        mac_addr[1]++;
                                    } else {
                                        resp_->set_success(false);
                                        resp_->set_error("Failed to add Mac");
                                    }
                                }
                            }
                        }
                    }

                    if (!same_dest_) {
                        unsigned long dest_long = dest.to_ulong();
                        dest_long++;
                        // increment IP
                        dest = Ip4Address(dest_long);
                    }
                }
                resp_->set_success(true);
                resp_->set_error("Route Export Done");
            }
        }
        SendResponse();
        return true;
    }
    std::string Description() const { return "TestOvsdbRouteExportTask"; }
private:
    void SendResponse() {
        resp_->set_context(resp_data_);
        resp_->set_more(false);
        resp_->Response();
    }

    TestOvsdbRouteExportResp *resp_;
    string resp_data_;
    string vn_uuid_;
    string mac_;
    string dest_ip_;
    uint16_t count_;
    bool deleted_;
    bool same_dest_;
    DISALLOW_COPY_AND_ASSIGN(TestOvsdbRouteExportTask);
};

void TestOvsdbRouteExportReq::HandleRequest() const {
    TestOvsdbRouteExportTask *task =
        new TestOvsdbRouteExportTask(context(), get_vn_uuid(), get_mac_addr(),
                                     get_dest_ip(), get_number_of_routes(),
                                     false, get_use_same_dest_ip());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void TestOvsdbRouteWithdrawReq::HandleRequest() const {
    TestOvsdbRouteExportTask *task =
        new TestOvsdbRouteExportTask(context(), get_vn_uuid(), get_mac_addr(),
                                     get_dest_ip(), get_number_of_routes(),
                                     true, false);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

