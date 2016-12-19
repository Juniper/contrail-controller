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

from sqlalchemy.engine import Engine
from sqlalchemy import event

import sqlite3


# http://docs.sqlalchemy.org/en/latest/dialects/sqlite.html#foreign-key-support
# http://stackoverflow.com/questions/13712381/
#  how-to-turn-on-pragma-foreign-keys-on-in-sqlalchemy-migration-script-or-conf
@event.listens_for(Engine, "connect")
def set_sqlite_pragma(dbapi_connection, connection_record):
    # play well with other DB backends
    if type(dbapi_connection) is not sqlite3.Connection:
        return
    cursor = dbapi_connection.cursor()
    cursor.execute("PRAGMA foreign_keys=ON")
    cursor.close()


def set_sqlite_synchronous_off(engine):
    @event.listens_for(engine, 'connect')
    def send_pragma_synchronous_off(dbapi_connection, connection_record):
        if type(dbapi_connection) is not sqlite3.Connection:
            return
        cursor = dbapi_connection.cursor()
        cursor.execute("PRAGMA synchronous = OFF")
        cursor.close()
