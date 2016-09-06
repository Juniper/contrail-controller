#  Sample Test Programs for  Generated Python API
#
#  Schema to APIs are generated using the following python lib
#     https://pypi.python.org/pypi/generateDS
#
#  Schema File: juniper_device.xsd
#
#  device_lib.py can be generated using following command line option
#  python generateDS.py -f -o "device_lib.py" juniper_device.xsd

from juniper_common_xsd import *
import sys

#sample
addr = Address()
addr.set_name("1.1.1.1/24")
addr.set_virtual_gateway_address("2.2.2.2")

inet = FamilyInet()
inet.set_address(addr)

family = Family()
family.set_inet(inet)

unit1 = Unit()
unit1.set_name(24)
unit1.set_family(family)

unit2 = Unit()
unit2.set_name(25)
unit2.set_family(family)

interface = Interface()
interface.name = "ge-0/0/0"
interface.set_gratuitous_arp_reply('')
interface.add_unit(unit1)
interface.add_unit(unit2)

interfaces = Interfaces()
interfaces.add_interface(interface)

#Add Forwarding options config
filter = InetFilter()
filter.input = "redirect_to_public_vrf_filter_inet4"
inet = FamilyInet()
inet.set_filter(filter)
family = Family()
family.set_inet(inet)

forwarding_options = ForwardingOptions()
forwarding_options.add_family(family)

neighbor = Neighbor()
neighbor.name = "172.0.0.1"
neighbor.peer_as = 620003

bgp_group = BgpGroup()
bgp_group.name = "__contrail__"
bgp_group.type_ = "internal"
bgp_group.add_neighbor(neighbor)

bgp = Bgp()
bgp.add_group(bgp_group)

protocols = Protocols()
protocols.set_bgp(bgp)

#Groups Config
groups = Groups()
groups.name = "_contrail"
groups.set_protocols(protocols)
groups.set_interfaces(interfaces)
groups.set_forwarding_options(forwarding_options)

config = configuration()
config.set_groups(groups)

config.export(sys.stdout, 1)
