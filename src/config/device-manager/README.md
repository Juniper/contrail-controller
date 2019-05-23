# Contrail Device Manager

## What is Contrail Device Manager?

Contrail Device Manager manages physical devices. Maintain netconf / gRPC
connections. Generate native configuration based on config data and intent
data model. Device Manager allows to extend a private/public network and
ports to a physical router. When Device Manager detects VNC configuration,
it pushes L2 (EVPN) & L3 VRF , import and export rules, fip/snat configuration
and interface configuration to physical router. It can configure
Service Chaining (PNF) policies.

## Running Tests

Before submitting a patch for review you should always ensure all tests pass; a
tox run is triggered by the jenkins gate executed on gerrit for each patch
pushed for review.

Contrail Device Manager uses [tox](http://tox.readthedocs.org/en/latest/) 
for managing the virtual environments for running test cases. It uses
[stestr](https://stestr.readthedocs.io/en/latest/index.html) for managing the
running of the test cases.

Tox handles the creation of a series of
[virtualenvs](https://pypi.python.org/pypi/virtualenv) that target specific
versions of Python (limited to 2.7 for the moment).

stestr handles the parallel execution of series of test cases as well as
the tracking of long-running tests and other things.

### PEP8 and Unit Tests

Running unit tests is as easy as executing this in the root directory
of the Contrail Device Manager source code:

    tox

To run only the unit tests:

    tox -e py27

To run only pep8:

    tox -e pep8

### Running Individual Tests

For running individual test modules, cases or tests, you just need to pass
the dot-separated path you want as an argument to it.

For example, the following would run only a single test or test case:

      $ tox -e py27 test.test_dm_bgp.TestBgpDM.check_router_id_config
      $ tox -e py27 test.test_dm_bgp.TestBgpDM

### Coverage

Contrail has a fast growing code base and there are plenty of areas that need
better coverage.

To get a grasp of the areas where tests are needed, you can check current unit
tests coverage by running:

    $ tox -e cover

### Debugging

By default, calls to pdb.set_trace() will be ignored when tests are run. For
pdb statements to work, invoke tox as follows:

    $ tox -e venv -- python -m testtools.run [test module path]

Tox-created virtual environments (venv's) can also be activated after a tox run
and reused for debugging:

    $ tox -e venv
    $ . .tox/venv/bin/activate
    $ python -m testtools.run [test module path]
