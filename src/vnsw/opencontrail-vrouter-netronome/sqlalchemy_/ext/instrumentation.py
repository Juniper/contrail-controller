"""Extensible class instrumentation.

The :mod:`sqlalchemy_.ext.instrumentation` package provides for alternate
systems of class instrumentation within the ORM.  Class instrumentation
refers to how the ORM places attributes on the class which maintain
data and track changes to that data, as well as event hooks installed
on the class.

.. note::
    The extension package is provided for the benefit of integration
    with other object management packages, which already perform
    their own instrumentation.  It is not intended for general use.

For examples of how the instrumentation extension is used,
see the example :ref:`examples_instrumentation`.

.. versionchanged:: 0.8
   The :mod:`sqlalchemy_.orm.instrumentation` was split out so
   that all functionality having to do with non-standard
   instrumentation was moved out to :mod:`sqlalchemy_.ext.instrumentation`.
   When imported, the module installs itself within
   :mod:`sqlalchemy_.orm.instrumentation` so that it
   takes effect, including recognition of
   ``__sa_instrumentation_manager__`` on mapped classes, as
   well :data:`.instrumentation_finders`
   being used to determine class instrumentation resolution.

"""
from ..orm import instrumentation as orm_instrumentation
from ..orm.instrumentation import (
    ClassManager, InstrumentationFactory, _default_state_getter,
    _default_dict_getter, _default_manager_getter
)
from ..orm import attributes, collections, base as orm_base
from .. import util
from ..orm import exc as orm_exc
import weakref

INSTRUMENTATION_MANAGER = '__sa_instrumentation_manager__'
"""Attribute, elects custom instrumentation when present on a mapped class.

Allows a class to specify a slightly or wildly different technique for
tracking changes made to mapped attributes and collections.

Only one instrumentation implementation is allowed in a given object
inheritance hierarchy.

The value of this attribute must be a callable and will be passed a class
object.  The callable must return one of:

  - An instance of an InstrumentationManager or subclass
  - An object implementing all or some of InstrumentationManager (TODO)
  - A dictionary of callables, implementing all or some of the above (TODO)
  - An instance of a ClassManager or subclass

This attribute is consulted by SQLAlchemy instrumentation
resolution, once the :mod:`sqlalchemy_.ext.instrumentation` module
has been imported.  If custom finders are installed in the global
instrumentation_finders list, they may or may not choose to honor this
attribute.

"""


def find_native_user_instrumentation_hook(cls):
    """Find user-specified instrumentation management for a class."""
    return getattr(cls, INSTRUMENTATION_MANAGER, None)

instrumentation_finders = [find_native_user_instrumentation_hook]
"""An extensible sequence of callables which return instrumentation
implementations

When a class is registered, each callable will be passed a class object.
If None is returned, the
next finder in the sequence is consulted.  Otherwise the return must be an
instrumentation factory that follows the same guidelines as
sqlalchemy_.ext.instrumentation.INSTRUMENTATION_MANAGER.

By default, the only finder is find_native_user_instrumentation_hook, which
searches for INSTRUMENTATION_MANAGER.  If all finders return None, standard
ClassManager instrumentation is used.

"""


class ExtendedInstrumentationRegistry(InstrumentationFactory):
    """Extends :class:`.InstrumentationFactory` with additional
    bookkeeping, to accommodate multiple types of
    class managers.

    """
    _manager_finders = weakref.WeakKeyDictionary()
    _state_finders = weakref.WeakKeyDictionary()
    _dict_finders = weakref.WeakKeyDictionary()
    _extended = False

    def _locate_extended_factory(self, class_):
        for finder in instrumentation_finders:
            factory = finder(class_)
            if factory is not None:
                manager = self._extended_class_manager(class_, factory)
                return manager, factory
        else:
            return None, None

    def _check_conflicts(self, class_, factory):
        existing_factories = self._collect_management_factories_for(class_).\
            difference([factory])
        if existing_factories:
            raise TypeError(
                "multiple instrumentation implementations specified "
                "in %s inheritance hierarchy: %r" % (
                    class_.__name__, list(existing_factories)))

    def _extended_class_manager(self, class_, factory):
        manager = factory(class_)
        if not isinstance(manager, ClassManager):
            manager = _ClassInstrumentationAdapter(class_, manager)

        if factory != ClassManager and not self._extended:
            # somebody invoked a custom ClassManager.
            # reinstall global "getter" functions with the more
            # expensive ones.
            self._extended = True
            _install_instrumented_lookups()

        self._manager_finders[class_] = manager.manager_getter()
        self._state_finders[class_] = manager.state_getter()
        self._dict_finders[class_] = manager.dict_getter()
        return manager

    def _collect_management_factories_for(self, cls):
        """Return a collection of factories in play or specified for a
        hierarchy.

        Traverses the entire inheritance graph of a cls and returns a
        collection of instrumentation factories for those classes. Factories
        are extracted from active ClassManagers, if available, otherwise
        instrumentation_finders is consulted.

        """
        hierarchy = util.class_hierarchy(cls)
        factories = set()
        for member in hierarchy:
            manager = self.manager_of_class(member)
            if manager is not None:
                factories.add(manager.factory)
            else:
                for finder in instrumentation_finders:
                    factory = finder(member)
                    if factory is not None:
                        break
                else:
                    factory = None
                factories.add(factory)
        factories.discard(None)
        return factories

    def unregister(self, class_):
        if class_ in self._manager_finders:
            del self._manager_finders[class_]
            del self._state_finders[class_]
            del self._dict_finders[class_]
        super(ExtendedInstrumentationRegistry, self).unregister(class_)

    def manager_of_class(self, cls):
        if cls is None:
            return None
        try:
            finder = self._manager_finders.get(cls, _default_manager_getter)
        except TypeError:
            # due to weakref lookup on invalid object
            return None
        else:
            return finder(cls)

    def state_of(self, instance):
        if instance is None:
            raise AttributeError("None has no persistent state.")
        return self._state_finders.get(
            instance.__class__, _default_state_getter)(instance)

    def dict_of(self, instance):
        if instance is None:
            raise AttributeError("None has no persistent state.")
        return self._dict_finders.get(
            instance.__class__, _default_dict_getter)(instance)


orm_instrumentation._instrumentation_factory = \
    _instrumentation_factory = ExtendedInstrumentationRegistry()
orm_instrumentation.instrumentation_finders = instrumentation_finders


class InstrumentationManager(object):
    """User-defined class instrumentation extension.

    :class:`.InstrumentationManager` can be subclassed in order
    to change
    how class instrumentation proceeds. This class exists for
    the purposes of integration with other object management
    frameworks which would like to entirely modify the
    instrumentation methodology of the ORM, and is not intended
    for regular usage.  For interception of class instrumentation
    events, see :class:`.InstrumentationEvents`.

    The API for this class should be considered as semi-stable,
    and may change slightly with new releases.

    .. versionchanged:: 0.8
       :class:`.InstrumentationManager` was moved from
       :mod:`sqlalchemy_.orm.instrumentation` to
       :mod:`sqlalchemy_.ext.instrumentation`.

    """

    # r4361 added a mandatory (cls) constructor to this interface.
    # given that, perhaps class_ should be dropped from all of these
    # signatures.

    def __init__(self, class_):
        pass

    def manage(self, class_, manager):
        setattr(class_, '_default_class_manager', manager)

    def dispose(self, class_, manager):
        delattr(class_, '_default_class_manager')

    def manager_getter(self, class_):
        def get(cls):
            return cls._default_class_manager
        return get

    def instrument_attribute(self, class_, key, inst):
        pass

    def post_configure_attribute(self, class_, key, inst):
        pass

    def install_descriptor(self, class_, key, inst):
        setattr(class_, key, inst)

    def uninstall_descriptor(self, class_, key):
        delattr(class_, key)

    def install_member(self, class_, key, implementation):
        setattr(class_, key, implementation)

    def uninstall_member(self, class_, key):
        delattr(class_, key)

    def instrument_collection_class(self, class_, key, collection_class):
        return collections.prepare_instrumentation(collection_class)

    def get_instance_dict(self, class_, instance):
        return instance.__dict__

    def initialize_instance_dict(self, class_, instance):
        pass

    def install_state(self, class_, instance, state):
        setattr(instance, '_default_state', state)

    def remove_state(self, class_, instance):
        delattr(instance, '_default_state')

    def state_getter(self, class_):
        return lambda instance: getattr(instance, '_default_state')

    def dict_getter(self, class_):
        return lambda inst: self.get_instance_dict(class_, inst)


class _ClassInstrumentationAdapter(ClassManager):
    """Adapts a user-defined InstrumentationManager to a ClassManager."""

    def __init__(self, class_, override):
        self._adapted = override
        self._get_state = self._adapted.state_getter(class_)
        self._get_dict = self._adapted.dict_getter(class_)

        ClassManager.__init__(self, class_)

    def manage(self):
        self._adapted.manage(self.class_, self)

    def dispose(self):
        self._adapted.dispose(self.class_)

    def manager_getter(self):
        return self._adapted.manager_getter(self.class_)

    def instrument_attribute(self, key, inst, propagated=False):
        ClassManager.instrument_attribute(self, key, inst, propagated)
        if not propagated:
            self._adapted.instrument_attribute(self.class_, key, inst)

    def post_configure_attribute(self, key):
        super(_ClassInstrumentationAdapter, self).post_configure_attribute(key)
        self._adapted.post_configure_attribute(self.class_, key, self[key])

    def install_descriptor(self, key, inst):
        self._adapted.install_descriptor(self.class_, key, inst)

    def uninstall_descriptor(self, key):
        self._adapted.uninstall_descriptor(self.class_, key)

    def install_member(self, key, implementation):
        self._adapted.install_member(self.class_, key, implementation)

    def uninstall_member(self, key):
        self._adapted.uninstall_member(self.class_, key)

    def instrument_collection_class(self, key, collection_class):
        return self._adapted.instrument_collection_class(
            self.class_, key, collection_class)

    def initialize_collection(self, key, state, factory):
        delegate = getattr(self._adapted, 'initialize_collection', None)
        if delegate:
            return delegate(key, state, factory)
        else:
            return ClassManager.initialize_collection(self, key,
                                                      state, factory)

    def new_instance(self, state=None):
        instance = self.class_.__new__(self.class_)
        self.setup_instance(instance, state)
        return instance

    def _new_state_if_none(self, instance):
        """Install a default InstanceState if none is present.

        A private convenience method used by the __init__ decorator.
        """
        if self.has_state(instance):
            return False
        else:
            return self.setup_instance(instance)

    def setup_instance(self, instance, state=None):
        self._adapted.initialize_instance_dict(self.class_, instance)

        if state is None:
            state = self._state_constructor(instance, self)

        # the given instance is assumed to have no state
        self._adapted.install_state(self.class_, instance, state)
        return state

    def teardown_instance(self, instance):
        self._adapted.remove_state(self.class_, instance)

    def has_state(self, instance):
        try:
            self._get_state(instance)
        except orm_exc.NO_STATE:
            return False
        else:
            return True

    def state_getter(self):
        return self._get_state

    def dict_getter(self):
        return self._get_dict


def _install_instrumented_lookups():
    """Replace global class/object management functions
    with ExtendedInstrumentationRegistry implementations, which
    allow multiple types of class managers to be present,
    at the cost of performance.

    This function is called only by ExtendedInstrumentationRegistry
    and unit tests specific to this behavior.

    The _reinstall_default_lookups() function can be called
    after this one to re-establish the default functions.

    """
    _install_lookups(
        dict(
            instance_state=_instrumentation_factory.state_of,
            instance_dict=_instrumentation_factory.dict_of,
            manager_of_class=_instrumentation_factory.manager_of_class
        )
    )


def _reinstall_default_lookups():
    """Restore simplified lookups."""
    _install_lookups(
        dict(
            instance_state=_default_state_getter,
            instance_dict=_default_dict_getter,
            manager_of_class=_default_manager_getter
        )
    )
    _instrumentation_factory._extended = False


def _install_lookups(lookups):
    global instance_state, instance_dict, manager_of_class
    instance_state = lookups['instance_state']
    instance_dict = lookups['instance_dict']
    manager_of_class = lookups['manager_of_class']
    orm_base.instance_state = attributes.instance_state = \
        orm_instrumentation.instance_state = instance_state
    orm_base.instance_dict = attributes.instance_dict = \
        orm_instrumentation.instance_dict = instance_dict
    orm_base.manager_of_class = attributes.manager_of_class = \
        orm_instrumentation.manager_of_class = manager_of_class
