/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <iostream>
#include <boost/property_tree/xml_parser.hpp>
#include <pugixml/pugixml.hpp>

#include "base/logging.h"
#include "testing/gunit.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml/pugixml.hpp>
#include <boost/lexical_cast.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <io/event_manager.h>

#include "cfg/init_config.h"
#include "oper/operdb_init.h"

#include "pkt/pkt_init.h"
#include "test/test_cmn_util.h"
#include "test/pkt_gen.h"
#include "pkt/pkt_handler.h"
#include "pkt/pkt_init.h"
#include "pkt/flowtable.h"
#include "pkt/pkt_flow.h"

#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "xml/xml_pugi.h"
#include "test/test_cmn_util.h"

using namespace pugi;
using namespace std;
using namespace boost::property_tree;
using namespace boost::uuids;

struct Ace {
  string protocol;
  string src;
  string dst;
  string sp_s;
  string sp_e;
  string dp_s;
  string dp_e;
  string action;
};

struct Acl {
  string name;
  string id;
  std::vector<Ace> ace_l;
};

struct Vm {
  Vm() : id(0), name() {};
  int id;
  string name;
  PortInfo pinfo;
};

struct Vn {
  Vn() : id(0), acl_id(0), name(), vrf() {};
  int id;
  int acl_id;
  string name;
  string vrf;
  std::vector<Vm *> vm_l;
};

struct ExceptionPacket {
  string vn;
  string vm;
  string sip;
  string dip;
  string sp;
  string dp;
  string proto;
  string act;
};

std::vector<Vn *> vn_l;
std::map<int, Acl *> acl_l;
std::vector<ExceptionPacket *> excep_p_l;

class TestSetup {
 public:
  std::vector<Vn *> vn_l;
  std::map<int, Acl *> acl_l;
 private:
};

static string FileRead(const char *init_file) {
    ifstream ifs(init_file);
    string content ((istreambuf_iterator<char>(ifs) ),
                    (istreambuf_iterator<char>() ));
    return content;
}

// Construct ACL xmls
void ConstructAce(Ace &ace, string &astr) {
    istringstream tokens(astr);
    tokens >> ace.src;
    tokens >> ace.dst;
    tokens >> ace.sp_s;
    tokens >> ace.sp_e;
    tokens >> ace.dp_s;
    tokens >> ace.dp_e;
    tokens >> ace.protocol;
    tokens >> ace.action;
}

void ReadAcl(ptree &config) {
     for (ptree::const_iterator cfg_iter = config.begin(); cfg_iter != config.end();
	  ++cfg_iter) {
         if (cfg_iter->first != "acl") {
	    continue;
	 }
	 ptree acl_tree = cfg_iter->second;
	 int i = 0;
	 Acl *acl = new Acl();
	 acl->name = acl_tree.get<string>("name");
	 acl->id = acl_tree.get<string>("id");
	 int id = boost::lexical_cast<int>(acl->id);
	 LOG(DEBUG, "Acl name:" << acl->name);
	 for (ptree::const_iterator iter = acl_tree.begin(); iter != acl_tree.end();
	      ++iter, ++i) {
	     if (iter->first == "ace") {
	         string astr = iter->second.data();
		 LOG(DEBUG, "Ace :" << astr);
		 Ace ace;
		 ConstructAce(ace, astr);
		 LOG(DEBUG, "Source  :" << ace.src);
		 LOG(DEBUG, "Dest    :" << ace.dst);	
		 LOG(DEBUG, "SP Start:" << ace.sp_s);
		 LOG(DEBUG, "SP End  :" << ace.sp_e);
		 LOG(DEBUG, "DP Start:" << ace.dp_s);
		 LOG(DEBUG, "DP End  :" << ace.dp_e);
		 LOG(DEBUG, "Proto   :" << ace.protocol);
		 LOG(DEBUG, "Action  :" << ace.action);
		 acl->ace_l.push_back(ace);
	     }
	     ++i;
	 }
      
	 pair<map<int, Acl*>::iterator,bool> ret;
	 map<int, Acl*>::iterator it;
	 it = acl_l.find(id);
	 if (it != acl_l.end()) {
	     LOG(DEBUG, "Modifying existing Acl" << id);
	     delete (it->second);
	     acl_l.erase(id);
	 }
	 acl_l.insert(pair<int, Acl*>(id, acl));
     }
     return;
}

static int vm_id = 1;
void ReadVmPort(Vn &vn, string &vm_str)
{
     Vm *vm = new Vm();
     istringstream tokens(vm_str);
     tokens >> vm->name;
     vm->id = vm_id++;
     string ip_address;
     tokens >> ip_address;
     string port_name = vm->name + boost::lexical_cast<std::string>(vm->id);
     strncpy(vm->pinfo.name, port_name.c_str(), sizeof(vm->pinfo.name));
     vm->pinfo.vm_id = vm->id;
     vm->pinfo.intf_id = vm->id; // ?
     vm->pinfo.vn_id = vn.id;
     strncpy(vm->pinfo.addr, ip_address.c_str(), sizeof(vm->pinfo.addr));
     sprintf(vm->pinfo.mac, "00:00:00:00:00:%02x", vm->pinfo.intf_id);
     vn.vm_l.push_back(vm);
     
     LOG(DEBUG, "VM id:" << vm->id);
     LOG(DEBUG, "VN id:" << vn.id);
     LOG(DEBUG, "VRF  :" << vn.vrf);  
     LOG(DEBUG, "VM name   :" << vm->name);
     LOG(DEBUG, "VM Port id:" << vm->pinfo.intf_id);
     LOG(DEBUG, "VM PortName:" << vm->pinfo.name);
     LOG(DEBUG, "VM Mac add:" << vm->pinfo.mac);
     LOG(DEBUG, "VM Port addr:" << vm->pinfo.addr);
     LOG(DEBUG, "VM Port vnid:" << vm->pinfo.vn_id);
     LOG(DEBUG, "VM Port vmid:" << vm->pinfo.vm_id);
     LOG(DEBUG, "");
}


static int vn_id = 1;
void ReadVn(ptree &vn_tree, Vn &vn) {
    for (ptree::iterator iter = vn_tree.begin(); 
	 iter != vn_tree.end(); ++iter) {
         if (iter->first == "vn-name") {
	     vn.name = iter->second.data();
	     vn.id = vn_id++;
	 } else if (iter->first == "vrf-name") {
	     vn.vrf = iter->second.data();
	 } else if (iter->first == "vm") {
	     string pstr =  iter->second.data();
	     ReadVmPort(vn, pstr);
	 } else if (iter->first == "acl-id") {
	     vn.acl_id = boost::lexical_cast<int>(iter->second.data());
	 }
    }
    LOG(DEBUG, "No of VMs:" << vn.vm_l.size());
    LOG(DEBUG, "");
}

void ReadExceptionPacket(string &str) {
    istringstream tokens(str);
    ExceptionPacket *ep = new ExceptionPacket();
    tokens >> ep->vn;
    LOG(DEBUG, "Source Vn:" << ep->vn);
    tokens >> ep->vm;
    LOG(DEBUG, "Source Vm:" << ep->vm);
    tokens >> ep->sip;
    LOG(DEBUG, "sip:" << ep->sip);
    tokens >> ep->dip;
    LOG(DEBUG, "dip:" << ep->dip);
    tokens >> ep->sp;
    LOG(DEBUG, "sp:" << ep->sp);
    tokens >> ep->dp;
    LOG(DEBUG, "dp:" << ep->dp);
    tokens >> ep->proto;
    LOG(DEBUG, "protocol:" << ep->proto);
    tokens >> ep->act;
    LOG(DEBUG, "action:" << ep->act);
    excep_p_l.push_back(ep);    
}

void ReadExceptionPackets(ptree &config) {
    ptree pkt_tree = config.get_child("exception-packet");
    for (ptree::iterator iter = pkt_tree.begin();
	 iter != pkt_tree.end(); ++iter) {
         if (iter->first == "packet") {
	     ReadExceptionPacket(iter->second.data());
	 }
    }
}

void ReadVn(ptree &ccfg) {
    for (ptree::iterator iter = ccfg.begin(); iter != ccfg.end();
         ++iter) {
        if (iter->first == "vn") {
	   Vn *vn = new Vn();
	   ReadVn(iter->second, *vn);
	   vn_l.push_back(vn);
	   LOG(DEBUG, "Vn Name:" << vn->name);
	   LOG(DEBUG, "Vn id:" << vn->id);
           LOG(DEBUG, "");
	}
    }
}

void ReadSetupFile(char *sfile) {
    string str;
    if (sfile == NULL) {
        str = FileRead("src/vnsw/agent/test/test_setup_create.xml");
    } else {
        str = FileRead(sfile);
    }

    istringstream sstream(str);
    ptree tree;
    try {
        read_xml(sstream, tree, xml_parser::trim_whitespace);
    } catch (exception &e) {
        LOG(WARN, "Invalid config file" << e.what());
        return;
    }

    ptree config = tree.get_child("config");
    ptree ccfg = config.get_child("create-config");

    ReadVn(ccfg);
    ReadAcl(config);
    ReadExceptionPackets(config);
}

// Finds
Vn *FindVn(string &str) {
   std::vector<Vn *>::iterator it;
   for (it = vn_l.begin(); it != vn_l.end(); ++it) {
        if (((*it)->name.compare(str)) == 0) {
	    return (*it);
	}
   }
   return NULL;
}

Vm *FindVm(Vn &vn, string &str) {
   std::vector<Vm *>::iterator it;
   for (it = vn.vm_l.begin(); it != vn.vm_l.end(); ++it) {
        if ((*it)->name.compare(str) == 0) {
	    return (*it);
	}
   }
   return NULL;
}

Acl *FindAcl(int acl_id)
{
    map<int, Acl*>::iterator it;
    it = acl_l.find(acl_id);
    if (it == acl_l.end()) {
        return NULL;
    }
    return (it->second);
}


void CreateAceXmlNode(xml_node &xn, Ace &ace) {
    xml_node acl_rule = xn.append_child("acl-rule");
    xml_node mc = acl_rule.append_child("match-condition");

    // protocol str
    xml_node protocol = mc.append_child("protocol");
    protocol.append_child(node_pcdata).set_value(ace.protocol.c_str());

    // sa
    xml_node sa = mc.append_child("src-address");
    xml_node sa_vn = sa.append_child("virtual-network");
    sa_vn.append_child(node_pcdata).set_value(ace.src.c_str());

    // sp sp
    xml_node sp = mc.append_child("src-port");
    xml_node sp_sp = sp.append_child("start-port");
    sp_sp.append_child(node_pcdata).set_value(ace.sp_s.c_str());

    // sp ep
    // Copy sp to ep
    xml_node sp_ep = sp.append_child("end-port");
    sp_ep.append_child(node_pcdata).set_value(ace.sp_e.c_str());

    // da
    xml_node da = mc.append_child("dst-address");
    xml_node da_vn = da.append_child("virtual-network");
    da_vn.append_child(node_pcdata).set_value(ace.dst.c_str());

    // dp sp
    xml_node dp = mc.append_child("dst-port");
    xml_node dp_sp = dp.append_child("start-port");
    dp_sp.append_child(node_pcdata).set_value(ace.dp_s.c_str());

    // dp ep
    // Copy sp to ep
    xml_node dp_ep = dp.append_child("end-port");
    dp_ep.append_child(node_pcdata).set_value(ace.dp_e.c_str());

    xml_node al = acl_rule.append_child("action-list");
    xml_node saction = al.append_child("simple-action");
    saction.append_child(node_pcdata).set_value(ace.action.c_str());

    al.append_child("gateway-name");
}

void CreateIdPermXmlNode(xml_node &xn, Acl &acl) {
    xml_node idperm = xn.append_child("id-perms");
    xml_node perm = idperm.append_child("permissions");

    // cloud-admin
    xml_node owner = perm.append_child("owner");
    owner.append_child(node_pcdata).set_value("cloud-admin");

    // access 7
    xml_node owner_access = perm.append_child("owner-access");
    owner_access.append_child(node_pcdata).set_value("7");

    // cloud-admin-group
    xml_node group = perm.append_child("group");
    group.append_child(node_pcdata).set_value("cloud-admin-group");

    // access 7
    xml_node group_access = perm.append_child("group-access");
    group_access.append_child(node_pcdata).set_value("7");

    // access 7
    xml_node other_access = perm.append_child("other-access");
    other_access.append_child(node_pcdata).set_value("7");

    //Uuid
    xml_node uuid_n = idperm.append_child("uuid");
    xml_node uuid_ms = uuid_n.append_child("uuid-mslong");
    uuid_ms.append_child(node_pcdata).set_value("0");
    xml_node uuid_ls = uuid_n.append_child("uuid-lslong");
    uuid_ls.append_child(node_pcdata).set_value(acl.id.c_str());
    
    //enable
    xml_node enable = perm.append_child("enable");
    enable.append_child(node_pcdata).set_value("true");
}

void CreateAclXmlNode(xml_node &xn, Acl &acl) {
    xml_node node = xn.append_child("node");
    node.append_attribute("type") = "access-control-list";
    xml_node name = node.append_child("name");
    // name str
    // string name_str = "default-domain:1:vn1:access-control-list:default-domain:1:vn1";
    name.append_child(node_pcdata).set_value(acl.name.c_str());
    xml_node acl_xml_node = node.append_child("access-control-list-entries");

    std::vector<Ace>::iterator it;
    for (it = acl.ace_l.begin(); it != acl.ace_l.end(); ++it) {
      CreateAceXmlNode(acl_xml_node, *it);
    }
    CreateIdPermXmlNode(node, acl);    
}

void ConstructAclXmlDoc() {
    xml_document xdoc;
    xml_node msg = xdoc.append_child("config");
    xml_node update = msg.append_child("update");
    
    map<int, Acl*>::iterator it;
    for (it = acl_l.begin(); it != acl_l.end(); ++it) {
        CreateAclXmlNode(update, *(it->second));
    }

    xdoc.print(std::cout);
    xdoc.save_file("data.xml", "  ", pugi::format_default, pugi::encoding_utf8);

}



