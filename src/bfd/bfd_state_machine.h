/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#ifndef SRC_BFD_STATE_MACHINE_H_
#define SRC_BFD_STATE_MACHINE_H_

#include "bfd/bfd_common.h"

#include <boost/function.hpp>
#include <boost/optional.hpp>
#include <io/event_manager.h>

namespace BFD {
class StateMachine {
 public:
    typedef boost::function<void(const BFD::BFDState &new_state)> ChangeCb;

    virtual void ProcessRemoteState(BFD::BFDState state) = 0;
    virtual void ProcessTimeout() = 0;
    virtual BFD::BFDState GetState() = 0;
    virtual void SetCallback(boost::optional<ChangeCb> cb) = 0;
    virtual ~StateMachine() {}
};

StateMachine *CreateStateMachine(EventManager *evm);
}  // namespace BFD

#endif  // SRC_BFD_STATE_MACHINE_H_
