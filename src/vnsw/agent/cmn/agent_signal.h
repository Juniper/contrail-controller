/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef AGENT_SIGNAL_H_
#define AGENT_SIGNAL_H_

#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <signal.h>

class EventManager;

class AgentSignal {
 public:
    AgentSignal(EventManager *evm);
    virtual ~AgentSignal();

    typedef boost::function<
                    void(const boost::system::error_code& error, int sig,
                         pid_t pid, int status)> SignalChildHandler;
    typedef boost::function<
                    void(const boost::system::error_code& error, int sig)> SignalHandler;

    void Initialize();
    void Terminate();
    void RegisterHandler(SignalHandler handler);
    void RegisterChildHandler(SignalChildHandler handler);

 private:
    void RegisterSigHandler();
    void HandleSig(const boost::system::error_code& error, int sig);
    void NotifyDefault(const boost::system::error_code &error, int sig);
    void NotifySigChld(const boost::system::error_code &error, int sig, int pid,
                       int status);

    std::vector<SignalChildHandler> sigchld_callbacks_;
    std::vector<SignalHandler> default_callbacks_;
    boost::asio::signal_set signal_;
};

#endif /* AGENT_SIGNAL_H_ */
