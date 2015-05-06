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
                             string dest_ip)
        : Task((TaskScheduler::GetInstance()->GetTaskId("db::DBTable")), 0),
        resp_(new TestOvsdbRouteExportResp()), resp_data_(resp_ctx),
        vn_uuid_(vn_uuid), mac_(mac), dest_ip_(dest_ip) {
    }
    virtual ~TestOvsdbRouteExportTask() {}
    virtual bool Run() {
        if (test_local_peer == NULL) {
            resp_->set_success(false);
            resp_->set_error("Route Peer Not Available");
        } else {
            VnKey key(StringToUuid(vn_uuid_));
            VnEntry *vn =
                static_cast<VnEntry *>(Agent::GetInstance()->vn_table()->FindActiveEntry(&key));
            if (vn == NULL) {
                resp_->set_success(false);
                resp_->set_error("VN not Available");
            } else {
                boost::system::error_code err;
                Ip4Address dest = Ip4Address::from_string(dest_ip_, err);
                test_local_peer->AddOvsRoute(vn->GetVrf(), vn->GetVxLanId(),
                                             vn->GetName(), MacAddress(mac_), dest);
                resp_->set_success(true);
                resp_->set_error("Route Export Done");
            }
        }
        SendResponse();
        return true;
    }
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
    DISALLOW_COPY_AND_ASSIGN(TestOvsdbRouteExportTask);
};

void TestOvsdbRouteExportReq::HandleRequest() const {
    TestOvsdbRouteExportTask *task =
        new TestOvsdbRouteExportTask(context(), get_vn_uuid(), get_mac_addr(),
                                     get_dest_ip());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

