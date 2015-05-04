/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OPER_PROFILE_H_
#define SRC_VNSW_AGENT_OPER_PROFILE_H_
class Agent;
class Timer;

class AgentProfile {
public:
    static const uint32_t kProfileTimeout = 2000;
    AgentProfile(Agent *agent, bool enable);
    ~AgentProfile();
    bool Init();
    bool Shutdown();

    bool TimerRun();
    void Log();
 private:
    Agent *agent_;
    Timer *timer_;
    time_t start_time_;
    bool enable_;

    DISALLOW_COPY_AND_ASSIGN(AgentProfile);
};

#endif  // SRC_VNSW_AGENT_OPER_PROFILE_H_
