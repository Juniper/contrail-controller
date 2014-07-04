#include "ifmap/ifmap_config_listener.h"

IFMapConfigListenerTypes::ConfigDelta::ConfigDelta() {
}

IFMapConfigListenerTypes::ConfigDelta::ConfigDelta(
  const IFMapConfigListenerTypes::ConfigDelta &rhs)
    : id_type(rhs.id_type), id_name(rhs.id_name),
      node(rhs.node), obj(rhs.obj) {
}
