/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "viz_collector.h"
#include "ruleeng.h" 
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include "io/event_manager.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_session.h"

using std::string;
using boost::system::error_code;

VizCollector::VizCollector(EventManager *evm, unsigned short listen_port,
            std::string cassandra_ip, unsigned short cassandra_port,
            std::string redis_ip, unsigned short redis_port,
            int gen_timeout, bool dup, int analytics_ttl) :
    evm_(evm),
    osp_(new OpServerProxy(evm, this, redis_ip, redis_port, gen_timeout)),
    db_handler_(new DbHandler(evm, boost::bind(&VizCollector::StartDbifReinit, this),
                cassandra_ip, cassandra_port, analytics_ttl)),
    ruleeng_(new Ruleeng(db_handler_.get(), osp_.get())),
    collector_(new Collector(evm, listen_port, db_handler_.get(), ruleeng_.get())),
    dbif_timer_(*evm_->io_service()) {
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
    dbif_timer_(*evm->io_service()) {
    error_code error;
    name_ = boost::asio::ip::host_name(error);
}

VizCollector::~VizCollector() {
}

bool
VizCollector::SendRemote(const string& destination,
        const string& dec_sandesh) {
    if (collector_){
        return collector_->SendRemote(destination, dec_sandesh);
    } else {
        return false;
    }
}


void VizCollector::Shutdown() {
    collector_->Shutdown();
}

void VizCollector::DbifReinit(const boost::system::error_code &error) {
    if (error) {
        if (error.value() != boost::system::errc::operation_canceled) {
            LOG(INFO, "VizCollector::DbifReinit error: "
                << error.category().name()
                << " " << error.message());
        }
    }
    Init();
}

void VizCollector::StartDbifReinitTimer() {
    boost::system::error_code ec;
    dbif_timer_.expires_from_now(boost::posix_time::seconds(DbifReinitTime), ec);
    dbif_timer_.async_wait(boost::bind(&VizCollector::DbifReinit, this,
                boost::asio::placeholders::error));
}

void VizCollector::StartDbifReinit() {
    db_handler_->UnInit();
    StartDbifReinitTimer();
}

void VizCollector::Init() {
    LOG(DEBUG, "VizCollector::" << __func__ << " Begin");
    if (!db_handler_->Init()) {
        LOG(DEBUG, __func__ << "db_handler_ init failed");
        StartDbifReinit();
        return;
    }
    ruleeng_->Init();

    LOG(DEBUG, "VizCollector::" << __func__ << " Done");
}

