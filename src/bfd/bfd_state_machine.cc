/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#include "bfd/bfd_state_machine.h"
#include "bfd/bfd_common.h"

#include <boost/statechart/event.hpp>
#include <boost/statechart/transition.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/mpl/list.hpp>

namespace mpl = boost::mpl;
namespace sc = boost::statechart;

namespace BFD {

struct EvRecvInit : sc::event< EvRecvInit > {};
struct EvRecvDown : sc::event< EvRecvDown > {};
struct EvRecvUp : sc::event< EvRecvUp > {};
struct EvRecvAdminDown : sc::event< EvRecvAdminDown > {};
struct EvTimeout : sc::event< EvTimeout > {};
// struct EvReset : sc::event< EvReset > {};

struct BFDStateAware {
    virtual BFDState getState() const = 0;
    virtual ~BFDStateAware(){}
};

struct InitState;
struct DownState;
struct UpState;
class StateMachineImpl : public StateMachine, public sc::state_machine< StateMachineImpl, InitState > {
 public:
    StateMachineImpl() {
        initiate();
    }
    void ProcessRemoteState(BFDState state) {
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
    }

    void ProcessTimeout() {
        process_event(EvTimeout());
    }

    BFDState GetState() {
        return state_cast<const BFDStateAware &>().getState();
    }
};

struct InitState : sc::simple_state< InitState, StateMachineImpl >, BFDStateAware {
  typedef mpl::list<
          sc::transition< EvRecvInit, UpState>,
          sc::transition< EvRecvDown, InitState>,
          sc::transition< EvRecvUp, UpState>,
          sc::transition< EvRecvAdminDown, DownState>,
              sc::transition< EvTimeout, DownState>
        > reactions;
    virtual BFDState getState() const { return kInit; }
    virtual ~InitState() {}
};

struct UpState : sc::simple_state< UpState, StateMachineImpl >, BFDStateAware {
    typedef mpl::list<
              sc::transition< EvRecvInit, UpState>,
              sc::transition< EvRecvDown, DownState>,
              sc::transition< EvRecvUp, UpState>,
              sc::transition< EvRecvAdminDown, DownState>,
                  sc::transition< EvTimeout, DownState>
            > reactions;
    virtual BFDState getState() const { return kUp; }
    virtual ~UpState() {}
};

struct DownState : sc::simple_state< DownState, StateMachineImpl >, BFDStateAware {
    typedef mpl::list<
              sc::transition< EvRecvInit, UpState>,
              sc::transition< EvRecvDown, InitState>,
              sc::transition< EvRecvUp, DownState>,
              sc::transition< EvRecvAdminDown, DownState>,
                  sc::transition< EvTimeout, DownState>
            > reactions;
    virtual BFDState getState() const { return kDown; }
    virtual ~DownState() {}
};

StateMachine *CreateStateMachine() {
    return new StateMachineImpl();
}

}  // namespace BFD
