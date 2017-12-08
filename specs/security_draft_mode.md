
# 1. Introduction

Allow to define if security modification in a scope (global or project) should
be applied directly or have to be audited by a tier team/person before enforce
them.

# 2. Problem statement

Some organizations needs to audit and validate any security before allowing
them.

# 3. Proposed solution

Introduce a security draft mode at the scope level that determine if any
modifications on the five security resources should be directly enforce or not.
If security needs to be audited, three cases can happen:

1. The modification create a new security resource

   In that case, we create that new security resource with a draft flag set to
   True. All reference are created and identical to a normal resource.

   Followed modification on that created security resource will be append to it.

2. The modification update an exiting security resource

   Here, we clone the security resource, set the draft flag, reference it with
   the original resource and apply modifications on that cloned resource. The
   clone also copy references and parent link, that permits to not remove a
   resource (e.g. tag) if a pending security resource reference it.
   
   If the user modify the a security resource in multiple state, Contrail will
   append modifications on the same cloned resource.

3. The modification remove a security resource

   In that case, we mark the security resource as 'to delete'. Don't touch
   links.

   Later, if the user decides to not remove the resource, the flag should be
   reset.

After, the security team/person, can review that proposed security modification
thanks to new REST API calls to list and show them. Security team/person have
two choices:

1. Commit modifications

   When security resource modifications were validated and ready to be enforced,
   the security team/person can commit them. In that first version, the commit
   granularity will be limited to the scope. When a commit is on going, no more
   security modifications are allowed.

2. Abandon modifications

   When security resource modifications were not validated, the security
   team/person can abandon them. In that first version, the abandon/revert
   granularity will be limited to the scope. When an abandon/revert is on going,
   no more security modifications are allowed.

The five security resources are:
* Application Policy Set
* Firewall Policy
* Firewall Rule
* Service Group
* Address Group

## 3.1 Alternatives considered

None

## 3.2 API schema changes

The Contrail model will be modify:

1. Add `allow_security_modification` in scope resources

   Add a flag to security scopes resources, aka. *Policy Management* and
   *Project* to know if security resource modifications in that scope should be
   directly enforce or not.

2. Add `draft` flag to the five security resources

   That flag permits to know which security resources were created/modified for
   for audit and commit.

3. Add `to_delete` flag to the five security resources

   That flag determines which security resources were delete for audit and
   commit.

For users no much changes, they will continue to use API to work with enforced
security resources. New API calls will permit to identify modified security
resources and to commit or revert them:

1. List pending modified security resources per scope
2. Commit or revert modified security resources per scope

## 3.3 User workflow impact

See introduction in 3.

## 3.4 UI changes
#### Describe any UI changes

## 3.5 Notification impact
#### Describe any log, UVE, alarm changes


# 4. Implementation
## 4.1 Work items
#### Describe changes needed for different components such as Controller, Analytics, Agent, UI. 
#### Add subsections as needed.

# 5. Performance and scaling impact
## 5.1 API and control plane
#### Scaling and performance for API and control plane

## 5.2 Forwarding performance
#### Scaling and performance for API and forwarding

# 6. Upgrade
#### Describe upgrade impact of the feature
#### Schema migration/transition

# 7. Deprecations
#### If this feature deprecates any older feature or API then list it here.

# 8. Dependencies
#### Describe dependent features or components.

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
