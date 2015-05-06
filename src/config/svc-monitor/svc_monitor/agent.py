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

import abc


class Agent(object):
    __metaclass__ = abc.ABCMeta

    def __init__(self, svc_mon, vnc_lib, cassandra, config_section):
        self._vnc_lib = vnc_lib
        self._svc_mon = svc_mon
        self._cassandra = cassandra
        self._args = config_section

    @abc.abstractmethod
    def handle_service_type(self):
        pass

    # method called just before creation of vm and vmi
    def pre_create_service_vm(self, instance_index, si, st, vm):
        pass

    # method called just after creation of vm and vmi
    def post_create_service_vm(self, instance_index, si, st, vm):
        pass
