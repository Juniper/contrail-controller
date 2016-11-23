# dialects/postgres.py
# Copyright (C) 2005-2016 the SQLAlchemy authors and contributors
# <see AUTHORS file>
#
# This module is part of SQLAlchemy and is released under
# the MIT License: http://www.opensource.org/licenses/mit-license.php

# backwards compat with the old name
from sqlalchemy_.util import warn_deprecated

warn_deprecated(
    "The SQLAlchemy PostgreSQL dialect has been renamed from 'postgres' to "
    "'postgresql'. The new URL format is "
    "postgresql[+driver]://<user>:<pass>@<host>/<dbname>"
)

from sqlalchemy_.dialects.postgresql import *
from sqlalchemy_.dialects.postgresql import base
