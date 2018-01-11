//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#include "cmn/agent_signal.h"

AgentSignal::AgentSignal(EventManager *evm) :
    process_signal_(evm, process::Signal::SignalCallbackMap(),
        std::vector<process::Signal::SignalChildHandler>(), true) {
}

AgentSignal::~AgentSignal() {
}

void AgentSignal::Terminate() {
    process_signal_.Terminate();
}

void AgentSignal::RegisterSigHupHandler(process::Signal::SignalHandler handler) {
#ifndef _WIN32
    process_signal_.RegisterHandler(SIGHUP, handler);
#endif
}
