# 1. Introduction

All Contrail services uses Sandesh, a southbound interface protocol based on
Apache Thrift to send analytics data such as systemlogs, objectlogs, UVEs,
flowlogs, etc., to the Collector service in Analytics node. This document
provides details about securing the Sandesh connection.

# 2. Problem statement

Until now, the Sandesh connection between the Generators (all Contrail services
that send analytics data to Collector) and the Collector is not secured, which
means

* The Generators do not verify the identity of the Collector and vice-versa.
  Therefore, the Generators has no way to ascertain if it indeed sends the
  analytics data (which may contain sensitive information) to the legitimate
  Collector.
* The data exchanged between the Generator and the Collector is not encrypted
  which could lead to potential eavesdropping and tampering.

# 3. Proposed solution

Transport Layer Security (TLS) protocol would be used to secure the
Sandesh connection. TLS protocol provides the following to ensure secure
communication between the Collector and the Generators in the Contrail
Virtual Network System (VNS)

* Authentication - Provides a means to verify the identity of the Endpoints
* Encryption - Ensures that only authorized entity can interpret the data
* Integrity - Ensures data has not been altered in flight

# 4. Implementation

## 4.1 Work items

### 4.1.1 Provisioning changes

To enable secure sandesh connection, the following parameters need to be
configured in all contrail services that connect to the Collector
(Sandesh Clients) and the Collector (Sandesh Server).

```
===========================================================================================
CONFIG PARAMETER               VALUE
===========================================================================================
[SANDESH].sandesh_keyfile      Path to the node's private key
                               default value -> /etc/contrail/ssl/private/server-privkey.pem

[SANDESH].sandesh_certfile     Path to the node's public certificate
                               default value -> /etc/contrail/ssl/certs/server.pem

[SANDESH].sandesh_ca_cert      Path to the CA certificate
                               default value -> /etc/contrail/ssl/certs/ca-cert.pem

[SANDESH].sandesh_ssl_enable   Enable/Disable secure sandesh connection
                               default value -> false
```

### 4.1.2 Sandesh library

C++ Sandesh library
    - SandeshClient and SandeshServer classes should to be derived from
      SslServer instead of TcpServer
    - ssl options should be passed in Sandesh::InitGenerator() and
      Sandesh::InitCollector()

Python Sandesh library
    - Create a SslSession class; derived from TcpSession
    - SandeshSession should be derived from SslSession instead of TcpSession
    - ssl options should be passed in Sandesh.init_generator()

# 5. Performance and scaling impact

TBD

# 6. Upgrade

TBD

# 7. Deprecations

None

# 8. Dependencies

# 9. Testing

## 9.1 Unit tests

## 9.2 Dev tests

## 9.3 System tests

# 10. Documentation Impact

# 11. References
