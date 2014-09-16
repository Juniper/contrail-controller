/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_kstate_h
#define vnsw_agent_test_kstate_h

#include "kstate/kstate.h"
#include "kstate/interface_kstate.h"
#include "kstate/nh_kstate.h"
#include "kstate/mpls_kstate.h"
#include "kstate/mirror_kstate.h"
#include "kstate/route_kstate.h"
#include "kstate/flow_kstate.h"
#include "kstate/vxlan_kstate.h"

class TestKStateBase {
public:
    static int handler_count_;
    static int fetched_count_;
    bool verify_;
    int expected_count_;
    int verify_idx_;

    TestKStateBase(bool verify, int ct, int id) : verify_(verify), 
                                          expected_count_(ct), verify_idx_(id) {
        fetched_count_ = 0;
        handler_count_ = 0;
    }
    virtual ~TestKStateBase() { }
    virtual void UpdateFetchCount() = 0;
    virtual void BaseHandler() {
        UpdateFetchCount();
        if (verify_) {
            EXPECT_EQ(expected_count_, fetched_count_);
        }
        handler_count_++;
    }
};

class TestIfKState: public InterfaceKState, public TestKStateBase {
public:
    TestIfKState(bool verify, int if_count, KInterfaceResp *obj, 
                 std::string resp_ctx, vr_interface_req &encoder, int id): 
                 InterfaceKState(obj, resp_ctx, encoder, id), 
                 TestKStateBase(verify, if_count, id) {}
    virtual void SendResponse() { }

    virtual void Handler() {
        BaseHandler();
        if (verify_idx_ != -1) {
            KInterfaceResp *resp = static_cast<KInterfaceResp *>(response_object_);
            EXPECT_TRUE(resp != NULL);
            if (resp) {
                vector<KInterfaceInfo> &list = 
                        const_cast<std::vector<KInterfaceInfo>&>(resp->get_if_list());
                EXPECT_EQ(1U, list.size());
                vector<KInterfaceInfo>::iterator it = list.begin();
                EXPECT_TRUE(it != list.end());
                if (it != list.end()) {
                    KInterfaceInfo intf = *it;
                    EXPECT_EQ(intf.get_idx(), verify_idx_);
                }
            }
        }
        more_context_ = NULL;
    }

    void PrintIfResp(KInterfaceResp *r) {
        vector<KInterfaceInfo> &list = 
                const_cast<std::vector<KInterfaceInfo>&>(r->get_if_list());
        vector<KInterfaceInfo>::iterator it = list.begin();
        KInterfaceInfo intf;
        while(it != list.end()) {
            intf = *it;
            LOG(DEBUG, "Intf ID " << intf.get_idx() << " Name " << intf.get_name());
            it++;
        }

    }
    static void Init(int verify_idx = -1, bool verify = false, int if_count = 0) {
        vr_interface_req req;
        KInterfaceResp *resp = new KInterfaceResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        singleton_ = new TestIfKState(verify, if_count, resp, "dummy", req, verify_idx);
        singleton_->EncodeAndSend(req);
    }

    ~TestIfKState() {
        if (response_object_) {
           response_object_->Release();
           response_object_ = NULL;
        }
    }
    virtual void UpdateFetchCount() {
        KInterfaceResp *resp = static_cast<KInterfaceResp *>(response_object_);
        if (resp) {
            vector<KInterfaceInfo> &list = 
                const_cast<std::vector<KInterfaceInfo>&>(resp->get_if_list());
            fetched_count_ += list.size();
            //PrintIfResp(resp);
        }
    }

private:
    static TestIfKState *singleton_;
};

class TestNHKState: public NHKState, public TestKStateBase {
public:
    TestNHKState(bool verify, int nh_count, KNHResp *obj, std::string resp_ctx,
                 vr_nexthop_req &encoder, int id): NHKState(obj, resp_ctx, 
                 encoder, id), TestKStateBase(verify, nh_count, id) {}
    virtual void SendResponse() { }

    virtual void Handler() {
        BaseHandler();
        if (verify_idx_ != -1) {
            KNHResp *resp = static_cast<KNHResp *>(response_object_);
            EXPECT_TRUE(resp != NULL);
            if (resp) {
                vector<KNHInfo> &list = 
                    const_cast<std::vector<KNHInfo>&>(resp->get_nh_list());
                EXPECT_EQ(1U, list.size());
                vector<KNHInfo>::iterator it = list.begin();
                KNHInfo nh;
                EXPECT_TRUE(it != list.end());
                if (it != list.end()) {
                    nh = *it;
                    EXPECT_EQ(nh.get_id(), verify_idx_);
                }
            }
        }
        more_context_ = NULL;
    }

    void PrintNHResp(KNHResp *r) {
        vector<KNHInfo> &list = 
                const_cast<std::vector<KNHInfo>&>(r->get_nh_list());
        vector<KNHInfo>::iterator it = list.begin();
        KNHInfo nh;
        while(it != list.end()) {
            nh = *it;
            LOG(DEBUG, "NH ID " << nh.get_id() << " Type " << nh.get_type());
            LOG(DEBUG, "    Encap Len " << nh.get_encap_len() << " Encap fam " 
                << nh.get_encap_family() << " vrf " << nh.get_vrf());
            it++;
        }

    }
    static void Init(int verify_idx = -1, bool verify = false, int nh_count = 0) {
        vr_nexthop_req req;
        KNHResp *resp = new KNHResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        singleton_ = new TestNHKState(verify, nh_count, resp, "dummy", req, verify_idx);
        singleton_->EncodeAndSend(req);
    }
    ~TestNHKState() {
        if (response_object_) {
           response_object_->Release();
        }
    }
    virtual void UpdateFetchCount() {
        KNHResp *resp = static_cast<KNHResp *>(response_object_);
        if (resp) {
            vector<KNHInfo> &list = 
                const_cast<std::vector<KNHInfo>&>(resp->get_nh_list());
            fetched_count_ += list.size();
            //PrintNHResp(resp);
        }
    }
private:
    static TestNHKState *singleton_;
};

class TestMplsKState: public MplsKState, public TestKStateBase {
public:
    TestMplsKState(bool ve, int lbl_count, KMplsResp *obj, std::string resp_ctx,
                   vr_mpls_req &encoder, int id): MplsKState(obj, resp_ctx, 
                   encoder, id), TestKStateBase(ve, lbl_count, id) {}
    virtual void SendResponse() { }

    virtual void Handler() {
        BaseHandler();
        if (verify_idx_ != -1) {
            KMplsResp *resp = static_cast<KMplsResp *>(response_object_);
            EXPECT_TRUE(resp != NULL);
            if (resp) {
                vector<KMplsInfo> &list = 
                    const_cast<std::vector<KMplsInfo>&>(resp->get_mpls_list());
                EXPECT_EQ(1U, list.size());
                vector<KMplsInfo>::iterator it = list.begin();
                KMplsInfo mpls;
                EXPECT_TRUE(it != list.end());
                if (it != list.end()) {
                    mpls = *it;
                    EXPECT_EQ(mpls.get_label(), verify_idx_);
                }
            }
        }
        more_context_ = NULL;
    }

    void PrintMplsResp(KMplsResp *r) {
        vector<KMplsInfo> &list = 
                const_cast<std::vector<KMplsInfo>&>(r->get_mpls_list());
        vector<KMplsInfo>::iterator it = list.begin();
        KMplsInfo mpls;
        while(it != list.end()) {
            mpls = *it;
            LOG(DEBUG, "Label " << mpls.get_label());
            it++;
        }

    }
    static void Init(int verify_idx = -1, bool verify = false, int lbl_count = 0) {
        vr_mpls_req req;
        KMplsResp *resp = new KMplsResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        singleton_ = new TestMplsKState(verify, lbl_count, resp, "dummy", req, 
                                        verify_idx);
        singleton_->EncodeAndSend(req);
    }
    ~TestMplsKState() {
        if (response_object_) {
           response_object_->Release();
        }
    }
    virtual void UpdateFetchCount() {
        KMplsResp *resp = static_cast<KMplsResp *>(response_object_);
        if (resp) {
            vector<KMplsInfo> &list = 
                const_cast<std::vector<KMplsInfo>&>(resp->get_mpls_list());
            fetched_count_ += list.size();
            //PrintMplsResp(resp);
        }
    }
private:
    static TestMplsKState *singleton_;
};

class TestMirrorKState: public MirrorKState, public TestKStateBase {
public:
    TestMirrorKState(bool ve, int count, KMirrorResp *obj, std::string resp_ctx,
                   vr_mirror_req &encoder, int id): MirrorKState(obj, resp_ctx, 
                   encoder, id), TestKStateBase(ve, count, id) {}
    virtual void SendResponse() {}

    virtual void Handler() {
        BaseHandler();
        if (verify_idx_ != -1) {
            KMirrorResp *resp = static_cast<KMirrorResp *>(response_object_);
            EXPECT_TRUE(resp != NULL);
            if (resp) {
                vector<KMirrorInfo> &list = 
                    const_cast<std::vector<KMirrorInfo>&>(resp->get_mirror_list());
                EXPECT_EQ(1U, list.size());
                vector<KMirrorInfo>::iterator it = list.begin();
                KMirrorInfo mr;
                EXPECT_TRUE(it != list.end());
                if (it != list.end()) {
                    mr = *it;
                    EXPECT_EQ(mr.get_mirr_index(), verify_idx_);
                }
            }
        }
        more_context_ = NULL;
    }

    void PrintMirrorResp(KMirrorResp *r) {
        vector<KMirrorInfo> &list = 
                const_cast<std::vector<KMirrorInfo>&>(r->get_mirror_list());
        vector<KMirrorInfo>::iterator it = list.begin();
        KMirrorInfo mr;
        while(it != list.end()) {
            mr = *it;
            LOG(DEBUG, "Mirror Idx " << mr.get_mirr_index());
            it++;
        }

    }
    static void Init(int verify_idx = -1, bool verify = false, int count = 0) {
        vr_mirror_req req;
        KMirrorResp *resp = new KMirrorResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        singleton_ = new TestMirrorKState(verify, count, resp, "dummy", req, 
                                          verify_idx);
        singleton_->EncodeAndSend(req);
    }
    ~TestMirrorKState() {
        if (response_object_) {
           response_object_->Release();
        }
    }
    virtual void UpdateFetchCount() {
        KMirrorResp *resp = static_cast<KMirrorResp *>(response_object_);
        if (resp) {
            vector<KMirrorInfo> &list = 
                const_cast<std::vector<KMirrorInfo>&>(resp->get_mirror_list());
            fetched_count_ += list.size();
            //PrintMirrorResp(resp);
        }
    }
private:
    static TestMirrorKState *singleton_;
};

class TestRouteKState: public RouteKState, public TestKStateBase {
public:
    TestRouteKState(bool vrfy, int count, KRouteResp *obj, std::string resp_ctx,
                    vr_route_req &encoder, int id): RouteKState(obj, resp_ctx, 
                    encoder, id), TestKStateBase(vrfy, count, id) {}
    virtual void SendResponse() {
        UpdateFetchCount();
        //Update the response_object_ with empty list
        KRouteResp *resp = static_cast<KRouteResp *>(response_object_);
        vector<KRouteInfo> list;
        resp->set_rt_list(list);
    }

    virtual void Handler() {
        RouteContext *rctx = NULL;

        KRouteResp *resp = static_cast<KRouteResp *>(response_object_);
        if (resp) {
            if (more_context_) {
                rctx = static_cast<RouteContext *>(more_context_);
            }   
            if (vr_response_code_ > 0) {
               /* There are more routes in Kernel. We need to query them from 
                * Kernel and send it to Sandesh.
                */
               SendResponse();
            } else {
                UpdateFetchCount();
                if (verify_) {
                    EXPECT_EQ(expected_count_, fetched_count_);
                }
                handler_count_++;
                if (rctx) {
                    delete rctx;
                    more_context_ = NULL;
                }
            }

            /* Send the next request to Kernel to query for next set of routes */
            if ((vr_response_code_ > 0) && rctx) {
                vr_route_req req;
                InitEncoder(req, rctx->vrf_id);
                req.set_rtr_marker(rctx->marker);
                req.set_rtr_marker_plen(rctx->marker_plen);
                EncodeAndSend(req);
            }
        }
    }

    static void Init(bool verify, int count = 0) {
        vr_route_req req;
        std::vector<int8_t> marker;

        req.set_rtr_marker(marker);
        KRouteResp *resp = new KRouteResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        singleton_ = new TestRouteKState(verify, count, resp, "dummy", req, 
                                         0);
        singleton_->EncodeAndSend(req);
    }
    ~TestRouteKState() {
        if (response_object_) {
           response_object_->Release();
        }
    }
    virtual void UpdateFetchCount() {
        KRouteResp *resp = static_cast<KRouteResp *>(response_object_);
        if (resp) {
            vector<KRouteInfo> &list = 
                const_cast<std::vector<KRouteInfo>&>(resp->get_rt_list());
            fetched_count_ += list.size();
            //PrintRouteResp(resp);
        }
    }
private:
    void PrintRouteResp(KRouteResp *r) {
        vector<KRouteInfo> &list = 
                const_cast<std::vector<KRouteInfo>&>(r->get_rt_list());
        vector<KRouteInfo>::iterator it = list.begin();
        KRouteInfo rt;
        while(it != list.end()) {
            rt = *it;
            LOG(DEBUG, "vrf " << rt.get_vrf_id() << " prefix_len " 
                << rt.get_prefix_len() << " prefix " << std::hex << rt.get_prefix());
            it++;
        }

    }
    static TestRouteKState *singleton_;
};

class TestFlowKState: public FlowKState, public TestKStateBase {
public:
    TestFlowKState(bool ve, int count, std::string resp_ctx, int idx) :
              FlowKState(Agent::GetInstance(), resp_ctx, idx), 
              TestKStateBase(ve, count, -1) {}
    void SendResponse(KFlowResp *resp) const {
        vector<KFlowInfo> &list =
                const_cast<std::vector<KFlowInfo>&>(resp->get_flow_list());
        fetched_count_ += list.size();
        handler_count_++;
        if (verify_) {
            EXPECT_EQ(expected_count_, fetched_count_);
        }
    }
    void SendPartialResponse() { }
    static void Init(bool verify, int idx, int count = 0) {
        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        singleton_ = new TestFlowKState(verify, count, "dummy", idx); 
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(singleton_);
    }

private:
    void UpdateFetchCount() {
    }
    static TestFlowKState *singleton_;
};

#endif //vnsw_agent_test_kstate_h
