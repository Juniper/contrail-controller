# Copyright (c) 2015 Redhat
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# @author: Sylvain Afchain


class AgentManager(object):

    def __init__(self):
        self._agents = {}

    def register_agent(self, agent):
        type = agent.handle_service_type()
        self._agents[type] = agent

    def pre_create_service_vm(self, instance_index, si, st, vm):
        agent = self._agents.get(st.params.get('service_type'))
        if agent:
            return agent.pre_create_service_vm(instance_index, si, st, vm)

    def post_create_service_vm(self, instance_index, si, st, vm):
        agent = self._agents.get(st.params.get('service_type'))
        if agent:
            return agent.post_create_service_vm(instance_index, si, st, vm)
