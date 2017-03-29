#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# Opserver
#
# Operational State Server for VNC
#

from gevent import monkey
monkey.patch_all()
import sys
import json
import socket
import time
import copy
import traceback
import signal
import logging
logging.getLogger('kafka').addHandler(logging.StreamHandler())
logging.getLogger('kafka').setLevel(logging.WARNING)
logging.getLogger('kazoo').addHandler(logging.StreamHandler())
logging.getLogger('kazoo').setLevel(logging.WARNING)
try:
    from collections import OrderedDict
except ImportError:
    # python 2.6 or earlier, use backport
    from ordereddict import OrderedDict
from pysandesh.sandesh_base import *
from pysandesh.connection_info import ConnectionState
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh_alarm.ttypes import SandeshAlarmAckResponseCode
import sandesh.viz.constants as viz_constants
from sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import AlarmTrace, \
    UVEAlarms, UVEAlarmInfo, UVEAlarmConfig, AlarmOperand2, AlarmCondition, \
    AlarmMatch, AlarmConditionMatch, AlarmAndList, AlarmRules
from sandesh.analytics.ttypes import *
from sandesh.nodeinfo.ttypes import NodeStatusUVE, NodeStatus
from sandesh.nodeinfo.cpuinfo.ttypes import *
from sandesh.nodeinfo.process_info.ttypes import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, CategoryNames,\
     ModuleCategoryMap, Module2NodeType, NodeTypeNames, ModuleIds,\
     INSTANCE_ID_DEFAULT, COLLECTOR_DISCOVERY_SERVICE_NAME,\
     ALARM_GENERATOR_SERVICE_NAME
from alarmgen_cfg import CfgParser
from uveserver import UVEServer
from partition_handler import PartitionHandler, UveStreamProc
from alarmgen_config_handler import AlarmGenConfigHandler
from sandesh.alarmgen_ctrl.ttypes import PartitionOwnershipReq, \
    PartitionOwnershipResp, PartitionStatusReq, UVECollInfo, UVEGenInfo, \
    PartitionStatusResp, UVETableAlarmReq, UVETableAlarmResp, \
    AlarmgenTrace, UVEKeyInfo, UVETypeCount, UVETypeInfo, AlarmgenStatusTrace, \
    AlarmgenStatus, AlarmgenStats, AlarmgenPartitionTrace, \
    AlarmgenPartition, AlarmgenPartionInfo, AlarmgenUpdate, \
    UVETableInfoReq, UVETableInfoResp, UVEObjectInfo, UVEStructInfo, \
    UVETablePerfReq, UVETablePerfResp, UVETableInfo, UVETableCount, \
    UVEAlarmStateMachineInfo, UVEAlarmState, UVEAlarmOperState,\
    AlarmStateChangeTrace, UVEQTrace, AlarmConfig, AlarmConfigRequest, \
    AlarmConfigResponse

from sandesh.discovery.ttypes import CollectorTrace
from opserver_util import ServicePoller
from stevedore import hook, extension
from pysandesh.util import UTCTimestampUsec
from libpartition.libpartition import PartitionClient
import discoveryclient.client as client 
from kafka import KafkaClient, SimpleProducer
import redis
from collections import namedtuple

OutputRow = namedtuple("OutputRow",["key","typ","val"])

class AGTabStats(object):
    """ This class is used to store per-UVE-table information
        about the time taken and number of instances when
        a UVE was retrieved, published or evaluated for alarms
    """
    def __init__(self):
        self.reset()

    def record_get(self, get_time):
        self.get_time += get_time
        self.get_n += 1

    def record_pub(self, get_time):
        self.pub_time += get_time
        self.pub_n += 1

    def record_call(self, get_time):
        self.call_time += get_time
        self.call_n += 1
    
    def get_result(self):
        if self.get_n:
            return self.get_time / self.get_n
        else:
            return 0

    def pub_result(self):
        if self.pub_n:
            return self.pub_time / self.pub_n
        else:
            return 0

    def call_result(self):
        if self.call_n:
            return self.call_time / self.call_n
        else:
            return 0

    def reset(self):
        self.call_time = 0
        self.call_n = 0
        self.get_time = 0
        self.get_n = 0
        self.pub_time = 0
        self.pub_n = 0
        
    
class AGKeyInfo(object):
    """ This class is used to maintain UVE contents
    """

    def __init__(self, part):
        self._part = part
        # key of struct name, value of content dict

        self.current_dict = {}
        self.update({})
        
    def update_single(self, typ, val):
        # A single UVE struct has changed
        # If the UVE has gone away, the val is passed in as None

        self.set_removed = set()
        self.set_added = set()
        self.set_changed = set()
        self.set_unchanged = self.current_dict.keys()

        if typ in self.current_dict:
            # the "added" set must stay empty in this case
            if val is None:
                self.set_unchanged.remove(typ)
                self.set_removed.add(typ)
                del self.current_dict[typ]
            else:
                # both "added" and "removed" will be empty
                if val != self.current_dict[typ]:
                    self.set_unchanged.remove(typ)
                    self.set_changed.add(typ)
                    self.current_dict[typ] = val
        else:
            if val != None:
                self.set_added.add(typ)
                self.current_dict[typ] = val

    def update(self, new_dict):
        # A UVE has changed, and we have the entire new 
        # content of the UVE available in new_dict
        set_current = set(new_dict.keys())
        set_past = set(self.current_dict.keys())
        set_intersect = set_current.intersection(set_past)

        self.set_added = set_current - set_intersect
        self.set_removed = set_past - set_intersect
        self.set_changed = set()
        self.set_unchanged = set()
        for o in set_intersect:
            if new_dict[o] != self.current_dict[o]:
                self.set_changed.add(o)
            else:
                self.set_unchanged.add(o)
        self.current_dict = new_dict

    def values(self):
        return self.current_dict

    def added(self):
        return self.set_added

    def removed(self):
        return self.set_removed

    def changed(self):
        return self.set_changed

    def unchanged(self):
        return self.set_unchanged

class AlarmProcessor(object):

    def __init__(self, logger):
        self.uve_alarms = {}
        self._logger = logger
        self.ActiveTimer = {}
        self.IdleTimer = {}
        self.FreqExceededCheck = {}
        self.FreqCheck_Times = {}
        self.FreqCheck_Seconds = {}

    def process_alarms(self, alarm_fqname, alarm, uv, local_uve):
        if not alarm.is_enabled():
            return
        sev = alarm.severity()
        if not uv in self.ActiveTimer:
            self.ActiveTimer[uv] = {}
        self.ActiveTimer[uv][alarm_fqname] = alarm.ActiveTimer()
        if not uv in self.IdleTimer:
            self.IdleTimer[uv] = {}
        self.IdleTimer[uv][alarm_fqname] = alarm.IdleTimer()
        if not uv in self.FreqExceededCheck:
            self.FreqExceededCheck[uv] = {}
        self.FreqExceededCheck[uv][alarm_fqname] = alarm.FreqExceededCheck()
        if not uv in self.FreqCheck_Times:
            self.FreqCheck_Times[uv] = {}
        self.FreqCheck_Times[uv][alarm_fqname] = alarm.FreqCheck_Times()
        if not uv in self.FreqCheck_Seconds:
            self.FreqCheck_Seconds[uv] = {}
        self.FreqCheck_Seconds[uv][alarm_fqname] = alarm.FreqCheck_Seconds()

        try:
            # __call__ method overrides the generic alarm processing code.
            if hasattr(alarm, '__call__'):
                or_list = alarm.__call__(uv, local_uve)
            else:
                or_list = self._evaluate_uve_for_alarms(
                    alarm.config(), uv, local_uve)
            self._logger.debug("Alarm[%s] %s: %s" %
                (uv, alarm_fqname, str(or_list)))
            if or_list:
                self.uve_alarms[alarm_fqname] = UVEAlarmInfo(type=alarm_fqname,
                    severity=sev, timestamp=0, token="",
                    alarm_rules=AlarmRules(or_list),
                    description=alarm.description(), ack=False)
	except Exception as ex:
	    template = "Exception {0} in Alarm Processing. Arguments:\n{1!r}"
	    messag = template.format(type(ex).__name__, ex.args)
            self._logger.error("%s\n UVE:[%s]:%s\n Alarm config: %s\n traceback %s" % \
                (messag, uv, str(local_uve), alarm.config().alarm_rules,
                traceback.format_exc()))
            self.uve_alarms[alarm_fqname] = UVEAlarmInfo(type=alarm_fqname,
                    severity=sev, timestamp=0, token="",
                    alarm_rules=AlarmRules(None),
                    description=alarm.description(), ack=False)
    # end process_alarms

    def _get_uve_attribute(self, tuve, attr_list, uve_path=None):
        if uve_path is None:
            uve_path = []
        if tuve is None or not attr_list:
            return {'value': tuve, 'uve_path': uve_path,
                    'status': False if len(attr_list) else True}
        if isinstance(tuve, dict):
            if attr_list[0] in ('*', '__value'):
                return [self._get_uve_attribute(val, attr_list[1:],
                        uve_path+[{key: val}]) \
                        for key, val in tuve.iteritems()]
            elif attr_list[0] == '__key':
                return [self._get_uve_attribute(key, attr_list[1:],
                        uve_path+[{key: val}]) \
                        for key, val in tuve.iteritems()]
            else:
                tuve = tuve.get(attr_list[0])
                uve_path.append({attr_list[0]: tuve})
                return self._get_uve_attribute(tuve, attr_list[1:], uve_path)
        elif isinstance(tuve, list):
            return [self._get_uve_attribute(elem, attr_list,
                    uve_path+[{'__list_element__': elem}]) \
                    for elem in tuve]
        elif isinstance(tuve, str):
            try:
                json_elem = json.loads(tuve)
            except ValueError:
                return {'value': None, 'uve_path': uve_path, 'status': False}
            else:
                return self._get_uve_attribute(json_elem, attr_list, uve_path)
    # end _get_uve_attribute

    def _get_operand_value(self, uve, operand):
        attr_list = operand.split('.')
        return self._get_uve_attribute(uve, attr_list)
    # end _get_operand_value

    def _get_json_value(self, val):
        try:
            tval = json.loads(val)
        except (ValueError, TypeError):
            return json.dumps(val)
        else:
            return val
    # end _get_json_value

    def _get_attribute_from_uve_path(self, attr, uve_path):
        attr_list = attr.split('.')
        ai = ui = 0
        pnode = uve_path[ui]
        while (ai < len(attr_list) and ui < len(uve_path)):
            if attr_list[ai] == '__key':
                return uve_path[ui].iterkeys().next()
            elif attr_list[ai] == '__value':
                return uve_path[ui].itervalues().next()
            if attr_list[ai] != '*' and attr_list[ai] not in uve_path[ui]:
                break
            pnode = uve_path[ui]
            ui += 1
            ai += 1
            if len(uve_path) > ui and '__list_element__' in uve_path[ui]:
                pnode = uve_path[ui]
                ui += 1
        if not ui:
            return None
        val = pnode.itervalues().next()
        for a in attr_list[ai:]:
            if val is None or not isinstance(val, dict):
                return None
            val = val.get(a)
        return val
    # end _get_attribute_from_uve_path

    def _get_json_variables(self, uve, exp, operand1_val,
                            operand2_val, is_operand2_json_val):
        json_vars = {}
        for var in exp.variables:
            p1 = os.path.commonprefix([exp.operand1.split('.'),
                var.split('.')])
            if not is_operand2_json_val:
                p2 = os.path.commonprefix(
                    [exp.operand2.uve_attribute.split('.'), var.split('.')])
                if p1 > p2:
                    var_val = self._get_attribute_from_uve_path(var,
                        operand1_val['uve_path'])
                else:
                    var_val = self._get_attribute_from_uve_path(var,
                        operand2_val['uve_path'])
            else:
                var_val = self._get_attribute_from_uve_path(var,
                    operand1_val['uve_path'])
            json_vars[var] = self._get_json_value(var_val)
        return json_vars
    # end _get_json_variables

    def _compare_operand_vals(self, val1, val2, operation):
        try:
            val1 = json.loads(val1)
        except (TypeError, ValueError):
            pass
        try:
            val2 = json.loads(val2)
        except (TypeError, ValueError):
            pass
        if operation == '==':
            return val1 == val2
        elif operation == '!=':
            return val1 != val2
        elif operation == '<':
            return val1 < val2
        elif operation == '<=':
            return val1 <= val2
        elif operation == '>':
            return val1 > val2
        elif operation == '>=':
            return val1 >= val2
        elif operation == 'in':
            if not isinstance(val2, list):
                return False
            return val1 in val2
        elif operation == 'not in':
            if not isinstance(val2, list):
                return True
            return val1 not in val2
        elif operation == 'range':
            return val2[0] <= val1 <= val2[1]
        elif operation == 'size==':
            if not isinstance(val1, list):
                return False
            return len(val1) == val2
        elif operation == 'size!=':
            if not isinstance(val1, list):
                return True
            return len(val1) != val2
    # end _compare_operand_vals

    def _get_alarm_match(self, uve, exp, operand1_val, operand2_val,
                         is_operand2_json_val):
        json_vars = self._get_json_variables(uve, exp, operand1_val,
            operand2_val, is_operand2_json_val)
        json_operand1_val = self._get_json_value(operand1_val['value'])
        if not is_operand2_json_val:
            json_operand2_val = self._get_json_value(operand2_val['value'])
        else:
            json_operand2_val = None
        return AlarmMatch(json_operand1_value=json_operand1_val,
            json_operand2_value=json_operand2_val, json_variables=json_vars)
    # end _get_alarm_match

    def _get_alarm_condition_match(self, uve, exp, operand1_val, operand2_val,
                                   is_operand2_json_val, match_list=None):
        if not match_list:
            match_list = [self._get_alarm_match(uve, exp, operand1_val,
                operand2_val, is_operand2_json_val)]
        return AlarmConditionMatch(
            condition=AlarmCondition(operation=exp.operation,
                operand1=exp.operand1, operand2=AlarmOperand2(
                    uve_attribute=exp.operand2.uve_attribute,
                    json_value=exp.operand2.json_value),
                variables=exp.variables),
            match=match_list)
    # end _get_alarm_condition_match

    def _get_uve_parent_fqname(self, table, uve_name, uve):
        try:
            # virtual-machine UVE key doesn't have project name in the prefix.
            # Hence extract the project name from the interface_list.
            if table == viz_constants.VM_TABLE:
                return uve['UveVirtualMachineAgent']['interface_list'][0].\
                    rsplit(':', 1)[0]
            else:
                return uve_name.rsplit(':', 1)[0]
        except (KeyError, IndexError):
            return None
    # end _get_uve_parent_fqname

    def _evaluate_uve_for_alarms(self, alarm_cfg, uve_key, uve):
        table, uve_name = uve_key.split(':', 1)
        # For alarms configured under project, the parent fq_name of the uve
        # should match with that of the alarm config
        if alarm_cfg.parent_type == 'project':
            uve_parent_fqname = self._get_uve_parent_fqname(table,
                uve_name, uve)
            if uve_parent_fqname != alarm_cfg.get_parent_fq_name_str():
                return None
        or_list = []
        for cfg_and_list in alarm_cfg.alarm_rules.or_list:
            and_list = []
            and_list_fail = False
            for exp in cfg_and_list.and_list:
                operand1_val = self._get_operand_value(uve, exp.operand1)
                if isinstance(operand1_val, dict) and \
                    operand1_val['status'] is False:
                    and_list_fail = True
                    break
                if exp.operand2.json_value is not None:
                    operand2_val = json.loads(exp.operand2.json_value)
                    is_operand2_json_val = True
                else:
                    operand2_val = self._get_operand_value(uve,
                        exp.operand2.uve_attribute)
                    if isinstance(operand2_val, dict) and \
                        operand2_val['status'] is False:
                        and_list_fail = True
                        break
                    is_operand2_json_val = False
                if isinstance(operand1_val, list) or \
                    (is_operand2_json_val is False and \
                        isinstance(operand2_val, list)):
                    match_list = []
                    # both operand1_val and operand2_val are list
                    if isinstance(operand1_val, list) and \
                        (is_operand2_json_val is False and \
                            isinstance(operand2_val, list)):
                            if len(operand1_val) != len(operand2_val):
                                and_list_fail = True
                                break
                            for i in range(0, len(operand1_val)):
                                if self._compare_operand_vals(
                                    operand1_val[i]['value'],
                                    operand2_val[i]['value'],
                                    exp.operation):
                                    match_list.append(
                                        self._get_alarm_match(
                                        uve, exp, operand1_val[i],
                                        operand2_val[i],
                                        is_operand2_json_val))
                    # operand1_val is list and operand2_val is not list
                    elif isinstance(operand1_val, list):
                        val2 = operand2_val
                        if not is_operand2_json_val:
                            val2 = operand2_val['value']
                        for val1 in operand1_val:
                            if self._compare_operand_vals(val1['value'],
                                val2, exp.operation):
                                match_list.append(self._get_alarm_match(
                                    uve, exp, val1, operand2_val,
                                    is_operand2_json_val))
                    # operand1_val is not list and operand2_val is list
                    elif is_operand2_json_val is False and \
                        isinstance(operand2_val, list):
                        for val2 in operand2_val:
                            if self._compare_operand_vals(
                                operand1_val['value'], val2['value'],
                                exp.operation):
                                match_list.append(self._get_alarm_match(
                                    uve, exp, operand1_val, val2,
                                    is_operand2_json_val))
                    if match_list:
                        and_list.append(self._get_alarm_condition_match(
                            uve, exp, operand1_val, operand2_val,
                            is_operand2_json_val, match_list))
                    else:
                        and_list_fail = True
                        break
                # Neither operand1_val nor operand2_val is a list
                else:
                    val1 = operand1_val['value']
                    val2 = operand2_val
                    if not is_operand2_json_val:
                        val2 = operand2_val['value']
                    if self._compare_operand_vals(val1, val2, exp.operation):
                        and_list.append(self._get_alarm_condition_match(
                            uve, exp, operand1_val, operand2_val,
                            is_operand2_json_val))
                    else:
                        and_list_fail = True
                        break
            if not and_list_fail:
                or_list.append(AlarmAndList(and_list))
        if or_list:
            return or_list
        return None
    # end _evaluate_uve_for_alarms


class AlarmStateMachine:
    tab_alarms_timer = {}
    last_timers_run = None
    def __init__(self, tab, uv, nm, sandesh, activeTimer, idleTimer,
            freqCheck_Times, freqCheck_Seconds, freqExceededCheck):
        self._sandesh = sandesh
        self._logger = sandesh._logger
        self.tab = tab
        self.uv = uv
        self.nm = nm
        self.uac = UVEAlarmConfig(ActiveTimer = activeTimer, IdleTimer = \
                idleTimer, FreqCheck_Times = freqCheck_Times, FreqCheck_Seconds
                = freqCheck_Seconds, FreqExceededCheck = freqExceededCheck)
        self.uas = UVEAlarmOperState(state = UVEAlarmState.Idle,
                                head_timestamp = 0, alarm_timestamp = [])
        self.uai = None
        self.activeTimeout = None
        self.deleteTimeout = None
        self.idleTimeout = None

    def get_uai(self, forced=False):
        """
        This functions returns all the alarms which are in Active or
        Soak_Idle state, all other alarms are not yet asserted or cleared
        """
        if forced:
            return self.uai
        if self.uas.state == UVEAlarmState.Active or \
                self.uas.state == UVEAlarmState.Soak_Idle:
            return self.uai
        return None

    def get_uac(self):
        return self.uac

    def get_uas(self):
        return self.uas

    def set_uai(self, uai):
        self.uai = uai

    def is_new_alarm_same(self, new_uai):
        old_or_list = self.uai.alarm_rules.or_list
        new_or_list = new_uai.alarm_rules.or_list
        if (not old_or_list and new_or_list) or \
            (old_or_list and not new_or_list):
            return False
        if old_or_list and new_or_list:
            if len(old_or_list) != len(new_or_list):
                return False
            for i in range(len(old_or_list)):
                old_or_term = old_or_list[i]
                new_or_term = new_or_list[i]
                if len(old_or_term.and_list) != len(new_or_term.and_list):
                    return False
                for j in range(len(old_or_term.and_list)):
                    old_and_term = old_or_term.and_list[j]
                    new_and_term = new_or_term.and_list[j]
                    if old_and_term.condition != new_and_term.condition:
                        return False
                    if len(old_and_term.match) != len(new_and_term.match):
                        return False
                    if old_and_term.condition.operation in \
                       ['<', '<=', '>', '>=', 'range']:
                        for k in range(len(old_and_term.match)):
                            if old_and_term.match[k].json_variables != \
                               new_and_term.match[k].json_variables:
                                return False
                    else:
                        if old_and_term.match != new_and_term.match:
                            return False
        if self.uas.state == UVEAlarmState.Active:
            return True
        return False

    def _remove_timer_from_list(self, index):
        AlarmStateMachine.tab_alarms_timer[index].discard((self.tab,\
                                self.uv, self.nm))
        if len(AlarmStateMachine.tab_alarms_timer[index]) == 0:
            del AlarmStateMachine.tab_alarms_timer[index]

    def set_alarms(self):
        """
        This function runs the state machine code for setting an alarm
        If a timer becomes Active, caller should send out updated AlarmUVE
        """
        old_state = self.uas.state
        curr_time = int(time.time())
        if self.uas.state == UVEAlarmState.Soak_Idle:
            self.uas.state = UVEAlarmState.Active
            self._remove_timer_from_list(self.idleTimeout)
        elif self.uas.state == UVEAlarmState.Idle:
            if self.deleteTimeout and (self.deleteTimeout in \
                    AlarmStateMachine.tab_alarms_timer):
                self._remove_timer_from_list(self.deleteTimeout)
            if self.uac.FreqExceededCheck:
                # log the timestamp
                ts = int(self.uai.timestamp/1000000.0)
                if len(self.uas.alarm_timestamp) <= self.uas.head_timestamp:
                    self.uas.alarm_timestamp.append(ts)
                else:
                    self.uas.alarm_timestamp[self.uas.head_timestamp] = ts
                self.uas.head_timestamp = (self.uas.head_timestamp + 1) % \
                                (self.uac.FreqCheck_Times + 1)
            if not self.uac.ActiveTimer or self.is_alarm_frequency_exceeded():
                self.uas.state = UVEAlarmState.Active
            else:
                # put it on the timer
                self.uas.state = UVEAlarmState.Soak_Active
                self.activeTimeout = curr_time + self.uac.ActiveTimer
                timeout_value = self.activeTimeout
                if not timeout_value in AlarmStateMachine.tab_alarms_timer:
                    AlarmStateMachine.tab_alarms_timer[timeout_value] = set()
                AlarmStateMachine.tab_alarms_timer[timeout_value].add\
                        ((self.tab, self.uv, self.nm))
        self.send_state_change_trace(old_state, self.uas.state)
    #end set_alarms

    def send_state_change_trace(self, os, ns):
        # No need to send if old and new states are same
        if os == ns:
            return
        state_trace = AlarmStateChangeTrace()
        state_trace.table = str(self.tab)
        state_trace.uv = str(self.uv)
        state_trace.alarm_type = str(self.nm)
        state_trace.old_state = os
        state_trace.new_state = ns
        state_trace.trace_msg(name="AlarmStateChangeTrace", \
                                    sandesh=self._sandesh)

    def clear_alarms(self):
        """
        This function runs the state machine code for clearing an alarm
        If a timer becomes Idle with no soaking enabled,
        caller should delete corresponding alarm and send out updated AlarmUVE
        """
        cur_time = int(time.time())
        old_state = self.uas.state
        delete_alarm = False
        if self.uas.state == UVEAlarmState.Soak_Active:
            # stop the active timer and start idle timer
            self.uas.state = UVEAlarmState.Idle
            self._remove_timer_from_list(self.activeTimeout)
            if self.uac.FreqCheck_Seconds:
                self.deleteTimeout = cur_time + self.uac.FreqCheck_Seconds
                to_value = self.deleteTimeout
                if not to_value in AlarmStateMachine.tab_alarms_timer:
                    AlarmStateMachine.tab_alarms_timer[to_value] = set()
                AlarmStateMachine.tab_alarms_timer[to_value].add\
                    ((self.tab, self.uv, self.nm))
            else:
                delete_alarm = True
        elif self.uas.state == UVEAlarmState.Active:
            if not self.uac.IdleTimer:
                # Move to Idle state, caller should delete it
                self.uas.state = UVEAlarmState.Idle
                if self.uac.FreqCheck_Seconds:
                    self.deleteTimeout = cur_time + self.uac.FreqCheck_Seconds
                    to_value = self.deleteTimeout
                    if not to_value in AlarmStateMachine.tab_alarms_timer:
                        AlarmStateMachine.tab_alarms_timer[to_value] = set()
                    AlarmStateMachine.tab_alarms_timer[to_value].add\
                        ((self.tab, self.uv, self.nm))
                else:
                    delete_alarm = True
            else:
                self.uas.state = UVEAlarmState.Soak_Idle
                self.idleTimeout = cur_time + self.uac.IdleTimer
                to_value = self.idleTimeout
                if not to_value in AlarmStateMachine.tab_alarms_timer:
                    AlarmStateMachine.tab_alarms_timer[to_value] = set()
                AlarmStateMachine.tab_alarms_timer[to_value].add\
                        ((self.tab, self.uv, self.nm))
        self.send_state_change_trace(old_state, self.uas.state)
        return delete_alarm

    def is_alarm_frequency_exceeded(self):
        if not self.uac.FreqExceededCheck or \
                not self.uac.FreqCheck_Times or \
                not self.uac.FreqCheck_Seconds:
            return False
        if len(self.uas.alarm_timestamp) < self.uac.FreqCheck_Times + 1:
            return False
        freqCheck_times = self.uac.FreqCheck_Times
        head = self.uas.head_timestamp
        start = (head + freqCheck_times + 1) % freqCheck_times
        end = (head + freqCheck_times) % freqCheck_times
        if (self.uas.alarm_timestamp[end] - self.uas.alarm_timestamp[start]) \
                <= self.uac.FreqCheck_Seconds:
            self._logger.info("alarm frequency is exceeded, raising alarm")
            return True
        return False

    def run_active_timer(self, curr_time):
        update_alarm = False
        timeout_value = None
        if curr_time >= self.activeTimeout:
            self.send_state_change_trace(self.uas.state,
                        UVEAlarmState.Active)
            self.uas.state = UVEAlarmState.Active
            timeout_value = -1
            update_alarm = True
        return timeout_value, update_alarm

    def run_idle_timer(self, curr_time):
        """
        This is the handler function for checking timer in Soak_Idle state. 
        State Machine should be deleted by the caller if this timer fires
        """
        idleTimerExpired = 0
        update_alarm = False
        timeout_value = None
        delete_alarm = False
        if self.idleTimeout:
            idleTimerExpired = curr_time - self.idleTimeout
        if idleTimerExpired >= 0:
            self.send_state_change_trace(self.uas.state, UVEAlarmState.Idle)
            self.uas.state = UVEAlarmState.Idle
            if self.uac.FreqCheck_Seconds:
                self.deleteTimeout = curr_time + self.uac.FreqCheck_Seconds
                timeout_value = self.deleteTimeout
                update_alarm = True
            else:
                delete_alarm = True
        return timeout_value, update_alarm, delete_alarm

    def run_delete_timer(self, curr_time):
        """
        This is the handler function for checking timer in Idle state. 
        State Machine should be deleted by the caller if this timer fires
        """
        delete_alarm = False
        idleTimerExpired = 0
        if self.deleteTimeout > 0:
            idleTimerExpired = curr_time - self.deleteTimeout
        if idleTimerExpired >= 0:
            delete_alarm = True
        return delete_alarm

    def run_uve_soaking_timer(self, curr_time):
        """
        This function goes through the list of alarms which were raised
        or set to delete but not soaked yet.
        If an alarm is soaked for corresponding soak_time then it is asserted
        or deleted
        """
        update_alarm = False
        delete_alarm = False
        timeout_value = None
        if self.uas.state == UVEAlarmState.Soak_Active:
            timeout_value, update_alarm = self.run_active_timer(curr_time)
        elif self.uas.state == UVEAlarmState.Soak_Idle:
            timeout_value, update_alarm, delete_alarm = self.run_idle_timer(curr_time)
        elif self.uas.state == UVEAlarmState.Idle:
            delete_alarm = self.run_delete_timer(curr_time)
        return delete_alarm, update_alarm, timeout_value
    #end run_uve_soaking_timer

    def delete_timers(self):
        if self.uas.state == UVEAlarmState.Idle:
            if self.deleteTimeout and self.deleteTimeout > 0:
                self._remove_timer_from_list(self.deleteTimeout)
        elif self.uas.state == UVEAlarmState.Soak_Active:
            if self.activeTimeout and self.activeTimeout > 0:
                self._remove_timer_from_list(self.activeTimeout)
        elif self.uas.state == UVEAlarmState.Soak_Idle:
            if self.idleTimeout and self.idleTimeout > 0:
                self._remove_timer_from_list(self.idleTimeout)

    @staticmethod
    def run_timers(curr_time, tab_alarms):
        inputs = namedtuple('inputs', ['tab', 'uv', 'nm',
                            'delete_alarm', 'timeout_val', 'old_to'])
        delete_alarms = []
        update_alarms = []
        if AlarmStateMachine.last_timers_run is None:
            AlarmStateMachine.last_timers_run = curr_time
        for next_timer in range(AlarmStateMachine.last_timers_run, curr_time + 1):
          if next_timer in AlarmStateMachine.tab_alarms_timer:
            update_timers = []
            for (tab, uv, nm) in AlarmStateMachine.tab_alarms_timer[next_timer]:
                asm = tab_alarms[tab][uv][nm]
                delete_alarm, update_alarm, timeout_val = \
                                asm.run_uve_soaking_timer(next_timer)
                if delete_alarm:
                    delete_alarms.append((asm.tab, asm.uv, asm.nm))
                if update_alarm:
                    update_alarms.append((asm.tab, asm.uv, asm.nm))
                update_timers.append(inputs(tab=asm.tab, uv=asm.uv, nm=asm.nm,
                                    delete_alarm=delete_alarm,
                                    timeout_val=timeout_val, old_to=next_timer))
            for timer in update_timers:
                if timer.timeout_val is not None or timer.delete_alarm:
                    AlarmStateMachine.update_tab_alarms_timer(timer.tab,
                                    timer.uv, timer.nm, timer.old_to, 
                                    timer.timeout_val, tab_alarms)
        AlarmStateMachine.last_timers_run = curr_time + 1
        return delete_alarms, update_alarms

    @staticmethod
    def update_tab_alarms_timer(tab, uv, nm, curr_index, timeout_val,
            tab_alarms):
        del_timers = []
        if curr_index is not None and curr_index > 0:
            timers = AlarmStateMachine.tab_alarms_timer[curr_index]
            if (tab, uv, nm) in timers:
                asm = tab_alarms[tab][uv][nm]
                timers.discard((tab, uv, nm))
            if len(timers) == 0:
                del_timers.append(curr_index)
        for timeout in del_timers:
            del AlarmStateMachine.tab_alarms_timer[timeout]
        if timeout_val >= 0:
            if not timeout_val in AlarmStateMachine.tab_alarms_timer:
                AlarmStateMachine.tab_alarms_timer[timeout_val] = set()
            if (tab, uv, nm) in AlarmStateMachine.tab_alarms_timer\
                    [timeout_val]:
                self._logger.error("Timer error for (%s,%s,%s)" % \
                    (tab, uv, nm))
                raise SystemExit(1)
            AlarmStateMachine.tab_alarms_timer[timeout_val].add\
                        ((asm.tab, asm.uv, asm.nm))



class Controller(object):

    @staticmethod
    def token(sandesh, timestamp):
        token = {'host_ip': sandesh.host_ip(),
                 'http_port': sandesh._http_server.get_port(),
                 'timestamp': timestamp}
        return base64.b64encode(json.dumps(token))

    def fail_cb(self, manager, entrypoint, exception):
        self._sandesh._logger.info("Load failed for %s with exception %s" % \
                                     (str(entrypoint),str(exception)))
        
    def __init__(self, conf, test_logger=None):
        self._conf = conf
        module = Module.ALARM_GENERATOR
        self._moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        self._node_type_name = NodeTypeNames[node_type]
        self.table = "ObjectCollectorInfo"
        self._hostname = socket.gethostname()
        self._instance_id = self._conf.worker_id()

        self.disc = None
        self._libpart_name = self._conf.host_ip() + ":" + self._instance_id
        self._libpart = None
        self._partset = set()
        if self._conf.discovery()['server']:
            self._max_out_rows = 20
            self.disc = client.DiscoveryClient(
                self._conf.discovery()['server'],
                self._conf.discovery()['port'],
                ModuleNames[Module.ALARM_GENERATOR],
                '%s-%s' % (self._hostname, self._instance_id))

        is_collector = True
        if test_logger is not None:
            is_collector = False
        self._sandesh = Sandesh()
        # Reset the sandesh send rate limit value
        if self._conf.sandesh_send_rate_limit() is not None:
            SandeshSystem.set_sandesh_send_rate_limit( \
                self._conf.sandesh_send_rate_limit())
        self._sandesh.init_generator(self._moduleid, self._hostname,
                                      self._node_type_name, self._instance_id,
                                      self._conf.collectors(),
                                      self._node_type_name,
                                      self._conf.http_port(),
                                      ['opserver.sandesh', 'sandesh'],
                                      host_ip=self._conf.host_ip(),
                                      discovery_client=self.disc,
                                      connect_to_collector = is_collector,
                                      alarm_ack_callback=self.alarm_ack_callback)
        if test_logger is not None:
            self._logger = test_logger
        else:
            self._sandesh.set_logging_params(
                enable_local_log=self._conf.log_local(),
                category=self._conf.log_category(),
                level=self._conf.log_level(),
                file=self._conf.log_file(),
                enable_syslog=self._conf.use_syslog(),
                syslog_facility=self._conf.syslog_facility())
            self._logger = self._sandesh._logger
        # Trace buffer list
        self.trace_buf = [
            {'name':'DiscoveryMsg', 'size':1000},
            {'name':'AlarmStateChangeTrace', 'size':1000},
            {'name':'UVEQTrace', 'size':20000}
        ]
        # Create trace buffers 
        for buf in self.trace_buf:
            self._sandesh.trace_buffer_create(name=buf['name'], size=buf['size'])

        tables = set()
        mgrlist = extension.ExtensionManager('contrail.analytics.alarms')
        for elem in mgrlist:
            tables.add(elem.name)
        self._logger.error('Found extenstions for %s' % str(tables))

        self.mgrs = {}
        self.tab_alarms = {}
        self.ptab_info = {}
        self.tab_perf = {}
        self.tab_perf_prev = {}
        for table in tables:
            self.mgrs[table] = hook.HookManager(
                namespace='contrail.analytics.alarms',
                name=table,
                invoke_on_load=True,
                invoke_args=(),
                on_load_failure_callback=self.fail_cb
            )
            
            for extn in self.mgrs[table][table]:
                self._logger.info('Loaded extensions for %s: %s,%s doc %s' % \
                    (table, extn.name, extn.entry_point_target, extn.obj.__doc__))

            self.tab_alarms[table] = {}
            self.tab_perf[table] = AGTabStats()

        ConnectionState.init(self._sandesh, self._hostname, self._moduleid,
            self._instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus, self.table)

        self._us = UVEServer(None, self._logger, self._conf.redis_password())

        if not self.disc:
            self._max_out_rows = 2
            # If there is no discovery service, use fixed redis_uve list
            redis_uve_list = []
            try:
                for redis_uve in self._conf.redis_uve_list():
                    redis_ip_port = redis_uve.split(':')
                    redis_elem = (redis_ip_port[0], int(redis_ip_port[1]),0)
                    redis_uve_list.append(redis_elem)
            except Exception as e:
                print('Failed to parse redis_uve_list: %s' % e)
            else:
                self._us.update_redis_uve_list(redis_uve_list)

            # If there is no discovery service, use fixed alarmgen list
            self._libpart = self.start_libpart(self._conf.alarmgen_list())

        self._workers = {}
        self._uvestats = {}
        self._uveq = {}
        self._uveqf = {}
        self._alarm_config_change_map = {}

        # Create config handler to read/update alarm config
        rabbitmq_params = self._conf.rabbitmq_params()
        self._config_handler = AlarmGenConfigHandler(self._sandesh,
            self._moduleid, self._instance_id, self.config_log, self.disc,
            self._conf.keystone_params(), rabbitmq_params, self.mgrs,
            self.alarm_config_change_callback)
        if rabbitmq_params['servers'] and self.disc:
            self._config_handler.start()
        else:
            self._logger.error('Rabbitmq server and/or Discovery server '
                'not configured')

        PartitionOwnershipReq.handle_request = self.handle_PartitionOwnershipReq
        PartitionStatusReq.handle_request = self.handle_PartitionStatusReq
        UVETableAlarmReq.handle_request = self.handle_UVETableAlarmReq 
        UVETableInfoReq.handle_request = self.handle_UVETableInfoReq
        UVETablePerfReq.handle_request = self.handle_UVETablePerfReq
        AlarmConfigRequest.handle_request = self.handle_AlarmConfigRequest

    def config_log(self, msg, level):
        self._sandesh.logger().log(
            SandeshLogger.get_py_logger_level(level), msg)
    # end config_log

    def libpart_cb(self, part_list):

        agpi = AlarmgenPartionInfo()
        agpi.instance = self._instance_id
        agpi.partitions = part_list

        agp = AlarmgenPartition()
        agp.name = self._hostname
        agp.inst_parts = [agpi]
       
        agp_trace = AlarmgenPartitionTrace(data=agp, sandesh=self._sandesh)
        agp_trace.send(sandesh=self._sandesh) 

        newset = set(part_list)
        oldset = self._partset
        self._partset = newset

        try:
            self._logger.error('Partition List : new %s old %s' % \
                (str(newset),str(oldset)))

            self._logger.error('Partition Add : %s' % str(newset-oldset))
            self.partition_change(newset-oldset, True)

            self._logger.error('Partition Del : %s' % str(oldset-newset))
            if not self.partition_change(oldset-newset, False):
                self._logger.error('Partition Del : %s failed!' % str(oldset-newset))
                raise SystemExit(1)

	    self._logger.error('Partition Del done: %s' % str(oldset-newset))

        except Exception as ex:
            template = "Exception {0} in Partition List. Arguments:\n{1!r}"
            messag = template.format(type(ex).__name__, ex.args)
            self._logger.error("%s : traceback %s" % \
                                    (messag, traceback.format_exc()))
            self._logger.error('Partition List failed %s %s' % \
                (str(newset),str(oldset)))
        except SystemExit:
            raise SystemExit(1)

        self._logger.error('Partition List done : new %s old %s' % \
            (str(newset),str(oldset)))

    def start_libpart(self, ag_list):
        if not self._conf.zk_list():
            self._logger.error('Could not import libpartition: No zookeeper')
            return None
        if not ag_list:
            self._logger.error('Could not import libpartition: No alarmgen list')
            return None
        try:
            self._logger.error('Starting PC')
            agpi = AlarmgenPartionInfo()
            agpi.instance = self._instance_id
            agpi.partitions = []

            agp = AlarmgenPartition()
            agp.name = self._hostname
            agp.inst_parts = [agpi]
           
            agp_trace = AlarmgenPartitionTrace(data=agp, sandesh=self._sandesh)
            agp_trace.send(sandesh=self._sandesh) 

            pc = PartitionClient(self._conf.kafka_prefix() + "-alarmgen",
                    self._libpart_name, ag_list,
                    self._conf.partitions(), self.libpart_cb,
                    ','.join(self._conf.zk_list()), self._logger)
            self._logger.error('Started PC %s' % self._conf.kafka_prefix() + "-alarmgen")
            return pc
        except Exception as e:
            self._logger.error('Could not import libpartition: %s' % str(e))
            return None

    def handle_uve_notifq(self, part, uves):
        """
        uves : 
          This is a dict of UVEs that have changed, as per the following scheme:
          <UVE-Key> : None               # Any of the types may have changed
                                         # Used during stop_partition and GenDelete
          <UVE-Key> : { <Struct>: {} }   # The given struct may have changed
          <UVE-Key> : { <Struct>: None } # The given struct may have gone
          Our treatment of the 2nd and 3rd case above is the same
        """
        uveq_trace = UVEQTrace()
        uveq_trace.uves = uves.keys()
        uveq_trace.part = part
        if part not in self._uveq:
            self._uveq[part] = {}
            self._logger.error('Created uveQ for part %s' % str(part))
            uveq_trace.oper = "create"
        else:
            uveq_trace.oper = "update"
        uveq_trace.trace_msg(name="UVEQTrace",\
                sandesh=self._sandesh)

        for uv,types in uves.iteritems():
            if types is None:
                self._uveq[part][uv] = None
            else:
                if uv in self._uveq[part]:
                    if self._uveq[part][uv] is not None:
                        for kk in types.keys():
                            self._uveq[part][uv][kk] = {}
                else:
                    self._uveq[part][uv] = {}
                    for kk in types.keys():
                        self._uveq[part][uv][kk] = {}

    def handle_resource_check(self, part, current_inst, msgs):
        """
        This function compares the set of synced redis instances
        against the set now being reported by UVEServer
       
        It returns :
        - The updated set of redis instances
        - A set of collectors to be removed
        - A dict with the collector to be added, with the contents
        """
        us_redis_inst = self._us.redis_instances()
        disc_instances = copy.deepcopy(us_redis_inst) 
        
        r_added = disc_instances - current_inst
        r_deleted = current_inst - disc_instances
        
        coll_delete = set()
        for r_inst in r_deleted:
            ipaddr = r_inst[0]
            port = r_inst[1]
            coll_delete.add(ipaddr + ":" + str(port))
        
        chg_res = {}
        for r_inst in r_added:
            coll, res = self._us.get_part(part, r_inst)
            chg_res[coll] = res

        return disc_instances, coll_delete, chg_res            

    def reconnect_agg_uve(self, lredis):
        self._logger.error("Connected to Redis for Agg")
        lredis.set(self._moduleid+':'+self._instance_id, True)
        for pp in self._workers.keys():
            self._workers[pp].reset_acq_time()
            self._workers[pp].kill(\
                    RuntimeError('UVE Proc failed'),
                    block=False)
            self.clear_agg_uve(lredis,
                self._instance_id,
                pp,
                self._workers[pp].acq_time())
            self.stop_uve_partition(pp)

        for part in self._uveq.keys():
            self._logger.info("Clearing part %d uveQ : %s" % \
                    (part,str(self._uveq[part].keys())))
	    uveq_trace = UVEQTrace()
	    uveq_trace.uves = self._uveq[part].keys()
	    uveq_trace.part = part
            uveq_trace.oper = "clear"
	    uveq_trace.trace_msg(name="UVEQTrace",\
		    sandesh=self._sandesh)
            del self._uveq[part]

    def clear_agg_uve(self, redish, inst, part, acq_time):
	self._logger.error("Agg %s reset part %d, acq %d" % \
		(inst, part, acq_time))
	ppe2 = redish.pipeline()
	ppe2.hdel("AGPARTS:%s" % inst, part)
	ppe2.smembers("AGPARTKEYS:%s:%d" % (inst, part))
	pperes2 = ppe2.execute()
	ppe3 = redish.pipeline()
	# Remove all contents for this AG-Partition
	for elem in pperes2[-1]:
	    ppe3.delete("AGPARTVALUES:%s:%d:%s" % (inst, part, elem))
	ppe3.delete("AGPARTKEYS:%s:%d" % (inst, part))
	ppe3.hset("AGPARTS:%s" % inst, part, acq_time)
	pperes3 = ppe3.execute()

    def send_agg_uve(self, redish, inst, part, acq_time, rows):
        """ 
        This function writes aggregated UVEs to redis

        Each row has a UVE key, one of it's structs type names and the structs value
        If type is "None", it means that the UVE is being removed
        If value is none, it mean that struct of the UVE is being removed

        The key and typename information is also published on a redis channel
        """
        if not redish:
            self._logger.error("No redis handle")
            raise SystemExit(1)
        old_acq_time = redish.hget("AGPARTS:%s" % inst, part)
        if old_acq_time is None:
            self._logger.error("Agg %s part %d new" % (inst, part))
            redish.hset("AGPARTS:%s" % inst, part, acq_time)
        else:
            # Is there stale information for this partition?
            if int(old_acq_time) != acq_time:
                self._logger.error("Agg %s stale info part %d, acqs %d,%d" % \
                        (inst, part, int(old_acq_time), acq_time))
                self.clear_agg_uve(redish, inst, part, acq_time)

        pub_list = []        
        ppe = redish.pipeline()
        check_keys = set()
        for row in rows: 
            vjson = json.dumps(row.val)
            typ = row.typ
            key = row.key
            pub_list.append({"key":key,"type":typ})
            if typ is None:
                self._logger.debug("Agg remove part %d, key %s" % (part,key))
                # The entire contents of the UVE should be removed
                ppe.srem("AGPARTKEYS:%s:%d" % (inst, part), key)
                ppe.delete("AGPARTVALUES:%s:%d:%s" % (inst, part, key))
            else:
                if row.val is None:
                    self._logger.debug("Agg remove part %d, key %s, type %s" % (part,key,typ))
                    # Remove the given struct from the UVE
                    ppe.hdel("AGPARTVALUES:%s:%d:%s" % (inst, part, key), typ)
                    check_keys.add(key)
                else:
                    self._logger.debug("Agg update part %d, key %s, type %s" % (part,key,typ))
                    ppe.sadd("AGPARTKEYS:%s:%d" % (inst, part), key)
                    ppe.hset("AGPARTVALUES:%s:%d:%s" % (inst, part, key),
                        typ, vjson)
        ppe.execute()
        
        # Find the keys that have no content (all structs have been deleted) 
        ppe4 = redish.pipeline()
        check_keys_list = list(check_keys)
        for kk in check_keys_list:
           ppe4.exists("AGPARTVALUES:%s:%d:%s" % (inst, part, kk))
        pperes4 = ppe4.execute()

        retry = False
        # From the index, removes keys for which there are now no contents
        ppe5 = redish.pipeline()
        idx = 0
        for res in pperes4:
            if not res:
                self._logger.error("Agg unexpected key %s from inst:part %s:%d" % \
                        (check_keys_list[idx], inst, part))
                ppe5.srem("AGPARTKEYS:%s:%d" % (inst, part), check_keys_list[idx])
                # TODO: alarmgen should have already figured out if all structs of 
                #       the UVE are gone, and should have sent a UVE delete
                #       We should not need to figure this out again
                retry = True
            idx += 1
        ppe5.execute()

        redish.publish('AGPARTPUB:%s:%d' % (inst, part), json.dumps(pub_list))

        if retry:
            self._logger.error("Agg unexpected rows %s" % str(rows))
            raise SystemExit(1)
        
    def send_alarm_update(self, tab, uk):
        ustruct = None
        alm_copy = []
        for nm, asm in self.tab_alarms[tab][uk].iteritems():
            uai = asm.get_uai()
            if uai:
                alm_copy.append(copy.deepcopy(uai))
        if len(alm_copy) == 0:
            ustruct = UVEAlarms(name = str(uk).split(':',1)[1], deleted = True)
            self._logger.info('deleting alarm:')
        else:
            ustruct = UVEAlarms(name = str(uk).split(':',1)[1],
                                    alarms = alm_copy)
        alarm_msg = AlarmTrace(data=ustruct, table=tab, \
                                    sandesh=self._sandesh)
        alarm_msg.send(sandesh=self._sandesh)
        self._logger.info('raising alarm %s' % (alarm_msg.log()))

    def run_alarm_timers(self, curr_time):
        delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self.tab_alarms)

        for alarm in delete_alarms:
            del self.tab_alarms[alarm[0]][alarm[1]][alarm[2]]
            self.send_alarm_update(alarm[0], alarm[1])
        for alarm in update_alarms:
            self.send_alarm_update(alarm[0], alarm[1])

    def run_uve_agg(self, lredis, outp, part, acq_time):
        # Write the aggregate UVE for all UVE updates for the
        # given partition
        rows = []
        for ku,vu in outp.iteritems():
            if vu is None:
                # This message has no type!
                # Its used to indicate a delete of the entire UVE
                rows.append(OutputRow(key=ku, typ=None, val=None))
                if len(rows) >= self._max_out_rows:
                    self.send_agg_uve(lredis,
                        self._instance_id,
                        part,
                        acq_time,
                        rows)
                    rows[:] = []
                continue
            for kt,vt in vu.iteritems():
                rows.append(OutputRow(key=ku, typ=kt, val=vt))
                if len(rows) >= self._max_out_rows:
                    self.send_agg_uve(lredis,
                        self._instance_id,
                        part,
                        acq_time,
                        rows)
                    rows[:] = []
        # Flush all remaining rows
        if len(rows):
            self.send_agg_uve(lredis,
                self._instance_id,
                part,
                acq_time,
                rows)
            rows[:] = []

    def run_uve_processing(self):
        """
        This function runs in its own gevent, and provides state compression
        for UVEs.
        Kafka worker (PartitionHandler)  threads detect which UVE have changed
        and accumulate them onto a set. When this gevent runs, it processes
        all UVEs of the set. Even if this gevent cannot run for a while, the
        set should not grow in an unbounded manner (like a queue can)
        """

        lredis = None
        oldworkerset = None
        while True:
             
            for part in self._uveqf.keys():
                self._logger.error("Stop UVE processing for %d:%d" % \
                        (part, self._uveqf[part]))
                self.stop_uve_partition(part)
                del self._uveqf[part]
                if part in self._uveq:
		    uveq_trace = UVEQTrace()
		    uveq_trace.uves = self._uveq[part].keys()
		    uveq_trace.part = part
		    uveq_trace.oper = "stop"
		    uveq_trace.trace_msg(name="UVEQTrace",\
			    sandesh=self._sandesh)
                    self._logger.info("Stopping part %d uveQ : %s" % \
                            (part,str(self._uveq[part].keys())))
                    del self._uveq[part]
            prev = time.time()
            try:
                if lredis is None:
                    lredis = redis.StrictRedis(
                            host="127.0.0.1",
                            port=self._conf.redis_server_port(),
                            password=self._conf.redis_password(),
                            db=7)
                    self.reconnect_agg_uve(lredis)
                    ConnectionState.update(conn_type = ConnectionType.REDIS_UVE,
                          name = 'AggregateRedis', status = ConnectionStatus.UP)
                else:
                    if not lredis.exists(self._moduleid+':'+self._instance_id):
                        self._logger.error('Identified redis restart')
                        self.reconnect_agg_uve(lredis)
                gevs = {}
                pendingset = {}
		kafka_topic_down = False
                for part in self._uveq.keys():
                    if not len(self._uveq[part]):
                        continue
                    self._logger.info("UVE Process for %d" % part)
		    kafka_topic_down |= self._workers[part].failed()

                    # Allow the partition handlers to queue new UVEs without
                    # interfering with the work of processing the current UVEs
                    # Process no more than 200 keys at a time
                    pendingset[part] = {}
                    icount = 0
                    while (len(self._uveq[part]) > 0) and icount < 200:
                        kp,vp = self._uveq[part].popitem()
                        pendingset[part][kp] = vp
                        icount += 1
                    self._logger.info("UVE Process for %d : %d, %d remain" % \
                            (part, len(pendingset[part]), len(self._uveq[part])))
                        
                    gevs[part] = gevent.spawn(self.handle_uve_notif, part,\
                        pendingset[part])
		if kafka_topic_down:
                    ConnectionState.update(conn_type = ConnectionType.KAFKA_PUB,
                        name = 'KafkaTopic', status = ConnectionStatus.DOWN)
	        else:
                    ConnectionState.update(conn_type = ConnectionType.KAFKA_PUB,
                        name = 'KafkaTopic', status = ConnectionStatus.UP)

                if len(gevs):
                    gevent.joinall(gevs.values())
                    self._logger.info("UVE Processing joined")
                    outp={}
                    gevs_out={}
                    for part in gevs.keys():
                        # If UVE processing failed, requeue the working set
                        try:
                            outp[part] = gevs[part].get()
                        except Exception as ex:
                            template = "Exception {0} in notif worker. Arguments:\n{1!r}"
                            messag = template.format(type(ex).__name__, ex.args)
                            self._logger.error("%s : traceback %s" % \
                                    (messag, traceback.format_exc()))
                            outp[part] = None
                        if outp[part] is None:
                            self._logger.error("UVE Process failed for %d" % part)
                            self.handle_uve_notifq(part, pendingset[part])
                        elif not part in self._workers:
                            outp[part] = None
                            self._logger.error(
                                    "Part %d is gone, cannot process UVEs" % part)
                        else:
                            self._logger.info("UVE Agg on %d items in part %d" % \
                                    (len(outp), part))
                            gevs_out[part] = gevent.spawn(self.run_uve_agg, lredis,\
                                    outp[part], part, self._workers[part].acq_time())

                    if len(gevs_out):
                        gevent.joinall(gevs_out.values()) 

                        # Check for exceptions during processing
                        for part in gevs_out.keys():
                            gevs_out[part].get()                        

                # If there are alarm config changes, then start a gevent per
                # partition to process the alarm config changes
                if self._alarm_config_change_map:
                    alarm_config_change_map = copy.deepcopy(
                        self._alarm_config_change_map)
                    self._alarm_config_change_map = {}
                    alarm_workers = {}
                    for partition in self._workers.keys():
                        alarm_workers[partition] = gevent.spawn(
                            self.alarm_config_change_worker, partition,
                            alarm_config_change_map)
                    if alarm_workers:
                        gevent.joinall(alarm_workers.values())

            except Exception as ex:
                template = "Exception {0} in uve proc. Arguments:\n{1!r}"
                messag = template.format(type(ex).__name__, ex.args)

                if type(ex).__name__ == 'ConnectionError':
                    self._logger.error(messag)
                else:
                    self._logger.error("%s : traceback %s" % \
                                   (messag, traceback.format_exc()))

                lredis = None
                ConnectionState.update(conn_type = ConnectionType.REDIS_UVE,
                      name = 'AggregateRedis', status = ConnectionStatus.DOWN)
                if self.disc:
                    data = {
                        'ip-address': self._conf.host_ip(),
                        'instance-id': self._instance_id,
                        'redis-port': str(self._conf.redis_server_port()),
                        'partitions': None
                    }
                    self._logger.error("Disc Publish to %s : %s"
                                  % (str(self._conf.discovery()), str(data)))
                    self.disc.publish(ALARM_GENERATOR_SERVICE_NAME, data)
                oldworkerset = None
                gevent.sleep(1)

            else:
                workerset = {}
                for part in self._workers.keys():
                    if self._workers[part]._up:
                        workerset[part] = self._workers[part].acq_time()
                if workerset != oldworkerset:
                    data = {
                        'ip-address': self._conf.host_ip(),
                        'instance-id': self._instance_id,
                        'redis-port': str(self._conf.redis_server_port()),
                        'partitions': json.dumps(workerset)
                    }
                    if self.disc:
                        self._logger.error("Disc Publish to %s : %s"
                                      % (str(self._conf.discovery()), str(data)))
                        self.disc.publish(ALARM_GENERATOR_SERVICE_NAME, data)
                    oldworkerset = copy.deepcopy(workerset)
                        
            curr = time.time()
            try:
		self.run_alarm_timers(int(curr))
            except Exception as ex:
                template = "Exception {0} in timer proc. Arguments:\n{1!r}"
                messag = template.format(type(ex).__name__, ex.args)
                self._logger.error("%s : traceback %s" % \
                                  (messag, traceback.format_exc()))
                raise SystemExit(1)
            if (curr - prev) < 1:
                gevent.sleep(1 - (curr - prev))
                self._logger.info("UVE Done")
            else:
                self._logger.info("UVE Process saturated")
                gevent.sleep(0)

    def examine_uve_for_alarms(self, uve_key, uve):
        table = uve_key.split(':', 1)[0]
        alarm_cfg = self._config_handler.alarm_config_db()
        prevt = UTCTimestampUsec()
        aproc = AlarmProcessor(self._logger)
        # Process all alarms configured for this uve-type
        for alarm_fqname, alarm_obj in \
            alarm_cfg.get(table, {}).iteritems():
            aproc.process_alarms(alarm_fqname, alarm_obj, uve_key, uve)
        # Process all alarms configured for this uve-key
        for alarm_fqname, alarm_obj in \
            alarm_cfg.get(uve_key, {}).iteritems():
            aproc.process_alarms(alarm_fqname, alarm_obj, uve_key, uve)
        new_uve_alarms = aproc.uve_alarms
        self.tab_perf[table].record_call(UTCTimestampUsec() - prevt)

        del_types = []
        if not self.tab_alarms.has_key(table):
            self.tab_alarms[table] = {}
        if self.tab_alarms[table].has_key(uve_key):
            for nm, asm in self.tab_alarms[table][uve_key].iteritems():
                # This type was present earlier, but is now gone
                if not new_uve_alarms.has_key(nm):
                    del_types.append(nm)
                else:
                    # This type has no new information
                    if asm.is_new_alarm_same(new_uve_alarms[nm]):
                        del new_uve_alarms[nm]

        if len(del_types) != 0  or len(new_uve_alarms) != 0:
            self._logger.debug("Alarm[%s] Deleted %s" % \
                    (table, str(del_types)))
            self._logger.debug("Alarm[%s] Updated %s" % \
                    (table, str(new_uve_alarms)))
            # These alarm types are new or updated
            for nm, uai2 in new_uve_alarms.iteritems():
                uai = copy.deepcopy(uai2)
                uai.timestamp = UTCTimestampUsec()
                uai.token = Controller.token(self._sandesh, uai.timestamp)
                if not self.tab_alarms[table].has_key(uve_key):
                    self.tab_alarms[table][uve_key] = {}
                if not nm in self.tab_alarms[table][uve_key]:
                    self.tab_alarms[table][uve_key][nm] = AlarmStateMachine(
                        tab=table, uv=uve_key, nm=nm, sandesh=self._sandesh,
                        activeTimer=aproc.ActiveTimer[uve_key][nm],
                        idleTimer=aproc.IdleTimer[uve_key][nm],
                        freqCheck_Times=aproc.FreqCheck_Times[uve_key][nm],
                        freqCheck_Seconds= \
                            aproc.FreqCheck_Seconds[uve_key][nm],
                        freqExceededCheck= \
                            aproc.FreqExceededCheck[uve_key][nm])
                asm = self.tab_alarms[table][uve_key][nm]
                asm.set_uai(uai)
                # go through alarm set statemachine code
                asm.set_alarms()
            # These alarm types are now gone
            for dnm in del_types:
                if dnm in self.tab_alarms[table][uve_key]:
                    delete_alarm = \
                        self.tab_alarms[table][uve_key][dnm].clear_alarms()
                    if delete_alarm:
                        del self.tab_alarms[table][uve_key][dnm]
            self.send_alarm_update(table, uve_key)
    # end examine_uve_for_alarms

    def alarm_config_change_worker(self, partition, alarm_config_change_map):
        self._logger.debug('Alarm config change worker for partition %d'
            % (partition))
        try:
            for uve_key, alarm_map in alarm_config_change_map.iteritems():
                self._logger.debug('Handle alarm config change for '
                    '[partition:uve_key:{alarms}] -> [%d:%s:%s]' %
                    (partition, uve_key, str(alarm_map)))
                uve_type_name = uve_key.split(':', 1)
                try:
                    if len(uve_type_name) == 1:
                        uves = self.ptab_info[partition][uve_type_name[0]]
                    else:
                        uve = self.ptab_info[partition][uve_type_name[0]]\
                                [uve_type_name[1]]
                        uves = {uve_type_name[1]: uve}
                except KeyError:
                    continue
                else:
                    for name, data in uves.iteritems():
                        self._logger.debug('process alarm for uve %s' % (name))
                        self.examine_uve_for_alarms(uve_type_name[0]+':'+name,
                            data.values())
                    gevent.sleep(0)
        except Exception as e:
            self._logger.error('Error in alarm config change worker for '
                'partition %d - %s' % (partition, str(e)))
            self._logger.error('traceback %s' % (traceback.format_exc()))
    # end alarm_config_change_worker

    def alarm_config_change_callback(self, alarm_config_change_map):
        for table, alarm_map in alarm_config_change_map.iteritems():
            try:
                tamap = self._alarm_config_change_map[table]
            except KeyError:
                self._alarm_config_change_map[table] = alarm_map
            else:
                tamap.update(alarm_map)
    # end alarm_config_change_callback

    def stop_uve_partition(self, part):
        if not part in self.ptab_info:
            return
        for tk in self.ptab_info[part].keys():
            tcount = len(self.ptab_info[part][tk])
            for rkey in self.ptab_info[part][tk].keys():
                uk = tk + ":" + rkey
                if tk in self.tab_alarms:
                    if uk in self.tab_alarms[tk]:
                        self.delete_tab_alarms_timer(tk, uk)
                        del self.tab_alarms[tk][uk]
                        ustruct = UVEAlarms(name = rkey, deleted = True)
                        alarm_msg = AlarmTrace(data=ustruct, \
                                table=tk, sandesh=self._sandesh)
                        self._logger.error('send del alarm for stop: %s' % \
                                (alarm_msg.log()))
                        alarm_msg.send(sandesh=self._sandesh)
                del self.ptab_info[part][tk][rkey]
            self._logger.error("UVE stop removed %d UVEs of type %s" % \
                    (tcount, tk))
            del self.ptab_info[part][tk]
        del self.ptab_info[part]

    def delete_tab_alarms_timer(self, tab, uv):
        """
        This function deletes all the timers for given tab,uv combination
        """
        for ak,av in self.tab_alarms[tab][uv].iteritems():
            av.delete_timers()

    def handle_uve_notif(self, part, uves):
        """
        Call this function when a UVE has changed. This can also
        happed when taking ownership of a partition, or when a
        generator is deleted.
        Args:
            part   : Partition Number
            uve    : dict, where the key is the UVE Name.
                     The value is either a dict of UVE structs, or "None",
                     which means that all UVE structs should be processed.

        Returns: 
            status of operation (True for success)
        """
        self._logger.debug("Changed part %d UVEs : %s" % (part, str(uves)))
        success = True
        output = {}

	uveq_trace = UVEQTrace()
	uveq_trace.uves = uves.keys()
	uveq_trace.part = part
	uveq_trace.oper = "process"
	uveq_trace.trace_msg(name="UVEQTrace",\
		sandesh=self._sandesh)

        erruves = []
        for uv,types in uves.iteritems():
            tab = uv.split(':',1)[0]
            if tab not in self.tab_perf:
                self.tab_perf[tab] = AGTabStats()

            if part in self._uvestats:
                # Record stats on UVE Keys being processed
                if not tab in self._uvestats[part]:
                    self._uvestats[part][tab] = {}
                if uv in self._uvestats[part][tab]:
                    self._uvestats[part][tab][uv] += 1
                else:
                    self._uvestats[part][tab][uv] = 1

            uve_name = uv.split(':',1)[1]
            prevt = UTCTimestampUsec() 
            filters = {}
            if types:
                filters["cfilt"] = {}
                for typ in types.keys():
                    filters["cfilt"][typ] = set()

            failures, uve_data = self._us.get_uve(uv, True, filters)
            if failures:
                erruves.append(uv)
                success = False
            self.tab_perf[tab].record_get(UTCTimestampUsec() - prevt)
            # Handling Agg UVEs
            if not part in self.ptab_info:
                self._logger.error("Creating UVE table for part %s" % str(part))
                self.ptab_info[part] = {}

            if not tab in self.ptab_info[part]:
                self.ptab_info[part][tab] = {}

            if uve_name not in self.ptab_info[part][tab]:
                self.ptab_info[part][tab][uve_name] = AGKeyInfo(part)
            prevt = UTCTimestampUsec()
            output[uv] = {}
            touched = False
            if not types:
                self.ptab_info[part][tab][uve_name].update(uve_data)
                if len(self.ptab_info[part][tab][uve_name].removed()):
                    touched = True
                    rset = self.ptab_info[part][tab][uve_name].removed()
                    self._logger.info("UVE %s removed structs %s" % (uv, rset))
		    uveq_trace = UVEQTrace()
		    uveq_trace.uves = [uv + "-" + str(rset)]
		    uveq_trace.part = part
		    uveq_trace.oper = "remove"
		    uveq_trace.trace_msg(name="UVEQTrace",\
			    sandesh=self._sandesh)
                    for rems in self.ptab_info[part][tab][uve_name].removed():
                        output[uv][rems] = None
                if len(self.ptab_info[part][tab][uve_name].changed()):
                    touched = True
                    self._logger.debug("UVE %s changed structs %s" % (uv, \
                            self.ptab_info[part][tab][uve_name].changed()))
                    for chgs in self.ptab_info[part][tab][uve_name].changed():
                        output[uv][chgs] = \
                                self.ptab_info[part][tab][uve_name].values()[chgs]
                if len(self.ptab_info[part][tab][uve_name].added()):
                    touched = True
                    self._logger.debug("UVE %s added structs %s" % (uv, \
                            self.ptab_info[part][tab][uve_name].added()))
                    for adds in self.ptab_info[part][tab][uve_name].added():
                        output[uv][adds] = \
                                self.ptab_info[part][tab][uve_name].values()[adds]
            else:
                for typ in types:
                    val = None
                    if typ in uve_data:
                        val = uve_data[typ]
                    self.ptab_info[part][tab][uve_name].update_single(typ, val)
                    if len(self.ptab_info[part][tab][uve_name].removed()):
                        touched = True
                        rset = self.ptab_info[part][tab][uve_name].removed()
			self._logger.info("UVE %s removed structs %s" % (uv, rset))
			uveq_trace = UVEQTrace()
			uveq_trace.uves = [uv + "-" + str(rset)]
			uveq_trace.part = part
			uveq_trace.oper = "remove"
			uveq_trace.trace_msg(name="UVEQTrace",\
				sandesh=self._sandesh)
                        for rems in self.ptab_info[part][tab][uve_name].removed():
                            output[uv][rems] = None
                    if len(self.ptab_info[part][tab][uve_name].changed()):
                        touched = True
                        self._logger.debug("UVE %s changed structs %s" % (uve_name, \
                                self.ptab_info[part][tab][uve_name].changed()))
                        for chgs in self.ptab_info[part][tab][uve_name].changed():
                            output[uv][chgs] = \
                                    self.ptab_info[part][tab][uve_name].values()[chgs]
                    if len(self.ptab_info[part][tab][uve_name].added()):
                        touched = True
                        self._logger.debug("UVE %s added structs %s" % (uve_name, \
                                self.ptab_info[part][tab][uve_name].added()))
                        for adds in self.ptab_info[part][tab][uve_name].added():
                            output[uv][adds] = \
                                    self.ptab_info[part][tab][uve_name].values()[adds]
            if not touched:
                del output[uv]
            local_uve = self.ptab_info[part][tab][uve_name].values()
            
            self.tab_perf[tab].record_pub(UTCTimestampUsec() - prevt)

            if len(local_uve.keys()) == 0:
                self._logger.info("UVE %s deleted in proc" % (uv))
                del self.ptab_info[part][tab][uve_name]
                output[uv] = None
                
                if tab in self.tab_alarms:
                    if uv in self.tab_alarms[tab]:
                        del_types = []
                        for nm, asm in self.tab_alarms[tab][uv].iteritems():
                            delete_alarm = \
                                self.tab_alarms[tab][uv][nm].clear_alarms()
                            if delete_alarm:
                                del_types.append(nm)
                        for nm in del_types:
                            del self.tab_alarms[tab][uv][nm]
                        self.send_alarm_update(tab, uv)
                # Both alarm and non-alarm contents are gone.
                # We do not need to do alarm evaluation
                continue
            
            # Withdraw the alarm if the UVE has no non-alarm structs
            if len(local_uve.keys()) == 1 and "UVEAlarms" in local_uve:
                if tab in self.tab_alarms:
                    if uv in self.tab_alarms[tab]:
                        self._logger.info("UVE %s has no non-alarm" % (uv))
                        del_types = []
                        for nm, asm in self.tab_alarms[tab][uv].iteritems():
                            delete_alarm = \
                                self.tab_alarms[tab][uv][nm].clear_alarms()
                            if delete_alarm:
                                del_types.append(nm)
                        for nm in del_types:
                            del self.tab_alarms[tab][uv][nm]
                        self.send_alarm_update(tab, uv)
                continue
            # Examine UVE to check if alarm need to be raised/deleted
            self.examine_uve_for_alarms(uv, local_uve)
        if success:
	    uveq_trace = UVEQTrace()
	    uveq_trace.uves = output.keys()
	    uveq_trace.part = part
	    uveq_trace.oper = "proc-output"
	    uveq_trace.trace_msg(name="UVEQTrace",\
		    sandesh=self._sandesh)
            self._logger.info("Ending UVE proc for part %d" % part)
            return output
        else:
	    uveq_trace = UVEQTrace()
	    uveq_trace.uves = erruves
	    uveq_trace.part = part
	    uveq_trace.oper = "proc-error"
	    uveq_trace.trace_msg(name="UVEQTrace",\
		    sandesh=self._sandesh)
            self._logger.info("Ending UVE proc for part %d with error" % part)
            return None
 
    def handle_UVETableInfoReq(self, req):
        if req.partition == -1:
            parts = self.ptab_info.keys()
        else:
            parts = [req.partition]
        
        self._logger.info("Got UVETableInfoReq : %s" % str(parts))
        np = 1
        for part in parts:
            if part not in self.ptab_info:
                continue
            tables = []
            for tab in self.ptab_info[part].keys():
                uvel = []
                for uk,uv in self.ptab_info[part][tab].iteritems():
                    types = []
                    for tk,tv in uv.values().iteritems():
                        types.append(UVEStructInfo(type = tk,
                                content = json.dumps(tv)))
                    uvel.append(UVEObjectInfo(
                            name = uk, structs = types))
                tables.append(UVETableInfo(table = tab, uves = uvel))
            resp = UVETableInfoResp(partition = part)
            resp.tables = tables

            if np == len(parts):
                mr = False
            else:
                mr = True
            resp.response(req.context(), mr)
            np = np + 1

    def handle_UVETableAlarmReq(self, req):
        status = False
        if req.table == "all":
            parts = self.tab_alarms.keys()
        else:
            parts = [req.table]
        self._logger.info("Got UVETableAlarmReq : %s" % str(parts))
        np = 1
        for pt in parts:
            resp = UVETableAlarmResp(table = pt)
            uves = []
            for uk,uv in self.tab_alarms[pt].iteritems():
                for ak,av in uv.iteritems():
                    alm_copy = []
                    uai = av.get_uai(forced=True)
                    if uai:
                        alm_copy.append(copy.deepcopy(uai))
                        uves.append(UVEAlarmStateMachineInfo(
                            uai = UVEAlarms(name = uk, alarms = alm_copy),
                            uac = av.get_uac(), uas = av.get_uas()))
            resp.uves = uves 
            if np == len(parts):
                mr = False
            else:
                mr = True
            resp.response(req.context(), mr)
            np = np + 1

    def handle_UVETablePerfReq(self, req):
        status = False
        if req.table == "all":
            parts = self.tab_perf_prev.keys()
        else:
            parts = [req.table]
        self._logger.info("Got UVETablePerfReq : %s" % str(parts))
        np = 1
        for pt in parts:
            resp = UVETablePerfResp(table = pt)
            resp.call_time = self.tab_perf_prev[pt].call_result()
            resp.get_time = self.tab_perf_prev[pt].get_result()
            resp.pub_time = self.tab_perf_prev[pt].pub_result()
            resp.updates = self.tab_perf_prev[pt].get_n

            if np == len(parts):
                mr = False
            else:
                mr = True
            resp.response(req.context(), mr)
            np = np + 1

    def handle_AlarmConfigRequest(self, req):
        config_db = self._config_handler.config_db()
        alarm_config_db = config_db.get('alarm', {})
        alarms = []
        if req.name is not None:
            alarm_config = alarm_config_db.get(req.name)
            alarm_config_db = {req.name: alarm_config} if alarm_config else {}
        for name, config in alarm_config_db.iteritems():
            config_dict = {k: json.dumps(v) for k, v in \
                self._config_handler.obj_to_dict(config).iteritems()}
            alarms.append(AlarmConfig(config_dict))
        res = AlarmConfigResponse(alarms)
        res.response(req.context())
    # end handle_AlarmConfigRequest

    def partition_change(self, parts, enl):
        """
        Call this function when getting or giving up
        ownership of a partition
        Args:
            parts : Set of Partition Numbers, or a single partition
            enl   : True for acquiring, False for giving up
        Returns: 
            status of operation (True for success)
        """
        if not isinstance(parts,set):
            parts = set([parts])
        status = False
        if enl:
            if len(parts - set(self._workers.keys())) != len(parts):
                self._logger.info("Dup partitions %s" % \
                    str(parts.intersection(set(self._workers.keys()))))
            else:
                for partno in parts:
                    ph = UveStreamProc(','.join(self._conf.kafka_broker_list()),
                            partno, self._conf.kafka_prefix()+"-uve-" + str(partno),
                            self._logger,
                            self.handle_uve_notifq, self._conf.host_ip(),
                            self.handle_resource_check,
                            self._instance_id,
                            self._conf.redis_server_port(),
                            self._conf.kafka_prefix()+"-workers")
                    ph.start()
                    self._workers[partno] = ph
                    self._uvestats[partno] = {}

                tout = 1200
                idx = 0
                while idx < tout:
                    # When this partitions starts,
                    # uveq will get created
                    if len(parts - set(self._uveq.keys())) != 0:
                        gevent.sleep(.1)
                    else:
                        break
                    idx += 1
                if len(parts - set(self._uveq.keys())) == 0:
                    status = True 
                else:
                    # TODO: The partition has not started yet,
                    #       but it still might start later.
                    #       We possibly need to exit
                    status = False
                    self._logger.error("Unable to start partitions %s" % \
                            str(parts - set(self._uveq.keys())))
        else:
            if len(parts - set(self._workers.keys())) == 0:
                for partno in parts:
                    ph = self._workers[partno]
                    self._logger.error("Kill part %s" % str(partno))
                    ph.kill(timeout=60)
                    try:
                        res,db = ph.get(False)
                    except gevent.Timeout:
                        self._logger.error("Unable to kill partition %d" % partno)
                        return False

                    self._logger.error("Returned " + str(res))
                    self._uveqf[partno] = self._workers[partno].acq_time()
                    del self._workers[partno]
                    del self._uvestats[partno]

                tout = 1200
                idx = 0
                self._logger.error("Wait for parts %s to exit" % str(parts))
                while idx < tout:
                    # When this partitions stop.s
                    # uveq will get destroyed
                    if len(parts - set(self._uveq.keys())) != len(parts):
                        gevent.sleep(.1)
                    else:
                        break
                    idx += 1
                if len(parts - set(self._uveq.keys())) == len(parts):
                    status = True 
                    self._logger.error("Wait done for parts %s to exit" % str(parts))
                else:
                    # TODO: The partition has not stopped yet
                    #       but it still might stop later.
                    #       We possibly need to exit
                    status = False
                    self._logger.error("Unable to stop partitions %s" % \
                            str(parts.intersection(set(self._uveq.keys()))))
            else:
                self._logger.info("No partition %d" % partno)

        return status
    
    def handle_PartitionOwnershipReq(self, req):
        self._logger.info("Got PartitionOwnershipReq: %s" % str(req))
        status = self.partition_change(req.partition, req.ownership)

        resp = PartitionOwnershipResp()
        resp.status = status
	resp.response(req.context())
               
    def process_stats(self):
        ''' Go through the UVEKey-Count stats collected over 
            the previous time period over all partitions
            and send it out
        '''
        self.tab_perf_prev = copy.deepcopy(self.tab_perf)
        for kt in self.tab_perf.keys():
            #self.tab_perf_prev[kt] = copy.deepcopy(self.tab_perf[kt])
            self.tab_perf[kt].reset()

        s_partitions = set()
        s_keys = set()
        n_updates = 0
        for pk,pc in self._workers.iteritems():
            s_partitions.add(pk)
            din = pc.stats()
            dout = copy.deepcopy(self._uvestats[pk])
            self._uvestats[pk] = {}
            for ktab,tab in dout.iteritems():
                utct = UVETableCount()
                utct.keys = 0
                utct.count = 0
                for uk,uc in tab.iteritems():
                    s_keys.add(uk)
                    n_updates += uc
                    utct.keys += 1
                    utct.count += uc
                au_obj = AlarmgenUpdate(name=self._sandesh._source + ':' + \
                        self._sandesh._node_type + ':' + \
                        self._sandesh._module + ':' + \
                        self._sandesh._instance_id,
                        partition = pk,
                        table = ktab,
                        o = utct,
                        i = None,
                        sandesh=self._sandesh)
                self._logger.debug('send output stats: %s' % (au_obj.log()))
                au_obj.send(sandesh=self._sandesh)

            for ktab,tab in din.iteritems():
                au_notifs = []
                for kcoll,coll in tab.iteritems():
                    for kgen,gen in coll.iteritems():
                        for tk,tc in gen.iteritems():
                            tkc = UVETypeInfo()
                            tkc.type= tk
                            tkc.count = tc
                            tkc.generator = kgen
                            tkc.collector = kcoll
                            au_notifs.append(tkc)
                au_obj = AlarmgenUpdate(name=self._sandesh._source + ':' + \
                        self._sandesh._node_type + ':' + \
                        self._sandesh._module + ':' + \
                        self._sandesh._instance_id,
                        partition = pk,
                        table = ktab,
                        o = None,
                        i = au_notifs,
                        sandesh=self._sandesh)
                self._logger.debug('send input stats: %s' % (au_obj.log()))
                au_obj.send(sandesh=self._sandesh)

        au = AlarmgenStatus()
        au.name = self._hostname
        au.counters = []
        au.alarmgens = []
        ags = AlarmgenStats()
        ags.instance =  self._instance_id
        ags.partitions = len(s_partitions)
        ags.keys = len(s_keys)
        ags.updates = n_updates
        au.counters.append(ags)

        agname = self._sandesh._source + ':' + \
                        self._sandesh._node_type + ':' + \
                        self._sandesh._module + ':' + \
                        self._sandesh._instance_id
        au.alarmgens.append(agname)
 
        atrace = AlarmgenStatusTrace(data = au, sandesh = self._sandesh)
        self._logger.debug('send alarmgen status : %s' % (atrace.log()))
        atrace.send(sandesh=self._sandesh)
         
    def handle_PartitionStatusReq(self, req):
        ''' Return the entire contents of the UVE DB for the 
            requested partitions
        '''
        if req.partition == -1:
            parts = self._workers.keys()
        else:
            parts = [req.partition]
        
        self._logger.info("Got PartitionStatusReq: %s" % str(parts))
        np = 1
        for pt in parts:
            resp = PartitionStatusResp()
            resp.partition = pt
            if self._workers.has_key(pt):
                resp.enabled = True
                resp.offset = self._workers[pt]._partoffset
                resp.uves = []
                for kcoll,coll in self._workers[pt].contents().iteritems():
                    uci = UVECollInfo()
                    uci.collector = kcoll
                    uci.uves = []
                    for kgen,gen in coll.iteritems():
                        ugi = UVEGenInfo()
                        ugi.generator = kgen
                        ugi.uves = []
                        for tabk,tabc in gen.iteritems():
                            for uk,uc in tabc.iteritems():
                                ukc = UVEKeyInfo()
                                ukc.key = tabk + ":" + uk
                                ukc.types = []
                                for tk,tc in uc.iteritems():
                                    uvtc = UVETypeCount()
                                    uvtc.type = tk
                                    uvtc.count = tc["c"]
                                    uvtc.agg_uuid = str(tc["u"])
                                    ukc.types.append(uvtc)
                                ugi.uves.append(ukc)
                        uci.uves.append(ugi)
                    resp.uves.append(uci)
            else:
                resp.enabled = False
            if np == len(parts):
                mr = False
            else:
                mr = True
            resp.response(req.context(), mr)
            np = np + 1

    def alarm_ack_callback(self, alarm_req):
        '''
        Callback function for sandesh alarm acknowledge request.
        This method is passed as a parameter in the init_generator().
        Upon receiving the SandeshAlarmAckRequest, the corresponding
        handler defined in the sandesh library would invoke this callback.
        This function returns one of the response codes defined in
        SandeshAlarmAckResponseCode.
        '''
        self._logger.debug('Alarm acknowledge request callback: %s' %
                           str(alarm_req))
        table = alarm_req.table
        uname = alarm_req.table+':'+alarm_req.name
        atype = alarm_req.type
        try:
            alarm_type = \
                self.tab_alarms[table][uname][atype].get_uai()
        except KeyError:
            return SandeshAlarmAckResponseCode.ALARM_NOT_PRESENT
        else:
            # Either alarm is not present ot it is not in Active or Soak_Idle
            # state
            if alarm_type is None:
                return SandeshAlarmAckResponseCode.ALARM_NOT_PRESENT
            # Either the timestamp sent by the client is invalid or
            # the alarm is updated.
            if alarm_type.timestamp != alarm_req.timestamp:
                return SandeshAlarmAckResponseCode.INVALID_ALARM_REQUEST
            # If the alarm was already acknowledged, just return SUCCESS.
            if alarm_type.ack:
                return SandeshAlarmAckResponseCode.SUCCESS
            # All sanity checks passed. Acknowledge the alarm.
            alarm_type.ack = True
            alarm = []
            for nm, asm in self.tab_alarms[table][uname].iteritems():
                uai = asm.get_uai()
                if uai:
                    alarm.append(copy.deepcopy(uai))
            alarm_data = UVEAlarms(name=alarm_req.name, alarms=alarm)
            alarm_sandesh = AlarmTrace(data=alarm_data, table=table,
                                       sandesh=self._sandesh)
            alarm_sandesh.send(sandesh=self._sandesh)
            return SandeshAlarmAckResponseCode.SUCCESS
    # end alarm_ack_callback

    def disc_cb_coll(self, clist):
        '''
        Analytics node may be brought up/down any time. For UVE aggregation,
        alarmgen needs to know the list of all Analytics nodes (redis-uves).
        Periodically poll the Collector list [in lieu of 
        redi-uve nodes] from the discovery. 
        '''
        self._logger.error("Discovery Collector callback : %s" % str(clist))
        newlist = []
        for elem in clist:
            ipaddr = elem["ip-address"]
            cpid = 0
            if "pid" in elem:
                cpid = int(elem["pid"])
            newlist.append((ipaddr, self._conf.redis_server_port(), cpid))
        self._us.update_redis_uve_list(newlist)

    def disc_cb_ag(self, alist):
        '''
        Analytics node may be brought up/down any time. For partitioning,
        alarmgen needs to know the list of all Analytics nodes (alarmgens).
        Periodically poll the alarmgen list from the discovery service
        '''
        self._logger.error("Discovery AG callback : %s" % str(alist))
        newlist = []
        for elem in alist:
            ipaddr = elem["ip-address"]
            inst = elem["instance-id"]
            # If AlarmGenerator sends partitions as NULL, its
            # unable to provide service
            if elem["partitions"]:
                newlist.append(ipaddr + ":" + inst)

        # Shut down libpartition if the current alarmgen is not up
        if self._libpart_name not in newlist:
            if self._libpart:
                self._libpart.close()
                self._libpart = None
                if not self.partition_change(set(self._workers.keys()), False):
                    self._logger.error('AlarmGen withdraw failed!')
                    raise SystemExit(1)
                self._partset = set()
                self._logger.error('AlarmGen withdrawn!')
        else:
            if not self._libpart:
                self._libpart = self.start_libpart(newlist)
            else:
                self._libpart.update_cluster_list(newlist)

    def run_process_stats(self):
        while True:
            before = time.time()
            # Send out the UVEKey-Count stats for this time period
            self.process_stats()

            duration = time.time() - before
            if duration < 60:
                gevent.sleep(60 - duration)
            else:
                self._logger.error("Periodic collection took %s sec" % duration)

    def run(self):
        self.gevs = [ gevent.spawn(self.run_process_stats),
                      gevent.spawn(self.run_uve_processing)]

        if self.disc:
            sp1 = ServicePoller(self._logger, CollectorTrace,
                                self.disc,
                                COLLECTOR_DISCOVERY_SERVICE_NAME,
                                self.disc_cb_coll, self._sandesh)

            sp1.start()
            self.gevs.append(sp1)

            sp2 = ServicePoller(self._logger, AlarmgenTrace,
                                self.disc, ALARM_GENERATOR_SERVICE_NAME,
                                self.disc_cb_ag, self._sandesh)
            sp2.start()
            self.gevs.append(sp2)

        try:
            gevent.joinall(self.gevs)
        except KeyboardInterrupt:
            print 'AlarmGen Exiting on ^C'
        except gevent.GreenletExit:
            self._logger.error('AlarmGen Exiting on gevent-kill')
        except:
            raise
        finally:
            self._logger.error('AlarmGen stopping everything')
            self.stop()
            exit()

    def stop(self):
        self._sandesh._client._connection.set_admin_state(down=True)
        self._sandesh.uninit()
        if self._config_handler:
            self._config_handler.stop()
        l = len(self.gevs)
        for idx in range(0,l):
            self._logger.error('AlarmGen killing %d of %d' % (idx+1, l))
            self.gevs[0].kill()
            self._logger.error('AlarmGen joining %d of %d' % (idx+1, l))
            self.gevs[0].join()
            self._logger.error('AlarmGen stopped %d of %d' % (idx+1, l))
            self.gevs = self.gevs[1:]

    def sigterm_handler(self):
        self.stop()
        exit()

def setup_controller(argv):
    config = CfgParser(argv)
    config.parse()
    return Controller(config)

def main(args=None):
    controller = setup_controller(args or ' '.join(sys.argv[1:]))
    gevent.hub.signal(signal.SIGTERM, controller.sigterm_handler)
    gv = gevent.getcurrent()
    gv._main_obj = controller
    controller.run()

if __name__ == '__main__':
    main()
