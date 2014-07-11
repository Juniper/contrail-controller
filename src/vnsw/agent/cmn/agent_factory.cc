/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_factory.h"

template <>
AgentObjectFactory *Factory<AgentObjectFactory>::singleton_ = NULL;

#include "cmn/agent_signal.h"
#include "oper/ifmap_dependency_manager.h"
FACTORY_STATIC_REGISTER(AgentObjectFactory, AgentSignal,
                        AgentSignal);
FACTORY_STATIC_REGISTER(AgentObjectFactory, IFMapDependencyManager,
                        IFMapDependencyManager);
