#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import gevent
from gevent import monkey; monkey.patch_all()
import argparse
import os
import socket
from pysandesh.sandesh_base import *
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames
from vrouter.sandesh.virtual_network.ttypes import UveVirtualNetworkAgent, UveVirtualNetworkAgentTrace 
from vrouter.sandesh.virtual_machine.ttypes import VmInterfaceAgent, UveVirtualMachineAgent, UveVirtualMachineAgentTrace
from vrouter.vrouter.ttypes import VrouterStatsAgent, VrouterStats
from vrouter.cpuinfo import CpuInfoData

class MockGenerator(object):

    def __init__(self, pid, instance, collectors):
        self._pid = pid
        self._instance = instance
        self._sandesh_instance = Sandesh()
        if not isinstance(collectors, list):
            collectors = [collectors] 
        self._collectors = collectors
    #end __init__

    @staticmethod
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("", 0))
        cport = cs.getsockname()[1]
        cs.close()
        return cport
    #end get_free_port

    def run_generator(self):
        module = Module.VROUTER_AGENT
        self._module_name = ModuleNames[module] + '-' + str(self._pid) + '-' + str(self._instance)
        self._sandesh_instance.init_generator(self._module_name, socket.gethostname(),
                self._collectors, '', MockGenerator.get_free_port(), 
                ['vrouter'])
        self._sandesh_instance.set_logging_params(enable_local_log = True,
                                                  level = SandeshLevel.SYS_DEBUG,
                                                  file = self._module_name + '.log')
        send_uve_task = gevent.spawn(self._send_uve_sandesh)
        cpu_info_task = gevent.spawn(self._send_cpu_info)
        return [send_uve_task, cpu_info_task]
    #end run_generator

    def _send_cpu_info(self):
        vrouter_cpu_info = CpuInfoData()
        vrouter_stats = VrouterStatsAgent()
        vrouter_stats.name = self._module_name
        while True:
            vrouter_stats.cpu_info = vrouter_cpu_info.get_cpu_info(system = False)
            vrouter_stats.cpu_share = vrouter_stats.cpu_info.cpu_share
            vrouter_stats.virt_mem = vrouter_stats.cpu_info.meminfo.virt
            stats = VrouterStats(sandesh = self._sandesh_instance,
                                 data = vrouter_stats)
            stats.send(sandesh = self._sandesh_instance)
            gevent.sleep(60)
    #end _send_cpu_info

    def _send_uve_sandesh(self):
        count = 0
        while True:
            count += 1
            
            vn_agent = UveVirtualNetworkAgent()
            vn_agent.name = 'sandesh-corp:vn45'
            uve_agent_vn = UveVirtualNetworkAgentTrace(data = vn_agent, 
                                                       sandesh = self._sandesh_instance)
            uve_agent_vn.send(sandesh = self._sandesh_instance)

            vm_if = VmInterfaceAgent()
            vm_if.name = 'vhost0'
            vm_if.in_pkts = count;
            vm_if.in_bytes = count*10;
            vm_agent = UveVirtualMachineAgent()
            vm_agent.name = 'sandesh-corp:vm-dns'
            vm_agent.interface_list = []
            vm_agent.interface_list.append(vm_if)
            uve_agent_vm = UveVirtualMachineAgentTrace(data = vm_agent, 
                                                       sandesh = self._sandesh_instance)
            uve_agent_vm.send(sandesh = self._sandesh_instance)
            
            gevent.sleep(10)
    #end _send_uve_sandesh

#end class MockGenerator

class MockGeneratorTest(object):

    def __init__(self):
        self._parse_args()
    #end __init__

    def _parse_args(self):
        '''
        Eg. python vrouter_generator_mock.py 
                               --num_generators 1000
                               --collectors 127.0.0.1:8086
        '''
        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument("--num_generators", type=int,
            default=1,
            help="Number of mock generators")
        parser.add_argument("--collectors",
            default='127.0.0.1:8086',
            help="List of Collector IP addresses in ip:port format",
            nargs="+")
        
        self._args = parser.parse_args()
        if isinstance(self._args.collectors, basestring):
            self._args.collectors = self._args.collectors.split()
    #end _parse_args

    def setup(self):
        collectors = self._args.collectors
        ngens = self._args.num_generators
        pid = os.getpid()
        self._generators = [MockGenerator(pid, x, collectors[x % len(collectors)]) for x in range(ngens)]
    #end setup

    def run(self):
        generator_run_tasks = [ gen.run_generator() for gen in self._generators ]
        generator_tasks = [ gen_task for gen_task_sublist in generator_run_tasks for gen_task in gen_task_sublist ]
        gevent.joinall(generator_tasks)
    #end run

#end class MockGeneratorTest

def main():
    test = MockGeneratorTest()
    test.setup()
    test.run()
#end main

if __name__ == '__main__':
    main()
