#  Schema to APIs are generated using the following python lib
#     https://pypi.python.org/pypi/generateDS 
#
#  Help document which describes the usage of Generated Python API
# 
#  Schema File: juniper_common.xsd
#  Generated Py API: juniper_common_xsd.py
# 
#  Build: API is generated when device manager scons is invoked
#

# Import the generated file
from juniper_common_xsd import *

# The following sample explains how to generate XML data using API.
# Data types are generated based on data model 
# defined in juniper_common.xsd

# Set address data type
addr = Address()
addr.set_name("1.1.1.1/24")
addr.set_virtual_gateway_address("2.2.2.2")

# Set family inet type
inet = FamilyInet()
inet.set_address(addr)

family = Family()
family.set_inet(inet)

#set units
unit1 = Unit()
unit1.set_name(24)
unit1.set_family(family)

unit2 = Unit()
unit2.set_name(25)
unit2.set_family(family)

#set interfaces object
interface = Interface()
interface.name = "ge-0/0/0"
interface.set_gratuitous_arp_reply('')
interface.add_unit(unit1)
interface.add_unit(unit2)

interfaces = Interfaces()
interfaces.add_interface(interface)

#Groups Config
groups = Groups()
groups.name = "_contrail"
groups.set_interfaces(interfaces)
config = configuration()
config.set_groups(groups)

# following 'export' method generates XML Data from object on the console, 
# change stdout to string io buffer to get xml data 

config.export(sys.stdout, 1)
