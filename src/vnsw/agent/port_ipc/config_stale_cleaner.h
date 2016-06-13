/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_CONFIG_STALE_CLEANER_H_
#define __AGENT_CONFIG_STALE_CLEANER_H_

#include <cmn/agent.h>
#include <boost/function.hpp>

// Stale cleaner to audit & delete old configuration, on a new client connect.
// Waits for timeout, before cleaning the old configuration.
class ConfigStaleCleaner {
public:
    static const uint32_t kConfigStaleTimeout = 60 * 1000;
    typedef boost::function<void(uint32_t)> TimerCallback;

    ConfigStaleCleaner(Agent *agent, TimerCallback callback);
    void set_callback(TimerCallback callback) { audit_callback_ = callback; }
    virtual ~ConfigStaleCleaner();
    virtual void StartStaleCleanTimer(int32_t version);
    virtual bool StaleEntryTimeout(int32_t version, Timer *timer);
    void set_timeout(uint32_t timeout) { timeout_ = timeout; }
    uint32_t timeout() const { return timeout_; }
    uint32_t TimersCount() const { return running_timer_list_.size(); }

protected:
    Agent *agent_;

private:
    uint32_t timeout_;
    // list of running audit timers (one per connect)
    std::set<Timer *> running_timer_list_;
    TimerCallback audit_callback_;
    DISALLOW_COPY_AND_ASSIGN(ConfigStaleCleaner);
};

class InterfaceConfigStaleCleaner : public ConfigStaleCleaner {
public:
    InterfaceConfigStaleCleaner(Agent *agent);
    virtual ~InterfaceConfigStaleCleaner();
    virtual bool OnInterfaceConfigStaleTimeout(int32_t version);

private:
    void CfgIntfWalkDone(int32_t version);
    bool CfgIntfWalk(DBTablePartBase *partition, DBEntryBase *entry,
                     int32_t version);

    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceConfigStaleCleaner);
};

#endif /* __AGENT_CONFIG_STALE_CLEANER_H_ */
