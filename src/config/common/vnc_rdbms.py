#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import uuid
import json
import time
from datetime import datetime

import struct

from sqlalchemy import create_engine, ForeignKey
from sqlalchemy import Column, String, Text, Boolean, Integer, BINARY
from sqlalchemy import or_, and_
from sqlalchemy.orm import sessionmaker, relationship, aliased
from sqlalchemy.ext.declarative import declarative_base, declared_attr
from sqlalchemy.orm.exc import NoResultFound

from vnc_api import vnc_api
from vnc_api.gen import vnc_api_client_gen
import cfgm_common.utils
from cfgm_common.exceptions import NoIdError, DatabaseUnavailableError, \
                                   NoUserAgentKey
from cfgm_common.exceptions import ResourceExhaustionError, ResourceExistsError
from cfgm_common import SGID_MIN_ALLOC

Base = declarative_base()

class SqaObjectBase(object):
    obj_uuid = Column(String(36), primary_key=True)
    obj_type = Column(String(255))
    obj_fq_name = Column(String(1024)) # TODO can this be marked index?
    obj_parent_type = Column(String(255))
    obj_parent_uuid = Column(String(36))
    meta_create_epoch = Column(String(36))
    meta_prop_ref_update_epoch = Column(String(36))
    meta_update_epoch = Column(String(36)) # including children + backref
    perms2_owner = Column(String(36))
    perms2_global_access = Integer()
    perms2_owner_access = Integer()

    def __repr__(self):
       return "<SqaObjectBase(obj_uuid='%s', obj_type='%s', obj_fq_name='%s', obj_parent_type='%s', obj_parent_uuid='%s')>" % (
                            self.obj_uuid, self.obj_type, self.obj_fq_name, self.obj_parent_type, self.obj_parent_uuid)
# end class SqaObjectBase

class SqaShare(object):
    tenant = Column(String(1024))
    tenant_access = Column(Integer, primary_key=True)

    def __repr__(self):
       return "<SqaShare(owner='%s' tenant='%s', tenant_access='%d')>" % (
           self.owner, self.tenant, self.tenant_access)

class SqaObjectRef(object):
    #ref_value = Column(Text())
    ref_value = Column(String(1024))
    is_relaxed_ref = Column(Boolean, default=False)

    #from_obj = relationship("SqaObjectBase", foreign_keys=[from_obj_uuid])
    #to_obj = relationship("SqaObjectBase", foreign_keys=[to_obj_uuid])

    def __repr__(self):
       return "<SqaObjectRef(ref_value='%s', from_obj_uuid='%s', to_obj_uuid='%s', is_relaxed_ref='%s')>" % (
           self.ref_value, self.from_obj_uuid, self.to_obj_uuid, self.is_relaxed_ref)
# end class SqaObjectRef

class SqaList(object):
    index = Column(Integer, primary_key=True)
    value = Column(Text())

    def __repr__(self):
       return "<SqaList(owner='%s' index='%d', value='%s')>" % (
           self.owner, self.index, self.value)

    @declared_attr
    def __mapper_args__(cls):
        return {'order_by': [cls.index]}

class SqaMap(object):
    key = Column(String(255), primary_key=True)
    value = Column(Text())

    def __repr__(self):
       return "<SqaMap(owner='%s' key='%s', value='%s')>" % (
           self.owner, self.key, self.value)

class UseragentKV(Base):
    __tablename__ = 'useragent_kv'

    useragent = Column(String(512), primary_key=True)
    key = Column(String(512), primary_key=True)
    value = Column(String(1024))

    def __repr__(self):
       return "<UseragentKV(useragent='%s', key='%s', value='%s')>" % (
                            self.useragent, self.key, self.value)
# end class UseragentKV

class ObjectMetadata(Base):
    __tablename__ = 'object_metadata'
    obj_uuid = Column(String(36), primary_key=True)
    obj_type = Column(String(255))
    obj_fq_name = Column(String(1024)) # TODO can this be marked index?

class IDPool(Base):
    __tablename__ = 'id_pool'

    start = Column(BINARY(16), primary_key=True)
    end = Column(BINARY(16))
    path = Column(String(1024), primary_key=True)
    value = Column(String(1024))
    used = Column(Boolean, default=False)

    def __repr__(self):
       return "<IDPool(start='%s', end='%s', path='%s', value='%s', used='%s')>" % (
                            unpack(self.start), unpack(self.end), self.path, self.value, self.used)

def use_session(func):
    def wrapper(self, *args, **kwargs):
        if self.session_ctx:
            created_session = False
        else:
            created_session = True
            self.session_ctx = self.Session()
        try:
            return func(self, *args, **kwargs)
        finally:
            if created_session and self.session_ctx:
                self.session_ctx.close()
                self.session_ctx = None
    # end wrapper

    return wrapper
# end use_session

def pack(var):
    return struct.pack("!QQ", var/(2**64), var%(2**64))

def unpack(b):
    num = struct.unpack("!QQ", b)
    return num[0] * 2**64 + num[1]

class RDBMSIndexAllocator(object):
    def __init__(self, db, path, size=0, start_idx=0,
                 reverse=False,alloc_list=None, max_alloc=0):
        self.path = path
        self.db = db
        self.reverse = reverse
        self.session_ctx = None
        self.Session = sessionmaker(bind=db)
        if alloc_list:
            self._alloc_list = sorted(alloc_list, key=lambda k: k['start'])
        else:
            self._alloc_list = [{'start':start_idx, 'end':start_idx+size-1}]

        alloc_count = len(self._alloc_list)
        for alloc_idx in range (0, alloc_count -1):
            idx_start_addr = self._alloc_list[alloc_idx]['start']
            idx_end_addr = self._alloc_list[alloc_idx]['end']
            next_start_addr = self._alloc_list[alloc_idx+1]['start']
            if next_start_addr <= idx_end_addr:
                raise Exception(
                    'Allocation Lists Overlapping: %s' %(alloc_list))

        self._init_alloc_list()

    def _new_allocation(self, allocation):
        return IDPool(
                start=pack(allocation['start']), end=pack(allocation['end']), path=self.path)

    @use_session
    def _init_alloc_list(self):
        for allocation in self._alloc_list:
            if self.check_overlap(allocation):
                id_pool = self._new_allocation(allocation)
                self.session_ctx.add(id_pool)
        self.session_ctx.commit()

    @use_session
    def check_overlap(self, allocation):
        session = self.session_ctx
        overlap = session.query(IDPool).filter(and_(
                            IDPool.path == self.path,
                            pack(allocation['start']) <= IDPool.end,
                            IDPool.start <= pack(allocation['end']))).all()
        return len(overlap) == 0

    @use_session
    def set_in_use(self, idx, value=None):
        session = self.session_ctx
        pool = session.query(IDPool).filter(and_(
                            IDPool.path == self.path,
                            IDPool.start <= pack(idx),
                            pack(idx) <= IDPool.end,
                            IDPool.used == False)).first()
        if pool:
            session.delete(pool)
            if unpack(pool.start) != idx:
                newAllocation = self._new_allocation({"start": unpack(pool.start), "end": idx - 1})
                session.add(newAllocation)
            allocated = self._new_allocation({"start": idx, "end": idx})
            allocated.used = True
            allocated.value = value
            session.add(allocated)
            if unpack(pool.end) != idx:
                newAllocation = self._new_allocation({"start": idx + 1, "end": unpack(pool.end)})
                session.add(newAllocation)
            session.commit()
            return idx

    @use_session
    def reset_in_use(self, idx):
        session = self.session_ctx
        pool = session.query(IDPool).filter(and_(
                            IDPool.path == self.path,
                            IDPool.start == pack(idx),
                            pack(idx) == IDPool.end,
                            IDPool.used == True)).first()
        if pool:
            pool.used = False
            session.add(pool)
            session.commit()

    @use_session
    def get_alloc_count(self):
        # TODO(nati) we can also store size in db and use sum query if
        # this become performance bottleneck
        session = self.session_ctx
        size = 0
        allocations = session.query(IDPool).filter(
            and_(
                IDPool.path == self.path,
                IDPool.used == True
        )).all()
        for allocation in allocations:
            size += unpack(allocation.end) - unpack(allocation.start) + 1
        return size

    @use_session
    def alloc(self, value=None):
        session = self.session_ctx
        query = session.query(IDPool).filter(
            and_(
                IDPool.path == self.path,
                IDPool.used == False
        ))

        if self.reverse:
            query = query.order_by(IDPool.start.desc())
        else:
            query = query.order_by(IDPool.start)

        pool = query.first()
        if not pool:
            raise ResourceExhaustionError()

        if unpack(pool.start) == unpack(pool.end):
            pool.used = True
            pool.value = value
            session.add(pool)
            session.commit()
            return unpack(pool.start)

        session.delete(pool)
        if self.reverse:
            allocated = self._new_allocation({"start": unpack(pool.end), "end": unpack(pool.end)})
            allocated.used = True
            allocated.value = value
            session.add(allocated)
            newAllocation = self._new_allocation({"start": unpack(pool.start), "end": unpack(pool.end) - 1})
            session.add(newAllocation)
        else:
            allocated = self._new_allocation({"start": unpack(pool.start), "end": unpack(pool.start)})
            allocated.used = True
            allocated.value = value
            session.add(allocated)
            newAllocation = self._new_allocation({"start": unpack(pool.start) + 1, "end": unpack(pool.end)})
            session.add(newAllocation)
        session.commit()
        return unpack(allocated.start)

    def reserve(self, idx, value=None):
       return self.set_in_use(idx, value=value)

    def delete(self, idx):
       self.reset_in_use(idx)

    @use_session
    def read(self, idx):
        session = self.session_ctx
        allocation = session.query(IDPool).filter(and_(
                            IDPool.path == self.path,
                            IDPool.start == pack(idx),
                            pack(idx) == IDPool.end,
                            IDPool.used == True)).first()
        if allocation:
            return allocation.value

    def empty(self):
       return self.get_alloc_count() == 0

    @classmethod
    def delete_all(cls, db, path):
       session = sessionmaker(bind=db)()
       session.query(IDPool).filter(IDPool.path == path).delete()
       session.commit()

init_done = False

def to_obj_type(res_type):
    return res_type.replace('-', '_')

def to_share_type(obj_type):
    return 'ref_share_%s' % obj_type

def to_ref_type(obj_type, ref_type):
    return 'ref_%s_%s' %(obj_type, ref_type)

def _foreign_key(obj_type):
    return ForeignKey('%s.obj_uuid' %(obj_type))

def _foreign_key_cascade(obj_type):
    return ForeignKey('%s.obj_uuid' %(obj_type), ondelete="CASCADE")

def to_list_type(obj_type, property):
    return 'ref_list_%s_%s' %(obj_type, property)

def to_map_type(obj_type, property):
    return 'ref_map_%s_%s' %(obj_type, property)

class VncRDBMSClient(object):
    sqa_classes = {}
    _SUBNET_PATH = "/api-server/subnets"
    _FQ_NAME_TO_UUID_PATH = "/fq-name-to-uuid"
    _MAX_SUBNET_ADDR_ALLOC = 65535

    _VN_ID_ALLOC_PATH = "/id/virtual-networks/"
    _VN_MAX_ID = 1 << 24

    _SG_ID_ALLOC_PATH = "/id/security-groups/id/"
    _SG_MAX_ID = 1 << 32

    @classmethod
    def create_sqalchemy_models(cls):
        global init_done
        if init_done:
            return
        # Create tables per type in rdbms
        for res_type in vnc_api_client_gen.all_resource_types:
            obj_type = to_obj_type(res_type)
            obj_class = vnc_api.get_object_class(res_type)
            obj_class_name = cfgm_common.utils.CamelCase(res_type)
            # Table for OBJ with PROPs
            sqa_class = type('Sqa'+obj_class_name,
                             (SqaObjectBase, Base),
                             {'__tablename__': obj_type})
            cls.sqa_classes[obj_type] = sqa_class

            sqa_share_table_name = to_share_type(obj_type)
            owner_column = Column(String(36),
                                    _foreign_key_cascade(obj_type),
                                    primary_key=True)
            sqa_share_class = type('SqaShare%s' % obj_class_name,
                                    (SqaShare, Base),
                                    {'__tablename__': sqa_share_table_name,
                                    'owner': owner_column})
            cls.sqa_classes[sqa_share_table_name] = sqa_share_class
            for prop_field in obj_class.prop_fields:
                if prop_field in obj_class.prop_list_fields:
                    sqa_table_name = to_list_type(obj_type, prop_field)
                    owner_column = Column(String(36),
                                        _foreign_key_cascade(obj_type),
                                        primary_key=True)
                    sqa_list_class = type(str('SqaPropertyList%s%s' %(obj_class_name,
                                                        prop_field)),
                                    (SqaList, Base),
                                    {'__tablename__': sqa_table_name,
                                     'property': prop_field,
                                     'owner': owner_column})
                    cls.sqa_classes[sqa_table_name] = sqa_list_class
                elif prop_field in obj_class.prop_map_fields:
                    sqa_table_name = to_map_type(obj_type, prop_field)
                    owner_column = Column(String(36),
                                        _foreign_key_cascade(obj_type),
                                        primary_key=True)
                    sqa_map_class = type(str('SqaPropertyMap%s%s' %(obj_class_name,
                                                        prop_field)),
                                    (SqaMap, Base),
                                    {'__tablename__': sqa_table_name,
                                    'property': prop_field,
                                    'owner': owner_column})
                    cls.sqa_classes[sqa_table_name] = sqa_map_class
                else:
                    #TODO(nati) choose proper column type per object
                    setattr(sqa_class, prop_field,
                       Column(Text(), nullable=True))

        for res_type in vnc_api_client_gen.all_resource_types:
            obj_type = to_obj_type(res_type)
            obj_class = vnc_api.get_object_class(res_type)
            obj_class_name = cfgm_common.utils.CamelCase(res_type)
            # Table for OBJ with REFs
            for ref_field in obj_class.ref_fields:
                ref_type = ref_field[:-5] # string trailing _refs
                ref_class_name = cfgm_common.utils.CamelCase(ref_type)
                sqa_table_name = to_ref_type(obj_type, ref_type)
                from_obj_uuid= Column(String(36),
                                      _foreign_key(obj_type),
                                      primary_key=True)
                to_obj_uuid= Column(String(36),
                                      _foreign_key(ref_type),
                                      primary_key=True)
                sqa_class = type(str('SqaRef%s%s' %(obj_class_name,
                                                    ref_class_name)),
                                 (SqaObjectRef, Base),
                                 {'__tablename__': sqa_table_name,
                                  'from_obj_uuid': from_obj_uuid,
                                  'to_obj_uuid': to_obj_uuid,
                                  'ref_type': ref_type,
                                  'backref_type': obj_type,
                                  'from_obj': relationship(
                                     cls.sqa_classes[obj_type],
                                     foreign_keys=[from_obj_uuid]),
                                  'to_obj': relationship(
                                     cls.sqa_classes[ref_type],
                                     foreign_keys=[to_obj_uuid]),
                                  })
                cls.sqa_classes[sqa_table_name] = sqa_class
        # end pass to create all models for refs
        init_done = True
    # end create_sqalchemy_models

    def scan_table_schema(self):
        session = self.session_ctx
        tables = {}
        tableRows = session.execute("show tables")
        for tableRow in tableRows:
            propertyRows = session.execute("desc %s" % tableRow[0])
            schema = {}
            tables[tableRow[0]] = schema
            for propertyRow in propertyRows:
                schema[propertyRow[0]] = propertyRow[1]
        return tables

    @use_session
    # auto migration code. Note that this method scan current table structure,
    # then add column if it is missing. Only works for MySQL DB
    def auto_migration(self):
        tables = self.scan_table_schema()
        session = self.session_ctx
        for res_type in vnc_api_client_gen.all_resource_types:
            obj_type = to_obj_type(res_type)
            obj_class = vnc_api.get_object_class(res_type)
            table = tables[obj_type]
            for prop_field in obj_class.prop_fields:
                if not table.get(prop_field):
                    session.execute("alter table %s add %s varchar(1024)" % (obj_type, prop_field))
        session.commit()
        # We don't expect adding column for ref table because basically it is json type field.

    def __init__(self, server_list=None, db_prefix=None,
            logger=None, generate_url=None, connection=None, reset_config=False, credential=None,
            obj_cache_entries=0):
        if not credential:
            credential = {"username": "root", "password": "secret" }
        username = credential.get('username')
        password = credential.get('password')
        engine_args = {'echo': False}
        if not connection:
            connection = 'mysql://%s:%s@%s/contrail' %( username, password, server_list[0] )

        is_mysql = connection.startswith("mysql")
        if is_mysql:
            engine_args = {
                'pool_recycle': 3600,
                'max_overflow': 20,
                'pool_timeout': 10,
                'convert_unicode': True,
                'pool_size': 10,
                'echo': False,
            }
        engine = create_engine(connection, **engine_args)
        self.db = engine
        self.create_sqalchemy_models()
        Base.metadata.create_all(engine)
        self.Session = sessionmaker(bind=engine)
        self.session_ctx = None
        if is_mysql:
            # TODO(nati) make auto db migration optional
            self.auto_migration()

        self._cache_uuid_to_fq_name = {}
        self._subnet_allocators = {}

        # Initialize the virtual network ID allocator
        self._vn_id_allocator = RDBMSIndexAllocator(engine,
                                               self._VN_ID_ALLOC_PATH,
                                               self._VN_MAX_ID)

        # Initialize the security group ID allocator
        self._sg_id_allocator = RDBMSIndexAllocator(engine,
                                               self._SG_ID_ALLOC_PATH,
                                               self._SG_MAX_ID)
        # 0 is not a valid sg id any more. So, if it was previously allocated,
        # delete it and reserve it
        if self._sg_id_allocator.read(0) != '__reserved__':
            self._sg_id_allocator.delete(0)
        self._sg_id_allocator.reserve(0, '__reserved__')
        self._generate_url = generate_url or (lambda x,y: '')
    # end __init__

    def _get_resource_class(self, obj_type):
        cls_name = '%s' %(cfgm_common.utils.CamelCase(obj_type.replace('-', '_')))
        return getattr(vnc_api, cls_name)
    # end _get_resource_class

    def _get_xsd_class(self, xsd_type):
        return getattr(vnc_api, xsd_type)
    # end _get_xsd_class

    @use_session
    def update_last_modified(self, obj_type, obj_uuid):
        session = self.session_ctx
        sqa_class = self.sqa_classes[obj_type]
        sqa_obj = session.query(sqa_class).filter_by(obj_uuid=obj_uuid).one()

        new_id_perms = json.loads(sqa_obj.id_perms)
        new_id_perms['last_modified'] = datetime.utcnow().isoformat()
        sqa_obj.id_perms = json.dumps(new_id_perms)

        session.add(sqa_obj)

    # end update_last_modified

    @use_session
    def object_create(self, res_type, obj_id, obj_dict):
        obj_type = to_obj_type(res_type)
        obj_class = self._get_resource_class(obj_type)
        self.cache_uuid_to_fq_name_add(obj_id, obj_dict['fq_name'], obj_type)

        session = self.session_ctx
        # create obj_base
        obj_fq_name_json = json.dumps(obj_dict['fq_name'])
        if 'parent_type' in obj_dict:
            # non config-root child
            parent_type = obj_dict['parent_type']
            if parent_type not in obj_class.parent_types:
                return False, (400, 'Invalid parent type: %s' % parent_type)
            parent_method_type = to_obj_type(parent_type)
            parent_fq_name = obj_dict['fq_name'][:-1]
            parent_uuid = self.fq_name_to_uuid(parent_method_type, parent_fq_name)
        else: # no parent
            parent_type = parent_uuid = ''

        now = datetime.utcnow()
        epoch = (time.mktime(now.timetuple()) + now.microsecond*1e-6) * 1e6
        sqa_class = self.sqa_classes[obj_type]
        object_metadata = ObjectMetadata(obj_uuid=obj_id,
                         obj_type=obj_type,
                         obj_fq_name=obj_fq_name_json)
        session.add(object_metadata)

        sqa_obj = sqa_class(obj_uuid=obj_id,
                         obj_type=obj_type,
                         obj_fq_name=obj_fq_name_json,
                         obj_parent_type=parent_type,
                         obj_parent_uuid=parent_uuid,
                         meta_create_epoch=epoch,
                         meta_prop_ref_update_epoch=epoch,
                         meta_update_epoch=epoch)

        # create obj_props
        for prop_field in obj_class.prop_fields:
            prop_value = obj_dict.get(prop_field)
            if prop_value is None:
                continue
            if prop_field == 'id_perms':
                prop_value['created'] = datetime.utcnow().isoformat()
                prop_value['last_modified'] = prop_value['created']

            if prop_field in obj_class.prop_list_fields:
                pass
            elif prop_field in obj_class.prop_map_fields:
                pass
            else:
                setattr(sqa_obj, prop_field, json.dumps(prop_value))
        # end handled props
        session.add(sqa_obj)

        # create obj_props
        for prop_field in obj_class.prop_fields:
            prop_value = obj_dict.get(prop_field)
            if not prop_value:
                continue
            if prop_field == 'perms2':
                sqa_obj.perms2_owner = prop_value.get('perms2')
                sqa_obj.perms2_owner_accesss = prop_value.get('owner_access')
                sqa_obj.perms2_global_accesss = prop_value.get('global_access')

                sqa_share_class = self.sqa_classes[to_share_type(obj_type)]
                for shared in prop_value.get('shared', []):
                    sqa_share_obj = sqa_share_class(
                        owner=obj_id,
                        tenant=shared.tenant,
                        tenant_access=shared.tenant_access,
                    )
                    session.add(sqa_share_obj)
                continue
            if prop_field in obj_class.prop_list_fields:
                if obj_class.prop_list_field_has_wrappers[prop_field]:
                    wrapper_field = prop_value.keys()[0]
                    prop_value = prop_value[wrapper_field]
                list_table = to_list_type(obj_type, prop_field)
                list_sqa_class = self.sqa_classes[list_table]
                for i, value in enumerate(prop_value):
                    element_obj = list_sqa_class(
                        index=i,
                        owner=obj_id,
                        value=json.dumps(value)
                    )
                    session.add(element_obj)
            elif prop_field in obj_class.prop_map_fields:
                if obj_class.prop_map_field_has_wrappers[prop_field]:
                    wrapper_field = prop_value.keys()[0]
                    prop_value = prop_value[wrapper_field]
                map_table = to_map_type(obj_type, prop_field)
                map_sqa_class = self.sqa_classes[map_table]

                map_key_name = obj_class.prop_map_field_key_names[prop_field]
                for data in prop_value:
                    element_obj = map_sqa_class(
                        key=data[map_key_name],
                        owner=obj_id,
                        value=json.dumps(data)
                    )
                    session.add(element_obj)

        # create obj_refs
        for ref_field in obj_class.ref_fields:
            ref_type = ref_field[:-5] # string trailing _refs
            refs = obj_dict.get(ref_field, [])
            sqa_ref_class = self.sqa_classes[to_ref_type(obj_type, ref_type)]
            for ref in refs:
                try:
                    ref_uuid = ref['uuid']
                except KeyError:
                    ref_uuid = self.fq_name_to_uuid(ref_type, ref['to'])
                ref_attr = ref.get('attr')
                ref_value = {'attr': ref_attr, 'is_weakref': False}
                sqa_ref_obj = sqa_ref_class(from_obj_uuid=obj_id, to_obj_uuid=ref_uuid,
                                    ref_value=json.dumps(ref_value))
                session.add(sqa_ref_obj)
                # TODO update epoch on parent and refs
        # end handled refs

        session.commit()

        return (True, '')
    # end object_create

    def _wrapper_field(self, obj_class, prop_name):
        prop_field_types = obj_class.prop_field_types[prop_name]
        wrapper_type = prop_field_types['xsd_type']
        wrapper_cls = self._get_xsd_class(wrapper_type)
        return wrapper_cls.attr_fields[0]

    def _format_rows(self, res_type, rows, extra_sqa_classes=None, req_prop_fields=None):
        obj_class = self._get_resource_class(res_type)
        objs_dict = {}
        for base_and_extra_objs in rows:
            if not extra_sqa_classes:
                sqa_obj = base_and_extra_objs
                sqa_extra_objs = []
            else:
                sqa_obj = base_and_extra_objs[0]
                sqa_extra_objs = base_and_extra_objs[1:]

            obj_uuid = sqa_obj.obj_uuid
            obj_dict = objs_dict.get(obj_uuid,
                                     {'uuid': obj_uuid,
                                      'fq_name': json.loads(sqa_obj.obj_fq_name)})

            if sqa_obj.obj_parent_uuid:
                parent_type = sqa_obj.obj_parent_type
                parent_uuid = sqa_obj.obj_parent_uuid
                obj_dict.update(
                    {'parent_type': parent_type,
                     'parent_uuid': parent_uuid,
                     'parent_href': self._generate_url(parent_type,
                                                       parent_uuid)})
            for prop_field in req_prop_fields:
                if prop_field in obj_class.prop_list_fields:
                    continue
                elif prop_field in obj_class.prop_map_fields:
                    continue

                prop_value_json = getattr(sqa_obj, prop_field)
                if prop_value_json != None:
                    obj_dict[prop_field] = json.loads(prop_value_json)

            objs_dict[obj_uuid] = obj_dict

            for extra_obj in sqa_extra_objs:
                if not extra_obj:
                    continue
                if hasattr(extra_obj, 'from_obj_uuid') and extra_obj.from_obj_uuid == obj_uuid:
                    ref_uuid = extra_obj.to_obj_uuid
                    ref_type = extra_obj.ref_type
                    ref_value = json.loads(extra_obj.ref_value)
                    ref_attr = ref_value.get('attr')
                    ref_info = {}
                    try:
                        ref_info['to'] = self.uuid_to_fq_name(ref_uuid)
                    except NoIdError as e:
                        ref_info['to'] = ['ERROR']

                    if ref_attr:
                        ref_info['attr'] = ref_attr
                    ref_info['href'] = self._generate_url(ref_type, ref_uuid)
                    ref_info['uuid'] = ref_uuid

                    try:
                        obj_dict['%s_refs' %(ref_type)].append(ref_info)
                    except KeyError:
                        obj_dict['%s_refs' %(ref_type)] = [ref_info]
                elif hasattr(extra_obj, 'to_obj_uuid') and extra_obj.to_obj_uuid == obj_uuid: # backref
                    backref_uuid = extra_obj.from_obj_uuid
                    backref_type = self.uuid_to_obj_type(backref_uuid)
                    backref_value = json.loads(extra_obj.ref_value)
                    backref_attr = backref_value.get('attr')

                    backref_info = {}
                    try:
                        backref_info['to'] = self.uuid_to_fq_name(backref_uuid)
                    except NoIdError as e:
                        backref_info['to'] = ['ERROR']

                    if backref_attr:
                        backref_info['attr'] = backref_attr
                    backref_info['href'] = self._generate_url(
                                               backref_type, backref_uuid)
                    backref_info['uuid'] = backref_uuid

                    try:
                        obj_dict['%s_back_refs' %(backref_type)].append(backref_info)
                    except KeyError:
                        obj_dict['%s_back_refs' %(backref_type)] = [backref_info]
                elif hasattr(extra_obj, 'key') and extra_obj.owner == obj_uuid: # map
                    prop_name = extra_obj.property
                    if obj_class.prop_map_field_has_wrappers[prop_name]:
                        wrapper_field = self._wrapper_field(obj_class, prop_name)
                        if prop_name not in obj_dict:
                            obj_dict[prop_name] = {wrapper_field: []}
                        obj_dict[prop_name][wrapper_field].append(json.loads(extra_obj.value))
                    else:
                        if prop_name not in obj_dict:
                            obj_dict[prop_name] = []
                        obj_dict[prop_name][extra_obj.key].append(json.loads(extra_obj.value))
                elif hasattr(extra_obj, 'index') and extra_obj.owner == obj_uuid: # list
                    prop_name = extra_obj.property
                    if obj_class.prop_list_field_has_wrappers[prop_name]:
                        wrapper_field = self._wrapper_field(obj_class, prop_name)
                        if prop_name not in obj_dict:
                            obj_dict[prop_name] = {wrapper_field: []}
                        obj_dict[prop_name][wrapper_field].append(json.loads(extra_obj.value))
                    else:
                        if prop_name not in obj_dict:
                            obj_dict[prop_name] = []
                        obj_dict[prop_name].append(json.loads(extra_obj.value))
        return objs_dict

    @use_session
    def object_read(self, res_type, obj_uuids, field_names=None, ret_readonly=False):
        obj_type = to_obj_type(res_type)
        obj_class = self._get_resource_class(res_type)
        sqa_class = self.sqa_classes[obj_type]
        field_names_set = set(field_names or [])
        req_backref_fields = req_children_fields = set([])
        req_map_fields = set([])
        req_list_fields = set([])

        if not field_names_set:
            req_prop_fields = obj_class.prop_fields
            req_ref_fields = obj_class.ref_fields
            req_backref_fields = obj_class.backref_fields
            req_children_fields = obj_class.children_fields
            req_map_fields = obj_class.prop_map_fields
            req_list_fields = obj_class.prop_list_fields
        elif (field_names_set & (obj_class.backref_fields |
                                 obj_class.children_fields)):
            req_prop_fields = obj_class.prop_fields
            req_ref_fields = obj_class.ref_fields
            req_backref_fields = field_names_set & obj_class.backref_fields
            req_children_fields = field_names_set & obj_class.children_fields
            req_map_fields = field_names_set & obj_class.prop_map_fields
            req_list_fields = field_names_set & obj_class.prop_list_fields
        else:
            req_prop_fields = (field_names_set &
                               obj_class.prop_fields) | set(['id_perms'])
            req_ref_fields = (field_names_set &
                              obj_class.ref_fields)
            req_backref_fields = field_names_set & obj_class.backref_fields
            req_children_fields = field_names_set & obj_class.children_fields
            req_map_fields = field_names_set & obj_class.prop_map_fields
            req_list_fields = field_names_set & obj_class.prop_list_fields
        session = self.session_ctx

        sqa_ref_classes = [aliased(self.sqa_classes['ref_%s_%s' %(
                             obj_type, ref_field[:-5])])
                             for ref_field in req_ref_fields]
        sqa_backref_classes = [aliased(self.sqa_classes['ref_%s_%s' %(
             backref_field[:-10], obj_type)])
             for backref_field in req_backref_fields]

        sqa_list_classes = [aliased(self.sqa_classes[to_list_type(obj_type, prop_field)])
             for prop_field in req_list_fields]
        sqa_map_classes = [aliased(self.sqa_classes[to_map_type(obj_type, prop_field)])
             for prop_field in req_map_fields]

        extra_sqa_classes = sqa_ref_classes + sqa_backref_classes + sqa_map_classes + sqa_list_classes
        extra_join_clauses = [(cls, sqa_class.obj_uuid == cls.from_obj_uuid)
                        for cls in sqa_ref_classes]
        extra_join_clauses.extend([(cls, sqa_class.obj_uuid == cls.to_obj_uuid)
                              for cls in sqa_backref_classes])
        extra_join_clauses.extend([(cls, sqa_class.obj_uuid == cls.owner)
                              for cls in sqa_list_classes])
        extra_join_clauses.extend([(cls, sqa_class.obj_uuid == cls.owner)
                              for cls in sqa_map_classes])
        sqa_base_and_extra_objs_list = session.query(
            sqa_class, *extra_sqa_classes).outerjoin(
               *extra_join_clauses).filter(
                    sqa_class.obj_uuid.in_(obj_uuids)).all()

        objs_dict = self._format_rows(
            res_type, sqa_base_and_extra_objs_list,
            extra_sqa_classes, req_prop_fields)

        if req_children_fields:
           sqa_children_objs = self.get_children(
               obj_type, obj_uuids, req_children_fields)
        else:
            sqa_children_objs = []

        for sqa_child_obj in sqa_children_objs:
           child_uuid = sqa_child_obj.obj_uuid
           child_type = sqa_child_obj.obj_type
           obj_uuid = sqa_child_obj.obj_parent_uuid
           obj_dict = objs_dict[obj_uuid]
           child_info = {
               'to': self.uuid_to_fq_name(child_uuid),
               'href': self._generate_url(child_type, child_uuid),
               'uuid': child_uuid,
           }
           try:
               obj_dict['%ss' % (child_type)].append(child_info)
           except KeyError:
               obj_dict['%ss' % (child_type)] = [child_info]

        return (True, objs_dict.values())
    # end object_read

    @use_session
    def object_update(self, res_type, obj_uuid, new_obj_dict):
        obj_type = to_obj_type(res_type)
        obj_class = self._get_resource_class(obj_type)
        sqa_class = self.sqa_classes[obj_type]
        new_ref_infos = {}
        new_props = {}
        for prop_field in obj_class.prop_fields:

            if prop_field in new_obj_dict:
                new_props[prop_field] = new_obj_dict[prop_field]

        for ref_field in obj_class.ref_fields:
            ref_type, ref_link_type, _, _ = \
                obj_class.ref_field_types[ref_field]
            ref_obj_type = to_obj_type(ref_type)

            if ref_field in new_obj_dict:
                new_refs = new_obj_dict[ref_field]
                new_ref_infos[ref_obj_type] = {}
                for new_ref in new_refs or []:
                    new_ref_uuid = self.fq_name_to_uuid(ref_type, new_ref['to'])
                    new_ref_attr = new_ref.get('attr')
                    new_ref_data = {'attr': new_ref_attr}
                    new_ref_infos[ref_obj_type][new_ref_uuid] = new_ref_data

        session = self.session_ctx
        sqa_obj = session.query(sqa_class).filter_by(obj_uuid=obj_uuid).one()
        for prop_field, prop_value in new_props.items():
            if prop_field == 'perms2':
                sqa_obj.perms2_owner = prop_value.get('perms2')
                sqa_obj.perms2_owner_accesss = prop_value.get('owner_access')
                sqa_obj.perms2_global_accesss = prop_value.get('global_access')

                sqa_share_class = self.sqa_classes[to_share_type(obj_type)]
                session.query(sqa_share_class).filter_by(owner=obj_uuid).delete()
                for shared in prop_value.get('shared', []):
                    sqa_share_obj = sqa_share_class(
                        owner=obj_id,
                        tenant=shared.tenant,
                        tenant_access=shared.tenant_access,
                    )
                    session.add(sqa_share_obj)

            if prop_field in obj_class.prop_list_fields:
                if obj_class.prop_list_field_has_wrappers[prop_field]:
                    wrapper_field = prop_value.keys()[0]
                    prop_value = prop_value[wrapper_field]
                list_table = to_list_type(obj_type, prop_field)
                list_sqa_class = self.sqa_classes[list_table]
                session.query(list_sqa_class).filter_by(owner=obj_uuid).delete()
                for i, value in enumerate(prop_value):
                    element_obj = list_sqa_class(
                        index=i,
                        owner=obj_uuid,
                        value=json.dumps(value)
                    )
                    session.add(element_obj)
            elif prop_field in obj_class.prop_map_fields:
                if obj_class.prop_map_field_has_wrappers[prop_field]:
                    wrapper_field = prop_value.keys()[0]
                    prop_value = prop_value[wrapper_field]
                map_table = to_map_type(obj_type, prop_field)
                map_sqa_class = self.sqa_classes[map_table]
                session.query(map_sqa_class).filter_by(owner=obj_uuid).delete()

                map_key_name = obj_class.prop_map_field_key_names[prop_field]
                for data in prop_value:
                    element_obj = map_sqa_class(
                        key=data[map_key_name],
                        owner=obj_uuid,
                        value=json.dumps(data)
                    )
                    session.add(element_obj)
            else:
                setattr(sqa_obj, prop_field, json.dumps(prop_value))

        new_id_perms = json.loads(sqa_obj.id_perms)
        new_id_perms['last_modified'] = datetime.utcnow().isoformat()
        sqa_obj.id_perms = json.dumps(new_id_perms)

        new_ref_types = new_ref_infos.keys()
        if not new_ref_types:
            sqa_ref_objs = []
        else:
            sqa_ref_objs, _ = self.get_refs_backrefs(obj_type, [obj_uuid],
                               new_ref_types, None)

        for sqa_ref_obj in sqa_ref_objs:
            ref_type = sqa_ref_obj.ref_type
            ref_uuid = sqa_ref_obj.to_obj_uuid

            if len(new_ref_infos) == 0:
                session.delete(sqa_ref_obj)
                continue

            new_ref_info = new_ref_infos.pop(ref_type)
            if ref_uuid not in new_ref_info:
                session.delete(sqa_ref_obj)
            else:
                new_ref_value = json.loads(sqa_ref_obj.ref_value)
                try:
                    new_ref_value['attr'] = new_ref_info[ref_uuid]['attr']
                    sqa_ref_obj.ref_value = json.dumps(new_ref_value)
                except KeyError:
                    # nothing changed, TODO allow is_weakref to be updated here?
                    pass

        for ref_type in new_ref_infos:
            sqa_ref_class = self.sqa_classes[ref_type(obj_type, ref_type)]
            for new_ref_uuid, ref_info in new_ref_infos[ref_type]:
                new_ref_value = {'is_weakref': False}
                try:
                    new_ref_value['attr'] = ref_info['attr']
                except KeyError:
                    pass # TODO add support and complain with should_have_attr

                new_sqa_ref_obj = sqa_ref_class(from_obj_uuid=obj_uuid,
                                        to_obj_uuid=new_ref_uuid,
                                        ref_value=json.dumps(new_ref_value))
                session.add(new_sqa_ref_obj)

        # TODO update epoch on refs

        session.commit()

        return (True, '')
    # end object_update
    @use_session
    def object_list(self, res_type, parent_uuids=None, back_ref_uuids=None,
                     obj_uuids=None, count=False, filters=None, field_names=None,
                     is_detail=False, tenant_id=None, domain=None):
        obj_type = to_obj_type(res_type)
        obj_class = self._get_resource_class(obj_type)
        sqa_class = self.sqa_classes[obj_type]
        sqa_share_class = aliased(self.sqa_classes[to_share_type(obj_type)])
        resource_type = obj_class.resource_type
        session = self.session_ctx
        field_names_set = set(field_names or [])

        req_prop_fields = obj_class.prop_fields
        req_ref_fields = obj_class.ref_fields
        req_backref_fields = field_names_set & obj_class.backref_fields
        req_children_fields = field_names_set & obj_class.children_fields
        req_map_fields = field_names_set & obj_class.prop_map_fields
        req_list_fields = field_names_set & obj_class.prop_list_fields
        if is_detail or back_ref_uuids or field_names_set:
            sqa_ref_classes = [self.sqa_classes['ref_%s_%s' %(
                                obj_type, ref_field[:-5])]
                                for ref_field in req_ref_fields]
            sqa_backref_classes = [self.sqa_classes['ref_%s_%s' %(
                backref_field[:-10], obj_type)]
                for backref_field in req_backref_fields]
            sqa_list_classes = [aliased(self.sqa_classes[to_list_type(obj_type, prop_field)])
                 for prop_field in req_list_fields]
            sqa_map_classes = [aliased(self.sqa_classes[to_map_type(obj_type, prop_field)])
                 for prop_field in req_map_fields]

            extra_sqa_classes = sqa_ref_classes + sqa_backref_classes + sqa_list_classes + sqa_map_classes
            extra_join_clauses = [(cls, sqa_class.obj_uuid == cls.from_obj_uuid)
                            for cls in sqa_ref_classes]
            extra_join_clauses.extend([(cls, sqa_class.obj_uuid == cls.to_obj_uuid)
                                for cls in sqa_backref_classes])
            extra_join_clauses.extend([(cls, sqa_class.obj_uuid == cls.owner)
                              for cls in sqa_list_classes])
            extra_join_clauses.extend([(cls, sqa_class.obj_uuid == cls.owner)
                              for cls in sqa_map_classes])

            if tenant_id or domain:
                extra_sqa_classes.append(sqa_share_class)
                extra_join_clauses.append((sqa_share_class, sqa_class.obj_uuid == sqa_share_class.owner))

            sqa_objs = session.query(sqa_class, *extra_sqa_classes).outerjoin(
                            *extra_join_clauses)
        else:
            extra_sqa_classes = None
            sqa_objs = session.query(
                sqa_class.obj_uuid,
                sqa_class.obj_fq_name,
                sqa_class.id_perms)


        if back_ref_uuids:
            ref_query = []
            for ref_field in obj_class.ref_fields:
                ref_type = ref_field[:-5] # string trailing _refs
                sqa_ref_class = self.sqa_classes[to_ref_type(obj_type, ref_type)]
                ref_query.append(sqa_ref_class.to_obj_uuid.in_(back_ref_uuids))

            sqa_objs = sqa_objs.filter(or_(f for f in ref_query))

        if obj_uuids:
            sqa_objs = sqa_objs.filter(
                sqa_class.obj_uuid.in_(obj_uuids))

        if parent_uuids:
            sqa_objs = sqa_objs.filter(
                sqa_class.obj_parent_uuid.in_(parent_uuids))

        shared_query = [sqa_class.perms2_owner==tenant_id,
                        sqa_class.perms2_global_access >= 4]
        if tenant_id:
            shared_query.append(
                and_(
                    sqa_share_class.tenant=="tenant:" + tenant_id,
                    sqa_share_class.tenant_access > 4
            ))

        if domain:
            shared_query.append(
                and_(
                    sqa_share_class.tenant=="domain:" + domain,
                    sqa_share_class.tenant_access > 4
            ))

        sqa_objs = sqa_objs.filter(or_(f for f in shared_query))

        if filters:
            for key, value in filters.iteritems():
                if key not in obj_class.prop_fields:
                    continue
                value = [json.dumps(v) for v in value]
                sqa_objs = sqa_objs.filter(getattr(sqa_class, key).in_(value))

        if count:
            return (True, sqa_objs.count())
        rows = sqa_objs.all()
        if is_detail or extra_sqa_classes:
            objs_dict = self._format_rows(
                res_type, rows,
                extra_sqa_classes, req_prop_fields)

            if req_children_fields:
                sqa_children_objs = self.get_children(
                    obj_type, obj_uuids, req_children_fields)
            else:
                sqa_children_objs = []

            for sqa_child_obj in sqa_children_objs:
                child_uuid = sqa_child_obj.obj_uuid
                child_type = sqa_child_obj.obj_type
                obj_uuid = sqa_child_obj.obj_parent_uuid
                obj_dict = objs_dict[obj_uuid]
                child_info = {
                    'to': self.uuid_to_fq_name(child_uuid),
                    'href': self._generate_url(child_type, child_uuid),
                    'uuid': child_uuid,
                }
                try:
                    obj_dict['%ss' % (child_type)].append(child_info)
                except KeyError:
                    obj_dict['%ss' % (child_type)] = [child_info]
            objs = []
            for detail_obj in objs_dict.values():
                if is_detail:
                    objs.append(detail_obj)
                else:
                    obj = {
                            "uuid": detail_obj["uuid"],
                            "fq_name": detail_obj["fq_name"],
                            "href": self._generate_url(res_type, detail_obj["uuid"]),
                            "id_perms": detail_obj["id_perms"]
                    }
                    for field in field_names_set:
                        if field in detail_obj:
                           obj[field] = detail_obj[field]

                    objs.append(obj)
        else:
            objs = []
            for row in rows:
                obj = {
                        "uuid": row.obj_uuid,
                        "fq_name": json.loads(row.obj_fq_name),
                        "href": self._generate_url(res_type, row.obj_uuid),
                        "id_perms": json.loads(row.id_perms)
                }
                for field in field_names_set:
                    obj[field] = json.loads(getattr(row, field))
                objs.append(obj)
        return (True, objs)
    # end object_list

    @use_session
    def object_delete(self, res_type, obj_uuid):
        obj_type = to_obj_type(res_type)
        obj_class = self._get_resource_class(obj_type)
        sqa_class = self.sqa_classes[obj_type]

        session = self.session_ctx
        for ref_field in obj_class.ref_fields:
            ref_type = ref_field[:-5] # string trailing _refs
            sqa_ref_class = self.sqa_classes[to_ref_type(obj_type, ref_type)]
            for sqa_ref_obj in session.query(sqa_ref_class).filter_by(
                               from_obj_uuid=obj_uuid).all():
                session.delete(sqa_ref_obj)

        sqa_share_class = self.sqa_classes[to_share_type(obj_type)]
        session.query(sqa_share_class).filter_by(owner=obj_uuid).delete()

        sqa_obj = session.query(sqa_class).filter_by(
                       obj_uuid=obj_uuid).one()

        # TODO update epoch of ref'd and parent
        session.delete(sqa_obj)

        object_metadata = session.query(ObjectMetadata).filter_by(
                       obj_uuid=obj_uuid).one()
        session.delete(object_metadata)
        session.commit()

        return (True, '')
    # end object_delete

    @use_session
    def ref_update(self, obj_type, obj_uuid, ref_type, ref_uuid, ref_data, operation):
        session = self.session_ctx
        sqa_class = self.sqa_classes[to_ref_type(obj_type, ref_type)]

        if operation == 'ADD':
            try:
                sqa_ref_obj = session.query(sqa_class).filter_by(
                    from_obj_uuid=obj_uuid, to_obj_uuid=ref_uuid).one()
                session.delete(sqa_ref_obj)
            except NoResultFound:
                pass
            sqa_ref_obj = sqa_class(from_obj_uuid=obj_uuid,
                                    to_obj_uuid=ref_uuid,
                                    ref_value=json.dumps(ref_data))
            session.add(sqa_ref_obj)
        elif operation == 'DELETE':
            sqa_ref_obj = session.query(sqa_class).filter_by(
                from_obj_uuid=obj_uuid, to_obj_uuid=ref_uuid).one()
            session.delete(sqa_ref_obj)
        else:
            pass

        self.update_last_modified(obj_type, obj_uuid)
        session.commit()
    # end ref_update

    @use_session
    def ref_relax_for_delete(self, obj_uuid, ref_uuid):
        session = self.session_ctx
        obj_type = self.uuid_to_obj_type(obj_uuid)
        ref_type = self.uuid_to_obj_type(ref_uuid)
        sqa_class = self.sqa_classes[to_ref_type(obj_type, ref_type)]

        sqa_ref_obj = session.query(sqa_class).filter_by(
                         from_obj_uuid=obj_uuid,
                         to_obj_uuid=ref_uuid).one()
        sqa_ref_obj.is_relaxed_ref = True
        session.commit()
    # end ref_relax_for_delete

    @use_session
    def get_relaxed_refs(self, obj_uuid):
        session = self.session_ctx
        obj_type = self.uuid_to_obj_type(obj_uuid)
        obj_class = self._get_resource_class(obj_type)

        sqa_backref_objs = []
        # strip trailing _back_refs
        backref_types = [x[:-10] for x in obj_class.backref_fields]
        for backref_type in backref_types:
            sqa_class = self.sqa_classes[to_ref_type(backref_type, obj_type)]
            try:
                sqa_backref_objs.extend(session.query(sqa_class).filter_by(
                    to_obj_uuid=obj_uuid,
                    is_relaxed_ref=True).all())
            except NoResultFound:
                pass

        return [r.from_obj_uuid for r in sqa_backref_objs]
    # end get_relaxed_refs

    @use_session
    def get_all_sqa_objs_info(self):
        session = self.session_ctx

        ret_objs_info = {}
        for sqa_class_name, sqa_class in self.sqa_classes.items():
            if sqa_class_name.startswith('ref_'):
                continue
            obj_type = sqa_class_name
            objs_info = session.query(
                sqa_class.obj_uuid, sqa_class.obj_fq_name).all()
            if not objs_info:
                continue
            ret_objs_info[obj_type] = objs_info

        return ret_objs_info
    # end get_all_sqa_objs_info

    @use_session
    def get_refs_backrefs(self, obj_type, obj_uuids, ref_fields, backref_fields):
        session = self.session_ctx
        sqa_ref_objs = []

        if ref_fields:
            #ref_types = [x[:-5] for x in ref_fields] # strip trailing _refs
            for ref_type in ref_fields:
                sqa_ref_class = self.sqa_classes[to_ref_type(obj_type, ref_type)]
                sqa_ref_objs.extend(session.query(sqa_ref_class).filter(
                    sqa_ref_class.from_obj_uuid.in_(obj_uuids)).all())

        sqa_backref_objs = []
        if backref_fields:
            backref_types = [x[:-10] for x in backref_fields] # strip trailing _back_refs
            for backref_type in backref_types:
                sqa_backref_class = self.sqa_classes['ref_%s_%s' %(
                                         backref_type, obj_type)]
                sqa_backref_objs.extend(
                    session.query(sqa_backref_class).filter(
                        sqa_backref_class.to_obj_uuid.in_(obj_uuids)).all())

        return sqa_ref_objs, sqa_backref_objs
    # end get_refs_backrefs

    @use_session
    def get_backrefs(self, obj_type, obj_uuids, backref_fields):
        session = self.session_ctx
        sqa_backref_objs = []
        if backref_fields:
            backref_types = [x[:-10] for x in backref_fields] # strip trailing _back_refs
            for backref_type in backref_types:
                sqa_backref_class = self.sqa_classes[to_ref_type(
                                         backref_type, obj_type)]
                sqa_backref_objs.extend(
                    session.query(sqa_backref_class).filter(
                        sqa_backref_class.to_obj_uuid.in_(obj_uuids)).all())

        return sqa_backref_objs
    # end get_backrefs

    @use_session
    def get_children(self, obj_type, obj_uuids, children_fields):
        session = self.session_ctx
        if not children_fields:
            return []

        child_types = [c[:-1] for c in children_fields] # strip plural
        sqa_child_objs = []
        for child_type in child_types:
            sqa_child_class = self.sqa_classes[child_type]
            sqa_child_objs.extend(session.query(sqa_child_class).filter(
                sqa_child_class.obj_parent_uuid.in_(obj_uuids)).all())

        return sqa_child_objs
    # end get_children

    def cache_uuid_to_fq_name_add(self, id, fq_name, obj_type):
        self._cache_uuid_to_fq_name[id] = (fq_name, obj_type)
    # end cache_uuid_to_fq_name_add

    def cache_uuid_to_fq_name_del(self, id):
        try:
            del self._cache_uuid_to_fq_name[id]
        except KeyError:
            pass
    # end cache_uuid_to_fq_name_del

    @use_session
    def fq_name_to_uuid(self, res_type, obj_fq_name):
        session = self.session_ctx
        obj_type = to_obj_type(res_type)
        sqa_class = self.sqa_classes[obj_type]
        try:
            obj_base = session.query(sqa_class).filter_by(
                           obj_fq_name=json.dumps(obj_fq_name)).one()
        except NoResultFound:
            raise NoIdError('%s %s' % (obj_type, obj_fq_name))

        return obj_base.obj_uuid
    # end fq_name_to_uuid

    @use_session
    def _uuid_to_fq_name_obj_type(self, id):
        session = self.session_ctx
        try:
            object_metadata = session.query(ObjectMetadata).filter_by(
                obj_uuid=id).one()
            return json.loads(object_metadata.obj_fq_name), object_metadata.obj_type
        except NoResultFound:
            raise NoIdError(id)
    # end _uuid_to_fq_name_obj_type

    def uuid_to_fq_name(self, id):
        try:
            return self._cache_uuid_to_fq_name[id][0]
        except KeyError:
            fq_name, obj_type = self._uuid_to_fq_name_obj_type(id)
            self.cache_uuid_to_fq_name_add(id, fq_name, obj_type)
            return fq_name
    # end uuid_to_fq_name

    @use_session
    def uuid_to_obj_type(self, id):
        try:
            return self._cache_uuid_to_fq_name[id][1]
        except KeyError:
            fq_name, obj_type = self._uuid_to_fq_name_obj_type(id)
            self.cache_uuid_to_fq_name_add(id, fq_name, obj_type)
            return obj_type
    # end uuid_to_obj_type

    @use_session
    def uuid_to_obj_dict(self, id, obj_type=None):
        session = self.session_ctx
        if not obj_type:
            obj_type = self.uuid_to_obj_type(id)
        sqa_class = self.sqa_classes[obj_type]
        try:
            sqa_obj = session.query(sqa_class).filter_by(obj_uuid=id).one()
        except NoResultFound:
            raise NoIdError(id)

        return {'uuid': sqa_obj.obj_uuid,
                'perms2': json.loads(sqa_obj.perms2),
                'fq_name':json.loads(sqa_obj.obj_fq_name),
                'obj_type': obj_type}

    @use_session
    def uuid_to_obj_perms(self, id):
        session = self.session_ctx
        obj_type = self.uuid_to_obj_type(id)
        sqa_class = self.sqa_classes[obj_type]
        try:
            sqa_obj = session.query(sqa_class).filter_by(obj_uuid=id).one()
        except NoResultFound:
            raise NoIdError(id)

        return json.loads(sqa_obj.id_perms)
    # end uuid_to_obj_perms

    # fetch perms2 for an object
    @use_session
    def uuid_to_obj_perms2(self, id):
        session = self.session_ctx
        obj_type = self.uuid_to_obj_type(id)
        sqa_class = self.sqa_classes[obj_type]
        try:
            sqa_obj = session.query(sqa_class).filter_by(obj_uuid=id).one()
        except NoResultFound:
            raise NoIdError(id)

        return json.loads(sqa_obj.perms2)
    # end uuid_to_obj_perms2

    @use_session
    def useragent_kv_store(self, key, value, useragent=None):
        if useragent is None:
            useragent = 'neutron:subnet'

        session = self.session_ctx
        ua_kv_entry = UseragentKV(useragent=useragent,
                                  key=key,
                                  value=value)
        session.add(ua_kv_entry)
        session.commit()
    # end useragent_kv_store

    @use_session
    def useragent_kv_retrieve(self, key, useragent=None):
        if useragent is None:
            useragent = 'neutron:subnet'

        session = self.session_ctx
        if key:
            if isinstance(key, list):
                ua_kv_entries = session.query(UseragentKV).filter_by(
                    useragent=useragent).filter(
                    UseragentKV.key.in_(key)).all()

                return [entry.value for entry in ua_kv_entries]
            else:
                try:
                    ua_kv_entry = session.query(UseragentKV).filter_by(
                        useragent=useragent, key=key).one()
                    return ua_kv_entry.value
                except NoResultFound:
                    raise NoUserAgentKey
        else:  # no key specified, return entire contents
            ua_kv_entries = session.query(UseragentKV).filter(
                UseragentKV.useragent.in_(useragent),
                UseragentKV.key.in_(key)).all()
            kv_list = []
            for ua_key, ua_cols in self._useragent_kv_cf.get_range():
                kv_list.append({'key': ua_key, 'value': ua_cols.get('value')})
            return [{'key': entry.key,
                     'value': entry.value} for entry in ua_kv_entries]
    # end useragent_kv_retrieve

    @use_session
    def useragent_kv_delete(self, key, useragent=None):
        if useragent is None:
            useragent = 'neutron:subnet'

        session = self.session_ctx
        try:
            ua_kv_entry = session.query(UseragentKV).filter_by(
                useragent=useragent, key=key).one()
            session.delete(ua_kv_entry)
            session.commit()
        except NoResultFound:
            pass
    # end useragent_kv_delete

    def create_subnet_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start, should_persist,
                                start_subnet, size, alloc_unit):
        # TODO handle subnet resizing change, ignore for now
        if subnet not in self._subnet_allocators:
            if addr_from_start is None:
                addr_from_start = False
            self._subnet_allocators[subnet] = RDBMSIndexAllocator(
                self.db, self._subnet_path+'/'+subnet+'/',
                size=size/alloc_unit, start_idx=start_subnet/alloc_unit,
                reverse=not addr_from_start,
                alloc_list=[{'start': x['start']/alloc_unit, 'end':x['end']/alloc_unit}
                            for x in subnet_alloc_list],
                max_alloc=self._MAX_SUBNET_ADDR_ALLOC/alloc_unit)
    # end create_subnet_allocator

    def delete_subnet_allocator(self, subnet):
        self._subnet_allocators.pop(subnet, None)
        RDBMSIndexAllocator.delete_all(self.db,
                                  self._subnet_path+'/'+subnet+'/')
    # end delete_subnet_allocator

    def _get_subnet_allocator(self, subnet):
        return self._subnet_allocators.get(subnet)
    # end _get_subnet_allocator

    def subnet_is_addr_allocated(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.read(addr)
    # end subnet_is_addr_allocated

    def subnet_set_in_use(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        allocator.set_in_use(addr)
    # end subnet_set_in_use

    def subnet_reset_in_use(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        allocator.reset_in_use(addr)
    # end subnet_reset_in_use

    def subnet_reserve_req(self, subnet, addr, value):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.reserve(addr, value)
    # end subnet_reserve_req

    def subnet_alloc_count(self, subnet):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.get_alloc_count()
    # end subnet_alloc_count

    def subnet_alloc_req(self, subnet, value=None):
        allocator = self._get_subnet_allocator(subnet)
        try:
            return allocator.alloc(value=value)
        except ResourceExhaustionError:
            return None
    # end subnet_alloc_req

    def subnet_free_req(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        if allocator:
            allocator.delete(addr)
    # end subnet_free_req

    def alloc_vn_id(self, name):
        if name is not None:
            return self._vn_id_allocator.alloc(name)

    def free_vn_id(self, vn_id):
        if vn_id is not None and vn_id < self._VN_MAX_ID:
            self._vn_id_allocator.delete(vn_id)

    def get_vn_from_id(self, vn_id):
        if vn_id is not None and vn_id < self._VN_MAX_ID:
            return self._vn_id_allocator.read(vn_id)

    def alloc_sg_id(self, name):
        if name is not None:
            return self._sg_id_allocator.alloc(name) + SGID_MIN_ALLOC

    def free_sg_id(self, sg_id):
        if (sg_id is not None and
                sg_id > SGID_MIN_ALLOC and
                sg_id < self._SG_MAX_ID):
            self._sg_id_allocator.delete(sg_id - SGID_MIN_ALLOC)

    def get_sg_from_id(self, sg_id):
        if (sg_id is not None and
                sg_id > SGID_MIN_ALLOC and
                sg_id < self._SG_MAX_ID):
            return self._sg_id_allocator.read(sg_id - SGID_MIN_ALLOC)

    @use_session
    def prop_collection_read(self, obj_type, obj_uuid, obj_fields, position):
        obj_class = self._get_resource_class(obj_type)
        field_names=obj_fields
        field_names.append("id_perms")
        ok, results = self.object_read(obj_type, [obj_uuid], field_names=field_names)
        if ok:
            result = results[0]
            for key in field_names:
                if key in obj_class.prop_list_fields:
                    if obj_class.prop_list_field_has_wrappers.get(key):
                        wrapper_field = self._wrapper_field(obj_class, key)
                        values = result.get(key, {}).get(wrapper_field)
                    else:
                        values = result.get(key)

                    if position:
                        position = int(position)
                        values = [values[position]]
                    if values:
                        result[key] = [(value, i) for i, value in enumerate(values)]
                    else:
                        result[key] = []
                if key in obj_class.prop_map_fields:
                    if obj_class.prop_map_field_has_wrappers.get(key):
                        wrapper_field = self._wrapper_field(obj_class, key)
                        values = result.get(key, {}).get(wrapper_field)
                    else:
                        values = result.get(key)

                    if values:
                        result[key] = values
                    else:
                        result[key] = {}
            return ok, result
        return ok, None

    @use_session
    def prop_collection_update(self, obj_type, obj_uuid, updates):
        session = self.session_ctx
        obj_class = self._db_client_mgr.get_resource_class(obj_type)
        for oper_param in updates:
            oper = oper_param['operation']
            prop_name = oper_param['field']

            if prop_name in obj_class.prop_list_fields:
                sqa_class = self.sqa_classes[to_list_type(obj_type, prop_name)]
                if oper == 'add' or oper == 'modify':
                    position = oper_param.get('position')
                    value = oper_param['value']
                    self._modify_to_prop_list(sqa_class, obj_uuid, position, value)
                elif oper == 'delete':
                    position = oper_param.get('position')
                    self._delete_from_prop_list(sqa_class, obj_uuid, position)
            elif prop_name in obj_class.prop_map_fields:
                sqa_class = self.sqa_classes[to_map_type(obj_type, prop_name)]
                key_name = obj_class.prop_map_field_key_names[prop_name]
                if oper == 'set':
                    value = oper_param['value']
                    key = value[key_name]
                    self._set_in_prop_map(sqa_class, obj_uuid, key, value)
                elif oper == 'delete':
                    key = oper_param['position']
                    self._delete_from_prop_map(sqa_class, obj_uuid, key)
        self.update_last_modified(obj_type, obj_uuid)
        session.commit()

    def _add_to_prop_list(self, sqa_class, obj_uuid, value):
        session = self.session_ctx
        position = session.query(sqa_class).filter_by(owner=obj_uuid).count()

        element = sqa_class(
            index=position,
            owner=obj_uuid,
            value=json.dumps(value)
        )
        session.add(element)
    # end _add_to_prop_list

    def _modify_to_prop_list(self, sqa_class, obj_uuid, position, value):
        session = self.session_ctx
        try:
            position = int(position)
            obj = session.query(sqa_class).filter_by(owner=obj_uuid).filter_by(index=position).one()
            obj.value = json.dumps(value)
            session.add(obj)
        except:
            self._add_to_prop_list(sqa_class, obj_uuid, value)

    def _delete_from_prop_list(self, sqa_class, obj_uuid, position):
        if not position:
            position = 0
        session = self.session_ctx
        session.query(sqa_class).filter_by(owner=obj_uuid).filter_by(index=position).delete()
    # end _delete_from_prop_list

    def _set_in_prop_map(self, sqa_class, obj_uuid, key, data):
        session = self.session_ctx
        try:
            obj = session.query(sqa_class).filter_by(owner=obj_uuid).filter_by(key=key).one()
            obj.value = json.dumps(data)
            session.add(obj)
        except:
            element = sqa_class(
                owner=obj_uuid,
                key=key,
                value=json.dumps(data)
            )
        session.add(element)
    # end _set_in_prop_map

    def _delete_from_prop_map(self, sqa_class, obj_uuid, key):
        session = self.session_ctx
        session.query(sqa_class).filter_by(owner=obj_uuid).filter_by(key=key).delete()
    # end _delete_from_prop_map

    def set_shared(self, *args, **kwargs):
        pass

    def del_shared(self, *args, **kwargs):
        pass

    def create_fq_name_to_uuid_mapping(self, *args, **kwargs):
        pass

    def delete_fq_name_to_uuid_mapping(self, *args, **kwargs):
        pass

    def is_connected(self):
        return True

    def is_latest(self, *args, **kwargs):
        return False

    def object_raw_read(self, obj_uuids, prop_names):
        raw_objects = []
        for obj_uuid in obj_uuids:
            type = self.uuid_to_obj_type(obj_uuid)
            ok, raw_object = self.object_read(type, [obj_uuid], field_names=prop_names)
            if ok and len(raw_object) > 0:
                raw_objects.append(raw_object[0])

        return raw_objects

def migrate_from_cassandra(mysql_server='127.0.0.1', mysql_username='root', mysql_password='b2618966b123426b0d3d'):
    # TODO remove default creds
    # TODO zookeeper migration
    import readline
    engine = create_engine(
        'mysql://%s:%s@%s/contrail' %(
            mysql_username, mysql_password, mysql_server),
        echo=False)
    Session = sessionmaker(bind=engine)
    session = Session()

    # Handle config_db_uuid keyspace migration
    pool = pycassa.ConnectionPool('config_db_uuid', [hostname], pool_timeout=120,
                                  max_retries=-1, timeout=5)
    obj_uuid_cf = pycassa.ColumnFamily(pool, 'obj_uuid_table')

    VncMysqlClient.create_sqalchemy_models()
    sqa_classes = VncMysqlClient.sqa_classes

    Base.metadata.create_all(engine)

    for rowkey,columns in obj_uuid_cf.get_range(column_count=100000):
        obj_uuid = rowkey
        try:
            obj_type = json.loads(columns['type'])
            obj_fq_name_json = columns['fq_name']
            parent_col = [c for c in columns if c.startswith('parent:')]
            if parent_col:
                _, parent_type, parent_uuid = parent_col[0].split(':')
            else:
                parent_type = parent_uuid = ''
        except KeyError as e:
            raise


        now = datetime.utcnow()
        epoch = (time.mktime(now.timetuple()) + now.microsecond*1e-6) * 1e6
        obj_class_name = cfgm_common.utils.CamelCase(obj_type)
        try:
            sqa_class = sqa_classes[obj_type]
        except KeyError:
            print "Unknown type " + obj_type #TODO log instead of print
            continue
        sqa_obj = sqa_class(obj_uuid=obj_uuid,
                         obj_type=obj_type,
                         obj_fq_name=obj_fq_name_json,
                         obj_parent_type=parent_type,
                         obj_parent_uuid=parent_uuid,
                         meta_create_epoch=epoch,
                         meta_prop_ref_update_epoch=epoch,
                         meta_update_epoch=epoch)

        for col_name, col_value in columns.items():
            if not col_name.startswith('prop:'):
                continue
            #elif TODO handle propm/propl
            prop_name = col_name.split(':')[1]
            prop_value_json = col_value
            setattr(sqa_obj, prop_name, prop_value_json)

        session.add(sqa_obj)
    # end all obj created with props
    session.commit()
    session.close()

    session = Session()
    for rowkey,columns in obj_uuid_cf.get_range(column_count=100000):
        obj_uuid = rowkey
        obj_type = json.loads(columns['type'])
        for col_name, col_value in columns.items():
            if not col_name.startswith('ref:'):
                continue
            _, ref_type, ref_uuid = col_name.split(':')
            ref_value_json = col_value
            sqa_class = sqa_classes['ref_%s_%s' %(obj_type, ref_type)]
            sqa_obj_ref = sqa_class(from_obj_uuid=obj_uuid, to_obj_uuid=ref_uuid,
                                    ref_value=ref_value_json)
            # TODO check from ref_uuid columns if relaxed backref has to be set
            session.add(sqa_obj_ref)
        # for all columns
    # for all rows

    # Handle useragent keyspace migration
    pool = pycassa.ConnectionPool('useragent', [hostname], pool_timeout=120,
                                  max_retries=-1, timeout=5)
    ua_kv_cf = pycassa.ColumnFamily(pool, 'useragent_keyval_table')
    for rowkey,columns in ua_kv_cf.get_range(column_count=100000):
        ua_kv_entry = UseragentKV(useragent='neutron:subnet',
                                  key=rowkey,
                                  value=columns['value'])
        session.add(ua_kv_entry)

    session.commit()
    session.close()
# end migrate_from_cassandra

mg = migrate_from_cassandra
