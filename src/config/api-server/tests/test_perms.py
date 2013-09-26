#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from keystoneclient.v2_0 import client
from vnc_api.vnc_api import *
import uuid
import argparse


class TestPerms():

    def parse_args(self):
        # Eg. python test_perms.py <api-server-ip>

        parser = argparse.ArgumentParser(
            description="Test API server resource permissions")
        parser.add_argument('api_server_ip', help="IP address of API server")
        parser.add_argument('--api-server-port',  type=int, default=8082,
                            help="API server port (default 8082)")

        self.args = parser.parse_args()
        print 'API server = %s : %d'\
            % (self.args.api_server_ip, self.args.api_server_port)
    # end parse_args

# create users specified as array of tuples (name, password, role)
# assumes admin user and tenant exists


def keystone_create_users(user_list):

    user_pass = {n: p for (n, p, r) in user_list}
    user_role = {n: r for (n, p, r) in user_list}
    user_set = set([n for (n, p, r) in user_list])
    role_set = set([r for (n, p, r) in user_list])

    kc = client.Client(username='admin', password='contrail123',
                       tenant_name='admin',
                       auth_url='http://127.0.0.1:5000/v2.0')
    users = set([user.name for user in kc.users.list()])
    roles = set([user.name for user in kc.roles.list()])
    tenants = kc.tenants.list()
    admin_tenant = [x for x in tenants if x.name == 'admin'][0]

    create_user_set = user_set - users
    create_role_set = role_set - roles

    # create missing roles
    for rolename in create_role_set:
        kc.roles.create(rolename)

    # rebuild name->role dictionary from keystone
    role_dict = {role.name: role for role in kc.roles.list()}

    for name in create_user_set:
        user = kc.users.create(
            name, user_pass[name], '', tenant_id=admin_tenant.id)
        kc.roles.add_user_role(user, role_dict[user_role[name]], admin_tenant)
# end keystobe_create_users

# display resource id-perms


def print_perms(perms):
    return '%s/%s %d%d%d' \
        % (perms.permissions.owner, perms.permissions.group,
           perms.permissions.owner_access, perms.permissions.group_access,
           perms.permissions.other_access)
# end print_perms

# set id perms for object


def set_perms(obj, mode=None, owner=None, group=None):
    perms = obj.get_id_perms()
    print 'Current perms %s = %s' % (obj.get_fq_name(), print_perms(perms))

    if mode:
        # convert 3 digit octal permissions to owner/group/other bits
        access = list(mode)
        if len(access) == 4:
            access = access[1:]
        perms.permissions.owner_access = int(access[0])
        perms.permissions.group_access = int(access[1])
        perms.permissions.other_access = int(access[2])

    if owner:
        perms.permissions.owner = owner

    if group:
        perms.permissions.group = group

    print 'New perms %s = %s' % (obj.get_fq_name(), print_perms(perms))
# end set_perms


def all(ip='127.0.0.1', port=8082, domain_name='my-domain',
        proj_name='my-proj', subnet='192.168.1.0', prefix=24, vn_name='my-vn'):

    testfail = 0
    testpass = 0

    # create test users and role
    keystone_create_users(
        [('alice', 'alice123', 'staff'), ('bob', 'bob123', 'staff')])

    # user=admin, role=admin
    vnc_lib = VncApi(
        username='admin', password='contrail123', tenant_name='admin',
        api_server_host=ip, api_server_port=port)

    # user=bob, role=staff
    vnc_lib_bob = VncApi(
        username='bob', password='bob123', tenant_name='admin',
        api_server_host=ip, api_server_port=port)

    # user=alice, role=staff
    vnc_lib_alice = VncApi(
        username='alice', password='alice123', tenant_name='admin',
        api_server_host=ip, api_server_port=port)

    # create domain
    domain = Domain(domain_name)
    vnc_lib.domain_create(domain)
    print 'Created domain'

    # create project
    project = Project(proj_name, domain)
    vnc_lib.project_create(project)
    project = vnc_lib.project_read(project.get_fq_name())
    print 'Created Project'

    # create IPAM
    ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
    vnc_lib.network_ipam_create(ipam)
    print 'Created network ipam'

    ipam = vnc_lib.network_ipam_read(
        fq_name=[domain_name, proj_name, 'default-network-ipam'])
    print 'Read network ipam'
    ipam_sn_1 = IpamSubnetType(subnet=SubnetType(subnet, prefix))

    vn = VirtualNetwork(vn_name, project)

    print
    print '############################# CREATE ##############################'
    print 'Disable write in domain/project for others'
    set_perms(project, mode='0775')
    vnc_lib.project_update(project)

    print 'Trying to create VN in project as bob/staff ... should fail'
    try:
        vnc_lib_bob.virtual_network_create(vn)
        print '*** Succeeded in creating VN ... Test failed!'
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to create VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print 'Resetting permissions to allow bob/staff to create VN'
    set_perms(project, mode='0777')
    vnc_lib.project_update(project)

    print 'Trying to create VN in project as bob/staff ... should go through'
    try:
        vnc_lib_bob.virtual_network_create(vn)
        print 'Success in creating VN ... Test passed!'
        testpass += 1
    except PermissionDenied as e:
        print '*** Failed to create VN ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print
    print '######################### READ ###################################'
    print 'Reading VN as bob/staff'
    print 'should go through because of other permissions'
    net_obj = vnc_lib_bob.virtual_network_read(id=vn.uuid)
    print 'VN name=%s, uuid=%s' % (net_obj.get_fq_name(), net_obj.uuid)

    print 'Resetting ownership and permission to disallow others to read VN'
    set_perms(net_obj, mode='0773', owner='admin', group='foobar')
    vnc_lib_bob.virtual_network_update(net_obj)

    print 'Reading VN as bob/staff ... should fail'
    try:
        net_obj = vnc_lib_bob.virtual_network_read(id=vn.uuid)
        print '*** Succeeded in reading VN. Test failed!'
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to read VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print
    print '######################### DELETE #################################'
    print 'Resetting project ownership and permission to disallow delete'
    project = vnc_lib.project_read(project.get_fq_name())
    set_perms(project, mode='0775')
    vnc_lib.project_update(project)
    print 'Trying to delete VN in project as bob/staff ... should fail'
    try:
        vnc_lib_bob.virtual_network_delete(id=vn.uuid)
        print '*** Succeeded in deleting VN. Test failed!'
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to delete VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print 'Resetting project ownership and permission to allow delete'
    project = vnc_lib.project_read(project.get_fq_name())
    set_perms(project, mode='0777')
    vnc_lib.project_update(project)
    print 'Trying to delete VN in project as bob/staff ... should succeed'
    try:
        vnc_lib_bob.virtual_network_delete(id=vn.uuid)
        testpass += 1
    except PermissionDenied as e:
        print '*** Failed to delete VN ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print 'Trying to read VN as admin ... should fail'
    try:
        net_obj = vnc_lib.virtual_network_read(id=vn.uuid)
        print '*** Successful in reading VN ... Test failed!'
        testfail += 1
    except HttpError as e:
        print 'Failed to read VN ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print
    print '########################### LINK ##################################'
    print 'Create VN'
    vnc_lib.virtual_network_create(vn)
    net_obj = vnc_lib.virtual_network_read(fq_name=vn.get_fq_name())
    print 'VN name=%s, uuid=%s' % (net_obj.get_fq_name(), net_obj.uuid)
    print 'Disallow network IPAM from linking by others'
    set_perms(ipam, mode='776')
    vnc_lib.network_ipam_update(ipam)
    net_obj.add_network_ipam(ipam, VnSubnetsType([ipam_sn_1]))
    try:
        vnc_lib_bob.virtual_network_update(net_obj)
        print '*** Succeeded in linking IPAM ... Test failed !'
        testfail += 1
    except PermissionDenied as e:
        print 'Failed to link IPAM ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print 'Allow network IPAM from linking by others'
    set_perms(ipam, mode='777')
    vnc_lib.network_ipam_update(ipam)
    try:
        vnc_lib_bob.virtual_network_update(net_obj)
        print 'Succeeded in linking IPAM ... Test passed !'
        testpass += 1
    except PermissionDenied as e:
        print '*** Failed to link IPAM ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print
    print '########################## READ OWNER #############################'
    print 'Set VN perms such that only owner has read/write permissions'
    net_obj = vnc_lib.virtual_network_read(fq_name=vn.get_fq_name())
    set_perms(net_obj, mode='0770', owner='bob')
    vnc_lib.virtual_network_update(net_obj)
    print 'Trying to read VN as Alice'
    try:
        net_obj = vnc_lib_alice.virtual_network_read(
            fq_name=vn.get_fq_name())
        print '*** Read perms successfully .. test failed !'
        testfail += 1
    except PermissionDenied as e:
        print ' -> Failed to read perms ... Test passed!'
        testpass += 1
    if testfail > 0:
        sys.exit()

    print 'Trying to read VN as bob'
    try:
        net_obj = vnc_lib_bob.virtual_network_read(fq_name=vn.get_fq_name())
        print ' -> name=%s, perms=%s'\
            % (net_obj.get_fq_name(), print_perms(net_obj.get_id_perms()))
        testpass += 1
    except PermissionDenied as e:
        print '*** Failed to read ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print
    print '######################### UPDATE OWNER ############################'
    print 'Set VN perms such that owner/group has read/write permissions'
    net_obj = vnc_lib_bob.virtual_network_read(fq_name=vn.get_fq_name())
    set_perms(net_obj, mode='0770', owner='bob', group='staff')
    try:
        vnc_lib_alice.virtual_network_update(net_obj)
        print ' *** was able to update VN as Alice. Test failed !'
        testfail += 1
    except PermissionDenied as e:
        print ' unable to update VN as Alice. Test successful !'
        testpass += 1
    vnc_lib_bob.virtual_network_update(net_obj)
    if testfail > 0:
        sys.exit()

    print
    print '########################## READ GROUP #############################'
    print 'Trying to read VN as Alice .. should go through now'
    try:
        net_obj = vnc_lib_alice.virtual_network_read(
            fq_name=vn.get_fq_name())
        print ' -> name=%s, perms=%s'\
            % (net_obj.get_fq_name(), print_perms(net_obj.get_id_perms()))
        testpass += 1
    except PermissionDenied as e:
        print ' *** Failed to read ... Test failed!'
        testfail += 1
    if testfail > 0:
        sys.exit()

    print
    print 'Tests fail=%d, pass=%d' % (testfail, testpass)

if __name__ == '__main__':
    perms = TestPerms()
    perms.parse_args()
    all(ip=perms.args.api_server_ip, port=perms.args.api_server_port)
