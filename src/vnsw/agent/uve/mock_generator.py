#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import gevent
from gevent import monkey; monkey.patch_all()
import argparse
import os
import socket
import random
import math
import uuid
from netaddr import IPAddress
from pysandesh.sandesh_base import *
from pysandesh.util import UTCTimestampUsec
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, \
    NodeTypeNames
from vrouter.sandesh.virtual_network.ttypes import UveVirtualNetworkAgent, \
    InterVnStats, UveInterVnStats, UveVirtualNetworkAgentTrace 
from vrouter.sandesh.virtual_machine.ttypes import VmInterfaceAgent, \
    UveVirtualMachineAgent, UveVirtualMachineAgentTrace
from vrouter.vrouter.ttypes import VrouterStatsAgent, VrouterStats
from vrouter.cpuinfo import CpuInfoData
from vrouter.sandesh.flow.ttypes import *

class MockGenerator(object):

    _VN_PREFIX = 'default-domain:mock-gen-test:vn'
    _VM_PREFIX = 'vm'
    _BYTES_PER_PACKET = 1024
    _OTHER_VN_PKTS_PER_SEC = 1000
    _UVE_MSG_INTVL_IN_SEC = 10 
    _GEVENT_SPAWN_DELAY_IN_SEC = 10
    _FLOW_GEVENT_SPAWN_DELAY_IN_SEC = 30
    _NUM_FLOWS_IN_ITERATION = 145 * 10
    _FLOW_MSG_INTVL_IN_SEC = 1
    _FLOW_PKTS_PER_SEC = 100

    def __init__(self, hostname, module_name, node_type_name, instance_id,
                 start_vn, end_vn, other_vn,
                 num_vns, vm_iterations, collectors, ip_vns, ip_start_index,
                 num_flows_per_vm):
        self._module_name = module_name
        self._hostname = hostname
        self._node_type_name = node_type_name
        self._instance_id = instance_id
        self._start_vn = start_vn
        self._end_vn = end_vn
        self._num_vns = num_vns 
        self._other_vn = other_vn
        self._ip_vns = ip_vns
        self._ip_start_index = ip_start_index
        self._vm_iterations = vm_iterations
        self._num_flows_per_vm = num_flows_per_vm
        self._sandesh_instance = Sandesh()
        if not isinstance(collectors, list):
            collectors = [collectors] 
        self._collectors = collectors
    #end __init__

    def run_generator(self):
        self._sandesh_instance.init_generator(self._module_name, self._hostname,
            self._node_type_name, self._instance_id, self._collectors, 
            '', -1, ['vrouter']) 
        self._sandesh_instance.set_logging_params(enable_local_log = False,
                                                  level = SandeshLevel.SYS_EMERG)
        send_uve_task = gevent.spawn_later(
            random.randint(0, self._GEVENT_SPAWN_DELAY_IN_SEC),
            self._send_uve_sandesh)
        cpu_info_task = gevent.spawn_later(
            random.randint(0, self._GEVENT_SPAWN_DELAY_IN_SEC),
            self._send_cpu_info)
        send_flow_task = gevent.spawn_later(
            random.randint(5, self._FLOW_GEVENT_SPAWN_DELAY_IN_SEC),
            self._send_flow_sandesh)
        return [send_uve_task, cpu_info_task, send_flow_task]
    #end run_generator

    def _send_flow_sandesh(self):
        flows = []
        while True:
            # Populate flows if not done
            if len(flows) == 0:
                other_vn = self._other_vn
                for vn in range(self._start_vn, self._end_vn):
                    for nvm in range(self._vm_iterations):
                        for nflow in range(self._num_flows_per_vm):
                            init_packets = random.randint(1, \
                                self._FLOW_PKTS_PER_SEC)
                            init_bytes = init_packets * \
                                random.randint(1, self._BYTES_PER_PACKET)
                            sourceip = int(self._ip_vns[vn] + \
                                self._ip_start_index + nvm)
                            destip = int(self._ip_vns[other_vn] + \
                                self._ip_start_index + nvm)
                            flows.append(FlowDataIpv4(
                                flowuuid = str(uuid.uuid1()),
                                direction_ing = random.randint(0, 1),
                                sourcevn = self._VN_PREFIX + str(vn),
                                destvn = self._VN_PREFIX + str(other_vn),
                                sourceip = sourceip,
                                destip = destip,
                                sport = random.randint(0, 65535), 
                                dport = random.randint(0, 65535),
                                protocol = random.choice([6, 17, 1]),
                                setup_time = UTCTimestampUsec(),
                                packets = init_packets,
                                bytes = init_bytes,
                                diff_packets = init_packets,
                                diff_bytes = init_bytes))
                    other_vn = (other_vn + 1) % self._num_vns

            # Send the flows periodically
            flow_cnt = 0
            for flow_data in flows:
                new_packets = random.randint(1, self._FLOW_PKTS_PER_SEC)
                new_bytes = new_packets * \
                    random.randint(1, self._BYTES_PER_PACKET)
                flow_data.packets += new_packets
                flow_data.bytes += new_bytes
                flow_data.diff_packets = new_packets
                flow_data.diff_bytes = new_bytes
                flow_object = FlowDataIpv4Object(flowdata = flow_data,
                                  sandesh = self._sandesh_instance)
                flow_object.send(sandesh = self._sandesh_instance)
                flow_cnt += 1
                if flow_cnt == self._NUM_FLOWS_IN_ITERATION:
                    flow_cnt = 0
                    gevent.sleep(self._FLOW_MSG_INTVL_IN_SEC)
                else:
                    gevent.sleep(0)
    #end _send_flow_sandesh

    def _send_cpu_info(self):
        vrouter_cpu_info = CpuInfoData()
        vrouter_stats = VrouterStatsAgent()
        vrouter_stats.name = self._hostname
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
        intervn.vrouter = self._hostname
        intervn.in_tpkts = random.randint(1, self._OTHER_VN_PKTS_PER_SEC * \
            self._num_vns * self._UVE_MSG_INTVL_IN_SEC)
        intervn.in_bytes = intervn.in_tpkts * random.randint(1, \
            self._BYTES_PER_PACKET)
        intervn.out_tpkts = random.randint(1, self._OTHER_VN_PKTS_PER_SEC * \
            self._num_vns * self._UVE_MSG_INTVL_IN_SEC)
        intervn.out_bytes = intervn.out_tpkts * random.randint(1, \
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
                    gevent.sleep(random.randint(0, self._UVE_MSG_INTVL_IN_SEC))
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
                
                for nvm in range(self._vm_iterations):
                    vm_if = VmInterfaceAgent()
                    vm_if.name = 'p2p1'
                    vm_if.ip_address = str(self._ip_vns[vn] + \
                        self._ip_start_index + nvm)
                    vm_if.virtual_network = vn_agent.name
                    vm_agent = UveVirtualMachineAgent()
                    vm_name = vn_agent.name + ':' + self._hostname + ':' + \
                        self._VM_PREFIX + str(vn) + '-' + str(nvm)
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
                    gevent.sleep(random.randint(0, self._UVE_MSG_INTVL_IN_SEC))
 
                other_vn += 1
                gevent.sleep(random.randint(0, self._UVE_MSG_INTVL_IN_SEC))

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
                               --num_generators 10
                               --collectors 127.0.0.1:8086
                               --num_instances_per_generator 10
                               --num_networks 100
                               --num_flows_per_instance 10
                               --start_ip_address 1.0.0.1
                               
        '''
        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument("--num_generators", type=int,
            default=10,
            help="Number of mock generators")
        parser.add_argument("--num_instances_per_generator", type=int,
            default=10,
            help="Number of instances (virtual machines) per generator")
        parser.add_argument("--num_networks", type=int,
            default=100,
            help="Number of virtual networks")
        parser.add_argument("--collectors",
            default='127.0.0.1:8086',
            help="List of Collector IP addresses in ip:port format",
            nargs="+")
        parser.add_argument("--num_flows_per_instance", type=int,
            default=10,
            help="Number of flows per instance (virtual machine)")
        parser.add_argument("--start_ip_address",
            default="1.0.0.1",
            help="Start IP address to be used for instances")
        
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
        moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        hostname = socket.gethostname() + '-' + str(pid)
        hostnames = [hostname + '-' + str(x) for x in range(ngens)]
        gen_factor = num_networks / num_instances
        if gen_factor == 0:
            print("Number of virtual networks(%d) should be "
                "greater than number of instances per generator(%d)" % \
                (num_networks, num_instances))
            return False
        start_vns = [(x % gen_factor) * num_instances for x in range(ngens)]
        end_vns = [((x % gen_factor) + 1) * num_instances \
            for x in range(ngens)]
        other_vn_adj = num_networks / 2
        other_vns = [x - other_vn_adj if x >= other_vn_adj \
            else x + other_vn_adj for x in start_vns]
        instance_iterations = int(math.ceil(float(num_instances) / \
            num_networks))
        num_ips_per_vn = int(math.ceil(float(ngens * num_instances) / \
            num_networks))
        start_ip_address = IPAddress(self._args.start_ip_address)
        ip_vns = [start_ip_address + num_ips_per_vn * x for x in \
            range(num_networks)]
        start_ip_index = [x * num_instances / num_networks for x in \
            range(ngens)]
        self._generators = [MockGenerator(hostnames[x], moduleid, \
            node_type_name, str(x), start_vns[x], end_vns[x], other_vns[x], \
            num_networks, instance_iterations, \
            collectors[x % len(collectors)], ip_vns, \
            start_ip_index[x], self._args.num_flows_per_instance) \
            for x in range(ngens)]
        return True
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
    success = test.setup()
    if success:
        test.run()
#end main

if __name__ == '__main__':
    main()
