//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#ifndef SRC_IO_PROCESS_SIGNAL_H_
#define SRC_IO_PROCESS_SIGNAL_H_

#include <signal.h>
#include <vector>
#include <map>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

class EventManager;
class ProcessSignalTest;

namespace process {

class Signal {
 public:
    typedef boost::function<
                    void(const boost::system::error_code& error, int sig,
                         pid_t pid, int status)> SignalChildHandler;
    typedef boost::function<
        void(const boost::system::error_code& error, int sig)> SignalHandler;
    typedef std::map<int, std::vector<SignalHandler> > SignalCallbackMap;

    Signal(EventManager *evm, const SignalCallbackMap &sig_callback_map,
        const std::vector<SignalChildHandler> &sigchld_callbacks,
        bool always_handle_sigchild);
    Signal(EventManager *evm, const SignalCallbackMap &sig_callback_map);
    virtual ~Signal();

    void Initialize();
    void Terminate();
    void RegisterHandler(int sig, SignalHandler handler);
    void RegisterHandler(SignalChildHandler handler);

 private:
    void RegisterSigHandler();
    void HandleSig(const boost::system::error_code& error, int sig);
    void NotifySig(const boost::system::error_code &error, int sig);
    void NotifySigChld(const boost::system::error_code &error, int sig, int pid,
                       int status);
    virtual int WaitPid(int pid, int *status, int options);

    boost::asio::signal_set signal_;
    SignalCallbackMap sig_callback_map_;
    std::vector<SignalChildHandler> sigchld_callbacks_;
    bool always_handle_sigchild_;
    friend class ::ProcessSignalTest;
};

}  // namespace process

#endif  // SRC_IO_PROCESS_SIGNAL_H_
