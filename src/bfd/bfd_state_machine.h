/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */


#ifndef BFD_STATE_MACHINE_H_
#define BFD_STATE_MACHINE_H_

#include "bfd/bfd_common.h"

namespace BFD {
    class StateMachine {
     public:
        virtual void ProcessRemoteState(BFD::BFDState state) = 0;
        virtual void ProcessTimeout() = 0;
        virtual BFD::BFDState GetState() = 0;
        virtual ~StateMachine() {}
    };

    StateMachine *CreateStateMachine();
}  // namespace BFD

#endif /* BFD_STATE_MACHINE_H_ */
