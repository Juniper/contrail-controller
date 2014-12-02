from snmp import SnmpSession
import copy

class DeviceConfig(object):
    def __init__(self, name, cfg={}, mibs=[]):
        self._raw = cfg
        self.name = name
        self.mibs = mibs or SnmpSession.TABLES() #all

    def set_mibs(self, mib):
        if mib in SnmpSession.TABLES() and mib not in self.mibs:
            self.mibs.append(mib)

    def get_mibs(self):
        return self.mibs
        
    def snmp_cfg(self):
        cfg = copy.copy(self._raw)
        cfg['DestHost'] = self.name
        cfg['Version'] = int(cfg['Version'])
        return cfg
