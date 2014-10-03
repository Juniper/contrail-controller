#!/usr/bin/python
#
import sys
import os

from service_policy import ServicePolicyCmd
from provision_vrouter import VrouterProvisioner
from add_virtual_dns import AddVirtualDns
from add_route_target import MxProvisioner as MxProvisionerAdd
from add_virtual_dns_record import AddVirtualDnsRecord
from associate_virtual_dns import AssociateVirtualDns
from del_projects import DeleteProjects
from del_route_target import MxProvisioner as MxProvisionerDel
from del_virtual_dns import DelVirtualDns
from del_virtual_dns_record import DelVirtualDnsRecord
from disassociate_virtual_dns import DisassociateVirtualDns
from provision_forwarding_mode import ForwardingModeSetup
from provision_static_route import StaticRouteProvisioner
from service_instance import ServiceInstanceCmd
from service_template import ServiceTemplateCmd
from create_floating_pool import VncProvisioner
from provision_bgp import BgpProvisioner
from provision_control import ControlProvisioner
from provision_encap import EncapsulationProvision
from provision_linklocal import MetadataProvisioner


from cliff.command import Command


class CmdVrouterProvisionAdd(Command):

    "Option to add vrouter "

    def take_action(self, parsed_args):
        self.app.stdout.write('Adding Vrouter!\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = VrouterProvisioner(args_str)
        sp.add_vrouter()


class CmdVrouterProvisionDel(Command):

    "Option to delete vrouter "

    def take_action(self, parsed_args):
        self.app.stdout.write('Deleting Vrouter!\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = VrouterProvisioner(args_str)
        sp.del_vrouter()


class CmdAddVirtualDns(Command):

    "Option to Add Virtual DNS"

    def take_action(self, parsed_args):
        self.app.stdout.write('Virtual DNS\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = AddVirtualDns(args_str)


class CmdAddRouteTarget(Command):

    "Option to add route target to  the MX router"

    def take_action(self, parsed_args):
        self.app.stdout.write('MX Provisioner to add route target\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = MxProvisionerAdd(args_str)


class CmdAddVirtualDnsRecord(Command):

    "Option to provision the MX router"

    def take_action(self, parsed_args):
        self.app.stdout.write('Add virtual dns record \n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = AddVirtualDnsRecord(args_str)


class CmdAssociateVirtualDns(Command):

    "Option to Associate virtual Dns"

    def take_action(self, parsed_args):
        self.app.stdout.write('Associate virtual dns\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = AssociateVirtualDns(args_str)


class CmdDeleteProjects(Command):

    "Option to Delete projects"

    def take_action(self, parsed_args):
        self.app.stdout.write('Delete specified projects\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        dp = DeleteProjects(args_str)
        dp._delete_project()


class CmdDeleteRouteTarget(Command):

    "Option to del route target to  the MX router"

    def take_action(self, parsed_args):
        self.app.stdout.write('MX Provisioner to del route target\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = MxProvisionerDel(args_str)


class CmdDelVirtualDns(Command):

    "Option to Delete Virtual DNS"

    def take_action(self, parsed_args):
        self.app.stdout.write('Delete Virtual DNS\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = DelVirtualDns(args_str)


class CmdDelVirtualDnsRecord(Command):

    "Option to Delete DNS record"

    def take_action(self, parsed_args):
        self.app.stdout.write('Del virtual dns record \n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = DelVirtualDnsRecord(args_str)


class CmdDisassociateVirtualDns(Command):

    "Option to Disasociate Virtual DNS"

    def take_action(self, parsed_args):
        self.app.stdout.write('Dsassociate virtual dns\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = DisassociateVirtualDns(args_str)


class CmdForwardingModeSetup(Command):

    "Option to provision forwarding mode"

    def take_action(self, parsed_args):
        self.app.stdout.write('Provision forwarding mode\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        sp = ForwardingModeSetup(args_str)


class CmdStaticRouteProvisionerAdd(Command):

    "Option to add static route"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to add static route\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        changed_args_str = '--oper add '
        args_str = changed_args_str + args_str
        sp = StaticRouteProvisioner(args_str)


class CmdStaticRouteProvisionerDelete(Command):

    "Option to delete static route"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to deletestatic route\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        changed_args_str = '--oper delete '
        args_str = changed_args_str + args_str
        sp = StaticRouteProvisioner(args_str)


class CmdServiceInstanceAdd(Command):

    "Option to add service instance"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to add service instance\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        changed_args_str = 'add '
        args_str = changed_args_str + args_str
        sp = ServiceInstanceCmd(args_str)
        sp.create_si()


class CmdServiceInstanceDelete(Command):

    "Option to delete service instance"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to delete service instance\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        changed_args_str = 'del '
        args_str = changed_args_str + args_str
        sp = ServiceInstanceCmd(args_str)
        sp.delete_si()


class CmdServicePolicyAdd(Command):

    "Option to add service policy"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to add service policy\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        changed_args_str = 'add '
        args_str = changed_args_str + args_str
        sp = ServicePolicyCmd(args_str)
        sp.create_policy()


class CmdServicePolicyDelete(Command):

    "Option to delete service policy"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to delete service policy\n')
        args_str = '  '.join(parsed_args[1:])
        #sys.argv = sys.argv[1:]
        sys.argv = parsed_args
        changed_args_str = 'del '
        args_str = changed_args_str + args_str
        sp = ServicePolicyCmd(args_str)
        sp.delete_policy()


class CmdServiceTemplateList(Command):

    "Option to list service templates"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to list service templates\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = 'list '
        args_str = changed_args_str + args_str
        sp = ServiceTemplateCmd(args_str)
        sp.list_st()


class CmdServiceTemplateAdd(Command):

    "Option to add service templates"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to add service templates\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = 'add '
        args_str = changed_args_str + args_str
        sp = ServiceTemplateCmd(args_str)
        sp.add_st()


class CmdServiceTemplateDel(Command):

    "Option to delete service templates"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to delete service templates\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = 'del '
        args_str = changed_args_str + args_str
        sp = ServiceTemplateCmd(args_str)
        sp.delete_st()


class CmdContrailStatus(Command):

    "Option to check contrail status"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to check contrail status\n')
        os.system("/usr/bin/contrail-status")


class CmdContrailVersion(Command):

    "Option to check contrail version"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to check contrail version\n')
        os.system("/usr/bin/contrail-version")


class CmdCreateFloatingIPPool(Command):

    "Option to create floating IP pool"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to create floating IP pool\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        sp = VncProvisioner(args_str)


class CmdAddBgpRouter(Command):

    "Option to add bgp router"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to add bgp router\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        p = BgpProvisioner(args_str)
        p.add_bgp_router()


class CmdDeleteBgpRouter(Command):

    "Option to delete bgp router"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to delete bgp router\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        p = BgpProvisioner(args_str)
        p.delete_bgp_router()


class CmdProvisionControlAdd(Command):

    "Option to provision/add control node"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to provision/add control node\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = '--oper add'
        args_str = changed_args_str + args_str
        sp = ControlProvisioner(args_str)


class CmdProvisionControlDelete(Command):

    "Option to provision/deletecontrol node"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to provision/deletecontrol node\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = '--oper del'
        args_str = changed_args_str + args_str
        sp = ControlProvisioner(args_str)


class CmdProvisionEncapsulationAdd(Command):

    "Option to provision/add encapsulation"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to provision/add encapsulation\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = '--oper add'
        args_str = changed_args_str + args_str
        sp = EncapsulationProvision(args_str)


class CmdProvisionEncapsulationUpdate(Command):

    "Option to provision/update encapsulation"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to provision/update encapsulation\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = '--oper update'
        args_str = changed_args_str + args_str
        sp = EncapsulationProvision(args_str)


class CmdProvisionLinklocalAdd(Command):

    "Option to provision/add Linklocal"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to provision/add Linklocal\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = '--oper add'
        args_str = changed_args_str + args_str
        sp = MetadataProvisioner(args_str)


class CmdProvisionLinklocalUpdate(Command):

    "Option to provision/update Linklocal"

    def take_action(self, parsed_args):
        self.app.stdout.write('Option to provision/update Linklocal\n')
        args_str = '  '.join(parsed_args[1:])
        sys.argv = parsed_args
        changed_args_str = '--oper update'
        args_str = changed_args_str + args_str
        sp = MetadataProvisioner(args_str)
