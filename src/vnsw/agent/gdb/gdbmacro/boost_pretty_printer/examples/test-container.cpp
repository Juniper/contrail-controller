#include <boost/container/flat_set.hpp>
#include <boost/container/flat_map.hpp>

void break_here() {
  while (false) {}
}

typedef boost::container::flat_set<int> FlatSetOfInt;

void test_flat_set() {
  FlatSetOfInt fset;
  break_here();
  fset.insert(1);
  fset.insert(2);
  FlatSetOfInt::iterator itr = fset.find(2);
  FlatSetOfInt::const_iterator empty_itr;
  break_here();
}

typedef boost::container::flat_map<int, double> FlatMapInt2Double;

void test_flat_map() {
  FlatMapInt2Double fmap;
  break_here();
  fmap[1] = 1.0;
  fmap[2] = 2.0;
  FlatMapInt2Double::iterator itr = fmap.find(2);
  FlatMapInt2Double::const_iterator empty_itr;
  break_here();
}

int main(int argc, char* argv[])
{
  test_flat_set();
  test_flat_map();
  return 0;
}
