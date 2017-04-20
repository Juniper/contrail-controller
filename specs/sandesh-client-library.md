
# 1. Introduction
Contrail Analytics is installed and deployed as standalone product
independent of Contrail SDN controller product. In such deployments,
data is sent to the collector using the Sandesh southbound protocol.
Clients or generators sending data to the collector use the C++ and
python client bindings, and the sandesh compiler to auto-generate code
from the Sandesh IDL file.

# 2. Problem statement
Analytics clients need Sandesh compiler, and C++ and python client
bindings to send data to the collector. Currently clients have to
checkout and build the contrail-controller and contrail-sandesh
github repositories to get access to the Sandesh compiler and client
bindings. This is not an ideal solution and hampers the adoption
and use of Sandesh as a southbound protocol and ultimately Contrail
Analytics as a standalone product.

# 3. Proposed solution
The proposed solution is to provide the Sandesh compiler and
the client bindings as RPM and Debian packages that can be
installed by the clients. Following packages can be provided:

1. contrail-sandesh-compiler
   This package contains the sandesh compiler that is used for
   translating from .sandesh files (containing the definitions)
   to the language binding for the supported languages.

2. libcontrail-sandesh
   This package contains the runtime library needed for C++ applications.

3. libcontrail-sandesh-dev/devel
   This package contains the development headers and static libraries needed
   for writing C++ applications.

4. python-contrail-sandesh
   This package contains the Python bindings for Sandesh. You will need
   the sandesh compiler tool (in the contrail-sandesh-compiler package) to
   compile your definition to Python classes, and then the modules in this
   package will allow you to use those classes in your programs.

## 3.1 Alternatives considered
None

## 3.2 API schema changes
None

## 3.3 User workflow impact
Clients will install the packages on their build environments instead
of checking out and building from sources.

## 3.4 UI changes
None

## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Sandesh compiler
Currently the sandesh compiler binary is named as sandesh. We will distribute
it as contrail-sandeshc or sandeshc.

## 4.2 Sandesh C++ client library
Currently the sandesh C++ client library is called libsandesh and only a
static library is built. Further libsandesh has contrail specific
dependencies on libbase, libio, libhttp, libhttp-parser. Two solutions
are possible:

## 4.2.1 Provide dependent libraries
The dependent contrail libraries can be provided in the package. Advantage
of this approach is that less development work will be needed to achieve
this solution. The disadvantage is that the client will have to link
with the dependent contrail libraries in the right order along with
libsandesh and external dependencies.

## 4.2.2 Provide single library
Build a library that includes sources from all the dependent contrail
libraries. The advantage of this approach is that the client has to
link with just libsandesh and external dependencies. Disadvantage is
that this approach requires more development work.

We will distribute it as either libcontrail-sandesh or libsandesh
depending on approach 4.2.1 or 4.2.2 respectively. If distributing
dependent libraries they will be named as libcontrail-XXX instead of
the current libXXX.

## 4.3 Sandesh python client bindings
Currently the sandesh python client package is called pysandesh. We
will distribute it as either pysandesh or contrail_pysandesh.

## 4.4 Internal usage
It is desirable but not necessary to change the contrail daemons to use
the above named ibraries and python packages.

# 5. Performance and scaling impact
None

# 6. Upgrade
None

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
sandesh client tests will be added to use these libraries and packages

## 9.2 Dev tests
scons targets will be added to ensure that clients using the libraries
and packages can be built

## 9.3 System tests
NA

# 10. Documentation Impact
None

# 11. References
None
