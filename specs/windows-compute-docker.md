Rationale and Scope
---------------------------
Windows Server 2016 comes with support for containers and docker. Pros and cons of using containers are well known. This also sets up Contrail for possible Hyper-V support in the future.

The scope is to port Compute node to Windows (Agent and vRouter) and integrate with Windows docker and Windows Containers.

Implementors
-------------------
Rajagopalan Sivaramakrishnan (raja@juniper.net)
Sagar Chitnis (sagarc@juniper.net)
CodiLime dev team (windows-contrail@codilime.com)

Target Release
---------------------
For release 1: 5.0
For release 2+: tbd

User-visible changes
----------------------------
On Linux, none.
On Windows, one can use Windows docker command line tools to spawn Windows containers and connect them to Contrail networks. (orchestration is not in scope of release 1)
For deployment, MSI installers are provided.

Internal changes
-----------------------
3 components are affected:

1. Windows docker driver is added. It is roughly equivalent to Nova Agent, but runs as a Windows service and implements docker driver APIs. It communicates with config, Agent and HNS (Host Network Service - Windows container management service). Written in Golang.

2. vRouter Agent. Parts of the codebase are not cross platform. Changes involve rewriting those and fixing related bugs.

3. vRouter Forwarding Extension. Implements vRouter kernel module functionality in terms of a Hyper-V Forwarding Extension, which is basically a kernel mode "plugin" for Windows virtual switch.

Linux communication channels between (2) and (3) are also ported using Named Pipes (for ksync and pkt0) and Windows shared memory (for flow).
