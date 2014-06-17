#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
from fabric.api import task, lcd, prefix, execute, local

@task
def setup_venv(build_top = "../../../../../build"):
    venv_base = "%s/debug/config/api-server" %(build_top)
    with lcd(venv_base):
        local("virtualenv ut-venv")

    venv_dir = "%s/ut-venv" %(venv_base)
    with lcd(venv_dir):
        with prefix(". bin/activate"):
            local(
                "pip install --upgrade "
                "%s/debug/config/common/dist/cfgm_common-0.1dev.tar.gz" % build_top)
            local(
                "pip install --upgrade "
                "%s/debug/config/api-server/dist/vnc_cfg_api_server-0.1dev.tar.gz" % build_top)
            local(
                "pip install --upgrade "
                "%s/debug/api-lib/dist/vnc_api-0.1dev.tar.gz" % build_top)
            local(
                "pip install --upgrade "
                "%s/debug/tools/sandesh/library/python/dist/sandesh-0.1dev.tar.gz" % build_top)
            local(
                "pip install --upgrade "
                "%s/debug/sandesh/common/dist/sandesh-common-0.1dev.tar.gz" % build_top)
            local(
                "pip install --upgrade "
                "%s/debug/discovery/client/dist/discoveryclient-0.1dev.tar.gz" % build_top)
            local("pip install redis==2.7.1")
            local("pip install stevedore==0.11")
            local("pip install netifaces==0.8")
            local("pip install xmltodict")
            local("pip install fixtures==0.3.12")
            local("pip install testtools==0.9.32")
            local("pip install flexmock==0.9.7")
            local("pip install python-novaclient==2.13.0")
            local("pip install stevedore")
            local("pip install netifaces")
            local("pip install requests==2.0.0")
            local("pip install kazoo")
            local("pip install kombu")
            local("pip install bottle")
            pyver = "%s.%s" % (sys.version_info[0], sys.version_info[1])
            # 2.6 requirements
            local("pip install ordereddict")
            if pyver == '2.6':
                local("pip install importlib")

            local(
                "cp ../../../../../controller/src/config/api-server/tests/"
                "test_common.py lib/python%s/site-packages/"
                "vnc_cfg_api_server/" %
                (pyver))
#end setup_venv

@task
def destroy_venv(build_top = "../../../../../build"):
    venv_base = "%s/debug/config/api-server" % (build_top)
    venv_dir = "%s/ut-venv" % (venv_base)
    local("rm -rf %s" % (venv_dir))
#end destroy_venv

@task
def run_tests(build_top = "../../../../../build"):
    venv_base = "%s/debug/config/api-server" % (build_top)
    venv_dir = "%s/ut-venv" % (venv_base)
    with lcd(venv_dir):
        with prefix("source bin/activate"):
            pyver = "%s.%s" % (sys.version_info[0], sys.version_info[1])
            local(
                "cp ../../../../../controller/src/config/api-server/tests/"
                "test_crud_basic.py lib/python%s/site-packages/"
                "vnc_cfg_api_server/" % (pyver))
            local(
                "python lib/python%s/site-packages/"
                "vnc_cfg_api_server/test_crud_basic.py" % (pyver))
#end run_tests

@task
def run_api_srv(build_top = "../../../../../build", listen_ip = None, listen_port = None):
    venv_base = "%s/debug/config/api-server" % (build_top)
    venv_dir = "%s/ut-venv" % (venv_base)
    with lcd(venv_dir):
        with prefix(". bin/activate"):
            pyver = "%s.%s" %(sys.version_info[0], sys.version_info[1])
            local(
                "cp ../../../../../controller/src/config/api-server/tests/"
                "fake_api_server.py lib/python%s/site-packages/"
                "vnc_cfg_api_server/" % (pyver))

            opt_str = ""
            if listen_ip:
                opt_str = "%s --listen_ip %s" % (opt_str, listen_ip)
            if listen_port:
                opt_str = "%s --listen_port %s" % (opt_str, listen_port)

            local(
                "python lib/python%s/site-packages/"
                "vnc_cfg_api_server/fake_api_server.py %s" % (pyver, opt_str))
#end run_api_srv

@task
def setup_and_run_tests(build_top = "../../../../../build"):
    execute(setup_venv, build_top)
    execute(run_tests, build_top)
#end setup_and_run_tests

@task
def setup_and_run_api_srv(build_top = "../../../../../build", listen_ip = None, listen_port = None):
    execute(setup_venv, build_top)
    execute(run_api_srv, build_top, listen_ip, listen_port)
#end setup_and_run_api_srv

