Library API Details
===================
The configuration API library provides a means of accessing and manipulating configuration
elements of the system through an object representation.  The library API can be classified into two categories:
   * those that manipulate an object locally (client-side)
   * those that reflect/get an object's content onto/from the configuration API server.

The configuration element types (also referred to as object types) have a hierarchical relationship 
described in :doc:`vnc_cfg.xsd` schema. The class definitions of all object types are available at
:mod:`vnc_cfg_api_server.gen.resource_common` module.

All objects have:
   * a fully-qualified name which is an array of strings representing ancestor names from root
   * an id-perms property which provide unix file-like permissions for the owner, group and others
   * zero or more properties which represent information relevant only to the object
   * zero or more references to other objects
   * zero or more back references from other objects (computed automatically by the server)
   * methods 
       + to construct an instance 

             >>> example_obj = ExampleType('name', <property_name> = <property_value>)

       + accessors for properties

             >>> print example_obj.get_<property_name>()
             >>> example_obj.set_<property_name>(<property-value>)

       + accessors for referenced objectes

             >>> print example_obj.get_<reference_type>_refs()
             >>> example_obj.add_<reference_type>(reference_obj)

       + getters for back-references (i.e. objects referring to this)

             >>> print example_obj.get_<reference_type>_back_refs()

       + getters for children objects

             >>> print example_obj.get_<child_type>s()

       These methods do not communicate with the API server

The main library class VncApi insert-ref-here has methods for every
object type to:

   * create an object

       >>> vnc_lib.example_type_create(example_obj)

   * read an object

       >>> example_obj = vnc_lib.example_type_read(id = <example-uuid>)
       >>> example_obj = vnc_lib.example_type_read(fq_name = ['example-root', ... ,'example-parent', 'example-name'])

   * update an object

       >>> vnc_lib.example_type_update(example_obj)

   * delete an object

       >>> vnc_lib.example_type_delete(id = <example-uuid>)
       >>> vnc_lib.example_type_delete(fq_name = ['example-root', ... ,'example-parent', 'example-name'])

   * list objects

       >>> vnc_lib.example_types_list(<example_parent_type>_id = <parent-uuid>)
       >>> vnc_lib.example_types_list(<example_parent_type>_fq_name = ['example-root', ... ,'example-parent'])

   These CRUD methods communicate with the API server.

The API server for most objects acts purely as a data store. However properties for some object types 
are allocated by the API server itself. These include
    * default gateway in case of subnet (if not specified by user)
    * ip address for instance-ip objects (if not specified by user)

Exceptions
----------
Errors from API server (http status codes and response content) are translated
to exception objects and raised. The different types of exceptions are defined at
cfgm_common.exceptions module

Tips
----
Online documentation of the vnc_api module can be found by:

    >>> help(vnc_api)
    Help on module vnc_api.vnc_api in vnc_api:
    NAME
        vnc_api.vnc_api
    FILE
        /usr/lib/python2.7/site-packages/vnc_api/vnc_api.py
    DESCRIPTION
        This is the main module in VNS Config API library. It handles connection to API server,
        exposes configuration elements as objects and allows for manipulating objects locally
        and updating API server
    CLASSES
        vnc_api.gen.vnc_api_client_gen.VncApiClientGen(__builtin__.object)
            VncApi
        class VncApi(vnc_api.gen.vnc_api_client_gen.VncApiClientGen)
         |  Method resolution order:
         |      VncApi
         |      vnc_api.gen.vnc_api_client_gen.VncApiClientGen
         |      __builtin__.object
         |
         |  Methods defined here:
         |
         |  __init__(self, username=None, password=None, tenant_name=None, api_server_host=None, api_server_port=None, api_server_url=None)
         |
    
Online documentation of object methods etc. with standard ``dir`` and ``help`` python commands

    >>> dir(vn_blue_obj)
    ['__class__', '__delattr__', '__dict__', '__doc__', '__format__', '__getattribute__', '__hash__', '__init__', '__module__', '__new__', '__reduce__', '__reduce_ex__', '__repr__', '__setattr__', '__sizeof__', '__str__', '__subclasshook__', '__weakref__', '_type', 'add_network_ipam', 'add_network_policy', 'del_network_ipam', 'del_network_policy', 'dump', 'factory', 'fq_name', 'from_fq_name', 'get_access_control_lists', 'get_floating_ip_pools', 'get_fq_name', 'get_fq_name_str', 'get_id_perms', 'get_instance_ip_back_refs', 'get_network_ipam_refs', 'get_network_policy_refs', 'get_parent_fq_name', 'get_parent_fq_name_str', 'get_project_back_refs', 'get_route_target_list', 'get_routing_instances', 'get_type', 'get_virtual_machine_interface_back_refs', 'name', 'network_ipam_refs', 'network_policy_refs', 'parent_name', 'set_id_perms', 'set_network_ipam', 'set_network_ipam_list', 'set_network_policy', 'set_network_policy_list', 'set_route_target_list', 'uuid']

    >>> help(vn_blue_obj)
    Help on VirtualNetwork in module vnc_api.gen.resource_common object:
    class VirtualNetwork(__builtin__.object)
     |  Represents virtual-network configuration representation.
     |  
     |  Child of :class:`.Project` object
     |  
     |  Properties of:
     |      * route-target-list (:class:`.RouteTargetList` type)
     |      * id-perms (:class:`.IdPermsType` type)
     |  
     |  References to:
     |      * list of (:class:`.NetworkIpam` object, :class:`.VnSubnetsType` attribute)
     |      * list of (:class:`.NetworkPolicy` object, :class:`.VirtualNetworkPolicyType` attribute)
     |  
     |  Referred by:
     |      * list of :class:`.Project` objects
     |      * list of :class:`.VirtualMachineInterface` objects
     |      * list of :class:`.InstanceIp` objects
     |
