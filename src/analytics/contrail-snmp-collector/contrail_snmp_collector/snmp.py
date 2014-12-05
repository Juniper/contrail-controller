import struct, netsnmp, string

class SnmpTable(object):
    def __init__(self, session):
        self._session = session

    def get_obj(self, o):
        Vars = netsnmp.VarList(netsnmp.Varbind(o))
        r = self._session.walk(Vars)
        return {o: {'result': r, 'vars': Vars}}

    def get_table(self, table):
        if isinstance(table, str):
            return self.get_obj(table)
        elif isinstance(table, list):
            d = {}
            for o in table:
                d.update(self.get_obj(o))
            return d

    def _to_mac(self, d):
        try:
            return "%02x:%02x:%02x:%02x:%02x:%02x" % struct.unpack("BBBBBB", d)
        except:
            return d

    def _to_bits(self, d):
        l = len(d)
        return struct.unpack('B' * l, d)[0]

    def _is_int(self, x):
        return x.type in ('INTEGER', 'COUNTER', 'INTEGER32', 'TICKS', 'GAUGE',
                'COUNTER64', 'INTEGER64')

    def normalize(self, varbind, mactags=(), extra_process=None):
        if self._is_int(varbind):
            return int(varbind.val)
        if varbind.tag in mactags:
            return self._to_mac(varbind.val)
        if callable(extra_process):
            return extra_process(varbind)
        return self.default_value(varbind)

    def sane(self, x):
        if all(c in string.printable for c in x):
            return x
        return ':'.join(c.encode('hex') for c in x)

    def default_value(self, varbind):
        return self.sane(varbind.val)

    def snmp_get(self):
        snmpvars = self.get_table(self.table_names())
        self.translator(snmpvars)

    def translator(self, snmpvars):
        for k in snmpvars:
            getattr(self, k + '_translator')(snmpvars[k])

    def py_obj(self):
        raise NotImplementedError, "Virtual funtion"

    def table_names(self):
        raise NotImplementedError, "Virtual funtion"

class LldpLocSysNameTable(SnmpTable):
    def table_names(self):
        return 'lldpLocSysName'

    def lldpLocSysName_translator(self, snmp_dict):
        for x in snmp_dict['vars']:
            self.name = self.normalize(x)

    def py_obj(self):
        return self.name

class IpMib(SnmpTable):
    def table_names(self):
        return 'ipAdEntIfIndex'

    def ipAdEntIfIndex_translator(self, snmp_dict):
        self.ips = []
        for x in snmp_dict['vars']:
            self.ips.append({'ifIndex': self.normalize(x),
                    'ipAdEntIfIndex': x.iid})

    def py_obj(self):
        return self.ips

class LldpTable(SnmpTable):
    def __init__(self, session):
        super(LldpTable, self).__init__(session)
        self.lldpLocalSystemData = {}
        self.lldpRemoteSystemsData = {}

    def get_name(self):
        return self.lldpLocalSystemData.get('lldpLocSysName', 'localhost')

    def py_obj(self):
        return {'lldpLocalSystemData':self.lldpLocalSystemData,
            'lldpRemoteSystemsData':self.lldpRemoteSystemsData.values()}

    def table_names(self):
        return ['lldpLocalSystemData', 'lldpRemoteSystemsData']

    def _capmap(self, d):
        _maps = [
            'other(0)',
            'repeater(1)',
            'bridge(2)',
            'wlanAccessPoint(3)',
            'router(4)',
            'telephone(5)',
            'docsisCableDevice(6)',
            'stationOnly(7)'
        ]
        cmap = []
        for i in range(7):
            if d & (0x80>>i):
                #cmap.append(_maps[i])
                cmap.append(i)
        return cmap

    def capabilities(self, x):
        if x.type == 'BITS':
            return self._capmap(self._to_bits(x.val))
        return self.default_value(x)

    def lldpLocalSystemData_translator(self, snmp_dict):
        for x in filter(lambda x:'LocPort' not in x.tag, snmp_dict['vars']):
            if x.iid == '0':
                self.lldpLocalSystemData[x.tag] = self.normalize(x,
                        ('lldpLocChassisId', ), self.capabilities)
            elif x.iid:
                if x.iid not in self.lldpLocalSystemData:
                    tr = x.iid.split('.')
                    self.lldpLocalSystemData['lldpLocManAddrEntry'] = {
                         'lldpLocManAddrSubtype': int(tr[0]),
                         'lldpLocManAddrOIDLen': int(tr[1]),
                         'lldpLocManAddr': '.'.join(tr[2:]),
                    }
                self.lldpLocalSystemData['lldpLocManAddrEntry'][
                                                    x.tag] = self.normalize(x)

    def _to_time_ifidx_nid(self, d):
        y = d.split('.')
        q, l, p = '.'.join(y[1:3]), map(int, y[:3]), '.'.join(y[3:])
        a, b, c = l
        return  a, b, c, p, q

    def lldpRemoteSystemsData_translator(self, snmp_dict):
        for x in snmp_dict['vars']:
          if x.iid:
            time, ifidx, nid, oid, xiid = self._to_time_ifidx_nid(x.iid)
            if xiid not in self.lldpRemoteSystemsData:
                self.lldpRemoteSystemsData[xiid] = {
                    'lldpRemTimeMark': time,
                    'lldpRemLocalPortNum': ifidx,
                    'lldpRemIndex': nid
                }
            if oid:
                if 'ManAddr' in x.tag:
                    tbl = 'lldpRemManAddrEntry'
                    if tbl not in self.lldpRemoteSystemsData[xiid]:
                        soid = oid.split('.')
                        self.lldpRemoteSystemsData[xiid][tbl] = {
                            'lldpRemManAddrSubtype': int(soid[0]),
                            'lldpRemManAddrOIDLen': int(soid[1]),
                            'lldpRemManAddr': '.'.join(soid[2:]),
                            'lldpRemTimeMark': time,
                        }
                    self.lldpRemoteSystemsData[xiid][tbl][x.tag] = \
                        self.normalize(x, ('lldpRemChassisId', ),
                                self.capabilities)
                elif 'OrgDefInfo' in x.tag:
                    tbl = 'lldpRemOrgDefInfoEntry'
                    soid = oid.split('.')
                    if tbl not in self.lldpRemoteSystemsData[xiid]:
                        self.lldpRemoteSystemsData[xiid][tbl] = {
                            'lldpRemOrgDefInfoOUI':self.sane(''.join(soid[:3])),
                            'lldpRemOrgDefInfoSubtype': int(soid[3]),
                            'lldpRemOrgDefInfoTable': [],
                        }
                    self.lldpRemoteSystemsData[xiid][tbl][
                        'lldpRemOrgDefInfoTable'].append(
                                {'lldpRemOrgDefInfoIndex': int(soid[4]),
                                x.tag: self.normalize(x, ('lldpRemChassisId', ),
                                    self.capabilities)})
            else:
                self.lldpRemoteSystemsData[xiid][x.tag] = self.normalize(x,
                        ('lldpRemChassisId', ), self.capabilities)

class IfMib(SnmpTable):
    def __init__(self, session):
        super(IfMib, self).__init__(session)
        self.ifTable = {}
        self.ifXTable = {}

    def py_obj(self):
        return {'ifTable': self.ifTable.values(),
                'ifXTable': self.ifXTable.values()}

    def table_names(self):
        return ['ifTable', 'ifXTable']

    def ifTable_translator(self, snmp_dict):
        for x in snmp_dict['vars']:
            ifidx = int(x.iid)
            if ifidx not in self.ifTable:
                self.ifTable[ifidx] = {}
            self.ifTable[ifidx][x.tag] = self.normalize(x, ('ifPhysAddress', ))

    def ifXTable_translator(self, snmp_dict):
        for x in snmp_dict['vars']:
            ifidx = int(x.iid)
            if ifidx not in self.ifXTable:
                self.ifXTable[ifidx] = {'ifIndex': ifidx}
            self.ifXTable[ifidx][x.tag] = self.normalize(x)

class ArpTable(SnmpTable):
    def __init__(self, session):
        super(ArpTable, self).__init__(session)
        self.arpTable = []

    def py_obj(self):
        return self.arpTable

    def table_names(self):
        return 'ipNetToMediaPhysAddress'

    def ifid(self, x):
        ns = x.split('.')
        ifindex = int(ns[0])
        return ifindex, '.'.join(ns[1:])

    def ipNetToMediaPhysAddress_translator(self, snmp_dict):
        for x in snmp_dict['vars']:
            ifidx, ip = self.ifid(x.iid)
            self.arpTable.append({'localIfIndex': ifidx,
                    'ip': ip, 'mac':self._to_mac(x.val)})

class SnmpSession(netsnmp.Session):
    table_list = ['LldpTable', 'IfMib', 'ArpTable', 'IpMib']

    @classmethod
    def TABLES(cls):
        return cls.table_list

    def __init__(self, netdev):
        self.netdev = netdev
        device = netdev.snmp_cfg()
        tables = netdev.get_mibs()
        super(SnmpSession, self).__init__(**device)
        if tables:
            self.tables = filter(lambda x: x in self.table_list, tables)
        else:
            self.tables = []
        self.snmpTables = dict(map(lambda x:(x, eval(x + '(obj)',
                    globals(), {'obj': self})), self.tables))

    def scan_device(self, fun=None):
        for t in self.snmpTables.values():
            t.snmp_get()
            if callable(fun):
                fun()

    def in_tuples(self, o):
        return o[0].lower() + o[1:], self.snmpTables[o].py_obj()

    def get_sysname(self):
        name = self.netdev.get_snmp_name()
        if name is None:
            name = self.get_sysname_by_snmp()
            self.netdev.set_snmp_name(name)
        return name

    def get_sysname_by_snmp(self):
        if 'LldpTable' in self.snmpTables:
            return self.snmpTables['LldpTable'].get_name()
        t = LldpLocSysNameTable(self)
        t.snmp_get()
        return t.py_obj()

    def get_data(self):
        d = dict(self.in_tuples(t) for t in self.snmpTables)
        d['name'] = self.get_sysname()
        return d

