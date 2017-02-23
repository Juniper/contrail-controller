# Copyright (c) 2014 Juniper Networks, Inc

import argparse
import os
import signal
import sys
import uuid

def import_path_setup():
    """ Test environment managed by easy_install """
    sys.path.append(os.curdir)
    for lsentry in os.listdir(os.curdir):
        if not os.path.isdir(lsentry):
            continue
        if lsentry.endswith('.egg'):
            sys.path.append(lsentry)

if __name__ == '__main__':
    import_path_setup()

from thrift.protocol import TBinaryProtocol
from thrift.transport import TTransport
import thrift.transport.TSocket as TSocket
from thrift.server import TServer

from contrail_vrouter_api.gen_py.instance_service import InstanceService

class VRouterApiServer(object):
    def __init__(self):
        self._dict = {}

    def _uuid_from_bytes(self, byte_array):
        return uuid.UUID(bytes=''.join([chr(x) for x in byte_array]))

    def _store(self, port):
        item = {}
        item['uuid'] = str(self._uuid_from_bytes(port.port_id))
        item['instance'] = str(self._uuid_from_bytes(port.instance_id))
        item['network'] = str(self._uuid_from_bytes(port.vn_id))
        item['display_name'] = port.display_name
        return item

    def AddPort(self, port_list):
        for port in port_list:
            uid = self._uuid_from_bytes(port.port_id)
            self._dict[str(uid)] = self._store(port)
        return True

    def KeepAliveCheck(self):
        return True

    def Connect(self):
        return True

    def DeletePort(self, port_id):
        return True

    def dump(self):
        import json
        print json.dumps(self._dict)

def create_server(handler, port):
        processor = InstanceService.Processor(handler)
        transport = TSocket.TServerSocket(port=port)
        tfactory = TTransport.TFramedTransportFactory()
        pfactory = TBinaryProtocol.TBinaryProtocolFactory()
        server = TServer.TSimpleServer(processor, transport, tfactory, pfactory)
        return server

def interrupt_handler(signal, frame):
    global handler
    handler.dump()
    sys.exit(0)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--server-port", type=int, help="Server Port")
    arguments = parser.parse_args(sys.argv[1:])

    global handler
    handler = VRouterApiServer()
    signal.signal(signal.SIGTERM, interrupt_handler)
    server = create_server(handler, arguments.server_port)
    try:
        server.serve()
    except IOError as ex:
        import errno
        if ex.errno != errno.EINTR:
            raise

if __name__ == '__main__':
    main()

