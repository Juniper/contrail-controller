/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_factory.h"

template <>
AgentObjectFactory *Factory<AgentObjectFactory>::singleton_ = NULL;

#include "cmn/agent_signal.h"
FACTORY_STATIC_REGISTER(AgentObjectFactory, AgentSignal,
                        AgentSignal);
