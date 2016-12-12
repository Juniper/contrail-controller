
1. Introduction
Pre 4.0, connections to introspect ports are open which may cause security
issues. This document provides details about securing introspect ports by
using ssl encryption. 

2. Problem statement
Whenever a component links with sandesh library, it creates a backend for
introspect support from where, all the debugging information can be fetched.
Till 4.0 version, these introspect pages connections were not secure.  This
feature aims to secure introspect pages by supporting ssl encryption for
introspect pages.  

3. Proposed solution
There are changes required in several components:
	▪	Location of SSL certificate and private key files will be
specified in testbed.py file.
	▪	Changes will be required in backend servers which host
introspect pages. There are python and C++ http servers which are used by
different apps so changes will be required in both type of servers to support
ssl certification
	▪	All the apps which are using introspect pages will have to
start using ssl certification, if it is enabled.

4. Implementation
4.1 Provisioning Changes
There will be some changes in provisioning to add support for specifying SSL
certificate and private key file location. 

4.2 Http server backend changes
Apps currently use Python or C++ based sandesh client, so changes will be
required in both of them. Python side uses WSGIServer which supports ssl
certification. All that is needed here is to pass corresponding ssl key and
certificate file to it.
C++ based python client currently derives directly from TcpServer which does
not support ssl certification. Changes will be needed to implement Http server
which supports ssl by using SslServer.

5. Performance and scaling impact
N/A

6. Upgrade
N/A

7. Deprecations
N/A

8. Dependencies
N/A

9. Testing
Individual apps will be brought up and tested for ssl support on corresponding
introspect pages
Some negative test cases will also be added to make sure that introspect page
access fails if wrong certificate is specified by the client.

10. Documentation Impact
N/A

11. References
N/A
