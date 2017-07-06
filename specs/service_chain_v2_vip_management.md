# 1. Introduction
In service-chaining v2, contrail-svc-monitor allocates service-instance ip from the same subnet for scaling up. This document explains the issue in the current scheme and solutuion for the same.

# 2. Problem statement
In service-chaining v2, contrail-svc-monitor allocates service-instance ip from the same subnet for scaling up. If the scaling is not required, ip will be wasted which is an issue in the limited ips.

# 3. Proposed solution
In service-chaining v2, Instead of allocating the service-instance ip from the same subnet, allocate from the diffrent subnet.

# 4. Implementation
"service_instance_subnet" option is given in contrail-svc-monitor.conf to provide subnet value. contrail-svc-monitor allocates service-instance ip from "service_instance_subnet" for service-chain v2.

# 5. Performance and scaling impact

## 5.1 API and control plane
None

## 5.2 Forwarding performance
None

# 6. Upgrade
  Existing service-chain v2 still would be in the existing scheme.
  New service-chain v2 will be created with the new scheme.

# 7. Deprecations
None

# 8. Dependencies

# 9. Debugging

# 10. Testing
## 10.1 Unit tests
## 10.2 Dev tests
## 10.3 System tests

# 11. Documentation Impact

# 12. References
