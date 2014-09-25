/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_kstate_util_h
#define vnsw_agent_test_kstate_util_h

#include "kstate/kstate.h"
#include "kstate/interface_kstate.h"
#include "kstate/nh_kstate.h"
#include "kstate/mpls_kstate.h"
#include "kstate/route_kstate.h"
#include "kstate/flow_kstate.h"
#include "kstate/vxlan_kstate.h"
#include <base/patricia.h>
#include "xmpp/test/xmpp_test_util.h"

class TestIntfEntry: public KInterfaceInfo {
public:
    TestIntfEntry(KInterfaceInfo &info) : KInterfaceInfo(info) {}
    TestIntfEntry(const char *name) {
        set_name(name);
    }

    class Key {
    public:
        static std::size_t Length (const TestIntfEntry *node) {
            return (node->get_name().length() << 3);
        }

        static char ByteValue(const TestIntfEntry *node, std::size_t i) {
            return node->get_name().at(i);
        }
    };

    Patricia::Node node_;
};

class TestIntfTable: public Patricia::Tree<TestIntfEntry, &TestIntfEntry::node_, TestIntfEntry::Key> {
public:
    TestIntfTable();

    ~TestIntfTable() {
        TestIntfEntry *entry;
        while ((entry = GetNext(NULL))) {
            Remove(entry);
            delete entry;
        }
    }

    bool initializing_;
};

class DumpIntfKState: public InterfaceKState {
public:
    DumpIntfKState(KInterfaceResp *obj, std::string resp_ctx, vr_interface_req &encoder, int id): 
                 InterfaceKState(obj, resp_ctx, encoder, id) {}
    virtual void SendResponse() {
        //Update the response_object_ with empty list
        KInterfaceResp *resp = static_cast<KInterfaceResp *>(response_object_);
        vector<KInterfaceInfo> list;
        resp->set_if_list(list);
    }

    virtual void Handler() {
        KInterfaceResp *resp = static_cast<KInterfaceResp *>(response_object_);
        EXPECT_TRUE(resp != NULL);
        if (resp) {
            vector<KInterfaceInfo> &list = 
                const_cast<std::vector<KInterfaceInfo>&>(resp->get_if_list());
            for (vector<KInterfaceInfo>::iterator it = list.begin();
                 it != list.end(); ++it) {
                KInterfaceInfo info = *it;
                TestIntfEntry *entry = new TestIntfEntry(info);
                if (!table_->Insert(entry)) {
                    delete entry;
                }
            }
            if (MoreData()) {
                SendNextRequest();
            } else {
                table_->initializing_ = false;
                more_context_ = NULL;
            }
        }
    }

    static void Init(TestIntfTable *table) {
        vr_interface_req req;
        KInterfaceResp *resp = new KInterfaceResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        DumpIntfKState *kstate = new DumpIntfKState(resp, "dummy", req, -1);
        kstate->table_ = table;
        kstate->EncodeAndSend(req);
    }

    ~DumpIntfKState() {
        if (response_object_) {
           response_object_->Release();
           response_object_ = NULL;
        }
    }

private:
    TestIntfTable  *table_;
};

TestIntfTable::TestIntfTable() {
    initializing_ = true;
    DumpIntfKState::Init(this);
    WAIT_FOR(10000, 10000, (initializing_ == false));
}

class TestRouteEntry: public KRouteInfo {
public:
    TestRouteEntry(KRouteInfo &info) : KRouteInfo(info) {}
    TestRouteEntry(const char *ip, const int len) {
        set_prefix(ip);
        set_prefix_len(len);
    }

    class Key {
    public:
        static std::size_t Length (const TestRouteEntry *node) {
            return node->get_prefix_len();
        }

        static char ByteValue(const TestRouteEntry *node, std::size_t i) {
            IpAddress addr = IpAddress::from_string(node->get_prefix());
            if (addr.is_v4()) {
                return addr.to_v4().to_bytes()[i];
            } else if(addr.is_v6()) {
                return addr.to_v6().to_bytes()[i];
            }
        }
    };

    Patricia::Node node_;
};

class TestRouteTable: public Patricia::Tree<TestRouteEntry, &TestRouteEntry::node_, TestRouteEntry::Key> {
public:
    TestRouteTable(int vrf_id);

    ~TestRouteTable() {
        TestRouteEntry *entry;
        while ((entry = GetNext(NULL))) {
            Remove(entry);
            delete entry;
        }
    }

    bool initializing_;
};

class DumpRouteKState: public RouteKState {
public:
    DumpRouteKState(KRouteResp *obj, std::string resp_ctx, vr_route_req &encoder, int id): 
                    RouteKState(obj, resp_ctx, encoder, id) {}
    virtual void SendResponse() {
        //Update the response_object_ with empty list
        KRouteResp *resp = static_cast<KRouteResp *>(response_object_);
        vector<KRouteInfo> list;
        resp->set_rt_list(list);
    }

    virtual void Handler() {
        KRouteResp *resp = static_cast<KRouteResp *>(response_object_);
        EXPECT_TRUE(resp != NULL);
        if (resp) {
            vector<KRouteInfo> &list = 
                const_cast<std::vector<KRouteInfo>&>(resp->get_rt_list());
            for (vector<KRouteInfo>::iterator it = list.begin();
                 it != list.end(); ++it) {
                KRouteInfo info = *it;
                TestRouteEntry *entry = new TestRouteEntry(info);
                if (!table_->Insert(entry)) {
                    delete entry;
                }
            }
            if (MoreData()) {
                SendNextRequest();
            } else {
                table_->initializing_ = false;
                RouteContext *rctx = static_cast<RouteContext *>(more_context_);
                if (rctx) {
                    delete rctx;
                    more_context_ = NULL;
                }
            }
        }
    }

    static void Init(TestRouteTable *table, int vrf_id) {
        vr_route_req req;
        KRouteResp *resp = new KRouteResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        DumpRouteKState *kstate = new DumpRouteKState(resp, "dummy", req, vrf_id);
        kstate->table_ = table;
        kstate->EncodeAndSend(req);
    }

    ~DumpRouteKState() {
        if (response_object_) {
           response_object_->Release();
           response_object_ = NULL;
        }
    }

private:
    TestRouteTable  *table_;
};

TestRouteTable::TestRouteTable(int vrf_id) {
    initializing_ = true;
    DumpRouteKState::Init(this, vrf_id);
    WAIT_FOR(10000, 10000, (initializing_ == false));
}

class TestNhTable: public std::map<int, KNHInfo *> {
public:
    TestNhTable();

    ~TestNhTable() {
        iterator it;
        while ((it = begin()) != end()) {
            KNHInfo *info = it->second;
            delete info;
            erase(it);
        }
    }

    bool initializing_;
};

class DumpNhKState: public NHKState {
public:
    DumpNhKState(KNHResp *obj, std::string resp_ctx, vr_nexthop_req &encoder, int id): 
                 NHKState(obj, resp_ctx, encoder, id) {}
    virtual void SendResponse() {
        //Update the response_object_ with empty list
        KNHResp *resp = static_cast<KNHResp *>(response_object_);
        vector<KNHInfo> list;
        resp->set_nh_list(list);
    }

    virtual void Handler() {
        KNHResp *resp = static_cast<KNHResp *>(response_object_);
        EXPECT_TRUE(resp != NULL);
        if (resp) {
            vector<KNHInfo> &list = 
                const_cast<std::vector<KNHInfo>&>(resp->get_nh_list());
            for (vector<KNHInfo>::iterator it = list.begin();
                 it != list.end(); ++it) {
                KNHInfo info = *it;
                KNHInfo *entry = new KNHInfo(info);
                std::pair<TestNhTable::iterator, bool> ret;
                ret = table_->insert(std::pair<int, KNHInfo *>(entry->get_id(), entry));
                if (ret.second == false) {
                    delete entry;
                }
            }
            if (MoreData()) {
                SendNextRequest();
            } else {
                table_->initializing_ = false;
                more_context_ = NULL;
            }
        }
    }

    static void Init(TestNhTable *table) {
        vr_nexthop_req req;
        KNHResp *resp = new KNHResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        DumpNhKState *kstate = new DumpNhKState(resp, "dummy", req, -1);
        kstate->table_ = table;
        kstate->EncodeAndSend(req);
    }

    ~DumpNhKState() {
        if (response_object_) {
           response_object_->Release();
           response_object_ = NULL;
        }
    }

private:
    TestNhTable  *table_;
};

TestNhTable::TestNhTable() {
    initializing_ = true;
    DumpNhKState::Init(this);
    WAIT_FOR(10000, 10000, (initializing_ == false));
}

class TestMplsTable: public std::map<int, KMplsInfo *> {
public:
    TestMplsTable();

    ~TestMplsTable() {
        iterator it;
        while ((it = begin()) != end()) {
            KMplsInfo *info = it->second;
            delete info;
            erase(it);
        }
    }

    bool initializing_;
};

class DumpMplsKState: public MplsKState {
public:
    DumpMplsKState(KMplsResp *obj, std::string resp_ctx, vr_mpls_req &encoder, int id): 
                 MplsKState(obj, resp_ctx, encoder, id) {}
    virtual void SendResponse() {
        //Update the response_object_ with empty list
        KMplsResp *resp = static_cast<KMplsResp *>(response_object_);
        vector<KMplsInfo> list;
        resp->set_mpls_list(list);
    }

    virtual void Handler() {
        KMplsResp *resp = static_cast<KMplsResp *>(response_object_);
        EXPECT_TRUE(resp != NULL);
        if (resp) {
            vector<KMplsInfo> &list = 
                const_cast<std::vector<KMplsInfo>&>(resp->get_mpls_list());
            for (vector<KMplsInfo>::iterator it = list.begin();
                 it != list.end(); ++it) {
                KMplsInfo info = *it;
                KMplsInfo *entry = new KMplsInfo(info);
                std::pair<TestMplsTable::iterator, bool> ret;
                ret = table_->insert(std::pair<int, KMplsInfo *>(entry->get_label(), entry));
                if (ret.second == false) {
                    delete entry;
                }
            }
            if (MoreData()) {
                SendNextRequest();
            } else {
                table_->initializing_ = false;
                more_context_ = NULL;
            }
        }
    }

    static void Init(TestMplsTable *table) {
        vr_mpls_req req;
        KMplsResp *resp = new KMplsResp();

        // The following object is deleted in KStateIoContext::Handler() 
        // after the Handler is invoked.
        DumpMplsKState *kstate = new DumpMplsKState(resp, "dummy", req, -1);
        kstate->table_ = table;
        kstate->EncodeAndSend(req);
    }

    ~DumpMplsKState() {
        if (response_object_) {
           response_object_->Release();
           response_object_ = NULL;
        }
    }

private:
    TestMplsTable  *table_;
};

TestMplsTable::TestMplsTable() {
    initializing_ = true;
    DumpMplsKState::Init(this);
    WAIT_FOR(10000, 10000, (initializing_ == false));
}

#endif //vnsw_agent_test_kstate_util_h
