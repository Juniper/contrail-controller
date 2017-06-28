
# 1. Introduction
Openstack allows VMs to access metadata by sending a HTTP request to the
link local address 169.254.169.254. This request from a VM is proxied to
to Nova API, with additional HTTP header fields added. Nova uses these to
identify the source instance and responds with appropriate metadata.

Contrail vRouter acts as the proxy, trapping the metadata requests, adding
the necessary header fields and sending the requests to the Nova API server.


# 2. Problem statement
The requests from vRouter to Nova API are not encrypted and can pose a
security request.

# 3. Proposed solution
Use SSL to encrypt the HTTP interactions between Contrail vRouter and Nova API.

On the Nova side, the following configuration has to be added in default section
of nova.conf file to enable this support.

> enabled_ssl_apis = metadata <br>
> nova_metadata_protocol = https <br>
> nova_metadata_insecure = False <br>
> ssl_cert_file = cert.pem <br>
> ssl_key_file = privkey.pem <br>
> ssl_ca_file = cacert.pem <br>

The following configuration has to be added in the METADATA section of
contrail-vrouter-agent.conf to enable this support on Contrail vrouter agent.

> metadata_use_ssl = True <br>
> metadata_client_cert = client_cert.pem <br>
> metadata_client_key = client_key.pem <br>
> metadata_ca_cert = cacert.pem <br>

Contrail provisioning will be updated to populate these in the respective
configuration files as well as copying the certificate files to the appropriate
paths.

## 3.1 Alternatives considered
None

## 3.2 API schema changes
Not Applicable

## 3.3 User workflow impact
Please see above for the required configuration to be done.

## 3.4 UI changes
None

## 3.5 Notification impact
None


# 4. Implementation
## 4.1 Work items
1. Http client code to accept SSL certificates
2. vRouter Agent to use the certificates during metadata proxy, if configured.
3. Provisioning changes to update the SSL options.

# 5. Performance and scaling impact
Considering that metadata is typically accessed during VM boot up, using SSL
for the metadata communication should not cause performance impact.

## 5.1 API and control plane
None

## 5.2 Forwarding performance
None

# 6. Upgrade
None

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
1. Check that relevant configuration options are parsed
2. Check http requests with SSL options are invoked

## 9.2 Dev tests
1. Check provisioning updates the configuration files
2. Check metadata communication works with and without SSL being enabled.

## 9.3 System tests
1. Check multiple metadata communication in parallel, with SSL enabled.

# 10. Documentation Impact
Update metadata section with configuration information to achieve this.

# 11. References
https://github.com/Juniper/contrail-controller/wiki/Metadata-service
