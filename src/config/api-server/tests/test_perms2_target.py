#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from keystoneclient.v2_0 import client
from vnc_api.vnc_api import *
import uuid
import argparse
import keystoneclient.exceptions as kc_exceptions
import cfgm_common
import pprint

PERMS_NONE = 0
PERMS_X = 1
PERMS_W = 2
PERMS_R = 4
PERMS_WX = 3
PERMS_RX = 5
PERMS_RW = 6
PERMS_RWX = 7

class TestPerms():
    def parse_args(self):
        # Eg. python test_perms.py <api-server-ip>

        parser = argparse.ArgumentParser(
            description="Test API server resource permissions")
        parser.add_argument('--api_server_ip', default='127.0.0.1',
                            help="IP address of API server")
        parser.add_argument('--api-server-port',  type=int, default=8082,
                            help="API server port (default 8082)")

        self.args = parser.parse_args()
        print 'API server = %s : %d'\
            % (self.args.api_server_ip, self.args.api_server_port)
    # end parse_args

# create users specified as array of tuples (name, password, role)
# assumes admin user and tenant exists

class User(object):
   def __init__(self, apis_ip, apis_port, kc, name, password, role, project):
       self.name = name
       self.password = password
       self.role = role
       self.project = project
       self.project_uuid = None
       self.project_obj = None

       # create user/role/tenant in keystone as needed
       kc_users = set([user.name for user in kc.users.list()])
       kc_roles = set([user.name for user in kc.roles.list()])
       kc_tenants = set([tenant.name for tenant in kc.tenants.list()])

       if self.role not in kc_roles:
           print 'role %s missing from keystone ... creating' % self.role
           kc.roles.create(self.role)

       if self.project not in kc_tenants:
           print 'tenant %s missing from keystone ... creating' % self.project
           kc.tenants.create(self.project)

       for tenant in kc.tenants.list():
           if tenant.name == self.project:
                break
       self.project_uuid = tenant.id
    
       if self.name not in kc_users:
           print 'user %s missing from keystone ... creating' % self.name
           user = kc.users.create(self.name, self.password, '', tenant_id=tenant.id)

       role_dict = {role.name:role for role in kc.roles.list()}
       user_dict = {user.name:user for user in kc.users.list()}

       print 'Adding user %s with role %s to tenant %s' \
            % (name, role, project)
       try:
           kc.roles.add_user_role(user_dict[self.name], role_dict[self.role], tenant)
       except kc_exceptions.Conflict:
           pass

       try:
           self.vnc_lib = VncApi(username = self.name, password = self.password,
               tenant_name = self.project,
               api_server_host = apis_ip,api_server_port = apis_port)
       except cfgm_common.exceptions.PermissionDenied:
           print 'Error creating API server client handle (VncApi)'
           print '*** RBAC disabled or missing user-token middleware in Neutron pipeline? Please verify'
           sys.exit(1)
   # end __init__
    
   def api_acl_name(self):
       rg_name = list(self.project_obj.get_fq_name())
       rg_name.append('default-api-access-list')
       return rg_name

# display resource id-perms
def print_perms(obj_perms):
    share_perms = ['%s:%d' % (x.tenant, x.tenant_access) for x in obj_perms.share]
    return '%s/%d %d %s' \
        % (obj_perms.owner, obj_perms.owner_access,
           obj_perms.global_access, share_perms)
# end print_perms

# set id perms for object
def set_perms(obj, owner=None, owner_access=None, share=None, global_access=None):
    try:
        perms = obj.get_perms2()
    except AttributeError:
        print '*** Unable to set perms2 in object %s' % obj.get_fq_name()
        sys.exit()
    print 'Current perms %s = %s' % (obj.get_fq_name(), print_perms(perms))

    if owner:
        perms.owner = owner

    if owner_access:
        perms.owner_access = owner_access

    if share is not None:
        perms.share = [ShareType(obj_uuid, obj_crud) for (obj_uuid, obj_crud) in share]

    if global_access is not None:
        perms.global_access = global_access

    obj.set_perms2(perms)
    print 'New perms %s = %s' % (obj.get_fq_name(), print_perms(perms))
# end set_perms

# Read VNC object. Return None if object doesn't exists
def vnc_read_obj(vnc, obj_type, name = None, obj_uuid = None):
    if name is None and obj_uuid is None:
        print 'Need FQN or UUID to read object'
        return None
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    try:
        if obj_uuid:
            if '-' not in obj_uuid:
                obj_uuid = str(uuid.UUID(obj_uuid))
            return method(id=obj_uuid)
        else:
            return method(fq_name=name)
    except NoIdError:
        print '%s %s not found!' % (obj_type, name if name else obj_uuid)
        return None
    except PermissionDenied:
        print 'Permission denied reading %s %s' % (obj_type, name)
        raise
# end

def show_rbac_rules(api_access_list_entries):
    if api_access_list_entries is None:
        print 'Empty RBAC group!'
        return

    # {u'rbac_rule': [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]}
    rule_list = api_access_list_entries.get_rbac_rule()
    print 'Rules (%d):' % len(rule_list)
    print '----------'
    idx = 1
    for rule in rule_list:
            o = rule.rule_object
            f = rule.rule_field
            ps = ''
            for p in rule.rule_perms:
                ps += p.role_name + ':' + p.role_crud + ','
            o_f = "%s.%s" % (o,f) if f else o
            print '%2d %-32s   %s' % (idx, o_f, ps)
            idx += 1
    print ''

def build_rule(rule_str):
    r = rule_str.split(" ") if rule_str else []
    if len(r) < 2:
        return None

    # [0] is object.field, [1] is list of perms
    obj_field = r[0].split(".")
    perms = r[1].split(",")

    o = obj_field[0]
    f = obj_field[1] if len(obj_field) > 1 else None
    o_f = "%s.%s" % (o,f) if f else o

    # perms eg ['foo:CRU', 'bar:CR']
    rule_perms = []
    for perm in perms:
        p = perm.split(":")
        rule_perms.append(RbacPermType(role_name = p[0], role_crud = p[1]))

    # build rule
    rule = RbacRuleType(
              rule_object = o,
              rule_field = f,
              rule_perms = rule_perms)
    return rule
#end

def match_rule(rule_list, rule_str):
    extend_rule_list = True
    nr = build_rule(rule_str)
    for r in rule_list:
        if r.rule_object != nr.rule_object or r.rule_field != nr.rule_field:
            continue

        # object and field match - fix rule in place
        extend_rule_list = False

        for np in nr.rule_perms:
            extend_perms = True
            for op in r.rule_perms:
                if op.role_name == np.role_name:
                    # role found - merge incoming and existing crud in place
                    x = set(list(op.role_crud)) | set(list(np.role_crud))
                    op.role_crud = ''.join(x)
                    extend_perms = False
            if extend_perms:
                r.rule_perms.append(RbacPermType(role_name = np.role_name, role_crud = np.role_crud))

    if extend_rule_list:
        rule_list.append(nr)
            
# end match_rule

def vnc_fix_api_access_list(vnc_lib, pobj, rule_str = None):
    rg_name = list(pobj.get_fq_name())
    rg_name.append('default-api-access-list')

    rg = vnc_read_obj(vnc_lib, 'api-access-list', name = rg_name)

    create = False
    rule_list = []
    if rg == None:
        rg = ApiAccessList(
                 name = 'default-api-access-list',
                 parent_obj = pobj,
                 api_access_list_entries = None)
        create = True
    elif rule_str:
        api_access_list_entries = rg.get_api_access_list_entries()
        rule_list = api_access_list_entries.get_rbac_rule()

    if rule_str:
        rule = match_rule(rule_list, rule_str)

    rentry = RbacRuleEntriesType(rule_list)
    rg.set_api_access_list_entries(rentry)
    if create:
        print 'API access list empty. Creating with default rule'
        vnc_lib.api_access_list_create(rg)
    else:
        vnc_lib.api_access_list_update(rg)
    show_rbac_rules(rg.get_api_access_list_entries())

def all(ip='127.0.0.1', port=8082, domain_name='default-domain',
        proj_name='my-proj', subnet='192.168.1.0', prefix=24, vn_name='my-vn'):

    testfail = 0
    testpass = 0
    fqdn = [domain_name]
    pobjs = {}

    kc = client.Client(username='admin', password='contrail123',
                       tenant_name='admin',
                       auth_url='http://127.0.0.1:5000/v2.0')

    # create admin account first to ensure there are no permission problems
    admin = User(ip, port, kc, 'admin', 'contrail123', 'admin', 'admin')
    alice = User(ip, port, kc, 'alice', 'alice123', 'alice-role', 'alice-proj')
    bob =   User(ip, port, kc, 'bob', 'bob123', 'bob-role', 'bob-proj')

    # create domain
    domain = vnc_read_obj(admin.vnc_lib, 'domain', name = fqdn)
    if domain == None:
        domain = Domain(domain_name)
        admin.vnc_lib.domain_create(domain)
        domain = vnc_read_obj(vnc_lib, 'domain', name = domain.get_fq_name())
    print 'Created domain %s' % fqdn

    # read projects
    alice.project_obj = vnc_read_obj(admin.vnc_lib, 'project', obj_uuid = alice.project_uuid)
    print 'Created Project object for %s' % alice.project
    bob.project_obj = vnc_read_obj(admin.vnc_lib, 'project', obj_uuid = bob.project_uuid)
    print 'Created Project object for %s' % bob.project

    # reassign ownership of projects to alice and bob (from admin)
    for user in [alice, bob]:
        print 'Change owner of project %s to %s' % (user.project, user.project_uuid)
        set_perms(user.project_obj, owner=user.project_uuid, share = [])
        admin.vnc_lib.project_update(user.project_obj)

    # delete test VN if it exists
    for net_name in [vn_name, 'second-vn', 'bob-vn-in-alice-project']:
        vn_fq_name = [domain_name, alice.project, net_name]
        vn = vnc_read_obj(admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        if vn:
            print '%s exists ... deleting to start fresh' % vn_fq_name
            admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

    print
    print '########### API ACCESS (CREATE) ##################'

    # delete api-access-list for alice and bob and disallow api access to their projects
    for user in [alice, bob]:
        print "Delete api-acl for project %s to disallow api access" % user.project
        vnc_fix_api_access_list(admin.vnc_lib, user.project_obj, rule_str = None)

    print 'alice: trying to create VN in her project'
    vn = VirtualNetwork(vn_name, alice.project_obj)
    try:
        alice.vnc_lib.virtual_network_create(vn)
        print '*** Created virtual network %s ... test failed!' % vn.get_fq_name()
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to create VN ... Test passes!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    # allow permission to create virtual-network
    for user in [alice, bob]:
        print "%s: project %s to allow full access to role %s" % \
            (user.name, user.project, user.role)
        # note that collection API is set for create operation
        vnc_fix_api_access_list(admin.vnc_lib, user.project_obj,
            rule_str = 'virtual-networks %s:C' % user.role)

    print ''
    print 'alice: trying to create VN in her project'
    try:
        alice.vnc_lib.virtual_network_create(vn)
        print 'Created virtual network %s ... test passed!' % vn.get_fq_name()
        testpass += 1
    except PermissionDenied as e:
        print 'Failed to create VN ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print
    print '########### API ACCESS (READ) ##################'
    print 'alice: trying to read VN in her project (should fail)'
    try:
        vn2 = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn.get_fq_name())
        print '*** Read VN without read permission ... test failed!!!'
        testfail += 1
    except PermissionDenied as e:
        print 'Unable to read VN ... test passed'
        testpass += 1
    if testfail > 0:
        sys.exit()

    # allow read access
    vnc_fix_api_access_list(admin.vnc_lib, alice.project_obj,
            rule_str = 'virtual-network %s:R' % alice.role)
    print 'alice: added permission to read virtual-network'
    print 'alice: trying to read VN in her project (should succeed)'
    try:
        vn2 = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn.get_fq_name())
        print 'Read VN successfully ... test passed'
        testpass += 1
    except PermissionDenied as e:
        testfail += 1
        print '*** Read VN failed ... test failed!!!'
    if testfail > 0:
        sys.exit()

    print
    print '########### API ACCESS (UPDATE) ##################'
    print 'alice: trying to update VN in her project (should fail)'
    try:
        vn.display_name = "foobar"
        alice.vnc_lib.virtual_network_update(vn)
        print '*** Set field in virtual network %s ... test failed!' % vn.get_fq_name()
        testfail += 1
    except PermissionDenied as e:
        print 'Unable to update field in VN ... Test succeeded!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    vnc_fix_api_access_list(admin.vnc_lib, alice.project_obj,
            rule_str = 'virtual-network %s:U' % alice.role)
    print ''
    print 'alice: added permission to update virtual-network'
    print 'alice: trying to set field in her VN '
    try:
        vn.display_name = "foobar"
        alice.vnc_lib.virtual_network_update(vn)
        print 'Set field in virtual network %s ... test passed!' % vn.get_fq_name()
        testpass += 1
    except PermissionDenied as e:
        print '*** Failed to update field in VN ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    vn2 = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn.get_fq_name())
    print 'alice: display_name %s' % vn2.display_name
    if vn2.display_name != "foobar":
        testfail += 1
        print '*** Failed to update shared field correctly in VN ... Test failed!'
    else:
        testpass += 1
        print 'Updated shared field correctly in virtual network %s ... test passed!' % vn.get_fq_name()
    if testfail > 0:
        sys.exit()

    print
    print '########### API ACCESS (update field) ##################'
    print 'Restricting update of field to admin only        '
    vnc_fix_api_access_list(admin.vnc_lib, alice.project_obj,
            rule_str = 'virtual-network.display_name admin:U')
    try:
        vn.display_name = "alice"
        alice.vnc_lib.virtual_network_update(vn)
        print '*** Set field in virtual network %s ... test failed!' % vn.get_fq_name()
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to update field in VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print
    print '########### API ACCESS (DELETE) ##################'

    # delete test VN  ... should fail
    vn_fq_name = [domain_name, alice.project, vn_name]
    try:
        alice.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)
        print '*** %s: Deleted VN %s ... test failed!' % (alice.name, vn_fq_name)
        testfail += 1
    except PermissionDenied as e:
        print '%s: Error deleting VN %s ... test passed!' % (alice.name, vn_fq_name)
        testpass += 1
    if testfail > 0:
        sys.exit()

    print 
    print '############### PERMS2 ##########################'
    print 'Giving bob API level access to perform all ops on virtual-network'
    vnc_fix_api_access_list(admin.vnc_lib, bob.project_obj,
            rule_str = 'virtual-network %s:RUD' % bob.role)

    print ''
    print 'bob: trying to create VN in alice project ... should fail'
    try:
        vn2 = VirtualNetwork('bob-vn-in-alice-project', alice.project_obj)
        bob.vnc_lib.virtual_network_create(vn2)
        print '*** Created virtual network %s ... test failed!' % vn2.get_fq_name()
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to create VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()


    vn = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn_fq_name)

    print
    print '########### READ (SHARED WITH TENANT) ##################'
    print 'Disable share in virtual networks for others'
    set_perms(vn, share = [], global_access = PERMS_NONE)
    alice.vnc_lib.virtual_network_update(vn)

    print 'Reading VN as bob ... should fail'
    try:
        net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
        print '*** Succeeded in reading VN. Test failed!'
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to read VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print 'Enable share in virtual network for bob project'
    set_perms(vn, share = [(bob.project_uuid, PERMS_R)])
    alice.vnc_lib.virtual_network_update(vn)

    print 'Reading VN as bob ... should succeed'
    try:
        net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
        print 'Succeeded in reading VN. Test passed!'
        testpass += 1
    except PermissionDenied as e:
        print '*** Failed to read VN ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print
    print '########### READ (GLOBALLY SHARED ) ##################'
    print 'Disable share in virtual networks for others'
    set_perms(vn, share = [])
    alice.vnc_lib.virtual_network_update(vn)

    print 'Reading VN as bob ... should fail'
    try:
        net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
        print 'Succeeded in reading VN. Test failed!'
        testfail += 1
    except PermissionDenied as e:
        print '*** Failed to read VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print
    print 'Enable virtual networks in alice project for global sharing (read only)'
    set_perms(vn, share = [], global_access = PERMS_R)
    alice.vnc_lib.virtual_network_update(vn)

    print 'Reading VN as bob ... should succeed'
    try:
        net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
        print 'Succeeded in reading VN. Test passed!'
        testpass += 1
    except PermissionDenied as e:
        print '*** Failed to read VN ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print 'Writing shared VN as bob ... should fail'
    try:
        vn.display_name = "foobar"
        bob.vnc_lib.virtual_network_update(vn)
        print '*** Succeeded in updating VN. Test failed!!'
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to update VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print
    print 'Enable virtual networks in alice project for global sharing (read, write)'
    print 'Writing shared VN as bob ... should succeed'
    # important: read VN afresh to overwrite display_name update pending status
    vn = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn_fq_name)
    set_perms(vn, global_access = PERMS_RW)
    alice.vnc_lib.virtual_network_update(vn)
    try:
        bob.vnc_lib.virtual_network_update(vn)
        print 'Succeeded in updating VN. Test passed!'
        testpass += 1
    except PermissionDenied as e:
        print '*** Failed to update VN ... Test failed!!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print ''
    print '########################### Collections #################'
    print 'User should be able to see VN in own project and any shared'
    print ''
    print 'alice: get virtual network collection ... should fail'

    try:
        x = alice.vnc_lib.virtual_networks_list(parent_id = alice.project_uuid)
        print '*** Read VN collection without list permission ... test failed!'
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to read VN collection ... test passed'
        testpass += 1
    if testfail > 0:
        sys.exit()

    # allow permission to read virtual-network collection
    for user in [alice, bob]:
        print "%s: project %s to allow collection access to role %s" % \
            (user.name, user.project, user.role)
        # note that collection API is set for create operation
        vnc_fix_api_access_list(admin.vnc_lib, user.project_obj,
            rule_str = 'virtual-networks %s:CR' % user.role)

    # create one more VN in alice project to differentiate from what bob sees
    vn2 = VirtualNetwork('second-vn', alice.project_obj)
    alice.vnc_lib.virtual_network_create(vn2)
    print 'Alice: created additional VN %s in her project' % vn2.get_fq_name()

    print 'Alice: network list'
    x = alice.vnc_lib.virtual_networks_list(parent_id = alice.project_uuid)
    for item in x['virtual-networks']:
        print '    %s: %s' % (item['uuid'], item['fq_name'])
    expected = set(['my-vn', 'second-vn'])
    received = set([item['fq_name'][-1] for item in x['virtual-networks']])
    if received != expected:
        print 'Alice: *** Received incorrect VN list ... test failed!'
        testfail += 1
    else:
        print 'Alice: Received correct VN list ... test passed'
        testpass += 1
    if testfail > 0:
        sys.exit()


    print
    print 'Bob: network list'
    y = bob.vnc_lib.virtual_networks_list(parent_id = bob.project_uuid)
    for item in y['virtual-networks']:
        print '    %s: %s' % (item['uuid'], item['fq_name'])
    # need changes in auto code generation for lists
    expected = set(['my-vn'])
    received = set([item['fq_name'][-1] for item in y['virtual-networks']])
    if received != expected:
        print 'Bob: *** Received incorrect VN list ... test failed!'
        testfail += 1
    else:
        print 'Bob: Received correct VN list ... test passed'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print
    print 'Tests fail=%d, pass=%d' % (testfail, testpass)

if __name__ == '__main__':
    perms = TestPerms()
    perms.parse_args()
    all(ip=perms.args.api_server_ip, port=perms.args.api_server_port)
