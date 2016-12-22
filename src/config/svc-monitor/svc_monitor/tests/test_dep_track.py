import mock
from mock import patch
import unittest
from cfgm_common.vnc_db import DBBase
from cfgm_common.dependency_tracker import DependencyTracker

#
# Red ------------------
#       |               |
#       |               |
#       v               v
#      Blue    <-->   Green
#        |              |
#        |              |
#        v              v
#      Purple  <-->   White

class DBBaseTM(DBBase):
    obj_type = __name__

class RedSM(DBBaseTM):
    _dict = {}
    obj_type = 'red'
    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.blues = set()
        self.greens = set()
        obj_dict = self.update(obj_dict)
        self.set_children('blue', obj_dict)
        self.set_children('green', obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        del cls._dict[uuid]
    # end delete
# end RedSM

class GreenSM(DBBaseTM):
    _dict = {}
    obj_type = 'green'
    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.whites = set()
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)
        self.red = self.get_parent_uuid(obj_dict)
        self.set_children('white', obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.update_single_ref('blue', obj)
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.remove_from_parent()
        obj.update_single_ref('blue', {})
        del cls._dict[uuid]
    # end delete
# end GreenSM

class BlueSM(DBBaseTM):
    _dict = {}
    obj_type = 'blue'
    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.purples = set()
        self.greens = set()
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)
        self.red = self.get_parent_uuid(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.update_multiple_refs('green', obj)
        self.name = obj['fq_name'][-1]
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.remove_from_parent()
        obj.update_multiple_refs('green', {})
        del cls._dict[uuid]
    # end delete
# end BlueSM

class PurpleSM(DBBaseTM):
    _dict = {}
    obj_type = 'purple'
    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)
        self.blue = self.get_parent_uuid(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.update_single_ref('white', obj)
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.remove_from_parent()
        obj.update_single_ref('white', {})
        del cls._dict[uuid]
    # end delete
# end PurpleSM

class WhiteSM(DBBaseTM):
    _dict = {}
    obj_type = 'white'
    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.purples = set()
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)
        self.green = self.get_parent_uuid(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.update_multiple_refs('purple', obj)
        self.name = obj['fq_name'][-1]
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.remove_from_parent()
        obj.update_multiple_refs('purple', {})
        del cls._dict[uuid]
    # end delete
# end WhiteSM


DBBase._OBJ_TYPE_MAP = {
    'red': RedSM,
    'blue': BlueSM,
    'green': GreenSM,
    'white': WhiteSM,
    'purple': PurpleSM
}


class DepTrackTester(unittest.TestCase):
    def green_read(self, obj_type, uuid, **kwargs):
        green_obj = {}
        green_obj['uuid'] = uuid[0]
        green_obj['fq_name'] = ['fake-green-' + uuid[0]]
        green_obj['parent_type'] = 'red'
        green_obj['parent_uuid'] = 'fake-red-uuid'
        return True, [green_obj]
    # end green_read

    def white_read(self, obj_type, uuid, **kwargs):
        white_obj = {}
        white_obj['uuid'] = uuid[0]
        white_obj['fq_name'] = ['fake-white-' + uuid[0]]
        white_obj['parent_type'] = 'green'
        white_obj['parent_uuid'] = 'fake-green-uuid'
        return True, [white_obj]
    # end white_read

    def purple_read(self, obj_type, uuid, **kwargs):
        purple_obj = {}
        purple_obj['uuid'] = uuid[0]
        purple_obj['fq_name'] = ['fake-purple-' + uuid[0]]
        purple_obj['parent_type'] = 'blue'
        purple_obj['parent_uuid'] = 'fake-blue-uuid'
        return True, [purple_obj]
    # end purple_read

    def green_read_with_refs(self, obj_type, uuid, **kwargs):
        green_obj = {}
        green_obj['uuid'] = 'fake-green-uuid'
        green_obj['fq_name'] = ['fake-green-uuid']
        green_obj['parent_type'] = 'red'
        green_obj['parent_uuid'] = 'fake-red-uuid'
        green_obj['blue_back_refs'] = [{'uuid': 'fake-blue-uuid'}]
        return True, [green_obj]
    # end green_read_with_refs

    def green_read_with_update_refs(self, obj_type, uuid, **kwargs):
        green_obj = {}
        green_obj['uuid'] = 'fake-green-uuid'
        green_obj['fq_name'] = ['fake-green-uuid']
        green_obj['parent_type'] = 'red'
        green_obj['parent_uuid'] = 'fake-red-uuid'
        green_obj['blue_back_refs'] = [{'uuid': 'fake-blue-uuid-1'}]
        return True, [green_obj]
    # end green_read_with_update_refs

    def white_read_with_refs(self, obj_type, uuid, **kwargs):
        white_obj = {}
        white_obj['uuid'] = 'fake-white-uuid'
        white_obj['fq_name'] = ['fake-white-uuid']
        white_obj['parent_type'] = 'green'
        white_obj['parent_uuid'] = 'fake-green-uuid-0'
        white_obj['purple_back_refs'] = [{'uuid': 'fake-purple-uuid'}]
        return True, [white_obj]
    # end white_read_with_refs


    def red_read(self, obj_type, uuid, **kwargs):
        red_obj = {}
        red_obj['uuid'] = 'fake-red-uuid'
        red_obj['fq_name'] = ['fake-red-uuid']
        return True, [red_obj]
    # end red_read

    def red_read_with_child(self, obj_type, uuid, **kwargs):
        red_obj = {}
        red_obj['uuid'] = 'fake-red-uuid'
        red_obj['fq_name'] = ['fake-red-uuid']
        red_obj['blues'] = [{'uuid': 'fake-blue-uuid'}]
        red_obj['greens'] = [{'to': 'fake-green-uuid'}]
        return True, [red_obj]
    # end red_read_with_child

    def blue_read(self, obj_type, uuid, **kwargs):
        blue_obj = {}
        blue_obj['uuid'] = uuid[0]
        blue_obj['fq_name'] = ['fake-blue-' + uuid[0]]
        blue_obj['parent_type'] = 'red'
        blue_obj['parent_uuid'] = 'fake-red-uuid'
        return True, [blue_obj]
    # end blue_read

    def blue_read_with_refs(self, obj_type, uuid, **kwargs):
        blue_obj = {}
        blue_obj['uuid'] = 'fake-blue-uuid'
        blue_obj['fq_name'] = ['fake-blue-uuid']
        blue_obj['parent_type'] = 'red'
        blue_obj['parent_uuid'] = 'fake-red-uuid'
        blue_obj['green_refs'] = [{'uuid': 'fake-green-uuid'}]
        return True, [blue_obj]
    # end blue_read_with_refs

    def blue_read_with_multi_refs(self, obj_type, uuid, **kwargs):
        blue_obj = {}
        blue_obj['uuid'] = 'fake-blue-uuid'
        blue_obj['fq_name'] = ['fake-blue-uuid']
        blue_obj['parent_type'] = 'red'
        blue_obj['parent_uuid'] = 'fake-red-uuid'
        blue_obj['green_refs'] = [{'uuid': 'fake-green-uuid-0'},{'uuid': 'fake-green-uuid-1'}]
        return True, [blue_obj]
    # end blue_read_with_multi_refs

    def blue_read_with_new_refs(self, obj_type, uuid, **kwargs):
        blue_obj = {}
        blue_obj['uuid'] = 'fake-blue-uuid'
        blue_obj['fq_name'] = ['fake-blue-uuid']
        blue_obj['parent_type'] = 'red'
        blue_obj['parent_uuid'] = 'fake-red-uuid'
        blue_obj['green_refs'] = [{'uuid': 'fake-green-uuid-2'},{'uuid': 'fake-green-uuid-3'}]
        return True, [blue_obj]
    # end blue_read_with_new_refs

    def purple_read_with_multi_refs(self, obj_type, uuid, **kwargs):
        purple_obj = {}
        purple_obj['uuid'] = 'fake-purple-uuid'
        purple_obj['fq_name'] = ['fake-purple-uuid']
        purple_obj['parent_type'] = 'blue'
        purple_obj['parent_uuid'] = 'fake-blue-uuid'
        purple_obj['white_refs'] = [{'uuid': 'fake-white-uuid-0'},{'uuid': 'fake-white-uuid-1'}]
        return True, [purple_obj]
    # end purple_read_with_multi_refs


    def setUp(self):
        DBBase.init(self, None, None)
        DBBase._OBJ_TYPE_MAP['red'] = RedSM
        DBBase._OBJ_TYPE_MAP['blue'] = BlueSM
        DBBase._OBJ_TYPE_MAP['green'] = GreenSM
        DBBase._OBJ_TYPE_MAP['white'] = WhiteSM
        DBBase._OBJ_TYPE_MAP['purple'] = PurpleSM
        BlueSM._cassandra = mock.MagicMock()
        BlueSM._cassandra.object_read = self.blue_read
        RedSM._cassandra = mock.MagicMock()
        RedSM._cassandra.object_read = self.red_read
        GreenSM._cassandra = mock.MagicMock()
        GreenSM._cassandra.object_read = self.green_read
        WhiteSM._cassandra = mock.MagicMock()
        WhiteSM._cassandra.object_read = self.white_read
        PurpleSM._cassandra = mock.MagicMock()
        PurpleSM._cassandra.object_read = self.purple_read
    # end setUp

    def tearDown(self):
        GreenSM.reset()
        BlueSM.reset()
        RedSM.reset()
        PurpleSM.reset()
        del DBBase._OBJ_TYPE_MAP['red']
        del DBBase._OBJ_TYPE_MAP['blue']
        del DBBase._OBJ_TYPE_MAP['green']
        del DBBase._OBJ_TYPE_MAP['white']
        del DBBase._OBJ_TYPE_MAP['purple']
    # end tearDown

    def test_add_delete(self):
        RedSM.locate("fake-red-uuid")
        BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid")

        blues = [blue.uuid for blue in BlueSM.values()]
        blues_1 = [id for id,value in BlueSM.items()]
        blues_2 = [value for value in BlueSM]

        self.assertEqual(blues, blues_1)
        self.assertEqual(blues_2, blues_1)
        self.assertIsNotNone(GreenSM.get("fake-green-uuid"))
        self.assertIsNotNone(RedSM.get("fake-red-uuid"))
        self.assertIsNotNone(BlueSM.get("fake-blue-uuid"))
        red = RedSM.get("fake-red-uuid")
        self.assertEqual(len(red.blues), 1)
        self.assertEqual(len(red.greens), 1)
        green = GreenSM.get("fake-green-uuid")
        self.assertEqual(green.red, "fake-red-uuid")
        blue = BlueSM.get("fake-blue-uuid")
        self.assertEqual(blue.red, "fake-red-uuid")

        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid")
        BlueSM.delete("fake-blue-uuid")
    # end test_add_delete


    def fq_name_to_uuid(self, type, name):
        return 'fake-green-uuid'
    # end fq_name_to_uuid

    def test_add_set_child(self):
        RedSM._cassandra = mock.MagicMock()
        RedSM._cassandra.object_read = self.red_read_with_child
        RedSM._cassandra.fq_name_to_uuid = self.fq_name_to_uuid

        RedSM.locate("fake-red-uuid")
        self.assertIsNotNone(RedSM.get("fake-red-uuid"))
        red = RedSM.get("fake-red-uuid")
        self.assertEqual(len(red.blues), 1)
        self.assertEqual(len(red.greens), 1)
        BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid")
        self.assertIsNotNone(GreenSM.get("fake-green-uuid"))
        self.assertIsNotNone(BlueSM.get("fake-blue-uuid"))
        red = RedSM.get("fake-red-uuid")
        green = GreenSM.get("fake-green-uuid")
        self.assertEqual(green.red, "fake-red-uuid")
        blue = BlueSM.get("fake-blue-uuid")
        self.assertEqual(blue.red, "fake-red-uuid")

        BlueSM.delete("fake-blue-uuid")
        self.assertEqual(len(red.blues), 0)
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid")
    # end test_add_set_child

    def test_add_with_refs(self):
        GreenSM._cassandra.object_read = self.green_read_with_refs
        BlueSM._cassandra.object_read = self.blue_read_with_refs
        RedSM.locate("fake-red-uuid")
        BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid")

        self.assertIsNotNone(RedSM.get("fake-red-uuid"))
        self.assertIsNotNone(GreenSM.get("fake-green-uuid"))
        self.assertIsNotNone(BlueSM.get("fake-blue-uuid"))

        red = RedSM.get("fake-red-uuid")
        self.assertEqual(len(red.blues), 1)
        self.assertEqual(len(red.greens), 1)
        green = GreenSM.get("fake-green-uuid")
        self.assertEqual(green.red, "fake-red-uuid")
        self.assertEqual(green.blue, "fake-blue-uuid")
        blue = BlueSM.get("fake-blue-uuid")
        self.assertEqual(blue.red, "fake-red-uuid")
        self.assertEqual(len(blue.greens), 1)

        BlueSM.delete("fake-blue-uuid")
        self.assertEqual(green.blue, None)

        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid")
    # end test_add_with_refs

    def test_update_with_refs(self):
        GreenSM._cassandra.object_read = self.green_read_with_refs
        BlueSM._cassandra.object_read = self.blue_read_with_refs
        RedSM.locate("fake-red-uuid")
        BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid")

        self.assertIsNotNone(RedSM.get("fake-red-uuid"))
        self.assertIsNotNone(GreenSM.get("fake-green-uuid"))
        self.assertIsNotNone(BlueSM.get("fake-blue-uuid"))

        red = RedSM.get("fake-red-uuid")
        self.assertEqual(len(red.blues), 1)
        self.assertEqual(len(red.greens), 1)
        green = GreenSM.get("fake-green-uuid")
        self.assertEqual(green.red, "fake-red-uuid")
        self.assertEqual(green.blue, "fake-blue-uuid")
        blue = BlueSM.get("fake-blue-uuid")
        self.assertEqual(blue.red, "fake-red-uuid")
        self.assertEqual(len(blue.greens), 1)

        BlueSM.delete("fake-blue-uuid")

        BlueSM.locate("fake-blue-uuid-1")
        GreenSM._cassandra.object_read = self.green_read_with_update_refs
        green.update()

        green = GreenSM.get("fake-green-uuid")
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid")
    # end test_update_with_refs

    def test_add_with_multi_refs(self):
        GreenSM._cassandra.object_read = self.green_read_with_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        RedSM.locate("fake-red-uuid")
        BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")

        self.assertIsNotNone(RedSM.get("fake-red-uuid"))
        self.assertIsNotNone(GreenSM.get("fake-green-uuid-0"))
        self.assertIsNotNone(GreenSM.get("fake-green-uuid-1"))
        self.assertIsNotNone(BlueSM.get("fake-blue-uuid"))

        red = RedSM.get("fake-red-uuid")
        self.assertEqual(len(red.blues), 1)
        self.assertEqual(len(red.greens), 2)
        green_0 = GreenSM.get("fake-green-uuid-0")
        green_1 = GreenSM.get("fake-green-uuid-1")
        self.assertEqual(green_0.red, "fake-red-uuid")
        self.assertEqual(green_0.blue, "fake-blue-uuid")
        self.assertEqual(green_1.red, "fake-red-uuid")
        self.assertEqual(green_1.blue, "fake-blue-uuid")
        blue = BlueSM.get("fake-blue-uuid")
        self.assertEqual(blue.red, "fake-red-uuid")
        self.assertEqual(len(blue.greens), 2)
        self.assertTrue("fake-green-uuid-0" in (blue.greens))
        self.assertTrue("fake-green-uuid-1" in (blue.greens))

        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
    # end test_add_with_multi_refs

    def test_update_with_multi_refs(self):
        GreenSM._cassandra.object_read = self.green_read_with_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        RedSM.locate("fake-red-uuid")
        BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")

        self.assertIsNotNone(RedSM.get("fake-red-uuid"))
        self.assertIsNotNone(GreenSM.get("fake-green-uuid-0"))
        self.assertIsNotNone(GreenSM.get("fake-green-uuid-1"))
        self.assertIsNotNone(BlueSM.get("fake-blue-uuid"))

        red = RedSM.get("fake-red-uuid")
        self.assertEqual(len(red.blues), 1)
        self.assertEqual(len(red.greens), 2)
        green_0 = GreenSM.get("fake-green-uuid-0")
        green_1 = GreenSM.get("fake-green-uuid-1")
        self.assertEqual(green_0.red, "fake-red-uuid")
        self.assertEqual(green_0.blue, "fake-blue-uuid")
        self.assertEqual(green_1.red, "fake-red-uuid")
        self.assertEqual(green_1.blue, "fake-blue-uuid")
        blue = BlueSM.get("fake-blue-uuid")
        self.assertEqual(blue.red, "fake-red-uuid")
        self.assertEqual(len(blue.greens), 2)
        self.assertTrue("fake-green-uuid-0" in (blue.greens))
        self.assertTrue("fake-green-uuid-1" in (blue.greens))

        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")

        GreenSM.locate("fake-green-uuid-2")
        GreenSM.locate("fake-green-uuid-3")
        BlueSM._cassandra.object_read = self.blue_read_with_new_refs
        blue.update()

        green_2 = GreenSM.get("fake-green-uuid-2")
        green_3 = GreenSM.get("fake-green-uuid-3")
        self.assertEqual(green_2.red, "fake-red-uuid")
        self.assertEqual(green_2.blue, "fake-blue-uuid")
        self.assertEqual(green_3.red, "fake-red-uuid")
        self.assertEqual(green_3.blue, "fake-blue-uuid")
        blue = BlueSM.get("fake-blue-uuid")
        self.assertEqual(blue.red, "fake-red-uuid")
        self.assertEqual(len(blue.greens), 2)
        self.assertTrue("fake-green-uuid-2" in (blue.greens))
        self.assertTrue("fake-green-uuid-3" in (blue.greens))
        self.assertFalse("fake-green-uuid-0" in (blue.greens))
        self.assertFalse("fake-green-uuid-1" in (blue.greens))

        RedSM.delete("fake-red-uuid")
        BlueSM.delete("fake-blue-uuid")
        GreenSM.delete("fake-green-uuid-2")
        GreenSM.delete("fake-green-uuid-3")
    # end test_update_with_mulit_refs

    def test_find(self):
        GreenSM._cassandra.object_read = self.green_read
        BlueSM._cassandra.object_read = self.blue_read
        RedSM.locate("fake-red-uuid")
        BlueSM.locate("OK")
        GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        self.assertIsNone(BlueSM.find_by_name_or_uuid("fake-blue-NOK"))
        self.assertIsNotNone(BlueSM.find_by_name_or_uuid("fake-blue-OK"))
        self.assertIsNotNone(BlueSM.find_by_name_or_uuid("OK"))
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
    # end test_find

    def test_basic_dep_track(self):
        reaction_map = {
            "red": {
                'self': ['blue', 'green'],
            },
            "blue": {
                'self': [],
                'red': [],
            },
            "green": {
                'self': [],
                'red': [],
            },
            "purple": {
                'self': [],
            },
        }
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid")
        dependency_tracker.evaluate('red', red)
        self.assertEqual(len(dependency_tracker.resources), 3)
        self.assertTrue("red" in dependency_tracker.resources)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertEqual(dependency_tracker.resources["red"], ["fake-red-uuid"])
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["green"], ["fake-green-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid")
        BlueSM.delete("fake-blue-uuid")
    # end test_basic_dep_track

    def test_basic_dep_track_1(self):
        reaction_map = {
            "red": {
                'self': [],
            },
            "blue": {
                'self': ['green'],
            },
            "green": {
                'self': [],
                'blue': [],
            },
            "purple": {
                'self': [],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid")
        dependency_tracker.evaluate('blue', blue)
        self.assertEqual(len(dependency_tracker.resources), 2)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["green"], ["fake-green-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid")
        BlueSM.delete("fake-blue-uuid")
    # end test_basic_dep_track_1

    def test_basic_dep_track_2(self):
        reaction_map = {
            "red": {
                'self': [],
            },
            "blue": {
                'self': ['green'],
            },
            "green": {
                'self': [],
                'blue': [],
            },
            "purple": {
                'self': [],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        dependency_tracker.evaluate('blue', blue)
        self.assertEqual(len(dependency_tracker.resources), 2)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertEqual(dependency_tracker.resources["green"], ["fake-green-uuid-1", "fake-green-uuid-0"])
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
    # end test_basic_dep_track

    def test_basic_dep_track_3(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': [],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': [],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('blue', blue)
        self.assertEqual(len(dependency_tracker.resources), 4)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertTrue("white" in dependency_tracker.resources)
        self.assertTrue("purple" in dependency_tracker.resources)
        self.assertEqual(dependency_tracker.resources["green"], ["fake-green-uuid-1", "fake-green-uuid-0"])
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["purple"], ["fake-purple-uuid"])
        self.assertEqual(dependency_tracker.resources["white"], ["fake-white-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_3

    def test_basic_dep_track_4(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': [],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': [],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('red', red)
        self.assertEqual(len(dependency_tracker.resources), 3)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertTrue("red" in dependency_tracker.resources)
        self.assertEqual(dependency_tracker.resources["green"], ["fake-green-uuid-1", "fake-green-uuid-0"])
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["red"], ["fake-red-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_4


    def test_basic_dep_track_5(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': [],
                'purple': ['green'],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': [],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('green', green)
        self.assertEqual(len(dependency_tracker.resources), 4)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertTrue("white" in dependency_tracker.resources)
        self.assertTrue("purple" in dependency_tracker.resources)
        self.assertEqual(set(dependency_tracker.resources["green"]), set(["fake-green-uuid-1", "fake-green-uuid-0"]))
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["white"], ["fake-white-uuid"])
        self.assertEqual(dependency_tracker.resources["purple"], ["fake-purple-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_5

    def test_basic_dep_track_6(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': [],
                'purple': ['green'],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': [],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('white', white)
        self.assertEqual(len(dependency_tracker.resources), 4)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertTrue("white" in dependency_tracker.resources)
        self.assertTrue("purple" in dependency_tracker.resources)
        self.assertEqual(set(dependency_tracker.resources["green"]), set(["fake-green-uuid-1", "fake-green-uuid-0"]))
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["white"], ["fake-white-uuid"])
        self.assertEqual(dependency_tracker.resources["purple"], ["fake-purple-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_6

    def test_basic_dep_track_7(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': [],
                'purple': ['green'],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': [],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('purple', purple)
        self.assertEqual(len(dependency_tracker.resources), 4)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertTrue("white" in dependency_tracker.resources)
        self.assertTrue("purple" in dependency_tracker.resources)
        self.assertEqual(set(dependency_tracker.resources["green"]), set(["fake-green-uuid-1", "fake-green-uuid-0"]))
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["white"], ["fake-white-uuid"])
        self.assertEqual(dependency_tracker.resources["purple"], ["fake-purple-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_7

    def test_basic_dep_track_8(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': ['blue'],
                'purple': ['green'],
                'green': [],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': ['blue'],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('red', red)
        self.assertEqual(len(dependency_tracker.resources), 3)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertTrue("red" in dependency_tracker.resources)
        self.assertEqual(set(dependency_tracker.resources["green"]), set(["fake-green-uuid-1", "fake-green-uuid-0"]))
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["red"], ["fake-red-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_8

    def test_basic_dep_track_update_1(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': ['blue'],
                'purple': ['green'],
                'green': [],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': ['blue'],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('red', red)
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")

        GreenSM.locate("fake-green-uuid-2")
        GreenSM.locate("fake-green-uuid-3")
        BlueSM._cassandra.object_read = self.blue_read_with_new_refs
        blue.update()
        dependency_tracker.resources = {}
        dependency_tracker.evaluate('red', red)
        self.assertEqual(len(dependency_tracker.resources), 3)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertTrue("red" in dependency_tracker.resources)
        self.assertEqual(set(dependency_tracker.resources["green"]), set(["fake-green-uuid-2", "fake-green-uuid-3"]))
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["red"], ["fake-red-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-2")
        GreenSM.delete("fake-green-uuid-3")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_update_1

    def test_basic_dep_track_update_2(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': ['blue'],
                'purple': ['green'],
                'green': [],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': ['blue'],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('blue', blue)
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        GreenSM.locate("fake-green-uuid-2")
        GreenSM.locate("fake-green-uuid-3")
        BlueSM._cassandra.object_read = self.blue_read_with_new_refs
        blue.update()
        dependency_tracker.resources = {}
        dependency_tracker.evaluate('blue', blue)
        self.assertEqual(len(dependency_tracker.resources), 2)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertEqual(set(dependency_tracker.resources["green"]), set(["fake-green-uuid-2", "fake-green-uuid-3"]))
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-2")
        GreenSM.delete("fake-green-uuid-3")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_update_2

    def test_basic_dep_track_update_3(self):
        reaction_map = {
            "red": {
                'self': ['green', 'blue'],
            },
            "blue": {
                'self': ['green'],
                'red': [],
                'purple': ['green'],
            },
            "green": {
                'self': ['white'],
                'blue': ['white'],
                'red': [],
            },
            "white": {
                'self': ['purple'],
                'green': ['purple'],
            },
            "purple": {
                'self': ['blue'],
                'white': ['blue'],
            },
        }
        GreenSM._cassandra.object_read = self.green_read_with_refs
        WhiteSM._cassandra.object_read = self.white_read_with_refs
        BlueSM._cassandra.object_read = self.purple_read_with_multi_refs
        BlueSM._cassandra.object_read = self.blue_read_with_multi_refs
        dependency_tracker = DependencyTracker(DBBase._OBJ_TYPE_MAP, reaction_map)
        red = RedSM.locate("fake-red-uuid")
        blue = BlueSM.locate("fake-blue-uuid")
        green = GreenSM.locate("fake-green-uuid-0")
        GreenSM.locate("fake-green-uuid-1")
        white = WhiteSM.locate("fake-white-uuid")
        purple = PurpleSM.locate("fake-purple-uuid")
        dependency_tracker.evaluate('blue', blue)
        self.assertEqual(len(dependency_tracker.resources), 4)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertTrue("white" in dependency_tracker.resources)
        self.assertTrue("purple" in dependency_tracker.resources)
        self.assertEqual(set(dependency_tracker.resources["green"]), set(["fake-green-uuid-1", "fake-green-uuid-0"]))
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        self.assertEqual(dependency_tracker.resources["white"], ["fake-white-uuid"])
        self.assertEqual(dependency_tracker.resources["purple"], ["fake-purple-uuid"])
        # update
        GreenSM.delete("fake-green-uuid-0")
        GreenSM.delete("fake-green-uuid-1")
        GreenSM.locate("fake-green-uuid-2")
        GreenSM.locate("fake-green-uuid-3")
        BlueSM._cassandra.object_read = self.blue_read_with_new_refs
        blue.update()
        dependency_tracker.resources = {}
        dependency_tracker.evaluate('blue', blue)
        self.assertEqual(len(dependency_tracker.resources), 2)
        self.assertTrue("blue" in dependency_tracker.resources)
        self.assertTrue("green" in dependency_tracker.resources)
        self.assertEqual(set(dependency_tracker.resources["green"]), set(["fake-green-uuid-3", "fake-green-uuid-2"]))
        self.assertEqual(dependency_tracker.resources["blue"], ["fake-blue-uuid"])
        RedSM.delete("fake-red-uuid")
        GreenSM.delete("fake-green-uuid-2")
        GreenSM.delete("fake-green-uuid-3")
        BlueSM.delete("fake-blue-uuid")
        WhiteSM.delete("fake-white-uuid")
        PurpleSM.delete("fake-purple-uuid")
    # end test_basic_dep_track_update_3
#end DepTrackTester(unittest.TestCase):
