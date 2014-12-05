import pprint, socket, copy
from pysandesh.sandesh_base import *
from pysandesh.connection_info import ConnectionState
from gen_py.prouter.ttypes import ArpTable, IfTable, IfXTable, IfStats, \
         IpMib, LldpSystemCapabilitiesMap, LldpLocManAddrEntry, \
         LldpLocalSystemData, LldpRemOrgDefInfoTable, \
         LldpRemOrgDefInfoTable, LldpRemOrgDefInfoEntry, \
         LldpRemManAddrEntry, LldpRemoteSystemsData, \
         LldpTable, PRouterEntry, PRouterUVE, PRouterFlowEntry, PRouterFlowUVE
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, CategoryNames,\
     ModuleCategoryMap, Module2NodeType, NodeTypeNames, ModuleIds,\
     INSTANCE_ID_DEFAULT

class SnmpUve(object):
    def __init__(self, conf):
        self._conf = conf
        module = Module.CONTRAIL_SNMP_COLLECTOR
        self._moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        self._node_type_name = NodeTypeNames[node_type]
        self._hostname = socket.gethostname()
        self._instance_id = '0'
        sandesh_global.init_generator(self._moduleid, self._hostname,
                                      self._node_type_name, self._instance_id,
                                      self._conf.collectors(),
                                      self._node_type_name,
                                      self._conf.http_port(),
                                      ['contrail_snmp_collector.gen_py'])
        sandesh_global.set_logging_params(
            enable_local_log=self._conf.log_local(),
            category=self._conf.log_category(),
            level=self._conf.log_level(),
            file=self._conf.log_file(),
            enable_syslog=self._conf.use_syslog(),
            syslog_facility=self._conf.syslog_facility())
        #ConnectionState.init(sandesh_global, self._hostname, self._moduleid,
        #    self._instance_id,
        #    staticmethod(ConnectionState.get_process_state_cb),
        #    NodeStatusUVE, NodeStatus)

        self.if_stat = {}

    def get_diff(self, data):
        pname = data['name']
        if pname not in self.if_stat:
            self.if_stat[pname] = {}
        diffs = {}
        for ife in data['ifMib']['ifTable'] + data['ifMib']['ifXTable']:
            if 'ifDescr' in ife:
                ifname = ife['ifDescr']
            elif 'ifName' in ife:
                ifname = ife['ifName']
            else:
                print 'Err: ', ife.keys()
                continue

            if ifname not in diffs:
                diffs[ifname] = {}
            if ifname in self.if_stat[pname]:
                diffs[ifname].update(self.diff(self.if_stat[pname][ifname],
                                                                        ife))
            if ifname not in self.if_stat[pname]:
                self.if_stat[pname][ifname] = {}
            self.if_stat[pname][ifname].update(self.stat_req(ife))
        data['ifStats'] = map(lambda x: IfStats(**x), diffs.values())

    def diff(self, old, new):
        d = {'ifIndex': new['ifIndex']}
        for k in self.stat_list():
            if k in new and k in old:
                d[k] = new[k] - old[k]
        return d

    def stat_list(self):
        return ('ifInUcastPkts', 'ifInMulticastPkts', 'ifInBroadcastPkts',
                'ifInDiscards', 'ifInErrors', 'ifOutUcastPkts',
                'ifOutMulticastPkts', 'ifOutBroadcastPkts', 'ifOutDiscards',
                'ifOutErrors',)

    def stat_req(self, d):
        r = {}
        for k in self.stat_list():
            if k in d:
                r[k] = d[k]
        return r

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
        print 'Building UVE:', data['name']
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
        return PRouterUVE(data=PRouterEntry(**data))

    def send_uve(self, uve):
        print 'Sending UVE:', uve.data.name
        uve.send()

