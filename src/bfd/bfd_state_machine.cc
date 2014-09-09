/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_state_machine.h"
#include "bfd/bfd_common.h"

#include <list>
#include <boost/optional.hpp>
#include <boost/bind.hpp>
#include <boost/statechart/event.hpp>
#include <boost/statechart/transition.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/mpl/list.hpp>

#include "base/logging.h"

namespace mpl = boost::mpl;
namespace sc = boost::statechart;

namespace BFD {

struct EvRecvInit : sc::event<EvRecvInit> {};
struct EvRecvDown : sc::event<EvRecvDown> {};
struct EvRecvUp : sc::event<EvRecvUp> {};
struct EvRecvAdminDown : sc::event<EvRecvAdminDown> {};
struct EvTimeout : sc::event<EvTimeout> {};
// struct EvReset : sc::event<EvReset> {};

struct BFDStateAware {
    virtual BFDState getState() const = 0;
    virtual ~BFDStateAware(){}
};

struct InitState;
struct DownState;
struct UpState;
class StateMachineImpl : public StateMachine,
                         public sc::state_machine<StateMachineImpl, InitState> {
 public:
    explicit StateMachineImpl(EventManager *evm) : evm_(evm) {
        initiate();
    }

    void Notify(BFDState state) {
        LOG(DEBUG, "StateMachine state: " << state);
        if (cb_.is_initialized())
            evm_->io_service()->post(boost::bind(cb_.get(), GetState()));
    }

    void ProcessRemoteState(BFDState state) {
        BFDState old_state = GetState();
        switch (state) {
            case kAdminDown:
                process_event(EvRecvAdminDown());
                break;
            case kDown:
                process_event(EvRecvDown());
                break;
            case kInit:
                process_event(EvRecvInit());
                break;
            case kUp:
                process_event(EvRecvUp());
                break;
        }
        if (old_state != GetState())
            Notify(GetState());
    }

    void ProcessTimeout() {
        BFDState old_state = GetState();
        process_event(EvTimeout());
        if (old_state != GetState())
            Notify(GetState());
    }

    BFDState GetState() {
        return state_cast<const BFDStateAware &>().getState();
    }

    void SetCallback(boost::optional<ChangeCb> cb) {
        cb_ = cb;
    }

 private:
    boost::optional<ChangeCb> cb_;
    EventManager *evm_;
};

struct InitState : sc::simple_state<InitState, StateMachineImpl>,
                   BFDStateAware {
    typedef mpl::list<
        sc::transition<EvRecvInit, UpState>,
        sc::transition<EvRecvDown, InitState>,
        sc::transition<EvRecvUp, UpState>,
        sc::transition<EvRecvAdminDown, DownState>,
        sc::transition<EvTimeout, DownState>
        > reactions;
    virtual BFDState getState() const { return kInit; }
    virtual ~InitState() {}
};

struct UpState : sc::simple_state<UpState, StateMachineImpl>, BFDStateAware {
    typedef mpl::list<
        sc::transition<EvRecvInit, UpState>,
        sc::transition<EvRecvDown, DownState>,
        sc::transition<EvRecvUp, UpState>,
        sc::transition<EvRecvAdminDown, DownState>,
        sc::transition<EvTimeout, DownState>
        > reactions;
    virtual BFDState getState() const { return kUp; }
    virtual ~UpState() {}
};

struct DownState : sc::simple_state<DownState, StateMachineImpl>,
                   BFDStateAware {
    typedef mpl::list<
        sc::transition<EvRecvInit, UpState>,
        sc::transition<EvRecvDown, InitState>,
        sc::transition<EvRecvUp, DownState>,
        sc::transition<EvRecvAdminDown, DownState>,
        sc::transition<EvTimeout, DownState>
        > reactions;
    virtual BFDState getState() const { return kDown; }
    virtual ~DownState() {}
};

StateMachine *CreateStateMachine(EventManager *evm) {
    return new StateMachineImpl(evm);
}

}  // namespace BFD
