/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cassandra_client_sm.h"

#include <boost/bind.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/statechart/event.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/transition.hpp>
#include <boost/mpl/list.hpp>

#include "base/logging.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

using boost::system::error_code;

namespace sc = boost::statechart;
namespace mpl = boost::mpl;

namespace ccas_sm {
// Events
struct EvStart : sc::event<EvStart> {
    EvStart() { }
    static const char * Name() {
        return "EvStart";
    }
};

struct EvReadSuccess : sc::event<EvReadSuccess> {
    EvReadSuccess() { }
    static const char * Name() {
        return "EvReadSuccess";
    }
};

struct EvReadFailed : sc::event<EvReadFailed> {
    EvReadFailed() { }
    static const char * Name() {
        return "EvReadFailed";
    }
};

// states
struct Idle;
struct DbInit;
struct Ready;
struct DbRead;

// Initial state machine state
struct Idle : sc::simple_state<Idle, ConfigCassandraClientSm> {
    typedef sc::transition<EvStart, DbInit, ConfigCassandraClientSm,
                           &ConfigCassandraClientSm::OnStart> reactions;
    Idle() {
        CONFIG_CASS_SM_DEBUG(IFMapString, "Entering Idle.");
    }
};

struct DbInit : sc::state<DbInit, ConfigCassandraClientSm> {
    typedef mpl::list<
    > reactions;
    DbInit(my_context ctx) : my_base(ctx) {
    };
};

struct Ready : sc::state<Ready, ConfigCassandraClientSm> {
    typedef mpl::list<
    > reactions;
    Ready(my_context ctx) : my_base(ctx) {
    };
};

struct DbRead : sc::state<DbRead, ConfigCassandraClientSm> {
    typedef mpl::list<
    > reactions;
    DbRead(my_context ctx) : my_base(ctx) {
    };
};

} // namespace ccas_sm

ConfigCassandraClientSm::ConfigCassandraClientSm() 
    : work_queue_(
          TaskScheduler::GetInstance()->GetTaskId("config::CassClientSM"),
          0, boost::bind(&ConfigCassandraClientSm::DequeueEvent, this, _1)),
          config_cass_client_(NULL), state_(IDLE), last_state_(IDLE),
          last_state_change_at_(0), last_event_at_(0) {

}

void ConfigCassandraClientSm::Initialize() {
    initiate();
    EnqueueEvent(ccas_sm::EvStart());
}

void ConfigCassandraClientSm::OnStart(const ccas_sm::EvStart &event) {
}

void ConfigCassandraClientSm::EnqueueEvent(const sc::event_base &event) {
    work_queue_.Enqueue(event.intrusive_from_this());
}

bool ConfigCassandraClientSm::DequeueEvent(
        boost::intrusive_ptr<const sc::event_base>& event) {
    set_last_event(TYPE_NAME(*event));
    process_event(*event);
    event.reset();
    return true;
}

void ConfigCassandraClientSm::LogSmTransition() {
    CONFIG_CASS_SM_DEBUG(ConfigCassSmTransitionMessage,
                         StateName(last_state_), "===>", StateName(state_));
}

void ConfigCassandraClientSm::set_state(State state) {
    last_state_ = state_; state_ = state;
    LogSmTransition();
    last_state_change_at_ = UTCTimestampUsec();
}

// This must match exactly with ConfigCassandraClientSm::State
static const std::string state_names[] = {
    "Idle",
    "DbInit",
    "Ready",
    "DbRead",
};

const std::string &ConfigCassandraClientSm::StateName() const {
    return state_names[state_];
}

const std::string &ConfigCassandraClientSm::StateName(State state) const {
    return state_names[state];
}

const std::string &ConfigCassandraClientSm::LastStateName() const {
    return state_names[last_state_];
}

const std::string ConfigCassandraClientSm::last_state_change_at() const {
    return duration_usecs_to_string(UTCTimestampUsec() - last_state_change_at_);
}

void ConfigCassandraClientSm::set_last_event(const std::string &event) {
    last_event_ = event;
    last_event_at_ = UTCTimestampUsec();
}

const std::string ConfigCassandraClientSm::last_event_at() const {
    return duration_usecs_to_string(UTCTimestampUsec() - last_event_at_);
}

