This directory contains all Python unit tests for source files under controller/src/config/fabric-ansible.
All unit test files for source files under controller/src/config/fabric-ansible should be contained within this test directory.

Tests can be run by:
```
/root/contrail#  scons controller/src/config/fabric-ansible
```
Any library dependencies for test files should be added to src/config/fabric-ansible/test-requirements.txt


#### Notes:

1) Python source files are primarily contained in these directories under controller/src/config/fabric-ansible

- library
- module_utils
- filter_plugins
- roles/{role}/filter_plugins

2) Tests for Ansible modules in the library directory require special handling because they use a special TestFabricModule class.
See controller/src/config/fabric-ansible/test/test_vnc_db_mod.py for an example.

3) For all unit tests, it may be necessary to add additional sys.path.append statements in the source files 
in order to properly import. Remember that the source files were originally created to run in a target environment.
For unit tests, these same source files need to run in a test environment in the source tree.
See controller/src/config/fabric-ansible/ansible-playbooks/library/vnc_db_mod.py for an example.

