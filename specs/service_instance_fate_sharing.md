
# 1. Introduction

Implement fate sharing for service instances (SIs) that are part of a service chain.

# 2. Problem statement

If there are multiple SIs in a service chain, failure of a single SI can cause black holing of traffic in one or both directions. This happens because re-origination of routes for each direction of every SI in the service chain happens independently. Hence the failure of an intermediate SI does not cause the service chain to stop attracting traffic in either direction. Similary, the failure of a bookend SI does not cause the service chain to stop attracting traffic in the other direction.

# 3. Proposed solution

The proposed solution is to implement fate sharing for SIs in a service chain. If one or more SIs in a service chain go down, re-origination of routes on both sides of the service chain is stopped. This causes routing to automatically converge to a backup service chain that is part of another contrail cluster.

## 3.1 Alternatives considered

None.

## 3.2 API schema changes

There are no user visible changes to the APIs.

A new element called service-chain-group is added to the ServiceChainInfo type. The Schema Transformer populates this field and sets it to the same value for all the SIs in a service chain.

## 3.3 User workflow impact

The fate sharing functionality is enabled by default, so there's no changes to the user workflow.

## 3.4 UI changes

None.

## 3.5 Notification impact

None.

# 4. Implementation
## 4.1 Work items

### Config

Schema Transformer populates the new service-chain-group field in ServiceChainInfo such that all SIs in give chain have the same service-chain-group.

### Control Node

Control Node uses the service-chain-group in ServiceChainInfo to tie togehter all the SIs in a service chain. If it detects that the connected route for either direction of any of the SIs with the same service-chain-group is down, it stops re-originating routes in both directions for all the SIs. It resumes re-origination of routes when the connected routes for both directions of all the SIs are up.

# 5. Performance and scaling impact

None.

# 6. Upgrade

Schema Transformer populates the new service-chain-group field in ServiceChainInfo for all service chains at startup.

# 7. Deprecations

None.

# 8. Dependencies

None.

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

TBD

# 11. References

TBD