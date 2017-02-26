#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# generator_fixture.py
#
# Python generator test fixtures
#

from gevent import monkey
monkey.patch_all()
import fixtures
import socket
import uuid
import random
import time
from util import retry
from pysandesh.sandesh_base import *
from pysandesh.util import UTCTimestampUsec
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
from sandesh.virtual_machine.ttypes import *
from sandesh.virtual_network.ttypes import *
from sandesh.flow.ttypes import *
from sandesh.alarm_test.ttypes import *
from sandesh.object_table_test.ttypes import *
from analytics_fixture import AnalyticsFixture
from generator_introspect_utils import VerificationGenerator
from opserver_introspect_utils import VerificationOpsSrv

class GeneratorFixture(fixtures.Fixture):
    _BYTES_PER_PACKET = 1024
    _PKTS_PER_SEC = 100
    _INITIAL_PKT_COUNT = 20
    _VM_IF_PREFIX = 'vhost'
    _KSECINMSEC = 1000 * 1000
    _VN_PREFIX = 'default-domain:vn'

    def __init__(self, name, collectors, logger, opserver_port,
                 start_time=None, node_type="Test",
                 hostname=socket.gethostname(), inst = "0",
                 sandesh_config=None):
        self._hostname = hostname
        self._name = name
        self._logger = logger
        self._collectors = collectors
        self._opserver_port = opserver_port
        self._start_time = start_time
        self._node_type = node_type
        self._inst = inst
        self._generator_id = self._hostname+':'+self._node_type+':'+self._name+':' + self._inst
        if sandesh_config:
            self._sandesh_config = SandeshConfig(
                sandesh_config.get('sandesh_keyfile'),
                sandesh_config.get('sandesh_certfile'),
                sandesh_config.get('sandesh_ca_cert'),
                sandesh_config.get('sandesh_ssl_enable', False),
                sandesh_config.get('introspect_ssl_enable', False))
        else:
            self._sandesh_config = None
        self.flow_vmi_uuid = str(uuid.uuid1())
    # end __init__

    def setUp(self):
        super(GeneratorFixture, self).setUp()
        self._sandesh_instance = Sandesh()
        self._http_port = AnalyticsFixture.get_free_port()
        sandesh_pkg = ['opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base',
                       'sandesh']
        self._sandesh_instance.init_generator(
            self._name, self._hostname, self._node_type, self._inst,
            self._collectors, '', self._http_port,
            sandesh_req_uve_pkg_list=sandesh_pkg, config=self._sandesh_config)
        self._sandesh_instance.set_logging_params(enable_local_log=True,
                                                  level=SandeshLevel.UT_DEBUG)
    # end setUp

    def cleanUp(self):
        self._sandesh_instance._client._connection.set_admin_state(down=True)
        super(GeneratorFixture, self).cleanUp()
    # end tearDown

    def get_generator_id(self):
        return self._generator_id
    # end get_generator_id

    def connect_to_collector(self):
        self._sandesh_instance._client._connection.set_admin_state(down=False)
    # end connect_to_collector

    def disconnect_from_collector(self):
        self._sandesh_instance._client._connection.set_admin_state(down=True)
    # end disconnect_from_collector

    @retry(delay=2, tries=5)
    def verify_on_setup(self):
        try:
            vg = VerificationGenerator('127.0.0.1', self._http_port, \
                            self._sandesh_config)
            conn_status = vg.get_collector_connection_status()
        except:
            return False
        else:
            return conn_status['status'] == "Established"
    # end verify_on_setup

    def send_flow_stat(self, flow, flow_bytes, flow_pkts, ts=None):
        self._logger.info('Sending Flow Stats')
        if flow.bytes:
            first_sample = False
            flow.diff_bytes = flow_bytes - flow.bytes
        else:
            first_sample = True
            flow.diff_bytes = flow_bytes
        if flow.packets:
            flow.diff_packets = flow_pkts - flow.packets
        else:
            flow.diff_packets = flow_pkts
        flow.bytes = flow_bytes
        flow.packets = flow_pkts
        if first_sample:
            action = flow.action
            drop_reason = flow.drop_reason
        else:
            action = drop_reason = None
        flow_data = FlowLogData(
            flowuuid=flow.flowuuid, direction_ing=flow.direction_ing,
            sourcevn=flow.sourcevn, destvn=flow.destvn,
            sourceip=flow.sourceip, destip=flow.destip,
            dport=flow.dport, sport=flow.sport,
            protocol=flow.protocol, bytes=flow.bytes,
            packets=flow.packets, diff_bytes=flow.diff_bytes,
            diff_packets=flow.diff_packets, action=action,
            sg_rule_uuid=flow.sg_rule_uuid,
            nw_ace_uuid=flow.nw_ace_uuid,
            vmi_uuid=flow.vmi_uuid,
            drop_reason=drop_reason)
        flow_object = FlowLogDataObject(flowdata=[flow_data],
                                        sandesh=self._sandesh_instance)
        # overwrite the timestamp of the flow, if specified.
        if ts:
            flow_object._timestamp = ts
        flow_object.send(sandesh=self._sandesh_instance)
        flow.samples.append(flow_object)
    # end send_flow_stat

    def generate_flow_samples(self):
        self.flows = []
        self.egress_flows = []
        self.flow_cnt = 5
        self.num_flow_samples = 0
        self.egress_num_flow_samples = 0
        self.flow_start_time = None
        self.flow_end_time = None
        self.egress_flow_start_time = None
        self.egress_flow_end_time = None
        for i in range(self.flow_cnt):
            self.flows.append(FlowLogData(flowuuid=str(uuid.uuid1()),
                direction_ing=1,
                sourcevn='domain1:admin:vn1',
                destvn='domain1:admin:vn2&>',
                sourceip=netaddr.IPAddress('10.10.10.1'),
                destip=netaddr.IPAddress('2001:db8::2:1'),
                sport=i*10+32747, dport=i+100,
                protocol=i/2, action='pass',
                sg_rule_uuid=str(uuid.uuid1()),
                nw_ace_uuid=str(uuid.uuid1()),
                vmi_uuid=self.flow_vmi_uuid))
            self.flows[i].samples = []
            self._logger.info(str(self.flows[i]))
        
        for i in range(self.flow_cnt):
            self.egress_flows.append(FlowLogData(flowuuid=str(uuid.uuid1()),
                direction_ing=0,
                destvn='domain1:admin:vn1',
                sourcevn='domain1:admin:vn2',
                destip=netaddr.IPAddress('10.10.10.1'),
                sourceip=netaddr.IPAddress('2001:db8::1:2'),
                dport=i*10+32747, sport=i+100,
                protocol=i/2, action='drop',
                sg_rule_uuid=str(uuid.uuid1()),
                nw_ace_uuid=str(uuid.uuid1()),
                vmi_uuid=self.flow_vmi_uuid,
                drop_reason='Reason'+str(i)))
            self.egress_flows[i].samples = []
            self._logger.info(str(self.egress_flows[i]))

        # 'duration' - lifetime of the flow in seconds
        # 'tdiff'    - time difference between consecutive flow samples
        # 'pdiff'    - packet increment factor
        # 'psize'    - packet size
        flow_template = [
            {'duration': 60, 'tdiff':
             5, 'pdiff': 1, 'psize': 50},
            {'duration': 30, 'tdiff': 4,
             'pdiff': 2, 'psize': 100},
            {'duration': 20, 'tdiff':
             3, 'pdiff': 3, 'psize': 25},
            {'duration': 10, 'tdiff': 2,
             'pdiff': 4, 'psize': 75},
            {'duration': 5,  'tdiff':
             1, 'pdiff': 5, 'psize': 120}
        ]
        assert(len(flow_template) == self.flow_cnt)

        # set the flow_end_time to _start_time + (max duration in
        # flow_template)
        max_duration = 0
        for fd in flow_template:
            if max_duration < fd['duration']:
                max_duration = fd['duration']
        assert(self._start_time is not None)
        self.flow_start_time = self._start_time
        self.flow_end_time = self.flow_start_time + \
            (max_duration * self._KSECINMSEC)
        assert(self.flow_end_time <= UTCTimestampUsec())

        # generate flows based on the flow template defined above
        cnt = 0
        for fd in flow_template:
            num_samples = (fd['duration'] / fd['tdiff']) +\
                bool((fd['duration'] % fd['tdiff']))
            for i in range(num_samples):
                ts = self.flow_start_time + \
                    (i * fd['tdiff'] * self._KSECINMSEC) + \
                    random.randint(1, 10000)
                pkts = (i + 1) * fd['pdiff']
                bytes = pkts * fd['psize']
                self.num_flow_samples += 1
                self.send_flow_stat(self.flows[cnt], bytes, pkts, ts)
            cnt += 1

        # set the egress_flow_start_time to flow_end_time + (max duration 
        # in flow template) 
        # set the egress_flow_end_time to egress_flow_start_time + (max 
        # duration in flow_template)
        self.egress_flow_start_time = self.flow_end_time + \
                (max_duration * self._KSECINMSEC)
        self.egress_flow_end_time = self.egress_flow_start_time + \
                (max_duration * self._KSECINMSEC)
        assert(self.egress_flow_end_time <= UTCTimestampUsec())

        # generate egress_flows based on the flow template defined above
        cnt = 0
        for fd in flow_template:
            num_samples = (fd['duration'] / fd['tdiff']) +\
                bool((fd['duration'] % fd['tdiff']))
            for i in range(num_samples):
                ts = self.egress_flow_start_time + \
                    (i * fd['tdiff'] * self._KSECINMSEC) + \
                    random.randint(1, 10000)
                pkts = (i + 1) * fd['pdiff']
                bytes = pkts * fd['psize']
                self.egress_num_flow_samples += 1
                self.send_flow_stat(self.egress_flows[cnt], bytes, pkts, ts)
            cnt += 1
   # end generate_flow_samples

    def send_vn_uve(self, vrouter, vn_id, num_vns):
        intervn_list = []
        for num in range(num_vns):
            intervn = InterVnStats()
            intervn.other_vn = self._VN_PREFIX + str(num)
            intervn.vrouter = vrouter
            intervn.in_tpkts = num
            intervn.in_bytes = num * self._BYTES_PER_PACKET
            intervn.out_tpkts = num
            intervn.out_bytes = num * self._BYTES_PER_PACKET
            intervn_list.append(intervn)
        vn_agent = UveVirtualNetworkAgent(vn_stats=intervn_list)
        vn_agent.name = self._VN_PREFIX + str(vn_id)
        uve_agent_vn = UveVirtualNetworkAgentTrace(
            data=vn_agent,
            sandesh=self._sandesh_instance)
        uve_agent_vn.send(sandesh=self._sandesh_instance)
        self._logger.info(
                'Sent UveVirtualNetworkAgentTrace:%s .. %d .. size %d' % (vn_id, num, len(vn_agent.vn_stats)))

    def generate_intervn(self):
        self.send_vn_uve(socket.gethostname(), 0, 2)
        time.sleep(1)
        self.send_vn_uve(socket.gethostname(), 1, 3)
        time.sleep(1)
        self.send_vn_uve(socket.gethostname(), 0, 3)
        time.sleep(1)

        self.vn_all_rows = {}
        self.vn_all_rows['whereclause'] = 'vn_stats.vrouter=' + socket.gethostname()
        self.vn_all_rows['rows'] = 8

        self.vn_sum_rows = {}
        self.vn_sum_rows['select'] = ['name','COUNT(vn_stats)','SUM(vn_stats.in_tpkts)']
        self.vn_sum_rows['whereclause'] = 'vn_stats.other_vn=' + self._VN_PREFIX + str(1) 
        self.vn_sum_rows['rows'] = 2

    def send_vm_uve(self, vm_id, num_vm_ifs, msg_count):
        vm_if_list = []
        vm_if_stats_list = []
        for num in range(num_vm_ifs):
            vm_if = VmInterfaceAgent()
            vm_if.name = self._VM_IF_PREFIX + str(num)
            vm_if_list.append(vm_if)

        for num in range(msg_count):
            vm_agent = UveVirtualMachineAgent(interface_list=vm_if_list)
            vm_agent.name = vm_id
            uve_agent_vm = UveVirtualMachineAgentTrace(
                data=vm_agent,
                sandesh=self._sandesh_instance)
            uve_agent_vm.send(sandesh=self._sandesh_instance)
            self._logger.info(
                'Sent UveVirtualMachineAgentTrace:%s .. %d' % (vm_id, num))
    # end send_uve_vm

    def delete_vm_uve(self, vm_id):
        vm_agent = UveVirtualMachineAgent(name=vm_id, deleted=True)
        uve_agent_vm = UveVirtualMachineAgentTrace(data=vm_agent, 
                            sandesh=self._sandesh_instance)
        uve_agent_vm.send(sandesh=self._sandesh_instance)
        self._logger.info('Delete VM UVE: %s' % (vm_id))
    # end delete_vm_uve

    def find_vm_entry(self, vm_uves, vm_id):
        if vm_uves is None:
            return False
        if type(vm_uves) is not list:
            vm_uves = [vm_uves]
        for uve in vm_uves:
            if uve['name'] == vm_id:
                return uve
        return None
    # end find_vm_entry

    @retry(delay=2, tries=5)
    def verify_vm_uve(self, vm_id, num_vm_ifs, msg_count, opserver_port=None):
        if opserver_port is not None:
            vns = VerificationOpsSrv('127.0.0.1', opserver_port)
        else:
            vns = VerificationOpsSrv('127.0.0.1', self._opserver_port)
        res = vns.get_ops_vm(vm_id)
        if res == {}:
            return False
        else:
            assert(len(res) > 0)
            self._logger.info(str(res))
            anum_vm_ifs = len(res.get_attr('Agent', 'interface_list'))
            assert anum_vm_ifs == num_vm_ifs
            for i in range(num_vm_ifs):
                vm_if_dict = res.get_attr('Agent', 'interface_list')[i]
                evm_if_name = self._VM_IF_PREFIX + str(i)
                avm_if_name = vm_if_dict['name']
                assert avm_if_name == evm_if_name
            return True
    # end verify_uve_vm

    @retry(delay=2, tries=5)
    def verify_vm_uve_cache(self, vm_id, delete=False):
        try:
            vg = VerificationGenerator('127.0.0.1', self._http_port)
            vm_uves = vg.get_uve('UveVirtualMachineAgent')
        except Exception as e:
            self._logger.info('Failed to get vm uves: %s' % (e))
            return False
        else:
            if vm_uves is None:
                self._logger.info('vm uve list empty')
                return False
            self._logger.info('%s' % (str(vm_uves)))
            vm_uve = self.find_vm_entry(vm_uves, vm_id)
            if vm_uve is None:
                return False
            if delete is True:
                try:
                    return vm_uve['deleted'] \
                                    == 'true'
                except:
                    return False
            else:
                try:
                    return vm_uve['deleted'] \
                                   == 'false'
                except:
                    return True
        return False
    # end verify_vm_uve_cache

    def send_vn_agent_uve(self, name, num_acl_rules=None, if_list=None,
                          ipkts=None, ibytes=None, opkts=None, obytes=None,
                          vm_list=None, vn_stats=None):
        vn_agent = UveVirtualNetworkAgent(name=name,
                    total_acl_rules=num_acl_rules, interface_list=if_list,
                    in_tpkts=ipkts, in_bytes=ibytes, out_tpkts=opkts,
                    out_bytes=obytes, virtualmachine_list=vm_list,
                    vn_stats=vn_stats)
        vn_uve = UveVirtualNetworkAgentTrace(data=vn_agent,
                    sandesh=self._sandesh_instance)
        self._logger.info('send uve: %s' % (vn_uve.log()))
        vn_uve.send(sandesh=self._sandesh_instance)
    # end send_vn_agent_uve

    def send_vn_config_uve(self, name, conn_nw=None, partial_conn_nw=None,
                           ri_list=None, num_acl_rules=None):
        vn_config = UveVirtualNetworkConfig(name=name,
                        connected_networks=conn_nw,
                        partially_connected_networks=partial_conn_nw,
                        routing_instance_list=ri_list,
                        total_acl_rules=num_acl_rules)
        vn_uve = UveVirtualNetworkConfigTrace(data=vn_config,
                    sandesh=self._sandesh_instance)
        self._logger.info('send uve: %s' % (vn_uve.log()))
        vn_uve.send(sandesh=self._sandesh_instance)
    # end send_vn_config_uve

    def send_vrouterinfo(self, name, b_info = False, deleted = False,
                         non_ascii = False):
        vinfo = None

        if deleted:
            vinfo = VrouterAgent(name=name,
                                 deleted = True)
        else:
            if b_info:
                build_info="testinfo"
                if non_ascii:
                    build_info += ' ' + chr(201) + chr(203) + chr(213) + ' ' + build_info
                vinfo = VrouterAgent(name=name,
                                     build_info=build_info,
                                     state="OK")
            else:
                vinfo = VrouterAgent(name=name, state="OK")
        v_uve = VrouterAgentTest(data=vinfo,
                    sandesh=self._sandesh_instance)
        self._logger.info('send uve: %s' % (v_uve.log()))
        v_uve.send(sandesh=self._sandesh_instance)

    def create_alarm(self, type, name=None, ack=None):
        alarms = []
        alarm_rules=None
        if name:
            alarm_rules = AlarmRules(or_list=[
                AlarmAndList(and_list=[
                    AlarmConditionMatch(condition=AlarmCondition(
                        operation='!=', operand1="test",
                        operand2=AlarmOperand2(json_value="\"UP\"")),
                        match=[AlarmMatch(json_operand1_value="\"DOWN\"")])])])
        alarms.append(UVEAlarmInfo(type=type, ack=ack,
            alarm_rules=alarm_rules))
        return alarms
    # end create_alarm

    def create_process_state_alarm(self, process):
        return self.create_alarm('ProcessStatus', process)
    # end create_process_state_alarm

    def send_alarm(self, name, alarms, table):
        alarm_data = UVEAlarms(name=name, alarms=alarms)
        alarm = AlarmTrace(data=alarm_data, table=table,
                           sandesh=self._sandesh_instance)
        self._logger.info('send alarm: %s' % (alarm.log()))
        alarm.send(sandesh=self._sandesh_instance)
        # store the alarm for verification
        if not hasattr(self, 'alarms'):
            self.alarms = {}
        if self.alarms.get(table) is None:
            self.alarms[table] = {}
        self.alarms[table][name] = alarm
    # end send_alarm

    def delete_alarm(self, name, table):
        alarm_data = UVEAlarms(name=name, deleted=True)
        alarm = AlarmTrace(data=alarm_data, table=table,
                           sandesh=self._sandesh_instance)
        self._logger.info('delete alarm: %s' % (alarm.log()))
        alarm.send(sandesh=self._sandesh_instance)
        del self.alarms[table][name]
    # end delete_alarm

    def send_all_sandesh_types_object_logs(self, name):
        # send all sandesh types that should be returned in the Object query.
        msg_types = []
        systemlog = ObjectTableSystemLogTest(name=name,
                        sandesh=self._sandesh_instance)
        msg_types.append(systemlog.__class__.__name__)
        self._logger.info('send systemlog: %s' % (systemlog.log()))
        systemlog.send(sandesh=self._sandesh_instance)
        objlog = ObjectTableObjectLogTest(name=name,
                    sandesh=self._sandesh_instance)
        msg_types.append(objlog.__class__.__name__)
        self._logger.info('send objectlog: %s' % (objlog.log()))
        objlog.send(sandesh=self._sandesh_instance)
        uve_data = ObjectTableUveData(name=name)
        uve = ObjectTableUveTest(data=uve_data,
                sandesh=self._sandesh_instance)
        msg_types.append(uve.__class__.__name__)
        self._logger.info('send uve: %s' % (uve.log()))
        uve.send(sandesh=self._sandesh_instance)
        alarm_data = UVEAlarms(name=name)
        alarm = AlarmTrace(data=alarm_data,
                           table='ObjectBgpRouter',
                           sandesh=self._sandesh_instance)
        msg_types.append(alarm.__class__.__name__)
        self._logger.info('send alarm: %s' % (alarm.log()))
        alarm.send(sandesh=self._sandesh_instance)
        return msg_types
    # end send_all_sandesh_types_object_logs

# end class GeneratorFixture
