#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from bitarray import bitarray
from netaddr import *
import json
import cfgm_common.exceptions


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


class AddrMgmtSubnetExhausted(AddrMgmtError):

    def __init__(self, vn_fq_name, subnet_val):
        self.vn_fq_name = vn_fq_name
        self.subnet_val = subnet_val
    # end __init__

    def __str__(self):
        return "Virtual-Network(%s) has exhausted subnet(%s)" %\
            (self.vn_fq_name, self.subnet_val)
    # end __str__
# end AddrMgmtSubnetExhausted

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

    def __init__(self, name, prefix, prefix_len, gw=None):
        self._version = 0

        """
        print 'Name = %s, prefix = %s, len = %s, gw = %s, db_conn = %s' \
            % (name, prefix, prefix_len, gw, 'Yes' if db_conn else 'No')
        """

        network = IPNetwork('%s/%s' % (prefix, prefix_len))

        # Exclude host, broadcast and gateway addresses
        exclude = [IPAddress(network.first), IPAddress(
            network.last - 1), network.broadcast]
        if gw:
            gw_ip = IPAddress(gw)
            exclude.append(gw_ip)
        else:
            # reserve a gateway ip in subnet
            gw_ip = IPAddress(network.last - 1)
            exclude.append(gw_ip)

        # Initialize in-use bit mask
        cols_dict = Subnet._db_conn.subnet_retrieve(name)

        # TODO backward compat start, earlier in-use bitmask was stored, now
        # actual ip-addrs are stored. handle transition
        if 'bitmask' in cols_dict:
            inuse_bitmask = bitarray(cols_dict.pop('bitmask'))

            cols_dict = {}
            # convert bitmask to ip-addresses
            subnet_pfx = name.split(':')[3]
            last_addr = IPNetwork(subnet_pfx).last
            inuse_bitmask.reverse()
            while True:
                try:
                    addr_off = inuse_bitmask.index(True)
                except ValueError:
                    break
                inuse_bitmask[addr_off] = 0
                inuse_addr = IPAddress(last_addr - addr_off)
                cols_dict[str(inuse_addr)] = ''
        # backward compat end
        inuse_addrs = cols_dict

        self._db_conn.subnet_create_allocator(name, network.first, network.last)

        # reserve excluded addresses
        for addr in exclude:
            self._db_conn.subnet_alloc(name, int(addr))

        for addr in inuse_addrs.keys():
            ipaddr = IPAddress(addr)
            self._db_conn.subnet_alloc(name, int(ipaddr))

        self._db_conn.subnet_delete(name)
        self._name = name
        self._network = network
        self._exclude = exclude
        self.gw_ip = gw_ip
    # end __init__

    def get_name(self):
        return self._name
    #end get_name

    def get_exclude(self):
        return self._exclude
    # end get_exclude

    def ip_alloc(self, ipaddr=None):
        req = None
        if ipaddr:
            ip = IPAddress(ipaddr)
            req = int(ip)
    
        addr = self._db_conn.subnet_alloc(self._name, req)
        if addr:
            return str(IPAddress(addr))
        return None
    # end ip_alloc

    # free IP unless it is invalid, excluded or already freed
    @classmethod
    def ip_free_cls(cls, subnet_fq_name, ip_network, exclude_addrs, ip_addr):
        if ((ip_addr in ip_network) and (ip_addr not in exclude_addrs)):
            if cls._db_conn:
                cls._db_conn.subnet_free(subnet_fq_name, int(ip_addr))
                return True

        return False
    # end ip_free_cls

    def ip_free(self, ip_addr):
        Subnet.ip_free_cls(self._name, self._network, self._exclude, ip_addr)
    # end ip_free

    # check if IP address belongs to us
    @classmethod
    def ip_belongs_to(cls, ipnet, ipaddr):
        return IPAddress(ipaddr) in ipnet
    # end ip_belongs_to

    def ip_belongs(self, ipaddr):
        return self.ip_belongs_to(self._network, ipaddr)
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
        #self.vninfo = {}
        self.version = 0
        self._server_mgr = server_mgr
        self._db_conn = None
        self._subnet_objs = {}
    # end __init__

    def _get_db_conn(self):
        if not self._db_conn:
            self._db_conn = self._server_mgr.get_db_connection()
            Subnet.set_db_conn(self._db_conn)

        return self._db_conn
    # end _get_db_conn

    def _vninfo_is_present(self, vn_fq_name_str):
        return self._db_conn.hkey_is_present('vninfo', vn_fq_name_str)
    # end _vninfo_is_present

    def _vninfo_set(self, vn_fq_name_str, vn_val):
        self._db_conn.hkey_set('vninfo', vn_fq_name_str, vn_val)
    # end _vninfo_get

    def _vninfo_is_present(self, vn_fq_name_str):
        return self._db_conn.hkey_is_present('vninfo', vn_fq_name_str)
    # end _vninfo_is_present

    def _vninfo_set(self, vn_fq_name_str, vn_val):
        self._db_conn.hkey_set('vninfo', vn_fq_name_str, vn_val)
    # end _vninfo_get

    def _vninfo_get(self, vn_fq_name_str):
        return self._db_conn.hkey_get('vninfo', vn_fq_name_str)
    # end _vninfo_get

    def _vninfo_getall(self):
        return self._db_conn.hkey_getall('vninfo')
    # end _vninfo_getall

    def _vninfo_delete(self, vn_fq_name_str):
        self._db_conn.hkey_delete('vninfo', vn_fq_name_str)
    # end _vninfo_delete

    def _vninfo_subnet_is_present(self, vn_fq_name_str, subnet_name):
        self._db_conn.hkey_is_present(vn_fq_name_str, subnet_name)
    # end _vninfo_subnet_is_present

    def _vninfo_subnet_set(self, vn_fq_name_str, subnet_name, subnet_dict):
        self._db_conn.hkey_set(vn_fq_name_str, subnet_name, subnet_dict)
    # end _vninfo_subnet_set

    def _vninfo_subnet_get(self, vn_fq_name_str, subnet_name):
        subnet_dict = self._db_conn.hkey_get(vn_fq_name_str, subnet_name)
        return subnet_dict
    # end _vninfo_subnet_get

    def _vninfo_subnet_getall(self, vn_fq_name_str):
        subnet_dicts = self._db_conn.hkey_getall(vn_fq_name_str)
        if not subnet_dicts:
            return None

        for key in subnet_dicts.keys():
            subnet_dicts[key] = json.loads(subnet_dicts[key])

        return subnet_dicts.items()
    #end _vninfo_subnet_getall

    def _vninfo_subnet_delete(self, vn_fq_name_str, subnet_name):
        self._db_conn.hkey_delete(vn_fq_name_str, subnet_name)
    # end _vninfo_subnet_delete

    def net_create(self, obj_dict, obj_uuid=None):
        db_conn = self._get_db_conn()
        if obj_uuid:
            vn_fq_name_str = ':'.join(db_conn.uuid_to_fq_name(obj_uuid))
        else:
            vn_fq_name_str = ':'.join(obj_dict['fq_name'])

        if not self._vninfo_is_present(vn_fq_name_str):
            self._vninfo_set(vn_fq_name_str, None)

        # using versioning to detect deleted subnets
        version = self.version + 1
        self.version = version

        # create subnet for each new subnet
        refs = obj_dict.get('network_ipam_refs', None)
        if refs:
            for ref in refs:
                vnsn_data = ref['attr']
                ipam_subnets = vnsn_data['ipam_subnets']
                for ipam_subnet in ipam_subnets:
                    subnet = ipam_subnet['subnet']
                    subnet_name = subnet['ip_prefix'] + '/' + str(
                        subnet['ip_prefix_len'])
                    ip_net = IPNetwork(subnet_name);
                    subnet['ip_prefix'] = u'' + str(ip_net.ip);
                    gateway_ip = ipam_subnet.get('default_gateway', None)
                    if not self._vninfo_subnet_is_present(vn_fq_name_str,
                                                          subnet_name):
                        subnet_obj = Subnet(
                            '%s:%s' % (vn_fq_name_str, subnet_name),
                            subnet['ip_prefix'], str(subnet['ip_prefix_len']),
                            gw=gateway_ip)
                        self._subnet_objs[subnet_obj.get_name()] = subnet_obj
                        ipam_subnet['default_gateway'] = str(subnet_obj.gw_ip)
                        subnet_obj.set_version(version)
                        subnet_dict = {'name': subnet_name,
                                       'version': version,
                                       'prefix': subnet['ip_prefix'],
                                       'prefix_len':
                                       str(subnet['ip_prefix_len']),
                                       'gw': gateway_ip,
                                       'exclude': [str(ip) for ip in
                                                   subnet_obj.get_exclude()]}
                    else:  # subnet is present
                        subnet_dict = self._vninfo_subnet_get(vn_fq_name_str,
                                                              subnet_name)
                        # bump up the version
                        subnet_dict['version'] = version
                        # always ensure default_gateway cannot be reset to NULL
                        if not gateway_ip:
                            subnet = self.vninfo[vn_fq_name_str][subnet_name]
                            ipam_subnet['default_gateway'] = subnet_dict['gw']

                    self._vninfo_subnet_set(vn_fq_name_str,
                                            subnet_name,
                                            subnet_dict)

            # purge old subnets based on version mismatch
            subnet_items = self._vninfo_subnet_getall(vn_fq_name_str)
            for subnet_name, subnet_dict in subnet_items:
                if subnet_dict['version'] != version:
                    self._vninfo_subnet_delete(vn_fq_name_str, subnet_name)
    # end net_create

    # purge all subnets associated with a virtual network
    def net_delete(self, obj_dict):
        vn_fq_name_str = ':'.join(obj_dict['fq_name'])
        self._vninfo_set(vn_fq_name_str, None)
        #vn_name = ':'.join(obj_dict['fq_name'])
        #self.vninfo[vn_name] = {}
    # end net_delete

    def _vn_to_subnets(self, obj_dict):
        # given a VN return its subnets in list of net/prefixlen strings

        ipam_refs = obj_dict.get('network_ipam_refs', None)
        if ipam_refs != None:
            subnet_list = []
            for ref in ipam_refs:
                vnsn_data = ref['attr']
                ipam_subnets = vnsn_data['ipam_subnets']
                for ipam_subnet in ipam_subnets:
                    subnet = ipam_subnet['subnet']
                    subnet_name = subnet['ip_prefix'] + '/' + str(
                        subnet['ip_prefix_len'])
                    subnet_list.append(subnet_name)
        else:
            subnet_list = None

        return subnet_list
    # end _vn_to_subnets

    # check subnets associated with a virtual network, return error if
    # any two subnets have overlap ip addresses
    def net_check_subnet_overlap(self, db_vn_dict, req_vn_dict):
        # get all subnets existing + requested and check any non-exact overlaps
        requested_subnets = self._vn_to_subnets(req_vn_dict)
        if not requested_subnets:
            return True, ""

        existing_subnets = self._vn_to_subnets(db_vn_dict)
        if not existing_subnets:
            existing_subnets = []

        # literal/string sets
        # eg. existing [1.1.1.0/24],
        #     requested [1.1.1.0/24, 2.2.2.0/24] OR
        #     requested [1.1.1.0/16, 2.2.2.0/24]
        existing_set = set([sn for sn in existing_subnets])
        requested_set = set([sn for sn in requested_subnets])
        new_set = requested_set - existing_set

        # IPSet to find any overlapping subnets
        overlap_set = IPSet(existing_set) & IPSet(new_set)
        if overlap_set:
            err_msg = "Overlapping addresses between requested and existing: "
            return False, err_msg + str(overlap_set)

        return True, ""
    # end net_check_subnet_overlap

    # check subnets associated with a virtual network, return error if
    # any subnet is being deleted and has backref to instance-ip/floating-ip
    def net_check_subnet_delete(self, db_vn_dict, req_vn_dict):
        # if all instance-ip/floating-ip are part of requested list
        # things are ok.
        # eg. existing [1.1.1.0/24, 2.2.2.0/24],
        #     requested [1.1.1.0/24] OR
        #     requested [1.1.1.0/28, 2.2.2.0/24]
        requested_subnets = self._vn_to_subnets(req_vn_dict)
        if requested_subnets == None:
            # subnets not modified in request
            return True, ""

        # if all subnets are being removed, check for any iip backrefs
        # or floating pools still present in DB version of VN
        if len(requested_subnets) == 0:
            if db_vn_dict.get('instance_ip_back_refs'):
                return False, "Cannot Delete IP Block, Instance IP(s) in use"
            if db_vn_dict.get('floating_ip_pools'):
                return False, "Cannot Delete IP Block, Floating Pool(s) in use"

        instip_refs = db_vn_dict.get('instance_ip_back_refs', [])
        for ref in instip_refs:
            try:
                db_conn = self._db_conn
                (ok, result) = db_conn.dbe_read(
                    'instance-ip', {'uuid': ref['uuid']})
            except cfgm_common.exceptions.NoIdError:
                continue

            if not ok:
                continue

            inst_ip = result.get('instance_ip_address', None)
            if not all_matching_cidrs(inst_ip, requested_subnets):
                return False,\
                    "Cannot Delete IP Block, IP(%s) is in use"\
                    % (inst_ip)

        fip_pool_refs = db_vn_dict.get('floating_ip_pools', [])
        for ref in fip_pool_refs:
            try:
                db_conn = self._db_conn
                (ok, result) = db_conn.dbe_read(
                    'floating-ip-pool', {'uuid': ref['uuid']})
            except cfgm_common.exceptions.NoIdError:
                continue

            if not ok:
                continue

            floating_ips = result.get('floating_ips', [])
            for floating_ip in floating_ips:
                # get floating_ip_address and this should be in
                # new subnet_list
                try:
                    (read_ok, read_result) = db_conn.dbe_read(
                        'floating-ip', {'uuid': floating_ip['uuid']})
                except cfgm_common.exceptions.NoIdError:
                    continue

                if not ok:
                    continue

                fip_addr = read_result.get('floating_ip_address', None)
                if not all_matching_cidrs(fip_addr, requested_subnets):
                    return False,\
                        "Cannot Delete IP Block, Floating IP(%s) is in use"\
                        % (fip_addr)

        return True, ""
    # end net_check_subnet_delete

    # allocate an IP address for given virtual network
    # we use the first available subnet unless provided
    def ip_alloc(self, vn_fq_name, sub=None, asked_ip_addr=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        subnet_name = ''
        if self._vninfo_is_present(vn_fq_name_str):
            subnet_items = self._vninfo_subnet_getall(vn_fq_name_str)
            if not subnet_items:
                raise AddrMgmtSubnetUndefined(vn_fq_name_str)

            for subnet_name, subnet_dict in subnet_items:
                if sub and sub != subnet_name:
                    continue

                # create subnet_obj internally if it was created by some other
                # api-server before
                try:
                    subnet_obj = self._subnet_objs['%s:%s'
                                                   % (vn_fq_name_str,
                                                      subnet_name)]
                except KeyError:
                    subnet_obj = Subnet('%s:%s' % (vn_fq_name_str,
                                                   subnet_name),
                                        subnet_dict['prefix'],
                                        subnet_dict['prefix_len'],
                                        gw=subnet_dict['gw'])
                    subnet_fq_name = '%s:%s' % (vn_fq_name_str, subnet_name)
                    self._subnet_objs[subnet_fq_name] = subnet_obj

                if asked_ip_addr and not subnet_obj.ip_belongs(asked_ip_addr):
                    continue

                ip_addr = subnet_obj.ip_alloc(ipaddr=asked_ip_addr)
                if ip_addr is not None or sub:
                    return ip_addr

        raise AddrMgmtSubnetExhausted(vn_fq_name_str, subnet_name)
    # end ip_alloc

    def ip_free(self, ip_addr, vn_fq_name, sub=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        if self._vninfo_is_present(vn_fq_name_str):
            subnet_items = self._vninfo_subnet_getall(vn_fq_name_str)
            for subnet_name, subnet_dict in subnet_items:
                if sub and sub != subnet_name:
                    continue

                # if we have subnet_obj free it via instance method,
                # updating inuse bitmask, else free it via class method
                # and there is no inuse bitmask to worry about
                try:
                    subnet_obj = self._subnet_objs['%s:%s'
                                                   % (vn_fq_name_str,
                                                      subnet_name)]
                    subnet_obj.ip_free(IPAddress(ip_addr))
                except KeyError:
                    exclude_addrs = [IPAddress(x) for x in
                                     subnet_dict['exclude']]
                    if Subnet.ip_belongs_to(IPNetwork(subnet_name),
                                            IPAddress(ip_addr)):
                        Subnet.ip_free_cls('%s:%s' % (vn_fq_name_str, subnet_name),
                                           IPNetwork(subnet_name),
                                           exclude_addrs,
                                           IPAddress(ip_addr))
                        break
    # end ip_free

    # Given IP address count on given virtual network, subnet/List of subnet
    def ip_count(self, obj_dict, subnet=None):
        addr_num = 0
        if not subnet:
            return addr_num

        instip_refs = obj_dict.get('instance_ip_back_refs', None)
        if instip_refs:
            for ref in instip_refs:
                uuid = ref['uuid']
                try:
                    db_conn = self._db_conn
                    #ifmap_id = db_conn.uuid_to_ifmap_id(uuid)
                    (ok, result) = db_conn.dbe_read(
                        'instance-ip', {'uuid': uuid})
                except cfgm_common.exceptions.NoIdError:
                # except NoIdError:
                    continue
                if not ok:
                    continue

                inst_ip = result.get('instance_ip_address', None)
                if IPAddress(inst_ip) in IPNetwork(subnet):
                    addr_num += 1

        return addr_num
    # end ip_count

    def mac_alloc(self, obj_dict):
        uid = obj_dict['uuid']
        return '02:%s:%s:%s:%s:%s' % (uid[0:2], uid[2:4], uid[4:6],
                                      uid[6:8], uid[9:11])
    # end mac_alloc

# end class AddrMgmt
