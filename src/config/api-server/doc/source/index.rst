.. vnc_api documentation master file, created by
   sphinx-quickstart on Sun Mar 10 23:39:27 2013.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Juniper Contrail Configuration API Model
========================================

Contrail configuration is expressed in terms of objects which have the following characteristics
   * Each object is identified by a UUID and a fully qualified name (FQ name).
   * Each object has a parent object (except for top level objects e.g. global-system-config, domain, virtual-machine, etc.)
       + E.g. a domain object is parent of project objects
   * An object can have any number of child objects
       + E.g. a project object can have one or more virtual-network objects and/or one or more network-policy objects as children
   * An object can refer to other objects (and conversely, can be referred to by other objects). We say that if an object Obj1 has reference to Obj2, then it means Obj2 has 'back-reference' from Obj1.
       + E.g. a virtual-network object can refer to one or more network-policy objects. A network-policy object can be referred to by one or more virtual-network objects.
   * There can be metadata attached to the reference between two objects.
       + E.g. a reference from virtual-network object to network-ipam object can have one or more subnets as metadata on the link
   * An object can have any number of property elements. These elements can be of simple types (integer, boolean, string) or complex types that contain other data types.
   * There are APIs to create/delete/update/read/list these objects. The list API can take various filters. Read and list APIs can also take the list of fields to be returned.
   * It is possible to atomically update a specific field in the object without affecting any other fields. Similarly, it is possible to atomically add or delete a reference without affecting anything else.


Juniper Contrail Configuration API Interfaces
=============================================

The Juniper Contrail configuration API server enables the manipulation of configuration
elements exposed by the Contrail API server. Interaction with the API server is possible using the following interfaces.
   * REST interface: This interface can be accessed using a command line tool (e.g. cURL) or through a browser with an extension to parse/send JSON data.
   * Contrail Python VNC API: This interface internally uses the same REST API, but provides an easy to use interface in a python client. The API is also available in other languages (e.g. java, go, etc).

This document provides:
   * Tutorials for using the REST and library interfaces
   * General examples to work on different configuration elements
   * Tips to use the system effectively
   * Reference to the package, module and classes involved

.. toctree::
   :maxdepth: 3

   tutorial_with_rest.rst
   tutorial_with_library.rst
   rest_details.rst
   library_details.rst
   library_reference.rst
   contrail_openapi.rst
   .. modules.rst

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

