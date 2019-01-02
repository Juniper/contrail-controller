/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "agent_signal.h"

#include <boost/bind.hpp>
#include <sys/wait.h>
#include "base/logging.h"
#include "io/event_manager.h"

AgentSignal::AgentSignal(EventManager *evm)
                : signal_(*(evm->io_service())) {
    Initialize();
}

AgentSignal::~AgentSignal() {
}

void AgentSignal::RegisterHandler(SignalHandler handler) {
    default_callbacks_.push_back(handler);
}

void AgentSignal::RegisterChildHandler(SignalChildHandler handler) {
    sigchld_callbacks_.push_back(handler);
}

void AgentSignal::NotifySigChld(const boost::system::error_code &error, int sig,
                                int pid, int status) {
    for (std::vector<SignalChildHandler>::iterator it =
                    sigchld_callbacks_.begin(); it != sigchld_callbacks_.end();
                    ++it) {
        SignalChildHandler sh = *it;
        sh(error, sig, pid, status);
    }
}

void AgentSignal::NotifyDefault(const boost::system::error_code &error,
                                int sig) {
    for (std::vector<SignalHandler>::iterator it = default_callbacks_.begin();
                    it != default_callbacks_.end(); ++it) {
        SignalHandler sh = *it;
        sh(error, sig);
    }
}

void AgentSignal::HandleSig(const boost::system::error_code &error, int sig) {
    if (!error) {
        int status = 0;
        pid_t pid = 0;

        switch (sig) {
            case SIGCHLD:
                while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0) {
                    NotifySigChld(error, sig, pid, status);
                }
                break;
            default:
                NotifyDefault(error, sig);
        }
        RegisterSigHandler();
    }
}

void AgentSignal::RegisterSigHandler() {
    signal_.async_wait(boost::bind(&AgentSignal::HandleSig, this, _1, _2));
}

static void HandleTermSig(int signal, siginfo_t *act, void *si) {
    FILE *fpipe = NULL;
    char pname_query[256] = "";
    char pname[256]="";

    snprintf (pname_query, 256,"ps -p %d -o comm=", act->si_pid);
    fpipe = popen(pname_query, "r");
    if (fpipe != NULL) {
        while (fgets(pname, sizeof(pname), fpipe) != NULL) { }
        pclose(fpipe);
    }
    LOG(ERROR, "Agent Received signal " << signal
              << " from pid: " << act->si_pid << " pname: " << pname);
    exit(signal);
}

void AgentSignal::RegisterSigTermHandler() {
    sact_.sa_sigaction = &HandleTermSig;
    sact_.sa_flags = SA_SIGINFO;
    sigaction(SIGTERM, &sact_, NULL);
}

void AgentSignal::Initialize() {
    boost::system::error_code ec;

    /*
     * FIX(safchain) currently only handling SIGCHLD
     */
    signal_.add(SIGCHLD, ec);
    if (ec) {
        LOG(ERROR, "SIGCHLD registration failed");
    }
    RegisterSigHandler();
    RegisterSigTermHandler();
}

void AgentSignal::Terminate() {
    boost::system::error_code ec;
    signal_.cancel(ec);
    signal_.clear(ec);
}
