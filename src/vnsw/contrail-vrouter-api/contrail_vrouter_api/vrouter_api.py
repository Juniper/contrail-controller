# Copyright (c) 2016 Juniper Networks, Inc

import subprocess

class ContrailVRouterApi(object):

    def __init__(self, server_port=9090, doconnect=False, semaphore=None):
        """
        Arguments server_port and doconnect are not used. Retained only
        for backward compatibility.

        local variables:
        _ports: dictionary of ports keyed by vif uuid.
        """
        self._ports = {}
        self._semaphore = semaphore

    def _resynchronize(self):
        """ Add all ports which we failed to add earlier """
        if (len(self._ports) == 0):
            return
        for key in self._ports.keys():
            ret_code = subprocess.call(self._ports[key])
            """ If port is added successfully, remove it from our list """
            if (ret_code == 0):
                self._ports.pop(key, None)

    def add_port(self, vm_uuid_str, vif_uuid_str, interface_name, mac_address,
                 **kwargs):
        """
        Add a port to the agent. Ports which we failed to add are stored in
        _ports dictionary since the vrouter agent may not be running at the
        moment or the REST API call may fail.
        """
        try:
            if self._semaphore:
                self._semaphore.acquire()

            if 'ip_address' in kwargs:
                ip_address = kwargs['ip_address']
            else:
                ip_address = '0.0.0.0'

            network_uuid = ''
            if 'vn_id' in kwargs:
                network_uuid = kwargs['vn_id']
            
            display_name=''
            if 'display_name' in kwargs:
                display_name = kwargs['display_name']
            vm_project_id=''

            if 'vm_project_id' in kwargs:
                vm_project_id = kwargs['vm_project_id']
            if ('port_type' in kwargs and
                kwargs['port_type'] in [0,1]):
                if (kwargs['port_type'] == 0):
                    port_type = "NovaVMPort"
                elif (kwargs['port_type'] == 1):
                    port_type = "NameSpacePort"
                elif (kwargs['port_type'] == 'NameSpacePort'):
                    port_type = "NameSpacePort"
                else:
                    port_type = "NovaVMPort"
            else:
                port_type = "NameSpacePort"

            ip6_address=''
            if 'ip6_address' in kwargs:
                ip6_address = kwargs['ip6_address']

            tx_vlan_id = -1
            if 'vlan' in kwargs:
                tx_vlan_id = kwargs['vlan']

            cmd_args = ("vrouter-port-control --oper=\"add\" --uuid=\"%s\" "
                        "--instance_uuid=\"%s\" --vn_uuid=\"%s\" "
                        "--vm_project_uuid=\"%s\" --ip_address=\"%s\" "
                        "--ipv6_address=\"%s\" --vm_name=\"%s\" --mac=\"%s\" "
                        "--tap_name=\"%s\" --port_type=\"%s\" "
                        "--vif_type=\"Vrouter\" --tx_vlan_id=\"%d\" "
                        "--rx_vlan_id=\"%d\"" %(vif_uuid_str, vm_uuid_str,
                        network_uuid, vm_project_id, ip_address, ip6_address,
                        display_name, mac_address, interface_name, port_type,
                        tx_vlan_id, -1))

            cmd = cmd_args.split()
            ret_code = subprocess.call(cmd)
            if (ret_code != 0):
                self._ports[vif_uuid_str] = cmd

            self._resynchronize()

        finally:
            if self._semaphore:
                self._semaphore.release()

    def delete_port(self, vif_uuid_str):
        """
        Delete a port form the agent. The port is first removed from the
        internal _ports dictionary.
        """
        try:
            if self._semaphore:
                self._semaphore.acquire()
            if (vif_uuid_str in self._ports.keys()):
                self._ports.pop(vif_uuid_str, None)
                return

            cmd_args = ("vrouter-port-control --oper=delete --uuid=%s" % vif_uuid_str)
            cmd = cmd_args.split()
            ret_code = subprocess.call(cmd)

            self._resynchronize()

        finally:
            if self._semaphore:
                self._semaphore.release()

    def periodic_connection_check(self):
        """
        Periodicly add ports to agent which we failed to add earlier.
        It is the API client's resposibility to periodically invoke this
        method.
        """
        try:
            if self._semaphore:
                self._semaphore.acquire()
            self._resynchronize()

        finally:
            if self._semaphore:
                self._semaphore.release()
