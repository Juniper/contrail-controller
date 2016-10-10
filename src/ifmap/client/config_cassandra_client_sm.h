/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_cass_config_sm_h
#define ctrlplane_cass_config_sm_h

#include <boost/statechart/state_machine.hpp>
#include <boost/system/error_code.hpp>

#include "base/queue_task.h"

namespace sc = boost::statechart;

namespace ccas_sm {
struct EvStart;
struct Idle;
}

class ConfigCassandraClient;

/*
 * This class describes a simple state machine to interact with the Cassandra
 * database server via ConfigCassandraClient.
 */
class ConfigCassandraClientSm :
        public sc::state_machine<ConfigCassandraClientSm, ccas_sm::Idle> {
public:

    enum State {
        IDLE                   = 0,
        DBINIT                 = 1,
        READY                  = 2,
        DBREAD                 = 3,
    };

    ConfigCassandraClientSm();
    void Initialize();
    ConfigCassandraClient *config_cass_client() { return config_cass_client_; }
    void set_config_cass_client(ConfigCassandraClient *client) {
        config_cass_client_ = client;
    }
    void OnStart(const ccas_sm::EvStart &event);
    void EnqueueEvent(const sc::event_base &event);
    bool DequeueEvent(boost::intrusive_ptr<const sc::event_base> &event);

    void set_state(State state);
    const std::string &StateName() const;
    const std::string &StateName(State state) const;
    const std::string &LastStateName() const;
    const std::string last_state_change_at() const;
    const std::string &last_event() const { return last_event_; }
    void set_last_event(const std::string &event);
    const std::string last_event_at() const;
    void LogSmTransition();

private:
    WorkQueue<boost::intrusive_ptr<const sc::event_base> > work_queue_;
    ConfigCassandraClient *config_cass_client_;

    State state_;
    State last_state_;
    uint64_t last_state_change_at_;
    std::string last_event_;
    uint64_t last_event_at_;
};

#endif // ctrlplane_cass_config_sm_h
