# vim: set expandtab shiftwidth=4 fileencoding=UTF-8:

# Copyright 2016 Netronome Systems, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from netronome.vrouter import (
    database as netro_db,
    fallback,
    pci,
    plug_modes as PM,
    vf as netro_vf
)

import argparse
import logging
import random
import sys
import uuid
import time

from multiprocessing import Process
from sqlalchemy.orm.session import sessionmaker

logger = logging.getLogger(__name__)

LOGGING_FORMAT = (
    '[%(asctime)s] %(levelname)s: %(name)s@%(process)d: %(message)s'
)


def _syntax():
    parser = argparse.ArgumentParser()
    parser.add_argument('db_fname', help='database pathname')
    parser.add_argument('n', help='number of concurrent allocators', type=int)
    return parser


def init_db(db_fname):
    engine, dsn = netro_db.create_engine(db_fname)
    netro_vf.create_metadata(engine)
    return engine, dsn


def create_fallback_map(n):
    out = fallback.FallbackMap()

    domain, bus, slot, fn = 0, 3, 0, 0
    pf_addr = pci.PciAddress(domain, bus, slot, fn)

    for vf_number in xrange(n):
        slot, fn = 8 + vf_number / 16, vf_number % 16
        vf_addr = pci.PciAddress(domain, bus, slot, fn)
        netdev = 'repif{}'.format(vf_number)

        out.add_interface(fallback.FallbackInterface(
            pf_addr, vf_addr, netdev, vf_number
        ))

    return out


def child_task(engine, vf_pool):
    u = uuid.uuid1()
    logger = logging.getLogger('child_task')

    Session = sessionmaker(bind=engine)

    # allocate_vf() performs session commits, so it can potentially trigger the
    # VRT-755 IntegrityError.
    session = Session()

    addr = vf_pool.allocate_vf(
        session=session, neutron_port=u, expires=None,
        plug_mode=random.choice(PM.accelerated_plug_modes),
        raise_on_failure=True
    )
    logger.info('port %s got address %s', u, addr)

    session.commit()


def main(db_fname, n):
    logging.basicConfig(format=LOGGING_FORMAT, level=logging.DEBUG)
    engine, dsn = init_db(db_fname)
    logger.debug('using DSN: %s', dsn)
    vf_pool = netro_vf.Pool(fallback_map=create_fallback_map(n))

    children = set()
    for i in xrange(n):
        p = Process(
            target=child_task, kwargs={'engine': engine, 'vf_pool': vf_pool}
        )
        p.start()
        children.add(p)

    exitcode = 0
    while children:
        active_children = set()

        for child in children:
            if child.is_alive():
                active_children.add(child)
                continue
            else:
                child.join()
                log = logger.warning if child.exitcode != 0 else logger.debug
                log(
                    'child %i exited with status %i', child.pid, child.exitcode
                )
                if child.exitcode > exitcode:
                    exitcode = child.exitcode

        if children != active_children:
            children = active_children
            continue

        time.sleep(0.05)

    logger.info('all done')
    return exitcode

if __name__ == '__main__':
    sys.exit(main(**vars(_syntax().parse_args())))
