#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import argparse
import uuid
import os

from vnc_api.vnc_api import *
import vnc_api

class MultiTenancy():
    def parse_args(self):
        # Eg. python chmod.py VirtualNetwork domain:default-project:default-virtual-network

        parser = argparse.ArgumentParser(description = "Turn multi tenancy on/off")
        parser.add_argument('server', help = "API server address in the form ip:port")
        parser.add_argument('--on',  help = "Enable Multi Tenancy", action="store_true")
        parser.add_argument('--off',  help = "Disable Multi Tenancy", action="store_true")
        parser.add_argument('--os-username',  help = "Keystone User Name", default=None)
        parser.add_argument('--os-password',  help = "Keystone User Password", default=None)
        parser.add_argument('--os-tenant-name',  help = "Keystone Tenant Name", default=None)

        self.args = parser.parse_args()
        self.opts = vars(self.args)
    #end parse_args

    def get_ks_var(self, name):
        uname = name.upper()
        cname = '-'.join(name.split('_'))
        if self.opts['os_%s' %(name)]:
            value = self.opts['os_%s' %(name)]
            return (value, '')

        rsp = ''
        try:
            value = os.environ['OS_' + uname]
            if value == '':
                value = None
        except KeyError:
            value = None

        if value is None:
            rsp = 'You must provide a %s via either --os-%s or env[OS_%s]' %(name, cname, uname)
        return (value, rsp)
    #end
#end

mt = MultiTenancy()
mt.parse_args()
conf = {}

# Validate API server information
server = mt.args.server.split(':')
if len(server) != 2:
    print 'API server address must be of the form ip:port, for example 127.0.0.1:8082'
    sys.exit(1)

# Validate keystone credentials
for name in ['username', 'password', 'tenant_name']:
    val, rsp = mt.get_ks_var(name)
    if val is None:
        print rsp
        sys.exit(1)
    conf[name] = val
    
if mt.args.on and mt.args.off:
    print 'Only one of --on or --off must be specified'
    sys.exit(1)

print 'API Server = ', mt.args.server
print 'Keystone credentials %s/%s/%s' %(conf['username'], conf['password'], conf['tenant_name'])
print ''

vnc = VncApi(conf['username'], conf['password'], conf['tenant_name'], server[0], server[1], user_info = None)

# force fetch auth token even if MT is disabled!
auth_token = vnc.get_auth_token()

url = '/multi-tenancy'
if mt.args.on or mt.args.off:
    data = {'enabled' : mt.args.on}
    try:
        rv = vnc._request_server(rest.OP_PUT, url, json.dumps(data))
    except vnc_api.common.exceptions.PermissionDenied:
        print 'Permission denied'
        sys.exit(1)

rv_json = vnc._request_server(rest.OP_GET, url)
rv = json.loads(rv_json)
print 'Multi Tenancy is %s' %('enabled' if rv['enabled'] else 'disabled')
