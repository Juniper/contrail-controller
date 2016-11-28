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

from __future__ import absolute_import

import netronome.vrouter.sa.sqlite as sa_sqlite
from netronome.vrouter.sa import pidguard

import os

import sqlalchemy as db
from sqlalchemy.ext.declarative import declarative_base

# need these in a common place so that multiple modules can share them
metadata = db.MetaData()
DeclarativeBase = declarative_base(metadata=metadata)

if 'NS_VROUTER_SHORT_TMP_PREFIXES' in os.environ:
    _TMP_PREFIX = 'database.'
else:
    _TMP_PREFIX = 'netronome.vrouter.database.'


def create_engine(database='memory'):
    if database == 'tmp':
        import tempfile
        tmp = tempfile.NamedTemporaryFile(
            suffix='.sqlite3', prefix=_TMP_PREFIX, delete=False
        )
        database = tmp.name
    elif database == 'memory':
        database = 'sqlite://'

    if '://' not in database:
        database = 'sqlite+pysqlite:///' + database

    dsn = database
    database_connect_args = {
        # The default SQLite busy timeout is quite short. This is long
        # enough that in practice, we should never encounter a timeout
        # (although obviously other things will start failing if vif
        # plugging actually takes this long).
        'timeout': 60,
    }
    engine = db.create_engine(dsn, connect_args=database_connect_args)
    pidguard.add_engine_pidguard(engine)
    return engine, dsn
