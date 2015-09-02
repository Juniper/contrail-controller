#include "ifmap/ifmap_factory.h"

template <>
IFMapFactory *Factory<IFMapFactory>::singleton_ = NULL;

#include "ifmap/ifmap_xmpp.h"
FACTORY_STATIC_REGISTER(IFMapFactory, IFMapXmppChannel, IFMapXmppChannel);
