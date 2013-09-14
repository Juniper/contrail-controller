========
Coding style
========

The contrail source code includes modules writen in C, C++ and Python; there
is also a Java API library, Lua scripts an analytics IDL and a code generator
based on an mini-DSL. Each of these languages as its own coding style.

--------
C++
--------
The C++ code should follow the `Google C++ Style Guide<http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml>` with the main distiction that we use 4-space indentation rather that google's 2-space style.

C++ code submissions require a unit-test that tests the class
interface; more complex code changes require tests that test the
functionality accross multiple modules.

Bugs should be first reproduced in a unit-test and then resolved.

--------
Python
--------
Python code follows PEP-8 style.
