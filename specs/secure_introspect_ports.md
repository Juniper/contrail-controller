
# 1. Introduction

Sandesh clients provide introspect support to the applications that link with
it. Presently these introspect pages access is open which may cayse security
issues. This document provides details about securing introspect ports by
using ssl encryption. 

# 2. Problem statement

Whenever a component links with sandesh library, it creates a backend for
introspect support from where, all the debugging information can be fetched.
Until now, these introspect pages connections were not secure, leaving
sensitive application data open for eavesdropping. This feature aims to 
secure introspect pages by supporting ssl encryption.

# 3. Proposed solution

In fiture releases, Transport Layer Security (TLS) protocol would be used to
secure the introspect page accesses. TLSprotocol provides the following to 
ensure secure access to introspect pages

* Authentication - Provides a means to verify the identity of the Endpoints
* Encryption - Ensures that only authorized entity can interpret the data
* Integrity - Ensures data has not been altered in flight

# 4. Implementation

## 4.1 Work items

### 4.1.1 Sandesh Http Server Changes

Apps currently use Python or C++ based sandesh client, so changes will be
required in both of them. Python client uses WSGIServer which supports ssl
certification. All that is needed here is to pass corresponding ssl key and
certificate file to it.
C++ based python client currently derives directly from TcpServer which does
not support ssl certification. Changes will be needed to implement Http server
which supports ssl by deriving from SslServer.

### 4.1.2 Provisioning changes

To enable secure introspect pages, the following parameters need to be
configured in all contrail services that use sandesh client.

[SANDESH].sandesh_keyfile
    - path to the node's private key
    - default path -> /etc/contrail/ssl/private/server-privkey.pem
[SANDESH].sandesh_certfile
    - path to the node's public certificate
    - default path -> /etc/contrail/ssl/certs/server.pem
[SANDESH].sandesh_ca_cert
    - path to the CA certificate
    - default path -> /etc/contrail/ssl/certs/ca-cert.pem

# 5. Performance and scaling impact
N/A

# 6. Upgrade
N/A

# 7. Deprecations
N/A

# 8. Dependencies
N/A

# 9. Testing
Individual apps will be brought up and tested for ssl support on corresponding
introspect pages.
Some negative test cases will also be added to make sure that introspect page
access fails if wrong certificate is specified by the client.

# 10. Documentation Impact
N/A

# 11. References
N/A
