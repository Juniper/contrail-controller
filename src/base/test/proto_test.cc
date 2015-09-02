/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <memory>
#include <string>

#include <boost/mpl/at.hpp>
#include <boost/mpl/for_each.hpp>
#include <boost/mpl/int.hpp>
#include <boost/mpl/list.hpp>
#include <boost/mpl/vector.hpp>

#include "base/proto.h"

namespace mpl = boost::mpl;
using namespace std;

class Base {
public:
    virtual string TypeName() = 0;
};

class Foo : public Base {
public:
    virtual string TypeName() { return "Foo"; }
    static string ToString() { return "Foo"; }
};

class Bar : public Base {
public:
    virtual string TypeName() { return "Bar"; }
    static string ToString() { return "Bar"; }
};

template <typename Sequence = mpl::vector<> >
class Vector {
public:
    typedef typename Sequence::type VectorType;
    Vector() : var_(0) { }
    void inc() { var_++; };
    Base *getElement() {
        Base *element = NULL;
        switch (var_) {
        case 0: {
            typedef typename mpl::at_c<Sequence, 0>::type iter;
            element = new iter;
            break;
        }
        case 1: {    
            typedef typename mpl::at_c<Sequence, 1>::type iter;
            element = new iter;
            break;
        }
        }
        return element;
    }

    struct concatenator {
        concatenator(string *str) : strp(str) { }
        concatenator(const concatenator &rhs) : strp(rhs.strp) { }
        template <typename T>
        void operator()(T x) {
            strp->append(x.ToString());
        }
        string *strp;
    };

    string concat() {
        string str;
        concatenator cinstance(&str);
        mpl::for_each<Sequence>(cinstance);
        return str;
    }
private:
    int var_;
};

class MplTest : public ::testing::Test {
protected:
    Vector<mpl::vector<Foo, Bar> > vec_;
};

TEST_F(MplTest, Vector) {
    auto_ptr<Base> element;
    element.reset(vec_.getElement());
    EXPECT_EQ("Foo", element->TypeName());
    vec_.inc();
    element.reset(vec_.getElement());
    EXPECT_EQ("Bar", element->TypeName());
    EXPECT_EQ("FooBar", vec_.concat());
}

struct MapFind {
    MapFind(int value, string *result) : value(value), result(result) { }
    template <typename T>
    void operator()(T x) {
        if (T::first::value == value) {
            *result = T::second::ToString();
        }
    }
    int value;
    string *result;
};

TEST_F(MplTest, Map) {
    typedef mpl::map<mpl::pair<mpl::int_<10>, Foo>,
                     mpl::pair<mpl::int_<20>, Bar> > Choice;
    string result;
    MapFind find(10, &result);
    mpl::for_each<Choice>(find);
    EXPECT_EQ("Foo", result);
}

struct OneContext : public ParseObject {
};

struct TwoContext : public ParseObject {
};

class One : public ProtoElement<One> {
public:
    static const int kSize = 2;
    static bool Verifier(const void * obj, const uint8_t *data, size_t size,
                         ParseContext *context) {
        int value = get_short(data);
        return (value == 1);
    }
    static void Writer(const void *ctx, uint8_t *data, size_t size) {
        put_value(data, kSize, 1);
    }
};

class Two : public ProtoElement<Two> {
public:
    static const int kSize = 2;
    typedef TwoContext ContextType;
    static bool Verifier(const void *obj, const uint8_t *data, size_t size,
                         ParseContext *context) {
        int value = get_short(data);
        return (value == 2);
    }
    static void Writer(const TwoContext *ctx, uint8_t *data, size_t size) {
        put_value(data, kSize, 2);
    }
};

class OneTwoChoice : public ProtoChoice<OneTwoChoice> {
public:
    static const int kSize = 1;
    typedef mpl::map<mpl::pair<mpl::int_<1>, One>,
                     mpl::pair<mpl::int_<2>, Two> > Choice;
    
};

class OneTwoSequence : public ProtoSequence<OneTwoSequence> {
public:
    typedef mpl::list<One, Two> Sequence;
};


class ProtoTest : public ::testing::Test {
protected:    
};

TEST_F(ProtoTest, Element) {
    uint8_t m1[] = {0x04};
    uint8_t m2[] = {0x04, 0x05};
    uint8_t m3[] = {0x00, 0x02};
    ParseContext ctx;
    void *null = NULL;
    EXPECT_EQ(-1, Two::Parse(m1, sizeof(m1), &ctx, null));
    EXPECT_EQ(-1, Two::Parse(m2, sizeof(m2), &ctx, null));
    EXPECT_EQ(2, Two::Parse(m3, sizeof(m3), &ctx, null));
}

TEST_F(ProtoTest, Choice) {
    uint8_t m1[] = {0x04};
    uint8_t m2[] = {0x04, 0x05, 0x00};
    uint8_t m3[] = {0x01, 0x00, 0x01};
    uint8_t m4[] = {0x02, 0x00, 0x02};
    ParseContext ctx;
    void *null = NULL;
    EXPECT_EQ(-1, OneTwoChoice::Parse(m1, sizeof(m1), &ctx, null));
    EXPECT_EQ(-1, OneTwoChoice::Parse(m2, sizeof(m2), &ctx, null));
    
    ParseContext c1;
    EXPECT_EQ(3, OneTwoChoice::Parse(m3, sizeof(m3), &c1, null));
    
    ParseContext c2;
    EXPECT_EQ(3, OneTwoChoice::Parse(m4, sizeof(m4), &c2, null));
    
    uint8_t out[256];
    memset(out, 0, 256);
    TwoContext *c2data = static_cast<TwoContext *>(c2.data());
    ASSERT_TRUE(c2data != NULL);
    EncodeContext enc;
    EXPECT_EQ(sizeof(m4),
              OneTwoChoice::Encode(&enc, c2data, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(m4, out, sizeof(m4)));

    uint8_t out_f[2];
    memset(out_f, 0, 1);
    EXPECT_EQ(-1,
              OneTwoChoice::Encode(&enc, c2data, out_f, sizeof(out_f)));
}

TEST_F(ProtoTest, Sequence) {
    uint8_t m1[] = {0x04};
    uint8_t m2[] = {0x00, 0x01, 0x00};
    uint8_t m3[] = {0x00, 0x01, 0x00, 0x002};
    ParseContext ctx;
    void *null = NULL;
    EXPECT_EQ(-1, OneTwoSequence::Parse(m1, sizeof(m1), &ctx, null));
    EXPECT_EQ(-1, OneTwoSequence::Parse(m2, sizeof(m2), &ctx, null));
    EXPECT_EQ(4, OneTwoSequence::Parse(m3, sizeof(m3), &ctx, null));
}

struct ContextData : public ParseObject {
    ContextData() : first(0), second(0) { }
    int first;
    int second;
};

struct CtxFirst : public ProtoElement<CtxFirst> {
    static const int kSize = 1;
    typedef Accessor<ContextData, int, &ContextData::first> Setter;
};

struct CtxSecond : public ProtoElement<CtxSecond> {
    static const int kSize = 2;
    typedef Accessor<ContextData, int, &ContextData::second> Setter;
};


struct ContextSequence : public ProtoSequence<ContextSequence> {
    typedef ContextData ContextType;
    typedef mpl::list<CtxFirst, CtxSecond> Sequence;
};

TEST_F(ProtoTest, Context) {
    uint8_t m1[] = { 0x01, 0x00, 0x02 };
    ParseContext ctx;
    ContextData cdata;
    EXPECT_EQ(3, ContextSequence::Parse(m1, sizeof(m1), &ctx, &cdata));
    EXPECT_EQ(1, cdata.first);
    EXPECT_EQ(2, cdata.second);
    
    uint8_t output[256];
    EncodeContext enc;
    EXPECT_EQ(3, ContextSequence::Encode(&enc, &cdata, output, sizeof(output)));
    EXPECT_EQ(0, memcmp(m1, output, 3));
}

struct OptionTestData : public ParseObject {
    struct OptValue : public ParseObject {
        OptValue() : key(-1), value(-1) { }
        int key;
        int value;
    };
    vector<OptValue *> values;
};

struct OptOne : public ProtoElement<OptOne> {
    static const int kSize = 0;
    struct OptInit {
        void operator()(OptionTestData::OptValue *opt) {
            opt->key = 'A';
        }
    };
    struct OptMatch {
        bool match(const OptionTestData::OptValue *opt) {
            return opt->key == 'A';
        }
    };
    typedef OptInit ContextInit;
    typedef OptMatch ContextMatch;
};

struct OptTwo : public ProtoElement<OptTwo> {
    static const int kSize = 2;
    typedef Accessor<OptionTestData::OptValue, int,
        &OptionTestData::OptValue::value> Setter;
    struct OptInit {
        void operator()(OptionTestData::OptValue *opt) {
            opt->key = 'B';
        }
    };
    struct OptMatch {
        bool match(const OptionTestData::OptValue *opt) {
            return opt->key == 'B';
        }
    };
    typedef OptInit ContextInit;
    typedef OptMatch ContextMatch;
};

struct OptThree : public ProtoElement<OptThree> {
    static const int kSize = 1;
};

struct OptLen : public ProtoElement<OptLen> {
    static const int kSize = 1;
    typedef int SequenceLength;
};

struct OptType : public ProtoChoice<OptType> {
    static const int kSize = 1;
    typedef mpl::map<
        mpl::pair<mpl::int_<1>, OptOne>, 
        mpl::pair<mpl::int_<2>, OptTwo>,
        mpl::pair<mpl::int_<3>, OptThree> > Choice;
};

struct OptSequenceOpt : public ProtoSequence<OptSequenceOpt> {
    static const int kSize = 1;
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;
    typedef mpl::list<OptLen, OptType> Sequence;
    typedef CollectionAccessor<OptionTestData,
        vector<OptionTestData::OptValue *>,
        &OptionTestData::values> ContextStorer;
};

struct OptSequenceMessage : public ProtoSequence<OptSequenceMessage> {
    typedef OptionTestData ContextType;
    typedef mpl::list<One, OptSequenceOpt> Sequence;
};

TEST_F(ProtoTest, OptSequence) {
    uint8_t m1[] = { 0x00, 0x01, 0x06, 0x01, 0x01, 0x03, 0x02, 0x00, 0x02};
    uint8_t m2[] = { 0x00, 0x01, 0x06, 0x01, 0x01, 0x02, 0x02, 0x00, 0x02};
    uint8_t m3[] = { 0x00, 0x01, 0x06, 0x01, 0x01, 0x7e, 0x02, 0x00, 0x02};

    ParseContext ctx;
    OptionTestData tdata;
    EXPECT_EQ(sizeof(m1),
              OptSequenceMessage::Parse(m1, sizeof(m1), &ctx, &tdata));
    EXPECT_EQ(2, tdata.values.size());
    EXPECT_EQ('A', tdata.values[0]->key);
    EXPECT_EQ('B', tdata.values[1]->key);
    EXPECT_EQ(2, tdata.values[1]->value);
    
    ParseContext c2;
    OptionTestData d2;
    EXPECT_EQ(-1, OptSequenceMessage::Parse(m2, sizeof(m2), &c2, &d2));
    ParseContext c3;
    OptionTestData d3;
    EXPECT_EQ(-1, OptSequenceMessage::Parse(m3, sizeof(m3), &c3, &d3));

    uint8_t out[256];
    EncodeContext enc;
    EXPECT_EQ(sizeof(m1), OptSequenceMessage::Encode(&enc, &tdata, NULL, 0));
    EXPECT_EQ(sizeof(m1),
              OptSequenceMessage::Encode(&enc, &tdata, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(m1, out, sizeof(m1)));
}

struct VarLengthTestData : ParseObject {
    int num;
    string data;
};

struct VLNum : public ProtoElement<VLNum> {
    static const int kSize = 1;
    typedef Accessor<VarLengthTestData, int, &VarLengthTestData::num> Setter;
};
struct VLSize : public ProtoElement<VLSize> {
    static const int kSize = 1;
    typedef int SequenceLength;
};
struct VLData : public ProtoElement<VLData> {
    static const int kSize = -1;
    typedef Accessor<VarLengthTestData, string,
        &VarLengthTestData::data> Setter;
};
struct VarLengthSequence : public ProtoSequence<VarLengthSequence> {
    typedef mpl::list<VLNum, VLSize, VLData> Sequence;
};

TEST_F(ProtoTest, VarLengthElement) {
    uint8_t m1[] = { 0x01, 0x04, 'a', 'b', 'c', 'd' };
    ParseContext ctx;
    VarLengthTestData *dataptr = new VarLengthTestData;
    ctx.Push(dataptr);
    EXPECT_EQ(sizeof(m1),
              VarLengthSequence::Parse(m1, sizeof(m1), &ctx, dataptr));
    EXPECT_TRUE(dataptr->data == "abcd");
    uint8_t out[256];
    EncodeContext enc;
    EXPECT_EQ(sizeof(m1),
              VarLengthSequence::Encode(&enc, dataptr, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(m1, out, sizeof(m1)));
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    //
    // Enable this debug flag when required during debugging
    //
    // detail::debug_ = false;
    return RUN_ALL_TESTS();
}
