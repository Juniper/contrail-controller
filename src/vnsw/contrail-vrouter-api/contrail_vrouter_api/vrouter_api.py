# Copyright (c) 2014 Juniper Networks, Inc

import logging
import uuid
from thrift.protocol import TBinaryProtocol
from thrift.transport import TTransport
from nova_contrail_vif.gen_py.instance_service import InstanceService, ttypes


class ContrailVRouterApi(object):

    def __init__(self):
        """
        local variables:
        _client: current transport connection
        _ports: dictionary of active ports keyed by vif uuid.
        """
        self._client = None
        self._ports = {}

    def _rpc_client_instance(self):
        """ Return an RPC client connection """
        import thrift.transport.TSocket as TSocket
        socket = TSocket.TSocket('127.0.0.1', 9090)
        try:
            transport = TTransport.TFramedTransport(socket)
            transport.open()
        except thrift.transport.TTransport.TTransportException:
            logging.error('Connection failure')
            return None
        protocol = TBinaryProtocol.TBinaryProtocol(transport)
        client = InstanceService.Client(protocol)
        return client

    def _resynchronize(self):
        """ Add all the active ports to the agent """
        for port in self._ports:
            try:
                self._client.AddPort(port)
            except:
                return

    def _uuid_from_string(self, idstr):
        """ Convert an uuid string into an uuid object """
        if not idstr:
            return None
        return uuid.UUID(idstr)

    def _uuid_to_hex(self, id):
        """ Convert an uuid into an array of integers """
        hexstr = id.hex
        return [int(hexstr[i:i + 2], 16) for i in range(32) if i % 2 == 0]

    def _uuid_string_to_hex(self, idstr):
        return self._uuid_to_hex(self._uuid_from_string(idstr))

    def add_port(self, vm_uuid_str, vif_uuid_str, interface_name, mac_address,
                 **kwargs):
        """
        Add a port to the agent. The information is stored in the _ports
        dictionary since the vrouter agent may not be running at the
        moment or the RPC may fail.
        """

        vif_uuid = self._uuid_from_string(vif_uuid_str)
        # ip_address and network_uuid are optional to this API but must
        # be present in the message. For instance, when running in
        # CloudStack/XenServer/XAPI these arguments are not known.
        if 'ip_address' in kwargs:
            ip_address = kwargs['ip_address']
        else:
            ip_address = '0.0.0.0'

        if 'network_uuid' in kwargs:
            network_uuid = self._uuid_string_to_hex(kwargs['network_uuid'])
        else:
            network_uuid = [0] * 16,

        # create port with mandatory arguments
        data = ttypes.Port(
            self._uuid_to_hex(vif_uuid),
            self._uuid_string_to_hex(vm_uuid_str),
            interface_name,
            ip_address,
            network_uuid,
            mac_address)

        if 'display_name' in kwargs:
            data.display_name = kwargs['display_name']
        if 'hostname' in kwargs:
            data.hostname = kwargs['hostname']
        if 'vm_project_uuid' in kwargs:
            data.vm_project_uuid = self._uuid_string_to_hex(
                kwargs['vm_project_uuid'])

        data.validate()

        self._ports[vif_uuid] = data

        try:
            if self._client is None:
                self._client = self._rpc_client_instance()
                self._resynchronize()
        except:
            return

        try:
            self._client.AddPort([data])
        except:
            self._client = None

    def delete_port(self, vif_uuid_str):
        """
        Delete a port form the agent. The port is first removed from the
        internal _ports dictionary.
        """
        vif_uuid = self._uuid_from_string(vif_uuid_str)
        del self._ports[vif_uuid]

        try:
            if self._client is None:
                self._client = self._rpc_client_instance()
                self._resynchronize()
        except:
            return

        try:
            self._client.DeletePort(self._uuid_to_hex(vif_uuid))
        except:
            self._client = None

    def periodic_connection_check(self):
        """
        Periodicly check if the connection to the agent is valid.
        It is the API client's resposibility to periodically invoke this
        method.
        """
        try:
            if self._client is None:
                self._client = self._rpc_client_instance()
                self._resynchronize()
        except:
            return

        try:
            if self._client:
                self._client.KeepAliveCheck()
        except:
            self._client = None
