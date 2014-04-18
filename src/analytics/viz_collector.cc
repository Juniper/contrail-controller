/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "viz_collector.h"
#include <boost/asio/ip/host_name.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include "io/event_manager.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_session.h"

#include "db_handler.h"
#include "ruleeng.h" 

using std::string;
using boost::system::error_code;

VizCollector::VizCollector(EventManager *evm, unsigned short listen_port,
            std::string cassandra_ip, unsigned short cassandra_port,
            const std::string redis_uve_ip, unsigned short redis_uve_port,
            int syslog_port, bool dup, int analytics_ttl) :
    evm_(evm),
    osp_(new OpServerProxy(evm, this, redis_uve_ip, redis_uve_port)),
    db_handler_(new DbHandler(evm, boost::bind(&VizCollector::StartDbifReinit, this),
                cassandra_ip, cassandra_port, analytics_ttl, DbifGlobalName(dup))),
    ruleeng_(new Ruleeng(db_handler_.get(), osp_.get())),
    collector_(new Collector(evm, listen_port, db_handler_.get(), ruleeng_.get(),
            cassandra_ip, cassandra_port, analytics_ttl)),
    syslog_listener_(new SyslogListeners (evm,
            boost::bind(&Ruleeng::rule_execute, ruleeng_.get(), _1, _2, _3),
            db_handler_.get(), syslog_port)),
    dbif_timer_(TimerManager::CreateTimer(
            *evm_->io_service(), "Collector DbIf Timer",
            TaskScheduler::GetInstance()->GetTaskId("collector::DbIf"))) {
    error_code error;
    if (dup)
        name_ = boost::asio::ip::host_name(error) + "dup";
    else
        name_ = boost::asio::ip::host_name(error);
}

VizCollector::VizCollector(EventManager *evm, DbHandler *db_handler,
        Ruleeng *ruleeng, Collector *collector, OpServerProxy *osp) :
    evm_(evm),
    osp_(osp),
    db_handler_(db_handler),
    ruleeng_(ruleeng),
    collector_(collector),
    syslog_listener_(new SyslogListeners (evm,
            boost::bind(&Ruleeng::rule_execute, ruleeng, _1, _2, _3),
            db_handler)),
    dbif_timer_(TimerManager::CreateTimer(
            *evm_->io_service(), "Collector DbIf Timer",
            TaskScheduler::GetInstance()->GetTaskId("collector::DbIf"))) {
    error_code error;
    name_ = boost::asio::ip::host_name(error);
}

VizCollector::~VizCollector() {
    TimerManager::DeleteTimer(dbif_timer_);
}

std::string VizCollector::DbifGlobalName(bool dup) {
    std::string name;
    error_code error;
    if (dup)
        name = boost::asio::ip::host_name(error) + "dup" + ":" + "Global";
    else
        name = boost::asio::ip::host_name(error) + ":" + "Global";

    return name;
}

bool VizCollector::SendRemote(const string& destination,
        const string& dec_sandesh) {
    if (collector_){
        return collector_->SendRemote(destination, dec_sandesh);
    } else {
        return false;
    }
}

void VizCollector::WaitForIdle() {
    static const int kTimeout = 15;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    for (int i = 0; i < (kTimeout * 1000); i++) {
        if (scheduler->IsEmpty()) {
            break;
        }
        usleep(1000);
    }
}

void VizCollector::Shutdown() {
    // First shutdown collector
    collector_->Shutdown();
    WaitForIdle();

    // Wait until all connections are cleaned up.
    for (int cnt = 0; collector_->ConnectionsCount() != 0 && cnt < 15; cnt++) {
        sleep(1);
    }
    TcpServerManager::DeleteServer(collector_);

    TimerManager::DeleteTimer(dbif_timer_);
    dbif_timer_ = NULL;

    syslog_listener_->Shutdown ();
    TcpServerManager::DeleteServer(syslog_listener_); //delete syslog_listener_;

    db_handler_->UnInit(true);
    LOG(DEBUG, __func__ << " viz_collector done");
}

bool VizCollector::DbifReinitTimerExpired() {
    bool done = Init();
    // Start the timer again if initialization is not done
    return !done;
}

void VizCollector::DbifReinitTimerErrorHandler(string error_name,
        string error_message) {
    LOG(ERROR, __func__ << " " << error_name << " " << error_message);
}

void VizCollector::StartDbifReinitTimer() {
    dbif_timer_->Start(DbifReinitTime * 1000,
            boost::bind(&VizCollector::DbifReinitTimerExpired, this),
            boost::bind(&VizCollector::DbifReinitTimerErrorHandler, this,
                    _1, _2));
}

void VizCollector::StartDbifReinit() {
    db_handler_->UnInit(-1);
    StartDbifReinitTimer();
}

bool VizCollector::Init() {
    if (!db_handler_->Init(true, -1)) {
        LOG(DEBUG, __func__ << " DB Handler initialization failed");
        StartDbifReinit();
        return false;
    }
    ruleeng_->Init();
    LOG(DEBUG, __func__ << " Initialization complete");
    if (!syslog_listener_->IsRunning ()) {
        syslog_listener_->Start ();
        LOG(DEBUG, __func__ << " Initialization of syslog listener done!");
    }
    return true;
}

