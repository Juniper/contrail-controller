import requests
import xmltodict


class IntrospectClient:
    def __init__(self, ip, port, protocol='http'):
        self.ip = ip
        self.port = port
        self.protocol = protocol

    @property
    def url(self):
        return "{proto}://{ip}:{port}".format(
            proto=self.protocol, ip=self.ip, port=self.port)

    def send_get(self, uri):
        response = requests.get("{url}/{uri}".format(url=self.url,
                                                     uri=uri))
        response.raise_for_status()
        return response

    def Snh_SandeshUVECacheReq(self, tname=''):
        uri = "Snh_SandeshUVECacheReq?tname=" + tname
        response = self.send_get(uri)
        return xmltodict.parse(response.content)

    def get_NodeStatusUVEList(self):
        data = self.Snh_SandeshUVECacheReq(tname='NodeStatus')
        return NodeStatusUVEList(data['__NodeStatusUVE_list'])


class IntrospectData(object):
    def __init__(self, obj):
        self.obj = obj

    @staticmethod
    def _wrap_in_list(class_type, obj):
        if isinstance(obj, list):
            return [class_type(o) for o in obj]
        else:
            return [class_type(obj)]


class NodeStatusUVEList(IntrospectData):
    def __init__(self, obj):
        super(NodeStatusUVEList, self).__init__(obj)
        self.NodeStatusUVE = \
            self._wrap_in_list(NodeStatusUVE, self.obj['NodeStatusUVE'])


class NodeStatusUVE(IntrospectData):
    def __init__(self, obj):
        super(NodeStatusUVE, self).__init__(obj)
        self.NodeStatus = \
            self._wrap_in_list(NodeStatus, self.obj['data']['NodeStatus'])


class NodeStatus(IntrospectData):
    def __init__(self, obj):
        super(NodeStatus, self).__init__(obj)
        self.ProcessStatus = self._wrap_in_list(
            ProcessStatus, self.obj['process_status']['list']['ProcessStatus'])

    @property
    def name(self):
        return self.obj.get('name').get('#text')


class ProcessStatus(IntrospectData):
    def __init__(self, obj):
        super(ProcessStatus, self).__init__(obj)
        self.ConnectionInfo = self._wrap_in_list(ConnectionInfo,
                                                 self.connection_infos)

    @property
    def module_id(self):
        return self.obj.get('module_id').get('#text')

    @property
    def instance_id(self):
        return self.obj.get('instance_id').get('#text')

    @property
    def state(self):
        return self.obj.get('state').get('#text')

    @property
    def description(self):
        return self.obj.get('description').get('#text')

    @property
    def connection_infos(self):
        res = self.obj.get('connection_infos')
        if res:
            return res.get('list').get('ConnectionInfo')
        else:
            return res


class ConnectionInfo(IntrospectData):
    def __init__(self, obj):
        super(ConnectionInfo, self).__init__(obj)

    @property
    def type(self):
        return self.obj.get('type').get('#text')

    @property
    def name(self):
        return self.obj.get('name').get('#text')

    @property
    def status(self):
        return self.obj.get('status').get('#text')

    @property
    def description(self):
        return self.obj.get('description').get('#text')

    @property
    def server_addrs(self):
        addr = self.obj['server_addrs']['list'].get('element')
        if isinstance(addr, list):
            return [srv for srv in addr]
        else:
            return [addr]
