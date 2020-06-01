#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

/*static inline boost::uuids::uuid StringToUuid(const std::string &str)
{
    boost::uuids::uuid u = boost::uuids::nil_uuid();
    std::stringstream uuidstring(str);
    uuidstring >> u;
    return u;
}*/

static inline boost::uuids::uuid CfgUuidSet(uint64_t ms_long, uint64_t ls_long) {
    boost::uuids::uuid u;
    for (int i = 0; i < 8; i++) {
        u.data[7 - i] = ms_long & 0xFF;
        ms_long = ms_long >> 8;
    }

    for (int i = 0; i < 8; i++) {
        u.data[15 - i] = ls_long & 0xFF;
        ls_long = ls_long >> 8;
    }
    return u;

}




int main() {
    boost::uuids::uuid uu = boost::uuids::random_generator()();
uint64_t lsms[][2] ={
{13793965218815312000L,17980812146320610000L},
{13793965218815312000L,17980752158512386000L},
{13793965218815312000L,17980876394736390000L},
{13793965218815312000L,17980743143376032000L},
{13793965218815312000L,17980723146008302000L},
{13793965218815312000L,17980820392657818000L},
{13793965218815312000L,17980907365745562000L},
{13793965218815312000L,17980739535603503000L},
{13793965218815312000L,17980633900882858000L},
{13793965218815312000L,17980930094712492000L},
{13793965218815312000L,17980835532417536000L},
{13793965218815312000L,17980850491788628000L},
{13793965218815312000L,17980746716788822000L}
};

uint64_t l= 13793965218815312000L, m= 17980907365745562000L;
 boost::uuids::uuid    uux=  CfgUuidSet(m,l);
std::cout << uux << std::endl;
exit(0);
for(int i =0;i<sizeof(lsms)/sizeof(uint64_t);i++)
{
 boost::uuids::uuid    uux=  CfgUuidSet(lsms[i][1],lsms[i][0]);
std::cout << uux << std::endl;
}


}


