/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include "base/util.h"
#include "base/test/task_test_util.h"
#include <iostream>
#include <rapidjson/stringbuffer.h>

#include "vncapi.h"

EventManager evm;

class Controller {
    public:
    Controller() : x(0), pc_(0),
            kill_timer_(TimerManager::CreateTimer(*evm.io_service(), "Bla")),
            poll_timer_(TimerManager::CreateTimer(*evm.io_service(), "Bla")),
            done_it_(false) {
        kill_timer_->Start(1500, boost::bind(&Controller::wait_for_done, this));
        poll_timer_->Start(5000, boost::bind(&Controller::Main, this));
        SetMore(true);

        std::string ip("10.84.14.38");// <-- good
        // std::string ip("10.84.14.39");// <-- bad
        std::string user("admin");
        std::string passwd("c0ntrail123");
        std::string tenant("admin");
        std::string proto("http");

        vnccfg_.cfg_srv_ip         = ip,
        vnccfg_.cfg_srv_port       = 8082,
        vnccfg_.ks_srv_ip          = ip,
        vnccfg_.ks_srv_port        = 35357,
        vnccfg_.protocol           = proto,
        vnccfg_.user               = user,
        vnccfg_.password           = passwd,
        vnccfg_.tenant             = tenant,

        vnc_.reset(new VncApi(&evm, &vnccfg_));
    }
#define MAXIT 3
    bool Main() {

        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("user_defined_counter");

        vnc_->GetConfig("global-system-config", ids, filters, parents, refs,
                fields, boost::bind(&Controller::UDCHandler, this, _1, _2, _3,
                    _4, _5, _6));

        done_it_ = ++pc_ > MAXIT;
        std::cout << "It : " << pc_ << std::endl;
        return !done_it_;

    }
    void Run() {
        evm.Run();
    }
    protected:
    void SetMore(bool x) { more_ = x; }
    void SetDone() { SetMore(false); }
    private:
    int x;
    int pc_;
    Timer *kill_timer_;
    Timer *poll_timer_;
    bool done_it_;
    boost::scoped_ptr<VncApi> vnc_;
    tbb::atomic<bool> more_;
    std::string resp_body_;
    VncApiConfig vnccfg_;
    bool wait_for_done() {
        std::cout << "------ entering wait ------------\n";
        if (more_) {
            //std::this_thread::sleep_for(std::chrono::seconds(1));
            //usleep(1000);
            std::cout  << "\n" << ++x << " waiting...\n";
            return true;
        }
        std::cout  << "\n" << x << " Done waiting...\n";
        task_util::WaitForIdle();
        vnc_->Stop();
        evm.Shutdown();
        std::cout  << "\n" << x << " all Done waiting...\n";
        return false;
    }
    void UDCHandler(rapidjson::Document &jdoc, boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers) {

        if (jdoc.IsObject() && jdoc.HasMember("global-system-configs")) {
            for (rapidjson::SizeType j=0;
                        j < jdoc["global-system-configs"].Size(); j++) {
                const rapidjson::Value& gsc = jdoc["global-system-configs"][j]
                            ["user_defined_counter"]["counter"];
                if (gsc.IsArray()) {
                    for (rapidjson::SizeType i = 0; i < gsc.Size(); i++)
                        std::cout << "\nname: " << gsc[i]["name"].GetString()
                            << "\npattern: " << gsc[i]["pattern"].GetString()
                            << "\n";
                } else {
                    //SetDone();
                }
            }
        }

        if (done_it_)
            SetDone();
    }
};

int main() {
    Controller c;

    //c.Main();
    c.Run();


    return 0;
}
