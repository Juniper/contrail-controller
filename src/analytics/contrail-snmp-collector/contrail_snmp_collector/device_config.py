#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from snmp import SnmpSession
import copy, traceback, time, ConfigParser
from vnc_api.vnc_api import VncApi

class DeviceDict(dict):
    def __init__(self, name, vnc, *a, **k):
        super(DeviceDict, self).__init__(*a, **k)
        self.name = name
        self._vnc = vnc

    def vnc(self):
        return self._vnc


class DeviceConfig(object):
    __pat = None

    def __init__(self, name, cfg={}, mibs=[], flow_export_source_ip=None,
                 mgmt_ip=None):
        self._raw = cfg
        self.name = name
        self.snmp_ip = mgmt_ip or name
        self.mibs = mibs or SnmpSession.TABLES() #all
        self.flow_export_source_ip = flow_export_source_ip
        self.snmp_name = None

    def set_snmp_name(self, name):
        self.snmp_name = name

    def get_snmp_name(self):
        return self.snmp_name

    def set_mibs(self, mib):
        if mib in SnmpSession.TABLES() and mib not in self.mibs:
            self.mibs.append(mib)

    def get_mibs(self):
        return self.mibs

    def get_flow_export_source_ip(self):
        return self.flow_export_source_ip

    def snmp_cfg(self):
        cfg = copy.copy(self._raw)
        cfg['DestHost'] = self.snmp_ip
        cfg['Version'] = int(cfg['Version'])
        return cfg

    @classmethod
    def _pat(self):
        if self.__pat is None:
           self.__pat = re.compile(', *| +')
        return self.__pat

    @staticmethod
    def _get_and_remove_key(data_dict, key, default=None):
        val = default
        if key in data_dict:
            val = data_dict[key]
            del data_dict[key]
        return val

    @classmethod
    def _mklist(self, s):
        if isinstance(s, str):
            return self._pat().split(s)
        return s

    @staticmethod
    def fom_file(filename):
        devices = []
        devcfg = ConfigParser.SafeConfigParser()
        devcfg.optionxform = str
        devcfg.read([filename])
        for dev in devcfg.sections():
            nd = dict(devcfg.items(dev))
            mibs = DeviceConfig._mklist(DeviceConfig._get_and_remove_key(nd,
                        'Mibs', []))
            flow_export_source_ip = DeviceConfig._get_and_remove_key(nd,
                    'FlowExportSourceIp')
            devices.append(DeviceConfig(dev, nd, mibs,
                        flow_export_source_ip))
        return devices

    @staticmethod
    def get_vnc(usr, passwd, tenant, api_servers, use_ssl=False,
            auth_host=None, auth_port=None, auth_protocol=None, notifycb=None):
        e = IOError('Api servers (%s) not reachable' % ','.join(api_servers))
        while True:
          for api_server in api_servers:
            srv = api_server.split(':')
            try:
                vnc = VncApi(usr, passwd, tenant, srv[0], srv[1],
                        api_server_use_ssl=use_ssl,
                        auth_host=auth_host, auth_port=auth_port,
                        auth_protocol=auth_protocol)
                if callable(notifycb):
                    notifycb('api', 'Connected', servers=api_server)
                return vnc
            except Exception as e:
                traceback.print_exc()
                if callable(notifycb):
                    notifycb('api', 'Not connected', servers=api_server,
                            up=False)
                time.sleep(3)

    @staticmethod
    def fom_api_server(api_servers, usr, passwd, tenant, use_ssl=False,
            auth_host=None, auth_port=None, auth_protocol=None, notifycb=None):
        while True:
            try:
                vnc = DeviceConfig.get_vnc(usr, passwd, tenant, api_servers,
                        use_ssl=use_ssl, auth_host=auth_host,
                        auth_port=auth_port, auth_protocol=auth_protocol,
                        notifycb=notifycb)
                devices = map(lambda e: DeviceDict(e['fq_name'][-1], vnc, **e),
                                                   vnc.physical_routers_list()[
                                'physical-routers'])
                return devices
            except Exception as e:
                traceback.print_exc()
                if callable(notifycb):
                    notifycb('api', 'Not connected', servers=','.join(
                             api_servers), up=False)
                time.sleep(3)

    @staticmethod
    def populate_cfg(devicelist):
        devices = []
        vnc = devicelist[0].vnc()
        for pr in vnc.physical_routers_list(detail=True, obj_uuids=[
                x['uuid'] for x in devicelist]):
            snmp = pr.get_physical_router_snmp_credentials()
            if snmp:
                nd = { 'Version': snmp.get_version() }
                if snmp.get_local_port():
                    nd['RemotePort'] = snmp.get_local_port()
                if snmp.get_retries():
                    nd['Retries'] = snmp.get_retries()
                if snmp.get_timeout():
                    #timeout converted from s to us
                    nd['Timeout'] = snmp.get_timeout()*1000000
                if nd['Version'] in (1, 2):
                    nd['Community'] = snmp.get_v2_community()
                elif nd['Version'] == 3:
                    if snmp.get_v3_security_name():
                        nd['SecName'] = snmp.get_v3_security_name()
                    if snmp.get_v3_security_level():
                        nd['SecLevel'] = snmp.get_v3_security_level()
                    if snmp.get_v3_security_engine_id():
                        nd['SecEngineId'] = snmp.get_v3_security_engine_id()
                    if snmp.get_v3_context():
                        nd['Context'] = snmp.get_v3_context()
                    if snmp.get_v3_context_engine_id():
                        nd['ContextEngineId'] = snmp.get_v3_context_engine_id()
                    if snmp.get_v3_authentication_protocol():
                        nd['AuthProto'] = snmp.get_v3_authentication_protocol()
                    if snmp.get_v3_authentication_password():
                        nd['AuthPass'] = snmp.get_v3_authentication_password()
                    if snmp.get_v3_privacy_protocol():
                        nd['PrivProto'] = snmp.get_v3_privacy_protocol()
                    if snmp.get_v3_privacy_password():
                        nd['PrivProto'] = snmp.get_v3_privacy_password()
                devices.append(DeviceConfig(
                            pr.name,
                            nd, [], pr.get_physical_router_management_ip(),
                            pr.get_physical_router_management_ip()))
        return devices
