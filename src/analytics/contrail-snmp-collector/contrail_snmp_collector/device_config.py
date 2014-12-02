from snmp import SnmpSession
import copy

class DeviceConfig(object):
    def __init__(self, name, cfg={}, mibs=[], flow_export_source_ip=None):
        self._raw = cfg
        self.name = name
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
        cfg['DestHost'] = self.name
        cfg['Version'] = int(cfg['Version'])
        return cfg
