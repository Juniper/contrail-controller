#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>

void break_here() {
    while (false) {}
}

struct IntListElement : public boost::intrusive::list_base_hook<> {
  IntListElement(int i) : int_(i) {}
  int int_;
  boost::intrusive::list_member_hook<> member_hook_;
};

typedef boost::intrusive::member_hook<
  IntListElement,
  boost::intrusive::list_member_hook<>,
  &IntListElement::member_hook_>
ListMemberOption;

typedef boost::intrusive::list<IntListElement> BaseList;
typedef boost::intrusive::list<
  IntListElement, boost::intrusive::constant_time_size<false> >
BaseListNoSize;

typedef boost::intrusive::list<IntListElement, ListMemberOption> MemberList;
typedef boost::intrusive::list<
  IntListElement,
  ListMemberOption,
  boost::intrusive::constant_time_size<false> >
MemberListNoSize;

void test_intrusive_list() {
  {
    BaseList blist;
    break_here();
    IntListElement elem1(1);
    IntListElement elem2(2);
    IntListElement elem3(3);
    blist.push_back(elem1);
    blist.push_back(elem2);
    blist.push_back(elem3);
    break_here();

    MemberList mlist;
    break_here();
    mlist.push_back(elem2);
    mlist.push_back(elem3);
    break_here();

    blist.clear();
    mlist.clear();
  }

  {
    BaseListNoSize blist;
    break_here();
    IntListElement elem1(1);
    IntListElement elem2(2);
    IntListElement elem3(3);
    blist.push_back(elem1);
    blist.push_back(elem2);
    blist.push_back(elem3);
    break_here();

    MemberListNoSize mlist;
    break_here();
    mlist.push_back(elem2);
    mlist.push_back(elem3);
    break_here();

    blist.clear();
    mlist.clear();
  }
}

struct IntSetElement : public boost::intrusive::set_base_hook<> {
  IntSetElement(int i) : int_(i) {}

  bool operator<(const IntSetElement& rhs) const { return int_ < rhs.int_; }
  int int_;
  boost::intrusive::set_member_hook<> member_hook_;
};

typedef boost::intrusive::member_hook<
  IntSetElement,
  boost::intrusive::set_member_hook<>,
  &IntSetElement::member_hook_>
SetMemberOption;

typedef boost::intrusive::set<IntSetElement> BaseSet;
typedef boost::intrusive::set<
  IntSetElement, boost::intrusive::constant_time_size<false> >
BaseSetNoSize;

typedef boost::intrusive::set<IntSetElement, SetMemberOption> MemberSet;
typedef boost::intrusive::set<
  IntSetElement, SetMemberOption, boost::intrusive::constant_time_size<false> >
MemberSetNoSize;

void test_intrusive_set() {
  {
    BaseSet bset;
    break_here();
    IntSetElement elem1(1);
    IntSetElement elem2(2);
    IntSetElement elem3(3);
    bset.insert(elem1);
    bset.insert(elem2);
    bset.insert(elem3);
    break_here();

    MemberSet mset;
    break_here();
    mset.insert(elem2);
    mset.insert(elem3);
    break_here();

    bset.clear();
    mset.clear();
  }

  {
    BaseSetNoSize bset;
    break_here();
    IntSetElement elem1(1);
    IntSetElement elem2(2);
    IntSetElement elem3(3);
    bset.insert(elem1);
    bset.insert(elem2);
    bset.insert(elem3);
    break_here();

    MemberSetNoSize mset;
    break_here();
    mset.insert(elem2);
    mset.insert(elem3);
    break_here();

    bset.clear();
    mset.clear();
  }
}

int main(int argc, char* argv[])
{
  test_intrusive_list();
  test_intrusive_set();
  return argc + (char)argv[0][0];
}
