#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import pprint, socket, copy
import datetime
from pysandesh.sandesh_base import *
from pysandesh.connection_info import ConnectionState
from sandesh.prouter.ttypes import ArpTable, IfTable, IfXTable, IfStats, \
         IpMib, LldpSystemCapabilitiesMap, LldpLocManAddrEntry, \
         LldpLocalSystemData, LldpRemOrgDefInfoTable, \
         LldpRemOrgDefInfoTable, LldpRemOrgDefInfoEntry, \
         LldpRemManAddrEntry, LldpRemoteSystemsData, \
         dot1qTpFdbPortTable, dot1dBasePortIfIndexTable, \
         LldpTable, PRouterEntry, PRouterUVE, PRouterFlowEntry, \
         PRouterFlowUVE, IfIndexOperStatusTable, LldpLocPortEntry
from sandesh.nodeinfo.ttypes import NodeStatusUVE, NodeStatus
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, CategoryNames,\
     ModuleCategoryMap, Module2NodeType, NodeTypeNames, ModuleIds,\
     INSTANCE_ID_DEFAULT
from pysandesh.gen_py.process_info.ttypes import ConnectionType,\
    ConnectionStatus

class SnmpUve(object):
    _composite_rules = dict(
            ifInPkts=['ifInUcastPkts', 'ifInBroadcastPkts', 'ifInMulticastPkts'],
            ifOutPkts=['ifOutUcastPkts', 'ifOutBroadcastPkts',
                       'ifOutMulticastPkts'],
            )

    def __init__(self, conf, instance='0'):
        self._conf = conf
        module = Module.CONTRAIL_SNMP_COLLECTOR
        self._moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        self._node_type_name = NodeTypeNames[node_type]
        self.table = "ObjectCollectorInfo"
        self._hostname = socket.gethostname()
        self._instance_id = instance
        if self._conf.sandesh_send_rate_limit() is not None:
            SandeshSystem.set_sandesh_send_rate_limit( \
                self._conf.sandesh_send_rate_limit());
        sandesh_global.init_generator(self._moduleid, self._hostname,
                                      self._node_type_name, self._instance_id,
                                      self._conf.random_collectors,
                                      self._node_type_name,
                                      self._conf.http_port(),
                                      ['contrail_snmp_collector.sandesh'],
                                      config=self._conf.sandesh_config())
        sandesh_global.set_logging_params(
            enable_local_log=self._conf.log_local(),
            category=self._conf.log_category(),
            level=self._conf.log_level(),
            file=self._conf.log_file(),
            enable_syslog=self._conf.use_syslog(),
            syslog_facility=self._conf.syslog_facility())
        ConnectionState.init(sandesh_global, self._hostname, self._moduleid,
            self._instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus, self.table)

        self.if_stat = {}
        self._logger = sandesh_global.logger()

    def map_svc(self, svc):
        return {
                'api'         : ConnectionType.APISERVER,
                'zookeeper'   : ConnectionType.ZOOKEEPER,
            }[svc]

    def map_sts(self, up):
        return {
            True   : ConnectionStatus.UP,
            False  : ConnectionStatus.DOWN,
        }[up]

    def conn_state_notify(self, svc, msg='', up=True, servers=''):
        ctype = self.map_svc(svc)
        status = self.map_sts(up)
        ConnectionState.update(conn_type=ctype, name='SNMP', status=status,
                message=msg, server_addrs=[servers])

    def killall(self):
        sandesh_global.uninit()

    def delete(self, dev):
        PRouterUVE(data=PRouterEntry(**dict(
                    name=dev['fq_name'][-1], deleted=True))).send()
        PRouterFlowUVE(data=PRouterFlowEntry(**dict(
                    name=dev['fq_name'][-1], deleted=True))).send()

    def logger(self):
        return self._logger

    def make_composites(self, ife):
        for k in self._composite_rules.keys():
            for cmpsr in self._composite_rules[k]:
                if cmpsr in ife:
                    if k not in ife:
                        ife[k] = 0
                    ife[k] += ife[cmpsr]

    def get_diff(self, data):
        pname = data['name']
        if pname not in self.if_stat:
            self.if_stat[pname] = {}
        diffs = {}
        iftables = {}
        for ife in data['ifMib']['ifTable'] + data['ifMib']['ifXTable']:
            if 'ifDescr' in ife:
                ifname = ife['ifDescr']
            elif 'ifName' in ife:
                ifname = ife['ifName']
            else:
                print str(datetime.datetime.now()),'Err: ', ife.keys(), pname
                continue
            if ifname not in iftables:
                iftables[ifname] = {}
            iftables[ifname].update(ife)
        for ifname, ife in iftables.items():
            self.make_composites(ife)

            if ifname not in diffs:
                diffs[ifname] = {}
            if ifname in self.if_stat[pname]:
                diffs[ifname].update(self.diff(self.if_stat[pname][ifname], ife,
                                     ifname))
            if ifname not in self.if_stat[pname]:
                self.if_stat[pname][ifname] = {}
            self.if_stat[pname][ifname].update(self.stat_req(ife))
        data['ifStats'] = map(lambda x: IfStats(**x), diffs.values())

    def diff(self, old, new, ifname):
        d = dict(ifIndex=new['ifIndex'], ifName=ifname)
        for k in self.stat_list():
            if k in new and k in old:
                d[k] = new[k] - old[k]
                if d[k] < 0:
                    d[k] = 0 # safety check for data miss
        return d

    def stat_list(self):
        return ('ifInPkts', 'ifOutPkts', 'ifInOctets', 'ifOutOctets',
                'ifInDiscards', 'ifInErrors', 'ifOutDiscards', 'ifOutErrors',)

    def stat_req(self, d):
        r = {}
        for k in self.stat_list():
            if k in d:
                r[k] = d[k]
        return r

    def send_ifstatus_update(self, data):
        for dev in data:
            ifIndexOperStatusTable = []
            for ifidx in data[dev]:
                ifIndexOperStatusTable.append(IfIndexOperStatusTable(
                            ifIndex=ifidx, ifOperStatus=data[dev][ifidx][0]))
            PRouterUVE(data=PRouterEntry(
                    name=dev,
                    ifIndexOperStatusTable=ifIndexOperStatusTable)).send()
        
    def send_flow_uve(self, data):
        if data['name']:
            PRouterFlowUVE(data=PRouterFlowEntry(**data)).send()

    def send(self, data):
        uve = self.make_uve(data)
        self.send_uve(uve)

    def _to_cap_map(self, cm):
        return eval('LldpSystemCapabilitiesMap.' + \
                LldpSystemCapabilitiesMap._VALUES_TO_NAMES[cm],
                globals(), locals())

    def make_uve(self, data):
        # print 'Building UVE:', data['name']
        if 'arpTable' in data:
            data['arpTable'] = map(lambda x: ArpTable(**x),
                    data['arpTable'])
        if 'ifMib' in data:
            self.get_diff(data)
            if 'ifTable' in data['ifMib']:
                data['ifTable'] = map(lambda x: IfTable(**x),
                    data['ifMib']['ifTable'])
            if 'ifXTable' in data['ifMib']:
                data['ifXTable'] = map(lambda x: IfXTable(**x),
                    data['ifMib']['ifXTable'])
            del data['ifMib']
        if 'ipMib' in data:
            data['ipMib'] = map(lambda x: IpMib(**x), data['ipMib'])
        if 'lldpTable' in data:
            if 'lldpLocalSystemData' in data['lldpTable']:
                if 'lldpLocManAddrEntry' in data['lldpTable'][
                  'lldpLocalSystemData']:
                    data['lldpTable']['lldpLocalSystemData'][
                        'lldpLocManAddrEntry'] = LldpLocManAddrEntry(
                            **data['lldpTable']['lldpLocalSystemData'][
                            'lldpLocManAddrEntry'])
                if 'lldpLocSysCapEnabled' in data['lldpTable'][
                  'lldpLocalSystemData']:
                    data['lldpTable']['lldpLocalSystemData'][
                        'lldpLocSysCapEnabled'] = map(self._to_cap_map, data[
                            'lldpTable']['lldpLocalSystemData'][
                            'lldpLocSysCapEnabled'])
                if 'lldpLocSysCapSupported' in data['lldpTable'][
                  'lldpLocalSystemData']:
                    data['lldpTable']['lldpLocalSystemData'][
                        'lldpLocSysCapSupported'] = map(self._to_cap_map,
                            data['lldpTable']['lldpLocalSystemData'][
                            'lldpLocSysCapSupported'])
                if 'lldpLocPortTable' in data['lldpTable'][
                                                        'lldpLocalSystemData']:
                    data['lldpTable']['lldpLocalSystemData'][
                        'lldpLocPortTable'] = [LldpLocPortEntry(
                            **x) for x in data['lldpTable'][
                            'lldpLocalSystemData']['lldpLocPortTable'].values()]
                data['lldpTable']['lldpLocalSystemData'] = \
                    LldpLocalSystemData(**data['lldpTable'][
                            'lldpLocalSystemData'])

            rl = []
            if 'lldpRemoteSystemsData' in data['lldpTable']:
                for d in data['lldpTable']['lldpRemoteSystemsData']:

                    if 'lldpRemSysCapEnabled' in d:
                        d['lldpRemSysCapEnabled'] = map(self._to_cap_map,
                                d['lldpRemSysCapEnabled'])
                    if 'lldpRemSysCapSupported' in d:
                        d['lldpRemSysCapSupported'] = map(self._to_cap_map,
                                d['lldpRemSysCapSupported'])
                    if 'lldpRemOrgDefInfoEntry' in d:
                        if 'lldpRemOrgDefInfoTable' in d[
                                                'lldpRemOrgDefInfoEntry']:
                            d['lldpRemOrgDefInfoEntry'][
                                 'lldpRemOrgDefInfoTable'] = map(lambda x: \
                                     LldpRemOrgDefInfoTable(**x),
                                     d['lldpRemOrgDefInfoEntry'][
                                        'lldpRemOrgDefInfoTable'])
                            d['lldpRemOrgDefInfoEntry'] = \
                                LldpRemOrgDefInfoEntry(**d[
                                        'lldpRemOrgDefInfoEntry'])
                    if 'lldpRemManAddrEntry' in d:
                        d['lldpRemManAddrEntry'] = LldpRemManAddrEntry(
                                **d['lldpRemManAddrEntry'])
                    rl.append(LldpRemoteSystemsData(**d))
                data['lldpTable']['lldpRemoteSystemsData'] = rl
            data['lldpTable'] = LldpTable(**data['lldpTable'])
        if 'qBridgeTable' in data:
            if 'dot1qTpFdbPortTable' in data['qBridgeTable']:
                data['fdbPortTable'] = map(lambda x: dot1qTpFdbPortTable(**x),
                    data['qBridgeTable']['dot1qTpFdbPortTable'])
            if 'dot1dBasePortIfIndexTable' in data['qBridgeTable']:
                data['fdbPortIfIndexTable'] = map(lambda x: dot1dBasePortIfIndexTable(**x),
                    data['qBridgeTable']['dot1dBasePortIfIndexTable'])
            del data['qBridgeTable']
        return PRouterUVE(data=PRouterEntry(**data))

    def send_uve(self, uve):
        # print 'Sending UVE:', uve.data.name
        uve.send()

    def sandesh_reconfig_collectors(self, collectors):
        sandesh_global.reconfig_collectors(collectors)
