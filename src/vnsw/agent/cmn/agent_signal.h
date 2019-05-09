//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#ifndef AGENT_SIGNAL_H_
#define AGENT_SIGNAL_H_

#include <io/process_signal.h>

class EventManager;

class AgentSignal {
 public:
    AgentSignal(EventManager *evm);
    ~AgentSignal();

    void Terminate();
    void RegisterSigHupHandler(process::Signal::SignalHandler handler);
    void RegisterDebugSigHandler(process::Signal::SignalHandler handler);

 private:
    process::Signal process_signal_;
};

#endif // AGENT_SIGNAL_H_
