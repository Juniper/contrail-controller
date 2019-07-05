#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import copy
import uuid
import netaddr
from netaddr import IPNetwork, IPAddress, IPRange, all_matching_cidrs
from vnc_quota import QuotaHelper
from pprint import pformat
import cfgm_common.exceptions
from cfgm_common.utils import _DEFAULT_ZK_COUNTER_PATH_PREFIX
from context import get_context
try:
    # python2.7
    from collections import OrderedDict
except:
    # python2.6
    from ordereddict import OrderedDict

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


class AddrMgmtError(Exception):
    pass
# end class AddrMgmtError


class AddrMgmtSubnetUndefined(AddrMgmtError):

    def __init__(self, vn_fq_name):
        self.vn_fq_name = vn_fq_name
    # end __init__

    def __str__(self):
        return "Virtual-Network(%s) has no defined subnet(s)" %\
            (self.vn_fq_name)
    # end __str__
# end AddrMgmtSubnetUndefined


class AddrMgmtAllocUnitInvalid(AddrMgmtError):

    def __init__(self, vn_fq_name, subnet_name, alloc_unit, reason):
        self.vn_fq_name = vn_fq_name
        self.subnet_name = subnet_name
        self.alloc_unit = alloc_unit
        self.reason = reason
    # end __init__

    def __str__(self):
        msg = "Virtual-Network(%s) has invalid alloc_unit(%s) in subnet(%s)" %\
            (self.vn_fq_name, self.alloc_unit, self.subnet_name)
        msg += " reason: %s" %(self.reason)
        return msg
    # end __str__
# end AddrMgmtAllocUnitInvalid

class AddrMgmtSubnetInvalid(AddrMgmtError):

    def __init__(self, vn_fq_name, subnet_name):
        self.vn_fq_name = vn_fq_name
        self.subnet_name = subnet_name
    # end __init__

    def __str__(self):
        return "Virtual-Network(%s) has invalid subnet(%s)" %\
            (self.vn_fq_name, self.subnet_name)
    # end __str__
# end AddrMgmtSubnetUndefined


class AddrMgmtSubnetAbsent(AddrMgmtError):

    def __init__(self, vn_fq_name):
        self.vn_fq_name = map(str, vn_fq_name)
    # end __init__

    def __str__(self):
        return "Virtual-Network(%s) has no defined subnets" %(self.vn_fq_name)
    # end __str__
# end AddrMgmtSubnetAbsent


class AddrMgmtSubnetExhausted(AddrMgmtError):

    def __init__(self, vn_fq_name, subnet_val):
        self.vn_fq_name = map(str, vn_fq_name)
        self.subnet_val = map(str, subnet_val)
    # end __init__

    def __str__(self):
        vn_fq_name_str = str([str(a) for a in self.vn_fq_name])
        return "Virtual-Network(%s) has exhausted subnet(%s)" %\
            (vn_fq_name_str, self.subnet_val)
    # end __str__
# end AddrMgmtSubnetExhausted


class AddrMgmtInvalidIpAddr(AddrMgmtError):

    def __init__(self, subnet_val, alloc_pool):
        self.subnet_val = subnet_val
        self.alloc_pool = alloc_pool
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid Ip address in Allocation Pool(%s)" %\
            (self.subnet_val, self.alloc_pool)
    # end __str__
# end AddrMgmtInvalidIpAddr


class AddrMgmtOutofBoundAllocPool(AddrMgmtError):

    def __init__(self, subnet_val, alloc_pool):
        self.subnet_val = subnet_val
        self.alloc_pool = alloc_pool
    # end __init__

    def __str__(self):
        return "subnet(%s) allocation pool (%s) is out of cidr" %\
            (self.subnet_val, self.alloc_pool)
    # end __str__
# end AddrMgmtOutofBoundAllocPool


class AddrMgmtInvalidAllocPool(AddrMgmtError):

    def __init__(self, subnet_val, alloc_pool):
        self.subnet_val = subnet_val
        self.alloc_pool = alloc_pool
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid Allocation Pool(%s)" %\
            (self.subnet_val, self.alloc_pool)
    # end __str__
# end AddrMgmtInvalidAllocPool


class AddrMgmtInvalidGatewayIp(AddrMgmtError):

    def __init__(self, subnet_val, gateway_ip):
        self.subnet_val = subnet_val
        self.gw_ip = gateway_ip
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid Gateway ip address(%s)" %\
            (self.subnet_val, self.gw_ip)
    # end __str__
# end AddrMgmtInvalidGatewayIp


class AddrMgmtInvalidServiceNodeIp(AddrMgmtError):

    def __init__(self, subnet_val, service_address):
        self.subnet_val = subnet_val
        self.service_address = service_address
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid Service Node ip address(%s)" %\
            (self.subnet_val, self.service_address)
    # end __str__
# end AddrMgmtInvalidServiceNodeIp


class AddrMgmtInvalidDnsNameServer(AddrMgmtError):

    def __init__(self, subnet_val, name_server):
        self.subnet_val = subnet_val
        self.name_server = name_server
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid DNS Nameserver(%s)" %\
            (self.subnet_val, self.name_server)
    # end __str__
# end AddrMgmtInvalidDnsNameServer


# Class to manage a single subnet
#  maintain free list of IP addresses, exclude list and CIDR info
class Subnet(object):

    """Create a subnet with prefix and len

    Gateway (if provided) is made unavailable for assignment.
    Inuse mask represent addresses already assigned and in use during previous
    incarnations of api server. These are also taken out of free pool to
    prevent duplicate assignment.
    """
    _db_conn = None

    @classmethod
    def set_db_conn(cls, db_conn):
        cls._db_conn = db_conn
    # end set_db_conn

    def __init__(self, name, prefix, prefix_len,
                 gw=None, service_address=None,
                 dns_nameservers=None,
                 alloc_pool_list=None,
                 addr_from_start=False,
                 should_persist=True,
                 ip_alloc_unit=1,
                 subnetting = False,
                 subscriber_tag=None):
        network = IPNetwork('%s/%s' % (prefix, prefix_len))

        # check allocation-pool
        for ip_pool in alloc_pool_list or []:
            try:
                start_ip = IPAddress(ip_pool['start'])
                end_ip = IPAddress(ip_pool['end'])
            except netaddr.core.AddrFormatError:
                raise AddrMgmtInvalidIpAddr(name, ip_pool)
            if (start_ip not in network or end_ip not in network):
                raise AddrMgmtOutofBoundAllocPool(name, ip_pool)
            if (end_ip < start_ip):
                raise AddrMgmtInvalidAllocPool(name, ip_pool)

        # check gw
        if gw and gw != 'None':
            try:
                gw_ip = IPAddress(gw)
            except netaddr.core.AddrFormatError:
                raise AddrMgmtInvalidGatewayIp(name, gw)

        else:
            if subnetting:
                gw_ip = None
            else:
                # reserve a gateway ip in subnet
                if addr_from_start:
                    gw_ip = IPAddress(network.first + 1)
                else:
                    gw_ip = IPAddress(network.last - 1)

        # check service_address
        if service_address and service_address != 'None':
            try:
                service_node_address = IPAddress(service_address)
            except netaddr.core.AddrFormatError:
                raise AddrMgmtInvalidServiceNodeIp(name, service_node_address)

        else:
            # reserve a service address ip in subnet
            # only if subnet is not a subnetting from bigger subnet
            if subnetting:
                service_node_address = None
            else:
                if addr_from_start:
                    service_node_address = IPAddress(network.first + 2)
                else:
                    service_node_address = IPAddress(network.last - 2)

        # check dns_nameservers
        for nameserver in dns_nameservers or []:
            try:
                ip_addr = IPAddress(nameserver)
            except netaddr.core.AddrFormatError:
                raise AddrMgmtInvalidDnsNameServer(name, nameserver)

        # check allocation-unit
        # alloc-unit should be power of 2
        if (ip_alloc_unit & (ip_alloc_unit-1)):
            raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                                           ip_alloc_unit, 'Not power of 2')

        # if allocation-pool is not specified, create one with entire cidr
        no_alloc_pool = False
        if not alloc_pool_list:
            alloc_pool_list = [{'start': str(IPAddress(network.first)),
                                'end': str(IPAddress(network.last-1))}]
            no_alloc_pool = True


        # need alloc_pool_list with integer to use in Allocator
        alloc_int_list = list()
        # store integer for given ip address in allocation list
        for alloc_pool in alloc_pool_list:
            alloc_int = {'start': int(IPAddress(alloc_pool['start'])),
                         'end': int(IPAddress(alloc_pool['end']))}
            alloc_int_list.append(alloc_int)

        # check alloc_pool starts at integer multiple of alloc_unit
        if ip_alloc_unit is not 1:
            for alloc_int in alloc_int_list:
                if alloc_int['start'] % ip_alloc_unit:
                    raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                        ip_alloc_unit, 'Pool start %s not aligned' %(
                            alloc_int['start']))

        # go through each alloc_pool and validate allocation pool
        # given ip_alloc_unit
        net_ip = IPAddress(network.first)
        bc_ip = IPAddress(network.last)
        for alloc_pool in alloc_pool_list:
            if no_alloc_pool:
                start_ip = IPAddress(network.first)
                end_ip = IPAddress(network.last)
            else:
                start_ip = IPAddress(alloc_pool['start'])
                end_ip = IPAddress(alloc_pool['end'])

            alloc_pool_range = int(end_ip) - int(start_ip) + 1
            # each alloc-pool range should be integer multiple of ip_alloc_unit
            if (alloc_pool_range < ip_alloc_unit):
                raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                    ip_alloc_unit, 'Pool range smaller than alloc_unit')
            if (alloc_pool_range % ip_alloc_unit):
                raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                    ip_alloc_unit,
                    'Pool range %s not aligned' %(alloc_pool_range))

            block_alloc_unit = 0
            if (net_ip == start_ip):
                block_alloc_unit += 1
                if (bc_ip == end_ip):
                    if (int(end_ip) - int(start_ip) >= ip_alloc_unit):
                        block_alloc_unit += 1
            else:
                if (bc_ip == end_ip):
                    block_alloc_unit += 1

            if (gw_ip and gw_ip >= start_ip and gw_ip <= end_ip):
                #gw_ip is part of this alloc_pool, block another alloc_unit
                # only if gw_ip is not a part of already counted block units.
                if ((int(gw_ip) - int(start_ip) >= ip_alloc_unit) and
                    (int(end_ip) - int(gw_ip) >= ip_alloc_unit)):
                    block_alloc_unit += 1

            if (service_node_address and service_node_address >=start_ip and
                service_node_address <= end_ip):
                #service_node_address is part of this alloc_pool,
                #block another alloc_unit only if service_node_address is not
                #part of already counted block units.
                if ((int(service_node_address) -
                     int(start_ip) >= ip_alloc_unit) and
                    (int(end_ip) -
                     int(service_node_address) >= ip_alloc_unit) and
                    (abs(int(gw_ip) -
                         int(service_node_address)) > ip_alloc_unit)):
                    block_alloc_unit += 1

            # each alloc-pool should have minimum block_alloc_unit+1,
            # possible allocation
            if (alloc_pool_range/ip_alloc_unit) <= block_alloc_unit:
                raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                    ip_alloc_unit, 'Pool range<=block_alloc_unit')

        # Exclude host and broadcast
        exclude = [IPAddress(network.first), IPAddress(network.broadcast)]

        # exclude gw_ip, service_node_address if they are within
        # allocation-pool and subnet is not subnetted
        if not subnetting:
            for alloc_int in alloc_int_list:
                if alloc_int['start'] <= int(gw_ip) <= alloc_int['end']:
                    exclude.append(gw_ip)
                if (alloc_int['start'] <= int(service_node_address)
                       <= alloc_int['end']):
                    exclude.append(service_node_address)
        # ip address allocator will be per alloc-unit
        self._db_conn.subnet_create_allocator(name, alloc_int_list,
                                              addr_from_start,
                                              should_persist,
                                              network.first,
                                              network.size,
                                              ip_alloc_unit)

        # reserve excluded addresses only in bitmap but not in zk
        for addr in exclude:
            self._db_conn.subnet_set_in_use(name, int(addr)/ip_alloc_unit)
        self._name = name
        self._network = network
        self._version = network.version
        self._exclude = exclude
        self.gw_ip = gw_ip
        self.dns_server_address = service_node_address
        self._alloc_pool_list = alloc_pool_list
        self.dns_nameservers = dns_nameservers
        self.alloc_unit = ip_alloc_unit
        self._prefix = prefix
        self._prefix_len = prefix_len
        self._addr_from_start = addr_from_start
        self.subnetting = subnetting
        self.subscriber_tag = subscriber_tag
    # end __init__

    @classmethod
    def delete_cls(cls, subnet_name, notify=True):
        # deletes the index allocator
        cls._db_conn.subnet_delete_allocator(subnet_name, notify)
    # end delete_cls

    def get_name(self):
        return self._name
    # end get_name

    def get_exclude(self):
        return self._exclude
    # end get_exclude

    # ip address management helper routines.
    # ip_set/reset_in_use do not persist to DB just internal bitmap maintenance
    # (for e.g. for notify context)
    # ip_reserve/alloc/free persist to DB
    def is_ip_allocated(self, ipaddr):
        ip = IPAddress(ipaddr)
        if ip in self._exclude:
            return True
        addr = int(ip)
        return self._db_conn.subnet_is_addr_allocated(self._name, addr/self.alloc_unit)
    # end is_ip_allocated

    def ip_set_in_use(self, ipaddr):
        ip = IPAddress(ipaddr)
        addr = int(ip)
        return self._db_conn.subnet_set_in_use(self._name, addr/self.alloc_unit)
    # end ip_set_in_use

    def ip_reset_in_use(self, ipaddr):
        ip = IPAddress(ipaddr)
        addr = int(ip)
        return self._db_conn.subnet_reset_in_use(self._name, addr/self.alloc_unit)
    # end ip_reset_in_use

    def ip_reserve(self, ipaddr, value):
        ip = IPAddress(ipaddr)
        req = int(ip)

        addr = self._db_conn.subnet_reserve_req(self._name, req/self.alloc_unit, value)
        if addr:
            return str(IPAddress(addr*self.alloc_unit))
        return None
    # end ip_reserve

    def ip_count(self):
        return self._db_conn.subnet_alloc_count(self._name)
    # end ip_count

    def ip_alloc(self, ipaddr=None, value=None, version=None,
                 sn_alloc_pools=None):
        if ipaddr:
            #check if ipaddr is multiple of alloc_unit
            if ipaddr % self.alloc_unit:
                raise AddrMgmtAllocUnitInvalid(self._name,
                          self._prefix+'/'+self._prefix_len, self.alloc_unit,
                          'IP address %s not aligned' %(ipaddr))
            return self.ip_reserve(ipaddr/self.alloc_unit, value)

        addr = self._db_conn.subnet_alloc_req(self._name, value,
                                              alloc_pools=sn_alloc_pools,
                                              alloc_unit=self.alloc_unit)
        if addr:
            ip_addr = IPAddress(addr * self.alloc_unit, version)
            return str(ip_addr)
        return None
    # end ip_alloc

    # free IP unless it is invalid, excluded or already freed
    @classmethod
    def ip_free_cls(cls, subnet_fq_name, ip_network, exclude_addrs, ip_addr,
                    alloc_unit=1):
        ip = IPAddress(ip_addr)
        if ((ip in ip_network) and (ip not in exclude_addrs)):
            if cls._db_conn:
                cls._db_conn.subnet_free_req(subnet_fq_name, int(ip)/alloc_unit)
                return True

        return False
    # end ip_free_cls

    def ip_free(self, ip_addr):
        Subnet.ip_free_cls(self._name, self._network, self._exclude, ip_addr, self.alloc_unit)
    # end ip_free

    # check if IP address belongs to us

    def ip_belongs(self, ipaddr):
        return IPAddress(ipaddr) in self._network
    # end ip_belongs

    def set_version(self, version):
        self._version = version
    # end set_version

    def get_version(self):
        return self._version
    # end get_version

# end class Subnet


# Address management for virtual network
class AddrMgmt(object):

    def __init__(self, server_mgr):
        # self.vninfo = {}
        self.version = 0
        self._server_mgr = server_mgr
        self._db_conn = None
        # dict of VN where each key has dict of subnets
        self._subnet_objs = {}
    # end __init__

    def _get_db_conn(self):
        if not self._db_conn:
            self._db_conn = self._server_mgr.get_db_connection()
            Subnet.set_db_conn(self._db_conn)

        return self._db_conn
    # end _get_db_conn

    def _ipam_subnet_to_subnet_dict(self, ipam_subnets):
        subnet_dicts = OrderedDict()
        for ipam_subnet in ipam_subnets:
            if 'subnet' not in ipam_subnet:
                continue

            subnet_dict = copy.deepcopy(ipam_subnet['subnet'])
            subnet_dict['gw'] = ipam_subnet.get('default_gateway')
            subnet_dict['alloc_unit'] = ipam_subnet.get('alloc_unit') or 1
            subnet_dict['dns_server_address'] = \
                ipam_subnet.get('dns_server_address')
            subnet_dict['allocation_pools'] = \
                ipam_subnet.get('allocation_pools')
            subnet_dict['dns_nameservers'] = \
                ipam_subnet.get('dns_nameservers')
            subnet_dict['addr_start'] = \
                ipam_subnet.get('addr_from_start') or False
            subnet_dict['subnet_uuid'] = ipam_subnet.get('subnet_uuid')
            subnet_name = subnet_dict['ip_prefix'] + '/' + str(
                          subnet_dict['ip_prefix_len'])
            subnet_dicts[subnet_name] = subnet_dict

        return subnet_dicts
    # end _ipam_subnet_to_subnet_dict

    def _uuid_to_obj_dict(self, req_obj_type, obj_uuid, req_fields=None):
        db_conn = self._get_db_conn()

        (ok, obj_dict) = db_conn.dbe_read(
                             obj_type=req_obj_type,
                             obj_id=obj_uuid,
                             obj_fields=req_fields)
        return (ok, obj_dict)
    # end _uuid_to_obj_dict

    def _fq_name_to_obj_dict(self, req_obj_type, fq_name, req_fields=None):
        db_conn = self._get_db_conn()
        obj_uuid = db_conn.fq_name_to_uuid(req_obj_type, fq_name)

        return self._uuid_to_obj_dict(req_obj_type, obj_uuid, req_fields)
    #end

    def _get_net_subnet_dicts(self, vn_uuid, vn_dict=None):
        # Read in the VN details if not passed in
        if not vn_dict:
            obj_fields=['network_ipam_refs']
            (ok, vn_dict) = self._uuid_to_obj_dict('virtual_network',
                                                    vn_uuid, obj_fields)
            if not ok:
                raise cfgm_common.exceptions.VncError(vn_dict)

        ipam_refs = vn_dict.get('network_ipam_refs') or []
        # gather all subnets, return dict keyed by name
        subnet_dicts = OrderedDict()
        for ipam_ref in ipam_refs:
            vnsn_data = ipam_ref.get('attr') or {}
            ipam_subnets = vnsn_data.get('ipam_subnets') or []
            subnet_dicts.update(self._ipam_subnet_to_subnet_dict(ipam_subnets))
        return subnet_dicts
    # end _get_net_subnet_dicts

    def _create_subnet_obj_for_ipam_subnet(self, ipam_subnet, fq_name_str,
                                           should_persist, subnetting=False):
        subnet = ipam_subnet['subnet']
        subnet_name = str(subnet['ip_prefix']) + '/' + str(subnet['ip_prefix_len'])
        gateway_ip = ipam_subnet.get('default_gateway')
        service_address = ipam_subnet.get('dns_server_address')
        allocation_pools = ipam_subnet.get('allocation_pools')
        nameservers = ipam_subnet.get('dns_nameservers')
        addr_start = ipam_subnet.get('addr_from_start') or False
        alloc_unit = ipam_subnet.get('alloc_unit') or 1
        subscriber_tag = ipam_subnet.get('subscriber_tag') or None
        subnet_obj = Subnet(
            '%s:%s' % (fq_name_str, subnet_name),
            subnet['ip_prefix'], str(subnet['ip_prefix_len']),
            gw=gateway_ip, service_address=service_address,
            dns_nameservers=nameservers,
            alloc_pool_list=allocation_pools,
            addr_from_start=addr_start, should_persist=should_persist,
            ip_alloc_unit=alloc_unit, subnetting=subnetting,
            subscriber_tag = subscriber_tag)

        return subnet_obj
    #end _create_subnet_obj_for_ipam_subnet

    def _get_ipam_subnet_objs_from_ipam_uuid(self, ipam_fq_name,
                                             ipam_uuid,
                                             should_persist=False,
                                             ipam_dict=None):
        ipam_fq_name_str = ':'.join(ipam_fq_name)
        subnet_objs = self._subnet_objs.get(ipam_uuid)
        if subnet_objs is None:
            if not ipam_dict:
                #read ipam to get ipam_subnets and generate subnet_objs
                (ok, ipam_dict) = self._uuid_to_obj_dict('network_ipam',
                                                         ipam_uuid)
                if not ok:
                    raise cfgm_common.exceptions.VncError(ipam_dict)

            self._subnet_objs[ipam_uuid] = OrderedDict()
            #read ipam to get ipam_subnets and generate subnet_objs
            (ok, ipam_dict) = self._uuid_to_obj_dict('network_ipam',
                                                     ipam_uuid)
            if not ok:
                raise cfgm_common.exceptions.VncError(ipam_dict)

            ipam_subnets_dict = ipam_dict.get('ipam_subnets') or {}
            ipam_subnets = ipam_subnets_dict.get('subnets') or []
            subnetting = ipam_dict.get('ipam_subnetting', False)
            # gather all subnets, return dict keyed by name
            if not ipam_subnets:
                return subnet_objs

            for ipam_subnet in ipam_subnets:
                subnet = ipam_subnet['subnet']
                subnet_name = subnet['ip_prefix'] + '/' + str(
                    subnet['ip_prefix_len'])
                subnet_obj = self._create_subnet_obj_for_ipam_subnet(
                                 ipam_subnet, ipam_fq_name_str, should_persist,
                                 subnetting)
                self._subnet_objs.setdefault(ipam_uuid, OrderedDict())
                self._subnet_objs[ipam_uuid][subnet_name] = subnet_obj
            subnet_objs = self._subnet_objs[ipam_uuid]

        return subnet_objs
    # end _get_ipam_subnet_objs_from_ipam_uuid

    def _get_subnet_obj_for_subs_tag(self, subnet_objs, subnet_tag):

        for subnet_name, subnet_obj in subnet_objs.items():
            if ((subnet_obj.subscriber_tag) and
                (subnet_obj.subscriber_tag == subnet_tag)):
                return (subnet_name, subnet_obj)

        return (None, None)
    # end _get_subnet_obj_for_subs_tag

    def _get_ipam_subnet_dicts(self, ipam_uuid, ipam_dict=None):
        # Read in the ipam details if not passed in
        if not ipam_dict:
            (ok, ipam_dict) = self._uuid_to_obj_dict('network_ipam',
                                                     ipam_uuid)
            if not ok:
                raise cfgm_common.exceptions.VncError(ipam_dict)

        ipam_subnets_dict = ipam_dict.get('ipam_subnets') or {}
        ipam_subnets = ipam_subnets_dict.get('subnets') or []
        # gather all subnets, return dict keyed by name
        subnet_dicts = self._ipam_subnet_to_subnet_dict(ipam_subnets)
        return subnet_dicts
    # end _get_ipam_subnet_dicts

    def _validate_alloc_pool_change(self, req_alloc_list,
                                    db_alloc_list):

        # check if all db_alloc_list pools are also in
        # req_alloc_list
        for db_alloc_pool in db_alloc_list:
            db_start_ip = IPAddress(db_alloc_pool['start'])
            db_end_ip = IPAddress(db_alloc_pool['end'])
            alloc_pool_present = False
            for req_alloc_pool in req_alloc_list:
                req_start_ip = IPAddress(req_alloc_pool['start'])
                req_end_ip = IPAddress(req_alloc_pool['end'])
                if ((db_start_ip == req_start_ip) and
                    (db_end_ip == req_end_ip)):
                    alloc_pool_present = True
                    break

            if not alloc_pool_present:
                return (False, False, "Allocation pool delete is not allowed")

        if len(req_alloc_list) > len(db_alloc_list):
                return (True, True, "")

        #allocation pools validated but no change
        return (True, False, "")
    # end _validate_alloc_pool_change

    def _validate_subnet_alloc_pools(self, req_subnet, alloc_pool_list=[]):
        if not alloc_pool_list:
            return (True, "")

        # check all allocation pools in the list are in the given subnet
        prefix = req_subnet['ip_prefix']
        prefix_len = req_subnet['ip_prefix_len']
        network = IPNetwork('%s/%s' % (prefix, prefix_len))
        for alloc_pool in alloc_pool_list:
            start_ip = IPAddress(alloc_pool['start'])
            end_ip = IPAddress(alloc_pool['end'])
            if end_ip < start_ip:
                return (False, "Allocation pool end ip is less than start ip")
            if start_ip not in network:
                return (False, "allocation pool start ip is out of network")
            if end_ip not in network:
                return (False, "allocation pool end ip is out of network")

            # check of this alloc_pool is not overalapping
            # with other alloc_pools
            pool_idx = alloc_pool_list.index(alloc_pool)
            for idx in range(pool_idx+1, len(alloc_pool_list)):
                next_alloc_pool = alloc_pool_list[idx]
                next_start_ip = IPAddress(next_alloc_pool['start'])
                next_end_ip = IPAddress(next_alloc_pool['end'])
                if (start_ip >= next_start_ip) and (start_ip <= next_end_ip):
                    return (False, "overlapping alloc-pools")
                if (end_ip >= next_start_ip) and (end_ip <= next_end_ip):
                    return (False, "overlapping alloc-pools")

        return (True, "")
    # end _validate_subnet_alloc_pools

    def _check_subnet_alloc_pools(self, req_subnet, db_subnet={}):
        subnet_with_change_pool = None
        if not req_subnet and not db_subnet:
            return (True, "", subnet_with_change_pool)

        if req_subnet:
            req_alloc_list = req_subnet['allocation_pools'] or []
            req_alloc_list = sorted(req_alloc_list, key=lambda k: k['start'],
                                    reverse=True)
        else:
            req_alloc_list = []

        if db_subnet:
            db_alloc_list = db_subnet['allocation_pools'] or []
            db_alloc_list = sorted(db_alloc_list, key=lambda k: k['start'],
                                   reverse=True)
        else:
            db_alloc_list = []

        if not req_alloc_list and not db_alloc_list:
            return (True, "", None)

        (ok, result) = self._validate_subnet_alloc_pools(req_subnet,
                                                         req_alloc_list)
        if not ok:
            return (ok, result, subnet_with_change_pool)

        if not db_alloc_list:
            return (True, "", subnet_with_change_pool)

        # when both db_alloc_list and req_alloc_list is not None
        # network/ipam update request
        # req alloc list can not have less allocation_pool
        # current support is to add allocation pool only
        if (len(req_alloc_list) < len(db_alloc_list)):
            return (False, "allocation pool delete is not allowed",
                    subnet_with_change_pool)

        (ok, alloc_pool_change, result) = self._validate_alloc_pool_change(
                                              req_alloc_list, db_alloc_list)
        if not ok:
                return (False, result, subnet_with_change_pool)

        if alloc_pool_change:
            subnet_name = req_subnet['ip_prefix'] + '/' + str(
                              req_subnet['ip_prefix_len'])
            subnet_with_change_pool = {'subnet_name': subnet_name,
                                       'alloc_pool': req_alloc_list}

        return (True, "", subnet_with_change_pool)
    # end _check_subnet_alloc_pools

    def _create_subnet_objs(self, fq_name_str, obj_uuid, ipam_subnets,
                            should_persist, alloc_pool_change, subnetting):
        for ipam_subnet in ipam_subnets:
            subnet = ipam_subnet['subnet']
            subnet_name = subnet['ip_prefix'] + '/' + str(
            subnet['ip_prefix_len'])
            try:
                subnet_obj = self._subnet_objs[obj_uuid][subnet_name]
                addr_from_start = subnet_obj._addr_from_start
                old_dns_addr = str(subnet_obj.dns_server_address)
                if not subnetting:
                    if ('dns_server_address' not in ipam_subnet) or \
                        (ipam_subnet['dns_server_address'] is None):
                        #we need to make sure subnet_obj has a default dns
                        network = subnet_obj._network
                        if addr_from_start:
                            new_dns_addr = str(IPAddress(network.first + 2))
                        else:
                            new_dns_addr = str(IPAddress(network.last - 2))
                    else:
                        new_dns_addr = ipam_subnet['dns_server_address']
                        if old_dns_addr != new_dns_addr:
                            if subnet_obj.ip_belongs(new_dns_addr):
                                read_ip_addr = subnet_obj.is_ip_allocated(
                                                   new_dns_addr)
                                if read_ip_addr is not None:
                                    raise AddrMgmtSubnetInvalid(fq_name_str,
                                                                subnet_name)

                # Update IndexAllocator if allocation_pools have changed
                for subnet_alloc_pool in alloc_pool_change:
                    if subnet_alloc_pool['subnet_name'] == subnet_name:
                        allocator_name = '%s:%s' % (fq_name_str, subnet_name)
                        new_alloc_list = subnet_alloc_pool['alloc_pool']
                        allocator_alloc_list = []
                        for alloc_pool in new_alloc_list:
                            allocator_pool = {}
                            allocator_pool['start'] = int(IPAddress(alloc_pool['start']))
                            allocator_pool['end'] = int(IPAddress(alloc_pool['end']))
                            allocator_alloc_list.append(allocator_pool)
                        self._db_conn.subnet_change_allocator(
                            allocator_name, allocator_alloc_list,
                            subnet_obj.alloc_unit)

                # assign new dns_server_address
                # reset old dns_server_address and set
                # new dns_server_address in bitmap
                if not subnetting:
                    if old_dns_addr != new_dns_addr:
                        if subnet_obj.ip_belongs(old_dns_addr):
                            if IPAddress(old_dns_addr) in subnet_obj._exclude:
                                subnet_obj._exclude.remove(IPAddress(old_dns_addr))
                            subnet_obj.ip_free(old_dns_addr)
                        if subnet_obj.ip_belongs(new_dns_addr):
                            subnet_obj._exclude.append(IPAddress(new_dns_addr))
                            subnet_obj.ip_reserve(new_dns_addr, 'dns_server')
                        subnet_obj.dns_server_address = IPAddress(new_dns_addr)

            except KeyError:
                subnet_obj = self._create_subnet_obj_for_ipam_subnet(
                                 ipam_subnet, fq_name_str, should_persist, subnetting)
                self._subnet_objs[obj_uuid][subnet_name] = subnet_obj

            ipam_subnet['default_gateway'] = str(subnet_obj.gw_ip)
            ipam_subnet['dns_server_address'] = \
                str(subnet_obj.dns_server_address)
    # end _create_subnet_objs

    def _create_ipam_subnet_objs(self, ipam_uuid, ipam_dict,
                                 should_persist, alloc_pool_change=[],
                                 ipam_subnetting=False):
        self._subnet_objs.setdefault(ipam_uuid, OrderedDict())
        ipam_fq_name_str = ':'.join(ipam_dict['fq_name'])
        ipam_subnets_dict = ipam_dict.get('ipam_subnets')
        if ipam_subnets_dict:
            ipam_subnets = ipam_subnets_dict['subnets']
            subnetting = ipam_subnetting
            self._create_subnet_objs(ipam_fq_name_str, ipam_uuid, ipam_subnets,
                                     should_persist, alloc_pool_change,
                                     subnetting)
    # end _create_ipam_subnet_objs

    def _create_net_subnet_objs(self, vn_fq_name_str, vn_uuid, vn_dict,
                                should_persist, alloc_pool_change=[]):
        self._subnet_objs.setdefault(vn_uuid, OrderedDict())
        # create subnet for each new subnet
        refs = vn_dict.get('network_ipam_refs')
        if refs:
            for ref in refs:
                # only create a subnet obj for a ref which is not a flat ipam
                # instead of reading ipam from db, inspect link between ref
                # and vn, if link has only one subnet without ip_prefix key
                # in subnet dict, that is a flat-subnet ipam
                vnsn_data = ref.get('attr') or {}
                ipam_subnets = vnsn_data.get('ipam_subnets') or []
                if len(ipam_subnets) == 1:
                    ipam_subnet = ipam_subnets[0]
                    subnet = ipam_subnet.get('subnet') or {}
                    if 'ip_prefix' not in subnet:
                        continue
                if ipam_subnets:
                    self._create_subnet_objs(vn_fq_name_str, vn_uuid,
                                             ipam_subnets, should_persist,
                                             alloc_pool_change, False)
    # end _create_net_subnet_objs

    def config_log(self, msg, level):
        self._server_mgr.config_log(msg, level)
    # end config_log

    def net_create_req(self, obj_dict):
        vn_fq_name_str = ':'.join(obj_dict['fq_name'])
        vn_uuid = obj_dict['uuid']
        req_subnet_dicts = self._get_net_subnet_dicts(vn_uuid, obj_dict)

        if req_subnet_dicts:
            for key in req_subnet_dicts.keys():
                req_subnet = req_subnet_dicts[key]
                (ok, msg, result) = self._check_subnet_alloc_pools(req_subnet)
                if not ok:
                    raise AddrMgmtSubnetInvalid(vn_fq_name_str, key+':'+msg)

        self._create_net_subnet_objs(vn_fq_name_str, vn_uuid,
                                     obj_dict, should_persist=True,
                                     alloc_pool_change=[])
    # end net_create_req

    def net_create_notify(self, obj_id, vn_dict):
        vn_fq_name_str = ':'.join(vn_dict['fq_name'])
        self._create_net_subnet_objs(vn_fq_name_str, obj_id, vn_dict,
                                     should_persist=False,
                                     alloc_pool_change=[])
    # end net_create_notify

    def net_update_req(self, vn_fq_name, db_vn_dict, req_vn_dict, obj_uuid=None):
        if 'network_ipam_refs' not in req_vn_dict:
            return

        vn_fq_name_str = ':'.join(vn_fq_name)
        vn_uuid = db_vn_dict['uuid']
        db_subnet_dicts = self._get_net_subnet_dicts(vn_uuid, db_vn_dict)
        req_subnet_dicts = self._get_net_subnet_dicts(vn_uuid, req_vn_dict)

        db_subnet_names = set(db_subnet_dicts.keys())
        req_subnet_names = set(req_subnet_dicts.keys())

        del_subnet_names = db_subnet_names - req_subnet_names
        add_subnet_names = req_subnet_names - db_subnet_names

        for subnet_name in del_subnet_names:
            Subnet.delete_cls('%s:%s' % (vn_fq_name_str, subnet_name), False)

        # Remove stale entries in _subnet_objs
        vn_subnet_objs = self._subnet_objs.get(vn_uuid)
        if vn_subnet_objs:
            for subnet_name in add_subnet_names:
                subnet_obj = vn_subnet_objs.pop(subnet_name, None)
                if subnet_obj is not None:
                    msg = "Removing stale subnet entry: %s:%s" % (
                        vn_fq_name_str, subnet_name,)
                    self.config_log(msg, level=SandeshLevel.SYS_WARN)

        # check db_subnet_dicts and req_subnet_dicts
        # following parameters are same for subnets present in both dicts
        # default_gateway, dns_server_address
        # allocation_pool,dns_nameservers

        subnets_pool_change = []
        for key in req_subnet_dicts.keys():
            req_subnet = req_subnet_dicts[key]
            if key in db_subnet_dicts.keys():
                db_subnet = db_subnet_dicts[key]
                if (req_subnet['gw'] and
                    req_subnet['gw'].lower() != db_subnet['gw'].lower()):
                    raise AddrMgmtSubnetInvalid(vn_fq_name_str, key)

                (ok, msg, result) = self._check_subnet_alloc_pools(req_subnet,
                                                                   db_subnet)
                if not ok:
                    raise AddrMgmtSubnetInvalid(vn_fq_name_str, key+':'+msg)
                if result is not None:
                    subnets_pool_change.append(result)

        self._create_net_subnet_objs(vn_fq_name_str, vn_uuid, req_vn_dict,
                                     should_persist=True,
                                     alloc_pool_change=subnets_pool_change)
    # end net_update_req

    def net_update_notify(self, obj_id):
        db_conn = self._get_db_conn()
        try:
            ok, result = db_conn.dbe_read(
                obj_type='virtual_network',
                obj_id=obj_id,
                obj_fields=['fq_name', 'network_ipam_refs'],
            )
        except cfgm_common.exceptions.NoIdError as e:
            return False, (404, str(e))
        if not ok:
            return False, result
        vn_dict = result
        vn_fq_name_str = ':'.join(vn_dict['fq_name'])

        # Gets vn's subnets list
        vn_list_subnets = self._vn_to_subnets(vn_dict) or []
        if obj_id in self._subnet_objs.keys():
            del_subnet_names = set(self._subnet_objs[obj_id]) - set(vn_list_subnets)
            for subnet_name in del_subnet_names:
                Subnet.delete_cls('%s:%s' % (vn_fq_name_str, subnet_name))
                try:
                    del self._subnet_objs[obj_id][subnet_name]
                except KeyError:
                    pass

        self._create_net_subnet_objs(vn_fq_name_str, obj_id, vn_dict,
                                     should_persist=False,
                                     alloc_pool_change=[])

        return True, ''
    # end net_update_notify

    def get_subnet_quota_counter(self, obj_dict, proj_dict, db_vn_dict=None):
        obj_type = 'subnet'
        quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)
        subnet_count = len(self._vn_to_subnets(obj_dict) or [])
        if quota_limit >= 0:
            path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + proj_dict['uuid']
            path = path_prefix + "/" + obj_type
            quota_counter = self._server_mgr.quota_counter
            if not quota_counter.get(path):
                # Init quota counter for subnet
                db_conn = self._get_db_conn()
                QuotaHelper._zk_quota_counter_init(
                    path_prefix,
                    {obj_type: quota_limit},
                    proj_dict['uuid'],
                    db_conn,
                    quota_counter)
            return (True, (subnet_count, quota_counter[path]))
        return (True, (0, ""))
    # end get_subnet_quota_counter

    # purge all subnets associated with a virtual network
    def net_delete_req(self, obj_dict):
        vn_fq_name = obj_dict['fq_name']
        vn_fq_name_str = ':'.join(vn_fq_name)
        vn_uuid = obj_dict['uuid']
        subnet_dicts = self._get_net_subnet_dicts(vn_uuid)

        for subnet_name in subnet_dicts:
            Subnet.delete_cls('%s:%s' % (vn_fq_name_str, subnet_name), False)
    # end net_delete_req

    def net_delete_notify(self, obj_id, obj_dict):
        vn_list_subnets = self._vn_to_subnets(obj_dict) or []
        if obj_id in self._subnet_objs:
            del_subnet_names = set(self._subnet_objs[obj_id]) - set(vn_list_subnets)
            vn_fq_name_str = ':'.join(obj_dict['fq_name'])
            for subnet_name in del_subnet_names:
                Subnet.delete_cls('%s:%s' % (vn_fq_name_str, subnet_name))
            try:
                del self._subnet_objs[obj_id]
            except KeyError:
                pass
    # end net_delete_notify

    def _ipam_to_subnets(self, ipam_dict):
        subnet_list = []
        ipams_subnets_dict = ipam_dict.get('ipam_subnets') or {}
        ipams_subnets = ipams_subnets_dict.get('subnets') or []
        for ipams_subnet in ipams_subnets:
            subnet = ipams_subnet['subnet']
            subnet_name = \
                subnet['ip_prefix'] + '/' + str(subnet['ip_prefix_len'])
            subnet_list.append(subnet_name)
        return subnet_list
    # _ipam_to_subnets

    def _vn_to_subnets(self, obj_dict):
        # given a VN return its subnets in list of net/prefixlen strings

        ipam_refs = obj_dict.get('network_ipam_refs')
        if ipam_refs != None:
            subnet_list = []
            for ref in ipam_refs:
                # flat subnet ipam may have a link to store uuid only
                # ignore that ipam as cidrs are irrelevant, skip flat-subnet
                # single entry in ipam_subnets with only uuid and
                # without any ip_prefix and prefix_len
                vnsn_data = ref['attr']
                ipam_subnets = vnsn_data.get('ipam_subnets') or []
                if len(ipam_subnets) is 0:
                    continue
                first_ipam_subnet = ipam_subnets[0]
                subnet = first_ipam_subnet.get('subnet') or {}
                if ('ip_prefix' not in subnet):
                    continue

                for ipam_subnet in ipam_subnets:
                    subnet = ipam_subnet['subnet']
                    subnet_name = subnet['ip_prefix'] + '/' + str(
                        subnet['ip_prefix_len'])
                    subnet_list.append(subnet_name)
        else:
            subnet_list = None

        return subnet_list
    # end _vn_to_subnets

    def net_check_subnet_quota(self, db_vn_dict, req_vn_dict, db_conn):
        if db_vn_dict:
            obj_dict = db_vn_dict
            db_subnets = self._vn_to_subnets(db_vn_dict) or []
        else:
            obj_dict = req_vn_dict
            db_subnets = []
        if obj_dict['id_perms'].get('user_visible', True) is False:
            return True, ""
        (ok, proj_dict) = QuotaHelper.get_project_dict_for_quota(
            obj_dict['parent_uuid'], db_conn)
        if not ok:
            return (False, (500, 'Bad Project error : ' + pformat(proj_dict)))
        req_subnet_count = len(self._vn_to_subnets(req_vn_dict) or [])
        db_subnet_count = len(db_subnets)
        subnet_count = req_subnet_count - db_subnet_count
        obj_type = 'subnet'
        quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)
        if (subnet_count and quota_limit >= 0):
            quota_counter = self._server_mgr.quota_counter
            path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + proj_dict['uuid']
            path = path_prefix + "/" + obj_type
            if not quota_counter.get(path):
                # Init quota counter for subnet
                QuotaHelper._zk_quota_counter_init(
                        path_prefix, {obj_type : quota_limit},
                        proj_dict['uuid'], db_conn, quota_counter)
            ok, result = QuotaHelper.verify_quota(
                obj_type, quota_limit, quota_counter[path],
                count=subnet_count)
            if not ok:
                return (False,
                        pformat(obj_dict['fq_name']) + ' : ' + str(quota_limit))

            def undo():
                # Revert back quota count
                quota_counter[path] -= subnet_count
            get_context().push_undo(undo)

        return True, ""
    # end net_check_subnet_quota

    # check subnets in the given list to make sure that none in the
    # list is in overlap with refs list
    def check_overlap_with_refs(self, refs_list, req_list=None):
        if req_list is None:
            return True, ""
        req_subnets = [IPNetwork(subnet) for subnet in req_list]
        refs_subnets = [IPNetwork(subnet) for subnet in refs_list]
        for net1 in req_subnets:
            for net2 in refs_subnets:
                if net1 in net2 or net2 in net1:
                    err_msg = "Overlapping addresses: "
                    return False, err_msg + str([net1, net2])

        return True, ""
    # end check_overlap_with_refs

    # check subnets associated with ipam or vn, return error if
    # any two subnets have overal ip address
    def check_subnet_overlap(self, requested_subnets):
        subnets = [IPNetwork(subnet) for subnet in requested_subnets]
        for i, net1 in enumerate(subnets):
            for net2 in subnets[i+1:]:
                if net1 in net2 or net2 in net1:
                    err_msg = "Overlapping addresses: "
                    return False, err_msg + str([net1, net2])

        return True, ""
    # end check_subnet_overlap

    # check subnets associated with a virtual network, return error if
    # any two subnets have overlap ip addresses
    def net_check_subnet_overlap(self, req_vn_subnets=[], req_ipam_subnets=[]):
        # get all subnets existing + requested and check any non-exact overlaps
        all_req_subnets = req_ipam_subnets + req_vn_subnets
        if len(all_req_subnets) == 0:
            return True, ""

        return self.check_subnet_overlap(all_req_subnets)
    # end net_check_subnet_overlap

    def net_check_subnet(self, ipam_subnets):

        for ipam_subnet in ipam_subnets:
            subnet_dict = copy.deepcopy(ipam_subnet['subnet'])
            prefix = subnet_dict['ip_prefix']
            prefix_len = subnet_dict['ip_prefix_len']
            network = IPNetwork('%s/%s' % (prefix, prefix_len))
            subnet_name = subnet_dict['ip_prefix'] + '/' + str(
                subnet_dict['ip_prefix_len'])

            # check subnet-uuid
            ipam_cfg_subnet_uuid = ipam_subnet.get('subnet_uuid')
            try:
                if ipam_cfg_subnet_uuid:
                    subnet_uuid = uuid.UUID(ipam_cfg_subnet_uuid)
            except ValueError:
                err_msg = "Invalid subnet-uuid %s in subnet:%s" \
                %(ipam_cfg_subnet_uuid, subnet_name)
                return False, err_msg

            # check allocation-pool
            alloc_pools = ipam_subnet.get('allocation_pools')
            for pool in alloc_pools or []:
                try:
                    iprange = IPRange(pool['start'], pool['end'])
                except netaddr.core.AddrFormatError:
                    err_msg = "Invalid allocation Pool start:%s, end:%s in subnet:%s" \
                    %(pool['start'], pool['end'], subnet_name)
                    return False, err_msg
                if iprange not in network:
                    err_msg = "allocation pool start:%s, end:%s out of cidr:%s" \
                    %(pool['start'], pool['end'], subnet_name)
                    return False, err_msg

            # check gw
            gw = ipam_subnet.get('default_gateway')
            if gw and gw != 'None':
                try:
                    gw_ip = IPAddress(gw)
                except netaddr.core.AddrFormatError:
                    err_msg = "Invalid gateway Ip address:%s" \
                    %(gw)
                    return False, err_msg
                if (gw_ip != IPAddress('0.0.0.0') and
                    gw_ip != IPAddress('::') and
                    (gw_ip < IPAddress(network.first + 1) or
                    gw_ip > IPAddress(network.last - 1))):
                    err_msg = "gateway Ip %s out of cidr: %s" \
                    %(gw, subnet_name)
                    return False, err_msg

            # check service address
            service_address = ipam_subnet.get('dns_server_address')
            if service_address and service_address != 'None':
                try:
                    service_node_address = IPAddress(service_address)
                except netaddr.core.AddrFormatError:
                    err_msg = "Invalid Dns Server Ip address:%s" \
                    %(service_address)
                    return False, err_msg

        return True, ""
    # end net_check_subnet

    #check if any ip address from given subnet sets is used in
    # in given virtual network, this includes instance_ip, fip and
    # alias_ip
    def _check_subnet_delete(self, subnets_set, vn_dict):
        db_conn = self._get_db_conn()
        instip_refs = vn_dict.get('instance_ip_back_refs') or []
        for ref in instip_refs:
            try:
                (ok, result) = db_conn.dbe_read('instance_ip', ref['uuid'])
            except cfgm_common.exceptions.NoIdError:
                continue
            if not ok:
                self.config_log(
                    "Error in subnet delete instance-ip check: %s" %(result),
                    level=SandeshLevel.SYS_ERR)
                return False, result

            inst_ip = result.get('instance_ip_address')
            if not inst_ip:
                self.config_log(
                    "Error in subnet delete ip null: %s" %(ref['uuid']),
                    level=SandeshLevel.SYS_ERR)
                continue
            if all_matching_cidrs(inst_ip, subnets_set):
                return (False,
                        "Cannot Delete IP Block, IP(%s) is in use"
                        % (inst_ip))

        fip_pool_refs = vn_dict.get('floating_ip_pools') or []
        for ref in fip_pool_refs:
            try:
                (ok, result) = db_conn.dbe_read('floating_ip_pool', ref['uuid'])
            except cfgm_common.exceptions.NoIdError:
                continue
            if not ok:
                self.config_log(
                    "Error in subnet delete floating-ip-pool check: %s"
                        %(result),
                    level=SandeshLevel.SYS_ERR)
                return False, result

            floating_ips = result.get('floating_ips') or []
            for floating_ip in floating_ips:
                try:
                    (read_ok, read_result) = db_conn.dbe_read(
                        'floating_ip', floating_ip['uuid'])
                except cfgm_common.exceptions.NoIdError:
                    continue
                if not read_ok:
                    self.config_log(
                        "Error in subnet delete floating-ip check: %s"
                            %(read_result),
                        level=SandeshLevel.SYS_ERR)
                    return False, result

                fip_addr = read_result.get('floating_ip_address')
                if not fip_addr:
                    self.config_log(
                        "Error in subnet delete fip null: %s"
                        %(floating_ip['uuid']),
                        level=SandeshLevel.SYS_ERR)
                    continue
                if all_matching_cidrs(fip_addr, subnets_set):
                    return (False,
                            "Cannot Delete IP Block, Floating IP(%s) is in use"
                            % (fip_addr))

        aip_pool_refs = vn_dict.get('alias_ip_pools') or []
        for ref in aip_pool_refs:
            try:
                (ok, result) = db_conn.dbe_read('alias_ip_pool', ref['uuid'])
            except cfgm_common.exceptions.NoIdError:
                continue
            if not ok:
                self.config_log(
                    "Error in subnet delete alias-ip-pool check: %s"
                        %(result),
                    level=SandeshLevel.SYS_ERR)
                return False, result

            alias_ips = result.get('alias_ips') or []
            for alias_ip in alias_ips:
                # get alias_ip_address and this should be in
                # new subnet_list
                try:
                    (read_ok, read_result) = db_conn.dbe_read(
                        'alias_ip', floating_ip['uuid'])
                except cfgm_common.exceptions.NoIdError:
                    continue
                if not read_ok:
                    self.config_log(
                        "Error in subnet delete floating-ip check: %s"
                            %(read_result),
                        level=SandeshLevel.SYS_ERR)
                    return False, result

                aip_addr = read_result.get('alias_ip_address')
                if not aip_addr:
                    self.config_log(
                        "Error in subnet delete aip null: %s"
                        %(alias_ip['uuid']),
                        level=SandeshLevel.SYS_ERR)
                    continue
                if all_matching_cidrs(aip_addr, subnets_set):
                    return (False,
                            "Cannot Delete IP Block, Alias IP(%s) is in use"
                            % (aip_addr))

        return True, ""
    # end _check_subnet_delete

    # check subnets associated with a ipam, return error if
    # any subnet is being deleted and has backref to
    # instance-ip/floating-ip/alias-ip
    def ipam_check_subnet_delete(self, db_ipam_dict, req_ipam_dict):
        if 'ipam_subnets' not in req_ipam_dict:
            # subnets not modified in request
            return True, ""

        req_subnets = self._ipam_to_subnets(req_ipam_dict)
        existing_subnets = self._ipam_to_subnets(db_ipam_dict)
        if not existing_subnets:
            # No subnets so far in ipam, no need to check any ip-allocation
            return True, ""

        subnet_method = db_ipam_dict.get('ipam_subnet_method')
        if subnet_method != 'flat-subnet':
            return True, ""

        db_conn = self._get_db_conn()
        delete_set = set(existing_subnets) - set(req_subnets)
        if not delete_set:
            return True, ""

        vn_refs = db_ipam_dict.get('virtual_network_back_refs')
        if not vn_refs:
            return True, ""

        for ref in vn_refs:
            vn_id = ref.get('uuid')
            try:
                (ok, read_result) = db_conn.dbe_read('virtual_network', vn_id)
            except cfgm_common.exceptions.NoIdError:
                continue
            if not ok:
                self.config_log(
                    "Error in ipam subnet delete check: %s" %(read_result),
                    level=SandeshLevel.SYS_ERR)
                return False, read_result

            vn_dict = read_result
            (ok, response) = self._check_subnet_delete(delete_set, vn_dict)
            if not ok:
                return ok, response

        return True, ""
    # end ipam_check_subnet_delete

    # check subnets associated with a virtual network, return error if
    # any subnet is being deleted and has backref to
    # instance-ip/floating-ip/alias-ip
    def net_check_subnet_delete(self, db_vn_dict, req_vn_dict):
        if 'network_ipam_refs' not in req_vn_dict:
            # subnets not modified in request
            return True, ""

        # if all ips are part of requested list
        # things are ok.
        # eg. existing [1.1.1.0/24, 2.2.2.0/24],
        #     requested [1.1.1.0/24] OR
        #     requested [1.1.1.0/28, 2.2.2.0/24]
        existing_subnets = self._vn_to_subnets(db_vn_dict)
        if not existing_subnets:
            return True, ""

        requested_subnets = self._vn_to_subnets(req_vn_dict)
        delete_set = set(existing_subnets) - set(requested_subnets)
        if not delete_set:
            return True, ""

        return self._check_subnet_delete(delete_set, db_vn_dict)
    # end net_check_subnet_delete

    # validate any change in subnet and reject if
    # dns server and gw_ip got changed

    def _validate_subnet_update(self, req_subnets, db_subnets):
        for req_subnet in req_subnets:
            req_cidr = req_subnet.get('subnet')
            req_df_gw = req_subnet.get('default_gateway')
            if req_df_gw:
                req_df_gw = req_df_gw.lower()

            for db_subnet in db_subnets:
                db_cidr = db_subnet.get('subnet')
                if req_cidr is None or db_cidr is None:
                    continue
                if req_cidr != db_cidr:
                    continue

                # for a given subnet, default gateway should not be different
                db_df_gw = db_subnet.get('default_gateway')
                if db_df_gw:
                    db_df_gw = db_df_gw.lower()
                    if db_df_gw == 'none':
                        db_df_gw = None

                db_prefix = db_cidr.get('ip_prefix')
                db_prefix_len = db_cidr.get('ip_prefix_len')

                network = IPNetwork('%s/%s' % (db_prefix, db_prefix_len))
                if db_subnet.get('addr_from_start'):
                    df_gw_ip = str(IPAddress(network.first + 1))
                else:
                    df_gw_ip = str(IPAddress(network.last - 1))

                if db_df_gw != req_df_gw:
                    invalid_update = False
                    if ((req_df_gw is None) and (db_df_gw != df_gw_ip)):
                        invalid_update = True

                    if ((req_df_gw != None) and (req_df_gw != 'none') and (req_df_gw != df_gw_ip)):
                        invalid_update = True
                    if invalid_update is True:
                        err_msg = "default gateway change is not allowed" +\
                                  " orig:%s, new: %s" \
                                  %(db_df_gw, req_df_gw)
                        return False, err_msg

        return True, ""
    # end _validate_subnet_update

    # validate ipam_subnets configured at ipam
    # reject change in gw_ip and dsn_server in any subnet
    def ipam_validate_subnet_update(self, db_ipam_dict, req_ipam_dict):
        req_ipam_subnets = req_ipam_dict.get('ipam_subnets')
        db_ipam_subnets = db_ipam_dict.get('ipam_subnets')
        if req_ipam_subnets is None or db_ipam_subnets is None:
            return True, ""

        req_subnets = req_ipam_subnets.get('subnets') or []
        db_subnets = db_ipam_subnets.get('subnets') or []
        (ok, result) = self._validate_subnet_update(req_subnets, db_subnets)
        if not ok:
            return ok, result

        return True, ""
    # end ipam_validate_subnet_update

    # validate ipam_subnets configured at ipam->vn link
    # reject change in gw_ip and dsn_server in any subnet
    def net_validate_subnet_update(self, db_vn_dict, req_vn_dict):
        if 'network_ipam_refs' not in req_vn_dict:
            # subnets not modified in request
            return True, ""
        if 'network_ipam_refs' not in db_vn_dict:
            # all subnets in req_vn_dict are new.
            return True, ""

        # check if gw_ip and dns_address are not modified
        # in requested network_ipam_refs for any given subnets.
        req_ipam_refs = req_vn_dict.get('network_ipam_refs')
        db_ipam_refs = db_vn_dict.get('network_ipam_refs')
        for req_ref in req_ipam_refs:
            req_ipam_ref_uuid = req_ref.get('uuid')
            for db_ref in db_ipam_refs:
                db_ipam_ref_uuid = db_ref.get('uuid')
                if db_ipam_ref_uuid != req_ipam_ref_uuid:
                    continue

                req_vnsn_data = req_ref.get('attr')
                req_subnets = req_vnsn_data.get('ipam_subnets') or []
                db_vnsn_data = db_ref.get('attr')
                db_subnets = db_vnsn_data.get('ipam_subnets') or []
                (ok, result) = self._validate_subnet_update(req_subnets, db_subnets)
                if not ok:
                    return ok, result

        return True, ""
    # end net_validate_subnet_update


    # return number of ip address currently allocated for a subnet
    # count will also include reserved ips
    def ip_count_req(self, vn_fq_name, subnet_uuid):
        if subnet_uuid is None:
            return 0

        db_conn = self._get_db_conn()
        vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
        subnet_dicts = self._get_net_subnet_dicts(vn_uuid)
        req_subnet_name = None
        for subnet_name, subnet_dict in subnet_dicts.items():
            if subnet_uuid == subnet_dict.get('subnet_uuid'):
                req_subnet_name = subnet_name
                break

        if req_subnet_name is None:
            # subnet is not on this network, return zero to reflect no ip addr
            # is allocated from requested subnet_uuid
            return 0

        subnet_obj = self._subnet_objs[vn_uuid][req_subnet_name]
        return subnet_obj.ip_count()
    # end ip_count_req

    def _get_subnet_obj(self, fq_name_str, obj_uuid, subnet_name, subnet_dict):
        try:
            subnet_obj = self._subnet_objs[obj_uuid][subnet_name]
        except KeyError:
            self._subnet_objs.setdefault(obj_uuid, OrderedDict())
            subnet_obj = Subnet(
                '%s:%s' % (fq_name_str, subnet_name),
                subnet_dict['ip_prefix'],
                subnet_dict['ip_prefix_len'],
                gw=subnet_dict['gw'],
                service_address=subnet_dict['dns_server_address'],
                dns_nameservers=subnet_dict['dns_nameservers'],
                alloc_pool_list=subnet_dict['allocation_pools'],
                addr_from_start = subnet_dict['addr_start'],
                should_persist=False,
                ip_alloc_unit=subnet_dict['alloc_unit'])
            self._subnet_objs[obj_uuid][subnet_name] = subnet_obj
        return subnet_obj
    # end _get_subnet_obj

    def _ipam_subnet_db_sync(self, ipam_uuid, subnet_name, field,
                             new_value=None):
        '''
        update specific field in a subnet of ipam object in the database
        '''
        db_conn = self._get_db_conn()
        (ok, ipam_dict) = db_conn.dbe_read(obj_type='network_ipam',
                                           obj_id=ipam_uuid)
        db_ipam_subnets_dict = ipam_dict.get('ipam_subnets') or {}
        db_ipam_subnets = db_ipam_subnets_dict.get('subnets') or []
        for db_ipam_subnet in db_ipam_subnets:
            db_subnet = db_ipam_subnet['subnet']
            db_subnet_name = db_subnet['ip_prefix'] + '/' + str(
                                 db_subnet['ip_prefix_len'])
            if db_subnet_name != subnet_name:
                continue
            if field in db_ipam_subnet:
                db_ipam_subnet[field] = new_value
                self._server_mgr.internal_request_update('network-ipam',
                                                         ipam_uuid,
                                                         ipam_dict)
            return
        return
    # end _ipam_subnet_db_sync

    def _ipam_ip_alloc(self, ipam_ref=None,
                sn_uuid=None, sub=None, asked_ip_addr=None,
                asked_ip_version=4, alloc_id=None, alloc_pools=None,
                subscriber_tag=None):
        db_conn = self._get_db_conn()
        subnets_tried = []
        ip_addr = None
        ipam_fq_name = ipam_ref['to']
        ipam_uuid = ipam_ref['uuid']
        subnet_objs = self._get_ipam_subnet_objs_from_ipam_uuid(
                                ipam_fq_name, ipam_uuid, False)
        if not subnet_objs:
            return (ip_addr, subnets_tried, None)

        if subscriber_tag:
            # This is a ip alloc for fabric iip and does not need to go through
            # other checks like asked_ip or uuid based allocation
            # subnet which has already served subscription tag will be used to
            # allocate ip address, otherwise pick up first available subnet
            # and store subscription tag for future ip allocation with same
            # tag
            (subnet_name, tagged_subnet) = self._get_subnet_obj_for_subs_tag(
                                               subnet_objs, subscriber_tag)
            if not tagged_subnet:
                # No subnet found for a given tag, allocation ip address
                # from subnetted subnet without any subscriber tag
                for subnet_name, subnet_obj in subnet_objs.items():
                    subnet_obj=subnet_objs[subnet_name]
                    if not subnet_obj.subnetting:
                        continue
                    if subnet_obj.subscriber_tag:
                        continue

                    subnets_tried.append(subnet_name)
                    try:
                        ip_addr = subnet_obj.ip_alloc(ipaddr=None, value=alloc_id,
                                      version=subnet_obj._network.version,
                                      sn_alloc_pools=[])
                    except cfgm_common.exceptions.ResourceExhaustionError as e:
                        continue
                    if ip_addr is not None:
                        subnet_obj.subscriber_tag = subscriber_tag
                        self._ipam_subnet_db_sync(ipam_uuid, subnet_name,
                                                  'subscriber_tag',
                                                   subscriber_tag)
                        return (ip_addr, subnets_tried, subnet_name)
                return (ip_addr, subnets_tried, None)
            else:
                # Subnet found with a tag,try to allocate ip address
                subnets_tried.append(subnet_name)
                try:
                    ip_addr = tagged_subnet.ip_alloc(ipaddr=None,
                                  value=alloc_id,
                                  version=tagged_subnet._network.version,
                                  sn_alloc_pools=[])
                    subnet_name = tagged_subnet._prefix + '/' + str(
                          tagged_subnet._prefix_len)
                    return (ip_addr, subnets_tried, subnet_name)

                except cfgm_common.exceptions.ResourceExhaustionError as e:
                    ip_addr = None
                    return (ip_addr, subnets_tried, None)

        # This is for tenant based ip allocation not for fabric
        # and subnet with subnetting flag should not be consider
        # for this allocation
        for subnet_name in subnet_objs:
            subnet_alloc_pools = []
            subnet_obj = subnet_objs[subnet_name]

            if subnet_obj.subnetting:
                continue
            if (asked_ip_version and asked_ip_version !=
                    subnet_obj.get_version()):
                continue
            if asked_ip_addr == str(subnet_obj.gw_ip):
                return (asked_ip_addr, subnets_tried, None)
            if asked_ip_addr == str(subnet_obj.dns_server_address):
                return (asked_ip_addr, subnets_tried, None)
            if asked_ip_addr and not subnet_obj.ip_belongs(asked_ip_addr):
                continue

            if alloc_pools:
                # build a alloc_pool list only for this subnet
                # check of alloc_pool is a part of this subnet
                for alloc_pool in alloc_pools:
                    pool_start = alloc_pool['start']
                    pool_end = alloc_pool['end']
                    if (subnet_obj.ip_belongs(pool_start) and
                        subnet_obj.ip_belongs(pool_end)):
                        subnet_pool = {'start': int(IPAddress(pool_start)),
                                       'end': int(IPAddress(pool_end))}
                        subnet_alloc_pools.append(subnet_pool)

            subnets_tried.append(subnet_name)
            # if user requests ip-addr and that can't be reserved due to
            # existing object(iip/fip) using it, return an exception with
            # the info. client can determine if its error or not
            if asked_ip_addr:
                if (int(IPAddress(asked_ip_addr)) % subnet_obj.alloc_unit):
                    raise AddrMgmtAllocUnitInvalid(
                        subnet_obj._name,
                        subnet_obj._prefix+'/'+subnet_obj._prefix_len,
                        subnet_obj.ip_alloc_unit,
                        'Requested IP address %s not aligned' %(
                        asked_ip_addr))

                return (subnet_obj.ip_reserve(ipaddr=asked_ip_addr,
                                                 value=alloc_id), subnets_tried,
                                              None)
            try:
                ip_addr = subnet_obj.ip_alloc(ipaddr=None, value=alloc_id,
                              version=subnet_obj._network.version,
                              sn_alloc_pools=subnet_alloc_pools)
            except cfgm_common.exceptions.ResourceExhaustionError as e:
                continue
            if ip_addr is not None or sub:
                return (ip_addr, subnets_tried, None)
        return (ip_addr, subnets_tried, None)
    # end _ipam_ip_alloc

    def _ipam_ip_alloc_from_pools(self, ipam_refs, sub=None,
            asked_ip_addr=None, asked_ip_version=4, alloc_id=None,
            alloc_pools=None):
        db_conn = self._get_db_conn()
        if not ipam_refs:
          ipam_refs = []

        sn_uuid = None
        for ipam_ref in ipam_refs:
            ip_addr, _, _ = \
                self._ipam_ip_alloc(ipam_ref, sn_uuid, sub,
                        asked_ip_addr, asked_ip_version, alloc_id, alloc_pools)
            if ip_addr:
                return (ip_addr, None, None)
        raise AddrMgmtSubnetExhausted('instace_ip_request', alloc_pools)
    # end _ipam_ip_alloc_from_pools

    def _ipam_ip_alloc_req(self, vn_fq_name, vn_dict=None, sub=None,
            asked_ip_addr=None, asked_ip_version=4, alloc_id=None,
            subscriber_tag=None):
        db_conn = self._get_db_conn()
        ipam_refs = vn_dict.get('network_ipam_refs', [])

        # read ipam_subnets from all ipam_refs to be used later
        # to find out any allocation pool not specific to vrouter
        ipam_uuid_list = [(ipam_ref['uuid']) for ipam_ref in ipam_refs]
        (ok, result, _) = db_conn.dbe_list('network_ipam',
                                           obj_uuids=ipam_uuid_list,
                                           field_names=['ipam_subnets'])
        if not ok:
            raise cfgm_common.exceptions.VncError(ipam_uuid_list)
        ipams_data = {k:v for k, v in zip(ipam_uuid_list, result)}

        subnets_tried = []
        found_subnet_match = False
        for ipam_ref in ipam_refs:
            # ip alloc will go through only if ipam has a flat-subnet
            # check the link between VN and ipam and it should have only one
            # to with no ip_prefix and ipam_subnet with subnet_uuid
            # otherwise it is not a flat subnet
            vnsn_data = ipam_ref.get('attr') or {}
            ipam_subnets = vnsn_data.get('ipam_subnets') or []
            # if there are no ipam_subnets then either
            # it is a user-define-subnet without any subnet
            # added or flat-subnet ipam without and ipam_subnets
            if len(ipam_subnets) is 0:
                continue
            first_ipam_subnet = ipam_subnets[0]
            subnet = first_ipam_subnet.get('subnet') or {}
            if ('ip_prefix' in subnet):
                #This is a user-define-subnet
                continue
            sn_uuid = first_ipam_subnet.get('subnet_uuid')

            if sub:
                #check if subnet_uuid is stored in the vm->ipam link
                # to represent this ipam for flat-allocation.
                ipam_subnets = ipam_ref['attr'].get('ipam_subnets') or []
                for ipam_subnet in ipam_subnets:
                    ipam_subnet_uuid = ipam_subnet.get('subnet_uuid')
                    if ipam_subnet_uuid != None and ipam_subnet_uuid == sub:
                        sn_uuid = ipam_subnet_uuid
                        found_subnet_match = True
                        break
                if not found_subnet_match:
                    continue
            # walk through all subnets in ipam and build list of alloc_pools
            # which are not specific to the vrouter
            ipam_data = ipams_data.get(ipam_ref['uuid'], {})
            ipam_subnets = ipam_data.get('ipam_subnets', {})
            subnets = ipam_subnets.get('subnets', [])

            alloc_pools = []
            vr_pool_found = False
            for ipam_subnet in subnets:
                subnet_alloc_pools = ipam_subnet.get('allocation_pools', [])
                for subnet_alloc_pool in subnet_alloc_pools:
                    vr_flag = subnet_alloc_pool.get('vrouter_specific_pool',
                                                    False)
                    if not vr_flag:
                        alloc_pools.append(subnet_alloc_pool)
                    else:
                        vr_pool_found = True

            # if no vr_pool found then no need to send alloc_pools
            if not vr_pool_found:
                alloc_pools = []

            ip_addr, subnets_tried, alloted_subnet = \
                self._ipam_ip_alloc(ipam_ref, sn_uuid,
                    sub, asked_ip_addr, asked_ip_version, alloc_id,
                    alloc_pools, subscriber_tag)
            if ip_addr:
                return (ip_addr, sn_uuid, alloted_subnet)

        if sub:
            if not found_subnet_match:
                raise AddrMgmtSubnetInvalid(vn_fq_name, sub)
            else: # specified, matched but couldn't use
                raise AddrMgmtSubnetExhausted(vn_fq_name, subnets_tried)

        raise AddrMgmtSubnetExhausted(vn_fq_name, subnets_tried)
    # end _ipam_ip_alloc_req

    def _net_ip_alloc_req(self, vn_fq_name, vn_dict=None, subnet_uuid=None,
                          asked_ip_addr=None, asked_ip_version=4,
                          alloc_id=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        db_conn = self._get_db_conn()
        if not vn_dict:
            vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
            (ok, vn_dict) = self._uuid_to_obj_dict('virtual_network', vn_uuid)
            if not ok:
                raise cfgm_common.exceptions.VncError(vn_dict)
        else:
            vn_uuid = vn_dict['uuid']

        subnets_tried = []
        found_subnet_match = False
        subnet_dicts = self._get_net_subnet_dicts(vn_uuid, vn_dict)
        if not subnet_dicts:
            raise AddrMgmtSubnetAbsent(vn_fq_name)

        for subnet_name in subnet_dicts:
            subnet_dict = subnet_dicts[subnet_name]
            sn_uuid = subnet_dict['subnet_uuid']
            if subnet_uuid:
                if subnet_uuid != sn_uuid:
                    continue
                else:
                    found_subnet_match = True

            # create subnet_obj internally if it was created by some other
            # api-server before
            subnet_obj = self._get_subnet_obj(vn_fq_name_str, vn_uuid,
                                              subnet_name, subnet_dict)
            if asked_ip_version and asked_ip_version != subnet_obj.get_version():
                continue
            if asked_ip_addr == str(subnet_obj.gw_ip):
                return (asked_ip_addr, sn_uuid, None)
            if asked_ip_addr == str(subnet_obj.dns_server_address):
                return (asked_ip_addr, sn_uuid, None)
            if asked_ip_addr and not subnet_obj.ip_belongs(asked_ip_addr):
                continue

            subnets_tried.append(subnet_name)
            # if user requests ip-addr and that can't be reserved due to
            # existing object(iip/fip) using it, return an exception with
            # the info. client can determine if its error or not
            if asked_ip_addr:
                if (int(IPAddress(asked_ip_addr)) % subnet_obj.alloc_unit):
                    raise AddrMgmtAllocUnitInvalid(
                        subnet_obj._name,
                        subnet_obj._prefix+'/'+subnet_obj._prefix_len,
                        subnet_obj.alloc_unit,
                        'Requested IP address %s not aligned' %(
                            asked_ip_addr))

                return (subnet_obj.ip_reserve(ipaddr=asked_ip_addr,
                                              value=alloc_id), sn_uuid, None)
            try:
                ip_addr = subnet_obj.ip_alloc(ipaddr=None,
                                              value=alloc_id)
            except cfgm_common.exceptions.ResourceExhaustionError as e:
                continue

            if ip_addr is not None or subnet_uuid:
                return (ip_addr, sn_uuid, None)

        if subnet_uuid:
            if not found_subnet_match:
                raise AddrMgmtSubnetInvalid(vn_fq_name, subnet_uuid)
            else: # specified, matched but couldn't use
                raise AddrMgmtSubnetExhausted(vn_fq_name, subnets_tried)

        raise AddrMgmtSubnetExhausted(vn_fq_name, subnets_tried)
    # end _net_ip_alloc_req

    # allocate an IP address for given virtual network
    # we use the first available subnet unless provided
    def ip_alloc_req(self, vn_fq_name, vn_dict=None, sub=None,
                     asked_ip_addr=None, asked_ip_version=4,
                     alloc_id=None, ipam_refs=None,
                     alloc_pools=None, iip_subscriber_tag=None):
        db_conn = self._get_db_conn()
        if ipam_refs and not alloc_pools:
            # This is a request for ip address from flat subnet
            # where instace_ip is directly referencing ipam
            # for internal ip address
            sn_uuid = None
            ip_addr, _, _ = \
                self._ipam_ip_alloc(ipam_refs[0], sn_uuid, sub,
                        asked_ip_addr, asked_ip_version, alloc_id)
            return (ip_addr, sn_uuid, None)

        if ipam_refs and alloc_pools:
            # This is a request for ip address from flat subnet
            # where instace_ip is referring to vrouter which has
            # allocation pools on vrouter->ipam link.
            ip_addr, _, _ = self._ipam_ip_alloc_from_pools(ipam_refs, sub,
                             asked_ip_addr, asked_ip_version,
                            alloc_id, alloc_pools=alloc_pools)
            return (ip_addr, None, None)

        # This is a Virtual network based ip allocation either from flat-subnet
        # ipam or user-defined connected ipam
        if not vn_dict:
            obj_fields=['network_ipam_refs']
            (ok, vn_dict) = self._fq_name_to_obj_dict('virtual_network',
                                                      vn_fq_name, obj_fields)
            if not ok:
                raise cfgm_common.exceptions.VncError(vn_dict)

        #if subnet_uuid or asked_ip given, first try in ipam_alloc followed
        #by net_alloc
        if sub or asked_ip_addr:
            try:
                return self._ipam_ip_alloc_req(vn_fq_name, vn_dict, sub,
                                               asked_ip_addr, asked_ip_version,
                                               alloc_id, iip_subscriber_tag)
            except (AddrMgmtSubnetInvalid, AddrMgmtSubnetExhausted):
                return self._net_ip_alloc_req(vn_fq_name, vn_dict, sub,
                                              asked_ip_addr, asked_ip_version,
                                              alloc_id)

        allocation_method = vn_dict.get('address_allocation_mode')
        if allocation_method is None:
            allocation_method = 'user-defined-subnet-preferred'

        if allocation_method == 'flat-subnet-only':
            return self._ipam_ip_alloc_req(vn_fq_name, vn_dict, sub,
                                           asked_ip_addr, asked_ip_version,
                                           alloc_id, iip_subscriber_tag)
        elif allocation_method == 'user-defined-subnet-only':
            return self._net_ip_alloc_req(vn_fq_name, vn_dict, sub,
                                          asked_ip_addr, asked_ip_version,
                                          alloc_id)

        elif allocation_method == 'user-defined-subnet-preferred':
            #first try ip allcoation from user-defined-subnets, if
            #allocation exhausted, go to ipam-subnets
            try:
                return self._net_ip_alloc_req(vn_fq_name, vn_dict, sub,
                                              asked_ip_addr, asked_ip_version,
                                              alloc_id)
            except Exception as e:
                return self._ipam_ip_alloc_req(vn_fq_name, vn_dict, sub,
                                               asked_ip_addr, asked_ip_version,
                                               alloc_id, iip_subscriber_tag)

        elif allocation_method == 'flat-subnet-preferred':
            #first try ip allcoation from ipam-subnets, if
            #allocation exhausted, go to user-defined-subnets
            try:
                return self._ipam_ip_alloc_req(vn_fq_name, vn_dict, sub,
                                               asked_ip_addr, asked_ip_version,
                                               alloc_id, iip_subscriber_tag)
            except Exception as e:
                return self._net_ip_alloc_req(vn_fq_name, vn_dict, sub,
                                              asked_ip_addr, asked_ip_version,
                                              alloc_id)
        return None
    # end ip_alloc_req

    def _ipam_ip_alloc_notify(self, ip_addr, vn_uuid, ipam_refs=None):
        db_conn = self._get_db_conn()

        if not ipam_refs:
            # Read in the VN
            obj_fields=['network_ipam_refs']
            (ok, vn_dict) = self._uuid_to_obj_dict('virtual_network',
                                                vn_uuid, obj_fields)
            if not ok:
                raise cfgm_common.exceptions.VncError(vn_dict)

            ipam_refs = vn_dict['network_ipam_refs']

        for ipam_ref in ipam_refs:
            ipam_fq_name = ipam_ref['to']
            ipam_uuid = ipam_ref['uuid']
            subnet_objs = self._get_ipam_subnet_objs_from_ipam_uuid(
                              ipam_fq_name, ipam_uuid, False)

            if not subnet_objs:
                continue

            for subnet_name in subnet_objs:
                subnet_obj = subnet_objs[subnet_name]
                if not subnet_obj.ip_belongs(ip_addr):
                    continue
                ip_addr = subnet_obj.ip_set_in_use(ipaddr=ip_addr)
                return True

        return False
    # end _ipam_ip_alloc_notify

    def _net_ip_alloc_notify(self, ip_addr, vn_uuid, vn_fq_name):
        db_conn = self._get_db_conn()
        vn_fq_name_str = ':'.join(vn_fq_name)
        try:
            subnet_dicts = self._get_net_subnet_dicts(vn_uuid)
        except cfgm_common.exceptions.NoIdError:
            return False

        for subnet_name in subnet_dicts:
            # create subnet_obj internally if it was created by some other
            # api-server before
            subnet_dict = subnet_dicts[subnet_name]
            subnet_obj = self._get_subnet_obj(vn_fq_name_str, vn_uuid,
                                              subnet_name, subnet_dict)

            if not subnet_obj.ip_belongs(ip_addr):
                continue

            ip_addr = subnet_obj.ip_set_in_use(ipaddr=ip_addr)
            return True
        return False
    # end _net_ip_alloc_notify

    def ip_alloc_notify(self, ip_addr, vn_fq_name, ipam_refs=None):
        db_conn = self._get_db_conn()
        if ipam_refs:
            self._ipam_ip_alloc_notify(ip_addr, None, ipam_refs)
            return
        vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
        if (self._ipam_ip_alloc_notify(ip_addr, vn_uuid) == False):
            self._net_ip_alloc_notify(ip_addr, vn_uuid, vn_fq_name)
    # end ip_alloc_notify

    def _ipam_ip_free_req(self, ip_addr, vn_uuid, sub=None, ipam_refs=None,
            vn_dict=None, ipam_dicts=None):
        ipam_dicts = ipam_dicts or {}
        db_conn = self._get_db_conn()

        if not ipam_refs:
            # Read in the VN
            if not vn_dict:
                obj_fields=['network_ipam_refs']
                (ok, vn_dict) = self._uuid_to_obj_dict('virtual_network',
                                               vn_uuid, obj_fields)
                if not ok:
                    raise cfgm_common.exceptions.VncError(vn_dict)

            ipam_refs = vn_dict['network_ipam_refs']

        for ipam_ref in ipam_refs:
            ipam_fq_name = ipam_ref['to']
            ipam_uuid = ipam_ref['uuid']
            subnet_objs = self._get_ipam_subnet_objs_from_ipam_uuid(
                                ipam_fq_name, ipam_uuid, False,
                                ipam_dicts.get(ipam_uuid))
            if subnet_objs is None:
                continue

            for subnet_name in subnet_objs:
                if sub and sub != subnet_name:
                    continue

                subnet_obj = subnet_objs[subnet_name]
                if subnet_obj.ip_belongs(ip_addr):
                    subnet_obj.ip_free(IPAddress(ip_addr))
                    if ((subnet_obj.subscriber_tag) and (subnet_obj.subscriber_tag != None)):
                        for ip_addr in subnet_obj._network:
                            if IPAddress(ip_addr) in subnet_obj._exclude:
                                continue
                            if(subnet_obj.is_ip_allocated(ip_addr)):
                                return True
                        # All ip address are free for this subscriber_tag, need to remove
                        # subscriber_tag from ipam object
                        subnet_obj.subscriber_tag = None
                        self._ipam_subnet_db_sync(ipam_uuid, subnet_name,
                                                  'subscriber_tag')
                    return True
        return False
    # end _ipam_ip_free_req

    def _net_ip_free_req(self, ip_addr, vn_uuid, vn_fq_name, sub=None,
            vn_dict=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        subnet_dicts = self._get_net_subnet_dicts(vn_uuid, vn_dict)
        for subnet_name in subnet_dicts:
            if sub and sub != subnet_name:
                continue
            subnet_dict = subnet_dicts[subnet_name]
            subnet_obj = self._get_subnet_obj(vn_fq_name_str, vn_uuid,
                                              subnet_name, subnet_dict)
            if subnet_obj.ip_belongs(ip_addr):
                subnet_obj.ip_free(IPAddress(ip_addr))
                return True
        return False
    # end _net_ip_free_req

    def get_ip_free_args(self, vn_fq_name, vn_dict=None):
        """ Returns the vn dict and ipam dicts of a VN
        which will be used by the ip free req to free
        ip from the zookeeper.

        This method should be called before deleting
        IP from the cassandra, to make sure we have all
        necessary data to delete IP from zookeeper even
        if the api to cassandra connection is down/flapping.
        LP# 1749294
        """
        db_conn = self._get_db_conn()
        kwargs = {}
        if not vn_dict:
            obj_type = 'virtual_network'
            req_fields = ['network_ipam_refs']
            try:
               vn_uuid = db_conn.fq_name_to_uuid(obj_type, vn_fq_name)
            except cfgm_common.exceptions.NoIdError:
               return (False,
                       (500, 'Virtual Network(%s) not found' % vn_fq_name))
            (ok, vn_dict) = self._uuid_to_obj_dict(
                    obj_type, vn_uuid, req_fields)
            if not ok:
                return (False,
                        (500, 'Could not fetch virtual network to free allocated IP : ' +
                            pformat(vn_dict)))
        kwargs.update({'vn_dict': vn_dict})

        ipam_dicts = {}
        for ipam_ref in vn_dict.get('network_ipam_refs', []):
            (ok, ipam_dict) = self._uuid_to_obj_dict(
                    'network_ipam', ipam_ref['uuid'])
            if not ok:
                return (False,
                        (500, 'Could not fetch IPAM to free allocated IP : ' +
                            pformat(ipam_dict)))
            ipam_dicts.update({ipam_ref['uuid'] : ipam_dict})

        kwargs.update({'ipam_dicts': ipam_dicts})
        return (True, kwargs)

    def ip_free_req(self, ip_addr, vn_fq_name, alloc_id=None,
            sub=None, ipam_refs=None, vn_dict=None, ipam_dicts=None):
        db_conn = self._get_db_conn()
        if ipam_refs:
            self._ipam_ip_free_req(ip_addr, None, sub, ipam_refs,
                                   vn_dict, ipam_dicts)
            return
        if not vn_dict:
            vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
        else:
            vn_uuid = vn_dict.get('uuid')

        if (alloc_id and
                alloc_id != self.is_ip_allocated(ip_addr, vn_fq_name,
                                                 vn_uuid=vn_uuid, sub=sub,
                                                 vn_dict=vn_dict,
                                                 ipam_dicts=ipam_dicts)):
            # In case of inconsistency in the zk db, we should read and check
            # the allocated IP belongs to the interface we are freing. If not
            # continuing to delete the interface and keeping the zk IP lock.
            # That permits to recover from the zk inconsistency.
            # https://bugs.launchpad.net/juniperopenstack/+bug/1702596
            return

        if not (self._net_ip_free_req(
            ip_addr, vn_uuid, vn_fq_name, sub, vn_dict)):
            self._ipam_ip_free_req(ip_addr, vn_uuid, sub,
                                   vn_dict=vn_dict, ipam_dicts=ipam_dicts)
    # end ip_free_req

    def _ipam_is_ip_allocated(self, ip_addr, vn_uuid, sub=None,
            vn_dict=None, ipam_dicts=None):
        ipam_dicts = ipam_dicts or {}
        # Read in the VN
        obj_fields=['network_ipam_refs']
        if not vn_dict:
            (ok, vn_dict) = self._uuid_to_obj_dict('virtual_network',
                                               vn_uuid, obj_fields)
            if not ok:
                raise cfgm_common.exceptions.VncError(vn_dict)
        ipam_refs = vn_dict['network_ipam_refs']
        for ipam_ref in ipam_refs:
            ipam_fq_name = ipam_ref['to']
            ipam_uuid = ipam_ref['uuid']
            subnet_objs = self._get_ipam_subnet_objs_from_ipam_uuid(
                                ipam_fq_name, ipam_uuid, False,
                                ipam_dicts.get(ipam_uuid))
            if subnet_objs is None:
                continue
            for subnet_name in subnet_objs:
                if sub and sub != subnet_name:
                    continue
                subnet_obj = subnet_objs[subnet_name]
                if subnet_obj.ip_belongs(ip_addr):
                    return subnet_obj.is_ip_allocated(IPAddress(ip_addr))
        raise cfgm_common.exceptions.VncError("")
    #end _ipam_is_ip_allocated

    def _net_is_ip_allocated(self, ip_addr, vn_fq_name, vn_uuid, sub=None,
            vn_dict=None):
        vn_fq_name_str = ':'.join(vn_fq_name)

        subnet_dicts = self._get_net_subnet_dicts(vn_uuid, vn_dict)
        for subnet_name in subnet_dicts:
            if sub and sub != subnet_name:
                continue
            subnet_dict = subnet_dicts[subnet_name]
            subnet_obj = self._get_subnet_obj(vn_fq_name_str, vn_uuid,
                                              subnet_name, subnet_dict)
            if subnet_obj.ip_belongs(ip_addr):
                return subnet_obj.is_ip_allocated(IPAddress(ip_addr))
        raise cfgm_common.exceptions.VncError("")
    # end _net_is_ip_allocated

    def is_ip_allocated(self, ip_addr, vn_fq_name, vn_uuid=None, sub=None,
            vn_dict=None, ipam_dicts=None):
        db_conn = self._get_db_conn()
        if vn_uuid is None:
            vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)

        try:
            return self._ipam_is_ip_allocated(ip_addr, vn_uuid, sub, vn_dict,
                    ipam_dicts)
        except cfgm_common.exceptions.VncError:
            try:
                return self._net_is_ip_allocated(ip_addr, vn_fq_name, vn_uuid,
                        sub, vn_dict)
            except cfgm_common.exceptions.VncError:
                return False
    # end is_ip_allocated

    def _ipam_ip_free_notify(self, ip_addr, vn_uuid, ipam_refs=None):
        db_conn = self._get_db_conn()
        if not ipam_refs:
            # Read in the VN
            obj_fields=['network_ipam_refs']
            ok, result = self._uuid_to_obj_dict('virtual_network', vn_uuid,
                                                 obj_fields)
            if not ok:
                raise False, result
            ipam_refs = result['network_ipam_refs']

        for ipam_ref in ipam_refs:
            ipam_uuid = ipam_ref['uuid']

            for subnet_name in self._subnet_objs.get(ipam_uuid) or []:
                subnet_obj = self._subnet_objs[ipam_uuid][subnet_name]
                if subnet_obj.ip_belongs(ip_addr):
                    subnet_obj.ip_reset_in_use(ip_addr)
                    return True, ''

        return True, ''
    # end _ipam_ip_free_notify

    def _net_ip_free_notify(self, ip_addr, vn_uuid):
        for subnet_name in self._subnet_objs.get(vn_uuid) or []:
            subnet_obj = self._subnet_objs[vn_uuid][subnet_name]
            if subnet_obj.ip_belongs(ip_addr):
                subnet_obj.ip_reset_in_use(ip_addr)
                return True
        return False
    # end _net_ip_free_notify

    def ip_free_notify(self, ip_addr, vn_fq_name, alloc_id=None,
                       ipam_refs=None):
        db_conn = self._get_db_conn()
        if ipam_refs:
            return self._ipam_ip_free_notify(ip_addr, None, ipam_refs)

        try:
            vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
        except cfgm_common.exceptions.NoIdError as e:
            return False, (400, str(e))

        if alloc_id:
            # In case of inconsistency in the zk db, we should read and check
            # the allocated IP belongs to the interface we are freeing. If not
            # continuing to delete the interface and keeping the zk IP lock.
            # That permits to recover from the zk inconsistency.
            # https://bugs.launchpad.net/juniperopenstack/+bug/1702596
            allocated_id = self.is_ip_allocated(ip_addr, vn_fq_name,
                                                vn_uuid=vn_uuid)
            if allocated_id is not None and alloc_id != allocated_id:
                return True, ''

        if not (self._net_ip_free_notify(ip_addr, vn_uuid)):
            return self._ipam_ip_free_notify(ip_addr, vn_uuid)

        return True, ''
    # end _ip_free_notify

    def mac_alloc(self, obj_dict):
        uid = obj_dict['uuid']
        return '02:%s:%s:%s:%s:%s' % (uid[0:2], uid[2:4], uid[4:6],
                                      uid[6:8], uid[9:11])
    # end mac_alloc

    def ipam_create_req(self, obj_dict):
        ipam_uuid = obj_dict['uuid']
        ipam_fq_name_str = ':'.join(obj_dict['fq_name'])

        #create subnet object if subnet_method is flat-subnet
        subnet_method = obj_dict.get('ipam_subnet_method')
        if subnet_method == 'flat-subnet':

            req_subnet_dicts = self._get_ipam_subnet_dicts(ipam_uuid,
                                                           obj_dict)
            if req_subnet_dicts:
                for key in req_subnet_dicts.keys():
                    req_subnet = req_subnet_dicts[key]
                    (ok, msg, result) = self._check_subnet_alloc_pools(
                                            req_subnet)
                    if not ok:
                        raise AddrMgmtSubnetInvalid(ipam_fq_name_str,
                                                    key+':'+msg)


            subnetting = obj_dict.get('ipam_subnetting', False)
            self._create_ipam_subnet_objs(ipam_uuid, obj_dict,
                                          should_persist=True,
                                          alloc_pool_change=[],
                                          ipam_subnetting=subnetting)
    # end ipam_create_req

    def ipam_create_notify(self, obj_dict):
        if obj_dict.get('ipam_subnet_method') == 'flat-subnet':
            subnetting = obj_dict.get('ipam_subnetting', False)
            self._create_ipam_subnet_objs(obj_dict['uuid'], obj_dict,
                                          should_persist=False,
                                          ipam_subnetting=subnetting)
    # end ipam_create_notify

    # purge all subnets associated with a ipam
    def ipam_delete_req(self, obj_dict):
        ipam_fq_name = obj_dict['fq_name']
        ipam_fq_name_str = ':'.join(ipam_fq_name)
        ipam_uuid = obj_dict['uuid']
        try:
            subnet_objs = self._get_ipam_subnet_objs_from_ipam_uuid(
                                ipam_fq_name, ipam_uuid, False)
        except cfgm_common.exceptions.VncError:
            return

        if subnet_objs is None:
            subnet_objs = {}
        for subnet_name in subnet_objs:
            Subnet.delete_cls('%s:%s' % (ipam_fq_name_str, subnet_name), False)
    # end ipam_delete_req

    def ipam_delete_notify(self, obj_id, obj_dict):
        ipam_list_subnets = self._ipam_to_subnets(obj_dict) or []
        if obj_id in self._subnet_objs:
            ipam_fq_name_str = ':'.join(obj_dict['fq_name'])
            for subnet_name in ipam_list_subnets:
                Subnet.delete_cls('%s:%s' % (ipam_fq_name_str, subnet_name))
            try:
                del self._subnet_objs[obj_id]
            except KeyError:
                pass
    # end ipam_delete_notify

    def ipam_update_req(self, ipam_fq_name, db_ipam_dict, req_ipam_dict,
                        obj_uuid):
        if 'ipam_subnets' not in req_ipam_dict:
            return

        ipam_fq_name_str = ':'.join(ipam_fq_name)
        db_subnet_dicts = self._get_ipam_subnet_dicts(obj_uuid, db_ipam_dict)
        req_subnet_dicts = self._get_ipam_subnet_dicts(obj_uuid, req_ipam_dict)

        subnetting = db_ipam_dict.get('ipam_subnetting', False)
        db_subnet_names = set(db_subnet_dicts.keys())
        req_subnet_names = set(req_subnet_dicts.keys())

        del_subnet_names = db_subnet_names - req_subnet_names
        add_subnet_names = req_subnet_names - db_subnet_names

        for subnet_name in del_subnet_names:
            Subnet.delete_cls('%s:%s' % (ipam_fq_name_str, subnet_name), False)

        # Remove stale entries in _subnet_objs
        ipam_subnet_objs = self._subnet_objs.get(obj_uuid)
        if ipam_subnet_objs:
            for subnet_name in add_subnet_names:
                subnet_obj = ipam_subnet_objs.pop(subnet_name, None)
                if subnet_obj is not None:
                    msg = "Removing stale ipam subnet entry: %s:%s" % (
                        ipam_fq_name_str, subnet_name,)
                    self.config_log(msg, level=SandeshLevel.SYS_WARN)

        # check db_subnet_dicts and req_subnet_dicts
        # following parameters are same for subnets present in both dicts
        # default_gateway,dns_server_address
        # allocation_pool,dns_nameservers
        subnets_pool_change = []
        for key in req_subnet_dicts.keys():
            req_subnet = req_subnet_dicts[key]
            if key in db_subnet_dicts.keys():
                db_subnet = db_subnet_dicts[key]
                if not subnetting:
                    if ((req_subnet['gw'] is not None) and
                        (req_subnet['gw'] != db_subnet['gw'])):
                        raise AddrMgmtSubnetInvalid(ipam_fq_name_str, key)
                    req_subnet['dns_server_address'] = db_subnet['dns_server_address']

                (ok, msg, result) = self._check_subnet_alloc_pools(req_subnet,
                                                                   db_subnet)
                if not ok:
                        raise AddrMgmtSubnetInvalid(ipam_fq_name_str,
                                                    key+':'+msg)
                if result is not None:
                    subnets_pool_change.append(result)

        req_ipam_dict['fq_name'] = ipam_fq_name
        self._create_ipam_subnet_objs(obj_uuid, req_ipam_dict,
                                      should_persist=True,
                                      alloc_pool_change=subnets_pool_change,
                                      ipam_subnetting=subnetting)
    # end ipam_update_req

    def ipam_update_notify(self, obj_id):
        db_conn = self._get_db_conn()
        try:
            ok, result = db_conn.dbe_read('network_ipam', obj_id=obj_id)
        except cfgm_common.exceptions.NoIdError as e:
            return False, (404, str(e))
        if not ok:
            return False, result
        ipam_dict = result

        ipam_fq_name_str = ':'.join(ipam_dict['fq_name'])
        ipam_list_subnets = self._ipam_to_subnets(ipam_dict) or []
        if obj_id in self._subnet_objs:
            del_subnet_names = set(self._subnet_objs[obj_id]) - set(ipam_list_subnets)
            for subnet_name in del_subnet_names:
                Subnet.delete_cls('%s:%s' % (ipam_fq_name_str, subnet_name))
                try:
                    del self._subnet_objs[obj_id][subnet_name]
                except KeyError:
                    pass

        self._create_ipam_subnet_objs(obj_id, ipam_dict,
                                     should_persist=False)

        return True, ''
    # end ipam_update_notify

    def _ipam_is_gateway_ip(self, vn_dict, ip_addr):
        ipam_refs = vn_dict['network_ipam_refs']
        for ipam_ref in ipam_refs:
            ipam_uuid = ipam_ref['uuid']
            (ok, ipam_dict) = self._uuid_to_obj_dict('network_ipam',
                                                     ipam_uuid)
            if not ok:
                raise cfgm_common.exceptions.VncError(ipam_dict)

            ipam_subnets_dict = ipam_dict.get('ipam_subnets') or []
            if 'subnets' in ipam_subnets_dict:
                ipam_subnets = ipam_subnets_dict['subnets']
                for ipam_subnet in ipam_subnets:
                    if ipam_subnet['default_gateway'] == ip_addr:
                        return True
        return False
    # end _ipam_is_gateway_ip

    def _net_is_gateway_ip(self, vn_dict, ip_addr):
        ipam_refs = vn_dict.get('network_ipam_refs') or []
        for ipam in ipam_refs:
            ipam_subnets = ipam['attr'].get('ipam_subnets') or []
            for subnet in ipam_subnets:
                gw_ip = subnet.get('default_gateway')
                if gw_ip != None and gw_ip == ip_addr:
                    return True
        return False
    # end _net_is_gateway_ip

    def is_gateway_ip(self, vn_dict, ip_addr):
        if vn_dict is None or ip_addr is None:
            return False

        if (self._ipam_is_gateway_ip(vn_dict, ip_addr)):
            return True
        else:
            return self._net_is_gateway_ip(vn_dict, ip_addr)
    # end is_gateway_ip
# end class AddrMgmt
