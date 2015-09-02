#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# generator_introspect_utils.py
#
# Python generator introspect utils
#

from lxml import etree
from opserver.introspect_util import *


class VerificationAlarmGen(IntrospectUtilBase):
    def __init__(self, ip, port):
        super(VerificationAlarmGen, self).__init__(ip, port, XmlDrv)
    #end __init__

    def get_PartitionStatus(self, partno):
        ''' 
        Eg.
        >>> xx = vag.get_PartitionStatus(3)
        >>> print xx
        {'partition': '3', 'enabled': 'true', 'uves': [{'collector': '127.0.0.1:6379', 'uves': [{'uves': ['ObjectGeneratorInfo:a6s40:Analytics:contrail-query-engine:0', 'ObjectGeneratorInfo:a6s40:Config:contrail-config-nodemgr:0', 'ObjectGeneratorInfo:a6s40:Analytics:contrail-topology:0', 'ObjectCollectorInfo:a6s40', 'ObjectGeneratorInfo:a6s40:Compute:contrail-vrouter-nodemgr:0', 'ObjectGeneratorInfo:a6s40:Analytics:contrail-analytics-nodemgr:0'], 'generator': 'a6s40:Analytics:contrail-collector:0'}, {'uves': ['ObjectGeneratorInfo:a6s40:Compute:contrail-vrouter-nodemgr:0', 'ObjectVRouter:a6s40'], 'generator': 'a6s40:Compute:contrail-vrouter-nodemgr:0'}, {'uves': ['ObjectGeneratorInfo:a6s40:Config:contrail-config-nodemgr:0'], 'generator': 'a6s40:Config:contrail-config-nodemgr:0'}, {'uves': ['ObjectCollectorInfo:a6s40', 'ObjectGeneratorInfo:a6s40:Analytics:contrail-analytics-nodemgr:0'], 'generator': 'a6s40:Analytics:contrail-analytics-nodemgr:0'}, {'uves': ['ObjectGeneratorInfo:a6s40:Analytics:contrail-query-engine:0', 'ObjectCollectorInfo:a6s40'], 'generator': 'a6s40:Analytics:contrail-query-engine:0'}, {'uves': ['ObjectCollectorInfo:a6s40'], 'generator': 'a6s40:Analytics:contrail-alarm-gen:0'}, {'uves': ['ObjectCollectorInfo:a6s40'], 'generator': 'a6s40:Analytics:contrail-analytics-api:0'}, {'uves': ['ObjectBgpPeer:default-domain:default-project:ip-fabric:__default__:a6s40:default-domain:default-project:ip-fabric:__default__:mx1'], 'generator': 'a6s40:Control:contrail-control:0'}, {'uves': ['ObjectVRouter:a6s40'], 'generator': 'a6s40:Compute:contrail-vrouter-agent:0'}]}]}
        >>>
        '''
        path = 'Snh_PartitionStatusReq?partition=%d' % partno
        xpath = '/PartitionStatusResp'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)

    def get_PartitionOwnership(self, partno, status):
        path = 'Snh_PartitionOwnershipReq?partition=%d&ownership=%d' % \
               (partno, status)
        xpath = '/PartitionOwnershipResp/status'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)

    def get_UVETableAlarm(self, table):
        '''
        Eg.
        >>> xx = vag.get_UVETableAlarm("ObjectCollectorInfo")
        >>> print xx
        {'table': 'ObjectCollectorInfo', 'uves': [{'alarms': [{'ack': 'false', 'type': 'ProcessStatus', 'description': [{'value': "{u'process_name': u'contrail-topology', u'process_state': u'PROCESS_STATE_STOPPED', u'last_stop_time': u'1423614955674319', u'start_count': 1, u'core_file_list': [], u'last_start_time': u'1423605696935359', u'stop_count': 1, u'last_exit_time': None, u'exit_count': 0}", 'rule': 'NodeStatus.process_info[].process_state != PROCESS_STATE_RUNNING'}]}], 'name': 'ObjectCollectorInfo:a6s40'}]}
        '''
        path = 'Snh_UVETableAlarmReq?table=%s' % table
        xpath = '/UVETableAlarmResp'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)
#end class VerificationAlarmGen
