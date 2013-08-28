/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "http/http_request.h"
#include "http/http_server.h"
#include "http/http_session.h"
#include "io/event_manager.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh_constants.h"
#include "sandesh/sandesh.h"
#include "sandesh/request_pipeline.h"
#include "http/http_message_test_types.h"
#include "http/route_test_types.h"
#include "config/uve/virtual_network_types.h"
#include <boost/bind.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
#include <boost/assign/list_of.hpp>
using namespace boost::assign;
using namespace std;

class ServerInfo {
public:
    void HandleRequest(HttpSession *session, const HttpRequest *request) {
	const char response[] =
"HTTP/1.1 200 OK\n"
"Content-Type: text/html; charset=UTF-8\n"
"Content-Length: 45\n"
"\n"
"<html>\n"
"<title>Server Status</title>\n"
"</html>\n"
;
	session->Send(reinterpret_cast<const u_int8_t *>(response),
		      sizeof(response), NULL);
        delete request;
    }
};

static void ServerShutdown(HttpServer *http, HttpSession *session,
			   const HttpRequest *request) {
    const char response[] =
"HTTP/1.1 OK\r\n"
	;
    session->Send(reinterpret_cast<const u_int8_t *>(response),
		  sizeof(response), NULL);
    http->event_manager()->Shutdown();
}


class HttpRequestTestData : public RequestPipeline::InstData {
public:
    HttpRequestTestData() : ctr(0) {}
    int ctr;
};


HttpRequestTestData*
HttpRequestTestFactory(int stage) {
    return new HttpRequestTestData;
}

bool
HttpRequestTestCallback(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData * dataInAndOut) {
    HttpRequestTestData* mydata =
        static_cast<HttpRequestTestData*>(dataInAndOut);

    if (!mydata) {
        LOG(DEBUG, "Callback Stage " << stage << " has no data");
        return true;
    }
    LOG(DEBUG, "Callback Stage " << stage << " ctr " << mydata->ctr);
    if (instNum == 1) {
        if ((++mydata->ctr) < 3 ) {
            sleep(1);
            return false; 
        }
        HttpRequestTestResp *resp = new(HttpRequestTestResp);
        resp->set_name("TestName");
        resp->set_num(1234);
        resp->set_context(sr->context());
        resp->Response();
    }
    sleep(3);
    return true; 
}


void
HttpRequestTest::HandleRequest() const{
    LOG(DEBUG, "Got HandleRequest! - i32Elem " << i32Elem << " descString " << descString << " ctx " << context());
    RequestPipeline::PipeSpec ps(this);

    RequestPipeline::StageSpec s1, s2, s3;
    s1.taskId_ = 100;
    s1.cbFn_ = HttpRequestTestCallback;
    s1.allocFn_ = HttpRequestTestFactory;
    s1.instances_ = list_of(-1)(-1)(20);

    s2.taskId_ = 200;
    s2.cbFn_ = HttpRequestTestCallback;
    s2.instances_ = list_of(10)(20)(30)(40);

    s3.taskId_ = 300;
    s3.cbFn_ = HttpRequestTestCallback;
    s3.allocFn_ = HttpRequestTestFactory;
    s3.instances_ = list_of(5);

    ps.stages_= list_of(s1)(s2)(s3);
    RequestPipeline rp(ps);
    return;
}

void
VNSwitchRouteReq::HandleRequest() const{
    // TODO: context should be from derived class
    LOG(DEBUG, "Got HandleRequest! - " <<
            " startRoutePrefix " << startRoutePrefix <<
            " endRoutePrefix " << endRoutePrefix <<
            " numRoutes " << numRoutes <<
            " ctx " << Sandesh::context());
    
    VNSwitchRouteResp *vsrr = new VNSwitchRouteResp();
    vsrr->set_vnSwitchId(100);
    vsrr->set_vnId(50);
    
    vector<VNSRoute> lval;
    VNSRoute vsnr;
    vsnr.prefix = 2; vsnr.desc = "two";
    lval.push_back(vsnr);
    vsnr.prefix = 3; vsnr.desc = "three";
    lval.push_back(vsnr);
    vsnr.prefix = 4; vsnr.desc = "four";
    lval.push_back(vsnr);
    vsrr->set_vnRoutes(lval);

    vector<string> vs;
    vs.push_back("XXX");
    vs.push_back("YYY");
    vsrr->set_names(vs);

    vsnr.prefix = 0; vsnr.desc = "zero";
    vsrr->set_vnMarkerRoute(vsnr);
    vsrr->set_context(context());
    if (numRoutes > 5)  vsrr->set_more(true);
    vsrr->Response();

    if (numRoutes > 5) {
        VNSContainTestResp *vctr = new VNSContainTestResp();
        vctr->set_desc("The Contained");

        VNSContained vcd1, vcd2;
        vcd1.set_magic(888);
        vcd1.set_magic1(887);
        vcd2.set_magic(999);
        vcd2.set_magic1(998);

        vector<VNSContained> vvcd;
        vvcd.push_back(vcd1);
        vvcd.push_back(vcd2);

        VNSContainer vcr1,vcr2;
        vcr1.set_magic2(8);
        vcr1.set_vnCon(vcd1);
        vcr1.set_vnLc(vvcd);

        vvcd.push_back(vcd1);
        vcr2.set_magic2(9);
        vcr2.set_vnCon(vcd2);
        vcr2.set_vnLc(vvcd);

        vctr->set_vnC(vcr1);
        vector<VNSContainer> vvc;
        vvc.push_back(vcr2);
        vvc.push_back(vcr1);
        vvc.push_back(vcr1);
        vvc.push_back(vcr2);
        vctr->set_vnL(vvc);
        vctr->set_context(context());
        vctr->set_more(true);
        vctr->Response();

        VNSwitchRouteResp *vsrr2 = new VNSwitchRouteResp();
        vsrr2->set_vnSwitchId(155);
        vsrr2->set_vnId(66);

        vsnr.prefix = 8; vsnr.desc = "ate";
        lval.push_back(vsnr);
        vsrr2->set_vnRoutes(lval);
        vsrr2->set_vnMarkerRoute(vsnr);

        vsrr2->set_context(context());
        vsrr2->Response();
    }

    return;
}

int
main(int argc, char *argv[]) {
    LoggingInit();

    ServerInfo info;
    EventManager evm;
    HttpServer http(&evm);
    http.RegisterHandler("/server-status",
	boost::bind(&ServerInfo::HandleRequest, &info, _1, _2));
    http.RegisterHandler("quitquitquit",
	boost::bind(&ServerShutdown, &http, _1, _2));
    http.Initialize(8090);
    Sandesh::InitGeneratorTest("httpd", boost::asio::ip::host_name(), &evm, 8080);
    Sandesh::SetLoggingParams(true, "HttpSession", SandeshLevel::UT_INFO);
    evm.Run();
    http.Shutdown();
}
