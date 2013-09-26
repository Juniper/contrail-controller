#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from bitarray import bitarray
from netaddr import *
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

    def __init__(self, name, prefix, prefix_len, gw=None, db_conn=None):
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
        inuse = db_conn.subnet_retrieve(name) if db_conn else None
        if inuse:
            self._inuse = bitarray()
            self._inuse.frombytes(inuse)
        else:
            self._inuse = bitarray(network.size)
            self._inuse.setall(0)

        # build free list of IP addresses
        free_list = []
        for addr in network:
            if (not addr in exclude and
                    not self._inuse[int(addr) - network.first]):
                free_list.append(addr)

        self._name = name
        self._free_list = free_list
        self._network = network
        self._exclude = exclude
        self._db_conn = db_conn
        self.gw_ip = gw_ip
    # end __init__

    # allocate IP address from this subnet
    def ip_alloc(self, ipaddr=None):
        addr = None
        if len(self._free_list) > 0:
            if ipaddr:
                ip = IPAddress(ipaddr)

                # TODO: return ip ipaddr passed
                addr = ip

                if ip in self._network and ip in self._free_list:
                    self._free_list.remove(ip)
                    addr = ip
            else:
                addr = self._free_list.pop()

        # update inuse mask in DB
        if addr and self._db_conn:
            self._inuse[int(addr) - self._network.first] = 1
            self._db_conn.subnet_store(self._name, self._inuse.tobytes())
            return str(addr)

        return None
    # end ip_alloc

    # free IP unless it is invalid, excluded or already freed
    def ip_free(self, ipaddr):
        ip = IPAddress(ipaddr)
        if ((ip in self._network) and (ip not in self._free_list) and
                (ip not in self._exclude)):
            if self._db_conn:
                self._inuse[int(ip) - self._network.first] = 0
                self._db_conn.subnet_store(self._name, self._inuse.tobytes())
            self._free_list.append(ip)
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
        self.vninfo = {}
        self.version = 0
        self._server_mgr = server_mgr
        self._db_conn = None
    # end __init__

    def _get_db_conn(self):
        if not self._db_conn:
            self._db_conn = self._server_mgr.get_db_connection()

        return self._db_conn
    # end _get_db_conn

    def net_create(self, obj_dict, obj_uuid=None):
        db_conn = self._get_db_conn()
        if obj_uuid:
            vn_fq_name_str = ':'.join(db_conn.uuid_to_fq_name(obj_uuid))
        else:
            vn_fq_name_str = ':'.join(obj_dict['fq_name'])

        if vn_fq_name_str not in self.vninfo:
            self.vninfo[vn_fq_name_str] = {}

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
                    gateway_ip = ipam_subnet.get('default_gateway', None)
                    if subnet_name not in self.vninfo[vn_fq_name_str]:
                        subnet = Subnet(
                            '%s:%s' % (vn_fq_name_str, subnet_name),
                            subnet['ip_prefix'], str(
                                subnet['ip_prefix_len']),
                            gw=gateway_ip,
                            db_conn=db_conn)
                        ipam_subnet['default_gateway'] = str(subnet.gw_ip)
                        self.vninfo[vn_fq_name_str][subnet_name] = subnet
                    else:  # subnet present already
                        # always ensure default_gateway cannot be reset to NULL
                        if not gateway_ip:
                            subnet = self.vninfo[vn_fq_name_str][subnet_name]
                            ipam_subnet['default_gateway'] = str(subnet.gw_ip)

                    # bump up the version
                    self.vninfo[vn_fq_name_str][
                        subnet_name].set_version(version)

            # purge old subnets based on version mismatch
            for name, subnet in self.vninfo[vn_fq_name_str].items():
                if subnet.get_version() != version:
                    del self.vninfo[vn_fq_name_str][name]
    # end net_create

    # purge all subnets associated with a virtual network
    def net_delete(self, obj_dict):
        vn_name = ':'.join(obj_dict['fq_name'])
        self.vninfo[vn_name] = {}
    # end net_delete

    # check subnets associated with a virtual network, return error if
    # any two subnets have overlap ip addresses
    def net_check_subnet_overlap(self, obj_dict):
        # get all subnets and check overlap condition for each pair
        ipam_refs = obj_dict.get('network_ipam_refs', None)
        new_subnet_list = []
        if ipam_refs:
            for ref in ipam_refs:
                vnsn_data = ref['attr']
                ipam_subnets = vnsn_data['ipam_subnets']
                for ipam_subnet in ipam_subnets:
                    subnet = ipam_subnet['subnet']
                    subnet_name = subnet['ip_prefix'] + '/' + str(
                        subnet['ip_prefix_len'])
                    new_subnet_list.append(subnet_name)

        size = len(new_subnet_list)
        for index, subnet in enumerate(new_subnet_list):
            next_index = index + 1
            # check if IPSet intersects
            while (next_index < size):
                if IPSet([str(subnet)]) &\
                        IPSet([str(new_subnet_list[next_index])]):
                    msg = new_subnet_list[index] +\
                        'and' + new_subnet_list[next_index] +\
                        'are overalap IP Blocks'
                    return False, msg
                next_index = next_index + 1

        return True, ""
    # end net_check_subnet_overlap

    # check subnets associated with a virtual network, return error if
    # any subnet is being deleted
    def net_check_subnet_delete(self, obj_dict):
        # get all new subnets
        ipam_refs = obj_dict.get('network_ipam_refs', None)
        new_subnet_list = []
        if ipam_refs:
            for ref in ipam_refs:
                vnsn_data = ref['attr']
                ipam_subnets = vnsn_data['ipam_subnets']
                for ipam_subnet in ipam_subnets:
                    subnet = ipam_subnet['subnet']
                    subnet_name = subnet['ip_prefix'] + '/' + str(
                        subnet['ip_prefix_len'])
                    new_subnet_list.append(subnet_name)

        # check each instance-ip presence in new subnets
        instip_refs = obj_dict.get('instance_ip_back_refs', None)
        if instip_refs:
            for ref in instip_refs:
                uuid = ref['uuid']
                try:
                    db_conn = self._db_conn
                    (ok, result) = db_conn.dbe_read(
                        'instance-ip', {'uuid': uuid})
                except cfgm_common.exceptions.NoIdError:
                    msg = 'ID ' + uuid + ' not found'
                    return False, msg
                if not ok:
                    # Not present in DB
                    return ok, result

                inst_ip = result.get('instance_ip_address', None)
                if not all_matching_cidrs(inst_ip, new_subnet_list):
                    return False,\
                        "Cannot Delete Ip Block, IP(%s) is in use"\
                        % (inst_ip)

        # check each floating-ip presence in new subnets
        fip_pool_refs = obj_dict.get('floating_ip_pools', None)
        if fip_pool_refs:
            for ref in fip_pool_refs:
                uuid = ref['uuid']
                try:
                    db_conn = self._db_conn
                    (ok, result) = db_conn.dbe_read(
                        'floating-ip-pool', {'uuid': uuid})
                except cfgm_common.exceptions.NoIdError:
                    msg = 'ID ' + uuid + ' not found'
                    return False, msg
                if not ok:
                    # Not present in DB
                    return ok, result

                floating_ips = result.get('floating_ips', None)
                if floating_ips:
                    for floating_ip in floating_ips:
                        fip_uuid = floating_ip['uuid']
                        # get floating_ip_address and this should be in
                        # new subnet_list
                        (read_ok, read_result) = db_conn.dbe_read(
                            'floating-ip', {'uuid': fip_uuid})
                        fip_addr = read_result.get('floating_ip_address', None)
                        if not fip_addr:
                            msg = 'ID' + fip_uuid + \
                                'does not have floating_ip_address'
                            return False, msg

                        if not all_matching_cidrs(fip_addr, new_subnet_list):
                            return False,\
                                "Cannot Delete Ip Block, IP(%s) is in use"\
                                % (fip_addr)

        return True, ""
    # end net_check_subnet_delete

    # allocate an IP address for given virtual network
    # we use the first available subnet unless provided
    def ip_alloc(self, vn_fq_name, sub=None, asked_ip_addr=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        if vn_fq_name_str in self.vninfo:
            if not self.vninfo[vn_fq_name_str]:
                raise AddrMgmtSubnetUndefined(vn_fq_name_str)

            for subnet_name, subnet in self.vninfo[vn_fq_name_str].items():
                if sub and sub != subnet_name:
                    continue
                if asked_ip_addr and not subnet.ip_belongs(asked_ip_addr):
                    continue
                ip_addr = subnet.ip_alloc(ipaddr=asked_ip_addr)
                if ip_addr is not None or sub:
                    return ip_addr

        raise AddrMgmtSubnetExhausted(vn_fq_name_str, subnet_name)
    # end ip_alloc

    def ip_free(self, ip_addr, vn_fq_name, sub=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        if vn_fq_name_str in self.vninfo:
            for subnet_name, subnet in self.vninfo[vn_fq_name_str].items():
                if sub and sub != subnet_name:
                    continue
                if subnet.ip_belongs(ip_addr):
                    subnet.ip_free(ip_addr)
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
