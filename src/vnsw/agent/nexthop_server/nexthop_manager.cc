/*
 * Implementation of NexthopManager class. It does the following:
 *   - instantiates NexthopServer object so that interested clients can
 *      register for nexthop notification.
 *   - registers with the vrouter-agent nexthopDB for "db.nexthop.0"
 *      table to get nexthop updates. The nexthops are actually stored
 *      in the NexthopServer object.
 */
#include "db/db.h"
#include "cmn/agent_signal.h"
#include "io/event_manager.h"
#include "nexthop_manager.h"
#include "oper/tunnel_nh.h"
#include <sys/wait.h>

NexthopManager::NexthopManager(EventManager *evm, const std::string &endpoint)
  : evm_(evm), nh_table_(NULL), nh_listener_(DBTableBase::kInvalidId),
    nh_server_(new NexthopDBServer(*(evm->io_service()), endpoint))
{
}

NexthopManager::~NexthopManager()
{
}

void
NexthopManager::Initialize(DB *database)
{
    nh_table_ = database->FindTable("db.nexthop.0");
    assert(nh_table_);
    nh_listener_ =
      nh_table_->Register(boost::bind(&NexthopManager::EventObserver, this,
                                      _1, _2));
}

void
NexthopManager::Terminate()
{
    nh_table_->Unregister(nh_listener_);
}

void
NexthopManager::EventObserver(DBTablePartBase *db_part, DBEntryBase *entry)
{
    NextHop *nh = static_cast <NextHop *>(entry);
    if (nh->GetType () == NextHop::TUNNEL) {
        TunnelNH *tnh = (TunnelNH *) nh;
        if (nh->IsDeleted()) {
            nh_server_->FindAndRemoveNexthop(tnh->GetDip()->to_string());
        } else {
            nh_server_->FindOrCreateNexthop(tnh->GetDip()->to_string());
        }
    }
}
