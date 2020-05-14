
============
Coding style
============

The contrail source code includes modules writen in C, C++ and Python; there
is also a Java API library, Lua scripts in analytics IDL, and a code generator
based on a mini-DSL. Each of these languages has a distinct coding style.

--------
C++
--------
The C++ code should follow the `Google C++ Style Guide<http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml>`
with the main distinction that we use 4-space indentation rather than 2-space.

C++ code submissions require a unit-test for the class
interface; more complex code changes require tests for the
functionality across multiple modules.

Bugs should be first reproduced in a unit-test and then resolved.

--------
Python
--------
Python code follows PEP-8 style.
