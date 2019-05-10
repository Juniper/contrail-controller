#include <string>
#include <tuple>
#include <boost/algorithm/string.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/optional.hpp>
#include <boost/ref.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/array.hpp>
#include <boost/variant.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>
#include "boost/date_time/gregorian/gregorian.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

char text[] = "hello dolly!";
boost::iterator_range<char*> v_iterator_range_1;
boost::iterator_range<char*> v_iterator_range_2 = boost::algorithm::find_last(text, "ll");

boost::optional<int> v_optional_1;
boost::optional<char*> v_optional_2(text);

int x = 42;
boost::reference_wrapper<int> v_reference_wrapper_1(x);

boost::logic::tribool v_tribool_1;
boost::logic::tribool v_tribool_2(boost::logic::indeterminate);

boost::scoped_ptr<int> v_scoped_ptr_1;
boost::scoped_ptr<int> v_scoped_ptr_2(new int(7));

boost::shared_ptr<int> v_shared_ptr_1;
boost::shared_ptr<int> v_shared_ptr_2(new int(9));

boost::circular_buffer<int> v_circular_buffer_1;
boost::circular_buffer<int> v_circular_buffer_2(3);

boost::array<int*, 3> v_array_1;
boost::array<short, 5> v_array_2 = {{0, 1, 2}};

boost::variant<int, std::string> v_variant_1;
boost::variant<char*, int> v_variant_2(x);

boost::uuids::uuid v_uuid_1;
boost::uuids::string_generator gen;
boost::uuids::uuid v_uuid_2 = gen("{01234567-89ab-cdef-0123-456789abcdef}");

                  //This is a base hook optimized for size
class MyClass : public boost::intrusive::set_base_hook<boost::intrusive::optimize_size<true> >
{
   int int_;

   public:
   //This is a member hook
   boost::intrusive::set_member_hook<> member_hook_;

   MyClass(int i)
      :  int_(i)
      {}
   friend bool operator< (const MyClass &a, const MyClass &b)
      {  return a.int_ < b.int_;  }
   friend bool operator> (const MyClass &a, const MyClass &b)
      {  return a.int_ > b.int_;  }
   friend bool operator== (const MyClass &a, const MyClass &b)
      {  return a.int_ == b.int_;  }
};

//Define a set using the base hook that will store values in reverse order
typedef boost::intrusive::set< MyClass, boost::intrusive::compare<std::greater<MyClass> > > BaseSet;
boost::intrusive::set<MyClass> v_intrusive_set_1;
boost::intrusive::set<MyClass> v_intrusive_set_2;

class MyClass_list : public boost::intrusive::list_base_hook<>   //This is a derivation hook
{
   int int_;

   public:
   //This is a member hook
   boost::intrusive::list_member_hook<> member_hook_;

   MyClass_list(int i)
      :  int_(i)
   {}
};

boost::intrusive::list<MyClass_list> v_intrusive_list_1;
boost::intrusive::list<MyClass_list> v_intrusive_list_2;

boost::gregorian::date v_gregorian_date_1;
boost::gregorian::date v_gregorian_date_2(boost::gregorian::day_clock::local_day());

boost::posix_time::ptime v_ptime_1;
boost::posix_time::ptime v_ptime_2(v_gregorian_date_2, boost::posix_time::hours(5));

int done()
{
    return 0;
}

int main(int argc, char* argv[])
{
    v_circular_buffer_2.push_back(1);
    v_circular_buffer_2.push_back(4);

    MyClass tmp(x);
    v_intrusive_set_2.insert(tmp);

    MyClass_list tmp_list(x);
    v_intrusive_list_2.push_front(tmp_list);

    int r = done();  // break here
    r += argc + (char)argv[0][0];
    return r % 2;
}
