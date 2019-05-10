#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <tuple>


int negative(int x)
{
    return -x;
}

int done()
{
    return 0;
}

typedef boost::multi_index_container<
    int,
    boost::multi_index::indexed_by<
        boost::multi_index::sequenced<>,
        boost::multi_index::ordered_unique<
            boost::multi_index::identity< int >
            >,
        boost::multi_index::random_access<>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::global_fun< int, int, &negative >
            >,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::identity< int >
            >
        >
    > Int_Set;

Int_Set s;


int main(int argc, char* argv[])
{
    (void)argv;
    s.insert(s.end(), argc);
    s.insert(s.end(), 5);
    s.insert(s.end(), 17);
    s.insert(s.end(), 4);
    s.insert(s.end(), 14);
    s.insert(s.end(), 3);
    s.insert(s.end(), 9);
    int r = done();  // break here
    for (Int_Set::iterator it = s.begin(); it != s.end(); ++it)
    {
        r += *it;
    }
    return r % 2;
}
