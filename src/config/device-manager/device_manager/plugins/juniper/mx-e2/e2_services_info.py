#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains error code for E2 services
"""

from builtins import object
from future.utils import with_metaclass
class ROClass(type):
    def __setattr__(self, name, value):
        raise ValueError("Cannot modify %s" % name)

class L2vpnErrors(with_metaclass(ROClass, object)):

    """
    It is not meant to be instantiated and will raise an exception if
    an attempt is made to do so.
    """

    _l2vpn_errors = {
        'EI': 'encapsulation invalid',
        'NC': 'interface encapsulation not CCC/TCC/VPLS',
        'EM': 'encapsulation mismatch',
        'WE': 'interface and instance encaps not same',
        'VC-Dn': 'Virtual circuit down',
        'NP': 'interface hardware not present',
        'CM': 'control-word mismatch',
        '->': 'only outbound connection is up',
        'CN': 'circuit not provisioned',
        '<-': 'only inbound connection is up',
        'OR': 'out of range',
        'Up': 'operational',
        'OL': 'no outgoing label',
        'Dn': 'down',
        'LD': 'local site signaled down',
        'CF': 'call admission control failure',
        'RD': 'remote site signaled down',
        'SC': 'local and remote site ID collision',
        'LN': 'local site not designated',
        'LM': 'local site ID not minimum designated',
        'RN': 'remote site not designated',
        'RM': 'remote site ID not minimum designated',
        'XX': 'unknown connection status',
        'IL': 'no incoming label',
        'MM': 'MTU mismatch',
        'MI': 'Mesh-Group ID not available',
        'BK': 'Backup connection',
        'ST': 'Standby connection',
        'PF': 'Profile parse failure',
        'PB': 'Profile busy',
        'RS': 'remote site standby',
        'SN': 'Static Neighbor',
        'LB': 'Local site not best-site',
        'RB': 'Remote site not best-site',
        'VM': 'VLAN ID mismatch',
        'HS': 'Hot-standby Connection',
    }

    def geterrorstr(self, errcode):
        try:
            return self._l2vpn_errors[errcode]
        except KeyError:
            return "No such errcode"
# end L2vpnErrors

class L2cktErrors(with_metaclass(ROClass, object)):

    """
    It is not meant to be instantiated and will raise an exception if
    an attempt is made to do so.
    """

    _l2ckt_errors = {
        'EI': 'encapsulation invalid',
        'NP': 'interface h/w not present',
        'MM': 'mtu mismatch',
        'Dn': 'down',
        'EM': 'encapsulation mismatch',
        'VC-Dn': 'Virtual circuit Down',
        'CM': 'control-word mismatch',
        'Up': 'operational',
        'VM': 'vlan id mismatch',
        'CF': 'Call admission control failure',
        'OL': 'no outgoing label',
        'IB': 'TDM incompatible bitrate',
        'NC': 'intf encaps not CCC/TCC',
        'TM': 'TDM misconfiguration ',
        'BK': 'Backup Connection',
        'ST': 'Standby Connection',
        'CB': 'rcvd cell-bundle size bad',
        'SP': 'Static Pseudowire',
        'LD': 'local site signaled down',
        'RS': 'remote site standby',
        'RD': 'remote site signaled down',
        'HS': 'Hot-standby Connection',
        'XX': 'unknown'
    }

    def geterrorstr(self, errcode):
        try:
            return self._l2ckt_errors[errcode]
        except KeyError:
            return "No such errcode"
# end L2cktErrors
