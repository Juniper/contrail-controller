#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
OP_POST = 1
OP_GET = 2
OP_PUT = 3
OP_DELETE = 4


def hdr_client_tenant():
    return 'X-Tenant-Name'
# end hdr_tenant_client

# TODO transform from client value


def hdr_server_tenant():
    return 'HTTP_X_TENANT_NAME'
# end hdr_tenant_server


class LinkObject(object):

    def __init__(self, rel, base_url, uri, name):
        self.rel = rel
        self.base_url = base_url
        self.uri = uri
        self.name = name
    # end __init__

    def to_dict(self, with_url=None):
        if not with_url:
            url = self.base_url
        else:
            url = with_url
        return {'rel': self.rel,
                'href': url + self.uri,
                'name': self.name}
    # end to_dict

# end class LinkObject
