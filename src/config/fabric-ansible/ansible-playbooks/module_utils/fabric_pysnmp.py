#!/usr/bin/python

from __future__ import print_function
import binascii
from builtins import object
from builtins import str
from collections import defaultdict

from ansible.module_utils._text import to_text

try:
    from pysnmp.entity.rfc3413.oneliner import cmdgen
    has_pysnmp = True
except BaseException:
    has_pysnmp = False

if not has_pysnmp:
    print('Missing required pysnmp module (check docs)')


class DefineOid(object):
    def __init__(self, dotprefix=False):
        """__init__."""
        if dotprefix:
            dp = "."
        else:
            dp = ""

        # From SNMPv2-MIB
        self.sysDescr = dp + "1.3.6.1.2.1.1.1.0"
        self.sysObjectId = dp + "1.3.6.1.2.1.1.2.0"
        self.sysUpTime = dp + "1.3.6.1.2.1.1.3.0"
        self.sysContact = dp + "1.3.6.1.2.1.1.4.0"
        self.sysName = dp + "1.3.6.1.2.1.1.5.0"
        self.sysLocation = dp + "1.3.6.1.2.1.1.6.0"

        # From IF-MIB
        self.ifIndex = dp + "1.3.6.1.2.1.2.2.1.1"
        self.ifDescr = dp + "1.3.6.1.2.1.2.2.1.2"
        self.ifMtu = dp + "1.3.6.1.2.1.2.2.1.4"
        self.ifSpeed = dp + "1.3.6.1.2.1.2.2.1.5"
        self.ifPhysAddress = dp + "1.3.6.1.2.1.2.2.1.6"
        self.ifAdminStatus = dp + "1.3.6.1.2.1.2.2.1.7"
        self.ifOperStatus = dp + "1.3.6.1.2.1.2.2.1.8"
        self.ifAlias = dp + "1.3.6.1.2.1.31.1.1.1.18"

        # From IP-MIB
        self.ipAdEntAddr = dp + "1.3.6.1.2.1.4.20.1.1"
        self.ipAdEntIfIndex = dp + "1.3.6.1.2.1.4.20.1.2"
        self.ipAdEntNetMask = dp + "1.3.6.1.2.1.4.20.1.3"


def decode_hex(hexstring):
    if len(hexstring) < 3:
        return hexstring
    if hexstring[:2] == "0x":
        return to_text(binascii.unhexlify(hexstring[2:]))
    else:
        return hexstring


def snmp_walk(host, version, community):

    cmdGen = cmdgen.CommandGenerator()

    # Use SNMP Version 2
    # if version == "v2" or version == "v2c":
    snmp_auth = cmdgen.CommunityData(community)

    # Use p to prefix OIDs with a dot for polling
    p = DefineOid(dotprefix=True)
    # Use v without a prefix to use with return values
    v = DefineOid(dotprefix=False)

    def Tree():
        return defaultdict(Tree)

    results = Tree()
    results['error'] = False

    errorIndication, errorStatus, errorIndex, varBinds = cmdGen.getCmd(
        snmp_auth,
        cmdgen.UdpTransportTarget((host, 161)),
        cmdgen.MibVariable(p.sysDescr,),
        cmdgen.MibVariable(p.sysObjectId,),
        cmdgen.MibVariable(p.sysUpTime,),
        cmdgen.MibVariable(p.sysContact,),
        cmdgen.MibVariable(p.sysName,),
        cmdgen.MibVariable(p.sysLocation,),
        lookupMib=False
    )

    if errorIndication:
        results['error'] = True
        results['error_msg'] = str(errorIndication)

    for oid, val in varBinds:
        current_oid = oid.prettyPrint()
        current_val = val.prettyPrint()
        if current_oid == v.sysDescr:
            results['ansible_sysdescr'] = decode_hex(current_val)
        elif current_oid == v.sysObjectId:
            results['ansible_sysobjectid'] = current_val
        elif current_oid == v.sysUpTime:
            results['ansible_sysuptime'] = current_val
        elif current_oid == v.sysContact:
            results['ansible_syscontact'] = current_val
        elif current_oid == v.sysName:
            results['ansible_sysname'] = current_val
        elif current_oid == v.sysLocation:
            results['ansible_syslocation'] = current_val

    errorIndication, errorStatus, errorIndex, varTable = cmdGen.nextCmd(
        snmp_auth,
        cmdgen.UdpTransportTarget((host, 161)),
        cmdgen.MibVariable(p.ifIndex,),
        cmdgen.MibVariable(p.ifDescr,),
        cmdgen.MibVariable(p.ifMtu,),
        cmdgen.MibVariable(p.ifSpeed,),
        cmdgen.MibVariable(p.ifPhysAddress,),
        cmdgen.MibVariable(p.ifAdminStatus,),
        cmdgen.MibVariable(p.ifOperStatus,),
        cmdgen.MibVariable(p.ipAdEntAddr,),
        cmdgen.MibVariable(p.ipAdEntIfIndex,),
        cmdgen.MibVariable(p.ipAdEntNetMask,),
        cmdgen.MibVariable(p.ifAlias,),
        lookupMib=False
    )

    if errorIndication:
        results['error'] = True
        results['error_msg'] = str(errorIndication)

    return results
