#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import gevent
from gevent import monkey; monkey.patch_all()
import argparse
import os
import socket
import random
from pysandesh.sandesh_base import *
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames
from vrouter.sandesh.virtual_network.ttypes import UveVirtualNetworkAgent, \
    InterVnStats, UveInterVnStats, UveVirtualNetworkAgentTrace 
from vrouter.sandesh.virtual_machine.ttypes import VmInterfaceAgent, \
    UveVirtualMachineAgent, UveVirtualMachineAgentTrace
from vrouter.vrouter.ttypes import VrouterStatsAgent, VrouterStats
from vrouter.cpuinfo import CpuInfoData

class MockGenerator(object):

    _VN_PREFIX = 'default-domain:mock-gen-test:vn'
    _VM_PREFIX = 'vm'
    _BYTES_PER_PACKET = 1024
    _OTHER_VN_PKTS_PER_SEC = 100
    _UVE_MSG_INTVL_IN_SEC = 1
    _GEVENT_SPAWN_DELAY_IN_SEC = 10

    def __init__(self, hostname, module_name, start_vn, end_vn, other_vn, \
                 collectors):
        self._module_name = module_name
        self._hostname = hostname
        self._start_vn = start_vn
        self._end_vn = end_vn
        self._num_vns = end_vn - start_vn
        self._other_vn = other_vn
        self._sandesh_instance = Sandesh()
        if not isinstance(collectors, list):
            collectors = [collectors] 
        self._collectors = collectors
    #end __init__

    def run_generator(self):
        self._sandesh_instance.init_generator(self._module_name, self._hostname,
                self._collectors, '', -1, 
                ['vrouter'])
        self._sandesh_instance.set_logging_params(enable_local_log = True,
                                                  level = SandeshLevel.SYS_DEBUG)
        send_uve_task = gevent.spawn_later(
            random.randint(0, self._GEVENT_SPAWN_DELAY_IN_SEC),
            self._send_uve_sandesh)
        cpu_info_task = gevent.spawn_later(
            random.randint(0, self._GEVENT_SPAWN_DELAY_IN_SEC),
            self._send_cpu_info)
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

    def _populate_other_vn_stats(self, other_vn, intervn_list, vn, vn_stats,
                                 in_uve_intervn_list, out_uve_intervn_list):
        other_vn_name = self._VN_PREFIX + str(other_vn)
        intervn = InterVnStats()
        intervn.other_vn = other_vn_name
        intervn.vrouter = self._module_name
        intervn.in_tpkts = random.randint(0, self._OTHER_VN_PKTS_PER_SEC * \
            self._num_vns * self._UVE_MSG_INTVL_IN_SEC)
        intervn.in_bytes = intervn.in_tpkts * random.randint(0, \
            self._BYTES_PER_PACKET)
        intervn.out_tpkts = random.randint(0, self._OTHER_VN_PKTS_PER_SEC * \
            self._num_vns * self._UVE_MSG_INTVL_IN_SEC)
        intervn.out_bytes = intervn.out_tpkts * random.randint(0, \
            self._BYTES_PER_PACKET)
        if vn in vn_stats:
            other_vn_stats = vn_stats[vn]
        else:
            other_vn_stats = None 
        if other_vn_stats is None:
            other_vn_stats = {}
            other_vn_stats[other_vn] = (intervn.in_tpkts, intervn.in_bytes, \
                intervn.out_tpkts, intervn.out_bytes)
        else:
            if other_vn in other_vn_stats:
                prev_in_tpkts, prev_in_bytes, prev_out_tpkts, prev_out_bytes = \
                        other_vn_stats[other_vn]
                new_in_tpkts = prev_in_tpkts + intervn.in_tpkts
                new_in_bytes = prev_in_bytes + intervn.in_bytes
                new_out_tpkts = prev_out_tpkts + intervn.out_tpkts
                new_out_bytes = prev_out_bytes + intervn.out_bytes
                other_vn_stats[other_vn] = (new_in_tpkts, new_in_bytes, \
                    new_out_tpkts, new_out_bytes)
            else:
                other_vn_stats[other_vn] = (intervn.in_tpkts, \
                    intervn.in_bytes, intervn.out_tpkts, intervn.out_bytes)
        vn_stats[vn] = other_vn_stats
        in_uve_intervn = UveInterVnStats()
        in_uve_intervn.other_vn = other_vn_name
        out_uve_intervn = UveInterVnStats()
        out_uve_intervn.other_vn = other_vn_name
        in_uve_intervn.tpkts, in_uve_intervn.bytes, out_uve_intervn.tpkts, \
            out_uve_intervn.bytes = other_vn_stats[other_vn]
        in_uve_intervn_list.append(in_uve_intervn)
        out_uve_intervn_list.append(out_uve_intervn)
        intervn_list.append(intervn)
    #end _populate_other_vn_stats

    def _send_uve_sandesh(self):
        vn_stats = {}
        vn_vm_list = {}
        vn_vm_list_populated = False
        vn_vm_list_sent = False
        while True:
            # Send VM list if populated and not already sent
            if vn_vm_list_populated and not vn_vm_list_sent:
                for vn in range(self._start_vn, self._end_vn):
                    vn_agent = UveVirtualNetworkAgent(virtualmachine_list = \
                                   vn_vm_list[vn])
                    vn_agent.name = self._VN_PREFIX + str(vn)
                    uve_agent_vn = UveVirtualNetworkAgentTrace( \
                        data = vn_agent, sandesh = self._sandesh_instance)
                    uve_agent_vn.send(sandesh = self._sandesh_instance)
                    gevent.sleep(self._UVE_MSG_INTVL_IN_SEC)
                vn_vm_list_sent = True
                
            other_vn = self._other_vn
            for vn in range(self._start_vn, self._end_vn):
                intervn_list = []
                in_uve_intervn_list = []
                out_uve_intervn_list = []
                # Populate inter-VN and UVE inter-VN stats for other_vn 
                self._populate_other_vn_stats(other_vn, intervn_list, vn, \
                    vn_stats, in_uve_intervn_list, out_uve_intervn_list)
                # Populate inter-VN and UVE inter-VN stats for self - vn
                self._populate_other_vn_stats(vn, intervn_list, vn, \
                    vn_stats, in_uve_intervn_list, out_uve_intervn_list)

                vn_agent = UveVirtualNetworkAgent(vn_stats = intervn_list,
                               in_stats = in_uve_intervn_list,
                               out_stats = out_uve_intervn_list)
                vn_agent.name = self._VN_PREFIX + str(vn)
                uve_agent_vn = UveVirtualNetworkAgentTrace(data = vn_agent, 
                                   sandesh = self._sandesh_instance)
                uve_agent_vn.send(sandesh = self._sandesh_instance)

                vm_if = VmInterfaceAgent()
                vm_if.name = 'p2p1'
                vm_agent = UveVirtualMachineAgent()
                vm_name = vn_agent.name + ':' + self._module_name + ':' + \
                    self._VM_PREFIX + str(vn)
                vm_agent.name = vm_name
                vm_agent.interface_list = []
                vm_agent.interface_list.append(vm_if)
                uve_agent_vm = UveVirtualMachineAgentTrace(data = vm_agent, 
                                   sandesh = self._sandesh_instance)
                uve_agent_vm.send(sandesh = self._sandesh_instance)
                # Populate VN VM list
                if not vn in vn_vm_list:
                    vn_vm_list[vn] = [vm_name]
                else:
                    vm_list = vn_vm_list[vn]
                    vm_list.append(vm_name) 
                other_vn += 1
                gevent.sleep(self._UVE_MSG_INTVL_IN_SEC)

            vn_vm_list_populated = True
    #end _send_uve_sandesh

#end class MockGenerator

class MockGeneratorTest(object):

    def __init__(self):
        self._parse_args()
    #end __init__

    def _parse_args(self):
        '''
        Eg. python mock_generator.py 
                               --num_generators 1000
                               --collectors 127.0.0.1:8086
                               --num_instances_per_generator 100
                               --num_networks 1000 
        '''
        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument("--num_generators", type=int,
            default=1000,
            help="Number of mock generators")
        parser.add_argument("--num_instances_per_generator", type=int,
            default=100,
            help="Number of instances (virtual machines) per generator")
        parser.add_argument("--num_networks", type=int,
            default=1000,
            help="Number of virtual networks")
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
        num_instances = self._args.num_instances_per_generator
        num_networks = self._args.num_networks
        module = Module.VROUTER_AGENT
        hostname = socket.gethostname()
        module_names = [ModuleNames[module] + '-' + hostname + '-' + \
            str(pid) + '-' + str(x) for x in range(ngens)]
        start_vns = [(x * num_instances) % num_networks for x in range(ngens)]
        end_vns = [((x + 1) * num_instances - 1) % num_networks + 1 \
            for x in range(ngens)]
        other_vn_adj = num_networks / 2
        other_vns = [x - other_vn_adj if x >= other_vn_adj \
            else x + other_vn_adj for x in start_vns]
        self._generators = [MockGenerator(hostname, module_names[x], \
            start_vns[x], end_vns[x], other_vns[x], \
            collectors[x % len(collectors)]) for x in range(ngens)]
    #end setup

    def run(self):
        generator_run_tasks = [gen.run_generator() for gen in self._generators]
        generator_tasks = [gen_task for gen_task_sublist in \
            generator_run_tasks for gen_task in gen_task_sublist ]
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
