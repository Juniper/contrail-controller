import sys


class FakeTSSLSocket(object):
    class TSSLSocket(object):
        pass


sys.modules["thrift.transport"] = FakeTSSLSocket
