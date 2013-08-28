#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from fabric.api import task, lcd, prefix, execute, local

@task
def setup_venv(build_top = "../../../../build"):
    venv_base = "%s/debug/cfgm/api-server" %(build_top)
    with lcd(venv_base):
        local("virtualenv ut-venv")

    venv_dir = "%s/ut-venv" %(venv_base)
    with lcd(venv_dir):
        with prefix("source bin/activate"):
            local("pip install --upgrade ../../common/dist/cfgm_common-0.1dev.tar.gz")
            local("pip install --upgrade ../dist/vnc_cfg_api_server-0.1dev.tar.gz")
            local("pip install --upgrade ../../../api-lib/dist/vnc_api-0.1dev.tar.gz")
            local("pip install --upgrade ../../../sandesh/library/python/dist/sandesh-0.1dev.tar.gz")
            local("pip install --upgrade ../../../discovery/dist/discovery-0.1dev.tar.gz")
            local("pip install xmltodict")
            local("pip install fixtures==0.3.12")
            local("pip install testtools==0.9.32")
            local("pip install flexmock==0.9.7")
            local("cp ../../../../../src/cfgm/api-server/tests/test_crud_basic.py lib/python2.7/site-packages/vnc_cfg_api_server/")
#end setup_venv

@task
def destroy_venv(build_top = "../../../../build"):
    venv_base = "%s/debug/cfgm/api-server" %(build_top)
    venv_dir = "%s/ut-venv" %(venv_base)
    local("rm -rf %s" %(venv_dir))
#end destroy_venv

@task
def run_tests(build_top = "../../../../build"):
    venv_base = "%s/debug/cfgm/api-server" %(build_top)
    venv_dir = "%s/ut-venv" %(venv_base)
    with lcd(venv_dir):
        with prefix("source bin/activate"):
            local("python lib/python2.7/site-packages/vnc_cfg_api_server/test_crud_basic.py")
#end run_tests

@task
def setup_and_run(build_top = "../../../../build"):
    execute(setup_venv, build_top)
    execute(run_tests, build_top)
#end setup_and_run
