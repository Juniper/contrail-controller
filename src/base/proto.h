/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_proto_h
#define ctrlplane_proto_h

#include <map>
#include <memory>

#include <vector>

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/function.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/type_traits/is_base_of.hpp>
#include <boost/mpl/equal_to.hpp>
#include <boost/mpl/for_each.hpp>
#include <boost/mpl/greater.hpp>
#include <boost/mpl/list.hpp>
#include <boost/mpl/map.hpp>
#include <boost/mpl/or.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/string.hpp>

#include "base/compiler.h"
#include "base/logging.h"
#include "base/parse_object.h"

namespace mpl = boost::mpl;

class ParseContext {
public:

    ParseContext();
    ~ParseContext();

    ParseObject *release();

    void Push(ParseObject *data);
    ParseObject *Pop();

    void SwapData(ParseObject *obj);
    void ReleaseData();

    ParseObject *data();

    void advance(int delta);
    int offset() const { return offset_; }

    void set_lensize(int lensize);
    int lensize() const;
    void set_size(size_t length);
    size_t size() const;
    void set_total_size();
    size_t total_size() const;

    void SetError(int error, int subcode, std::string type, const uint8_t *data,
                  int data_size);
    const ParseErrorContext &error_context() { return error_context_; }
private:

    ParseErrorContext error_context_;
    struct StackFrame;
    int offset_;
    std::vector<StackFrame *> stack_;
};

class EncodeContext {
public:
    typedef boost::function<void(EncodeContext *, uint8_t *, int, int)> CallbackType;

    EncodeContext();
    ~EncodeContext();

    void Push();
    void Pop(bool callback);
    void AddCallback(CallbackType cb, uint8_t *data, int arg);

    void advance(int delta);
    int length() const;

    void SaveOffset(std::string);
    EncodeOffsets &encode_offsets() { return offsets_; }
private:
    struct StackFrame;
    boost::ptr_vector<StackFrame> stack_;
    EncodeOffsets offsets_;
};

template <class C, typename T, T C::* Member>
struct Accessor {
    typedef T C::* member_ptr_t;
    static void set(C *obj, T value) {
        obj->*Member = value;
    }
    static T get(const C *obj) {
        return obj->*Member;
    }
};

template <class C, std::string C::* Member>
struct Accessor<C, std::string, Member> {
    static void set(C*obj, const uint8_t *data, size_t elem_size) {
        obj->*Member = std::string((const char *) data, elem_size);
    }
    static int size(const C *obj) {
        return (obj->*Member).size();
    }
    static std::string::const_iterator begin(const C *obj) {
        return (obj->*Member).begin();
    }
    static std::string::const_iterator end(const C *obj) {
        return (obj->*Member).end();
    }
};

template <class C, typename T, std::vector<T> C::* Member>
struct VectorAccessor {
    static void set(C*obj, const uint8_t *data, size_t elem_size) {
        obj->*Member = std::vector<T>();
        size_t size = sizeof(T);
        for (size_t i = 0; i < elem_size; i += size) {
            T value = get_value(data, size);
            data += size;
            (obj->*Member).push_back(value);
        }
    }
    static int size(const C *obj) {
        return (obj->*Member).size() * sizeof(T);
    }
    static typename std::vector<T>::const_iterator begin(const C *obj) {
        return (obj->*Member).begin();
    }
    static typename std::vector<T>::const_iterator end(const C *obj) {
        return (obj->*Member).end();
    }
};

// Extract the underlying type for pointer types.
template <typename T>
struct ValueType {
    typedef T type;
};
template <typename T>
struct ValueType<T *> {
    typedef T type;
};

template <typename Obj, typename Col, Col Obj::* Member>
struct CollectionAccessor {
    typedef typename ValueType<typename Col::value_type>::type ValueType;
    typedef Col CollectionType;
    typedef typename CollectionType::const_iterator iterator;
    static void insert(Obj *obj, ValueType *element) {
        (obj->*Member).push_back(element);
    }
    static iterator begin(const Obj *obj) {
        return (obj->*Member).begin();
    }
    static iterator end(const Obj *obj) {
        return (obj->*Member).end();
    }
};

// Interface
struct ElementBase {
    static const int kSize = 0;
    static const int kErrorCode = 0;
    static const int kErrorSubcode = 0;
    struct NullCtxInit {
        void operator()(void *) {
        }
    };
    struct NoMatch {
        bool match(const void *) {
            return false;
        }
    };
    static bool Verifier(const void * obj, const uint8_t *data, size_t size,
                         ParseContext *context) {
        return true;
    }

    typedef void SaveOffset;            // Save the offset in encode context
    typedef void ContextType;           // push a new context on the stack
    typedef NullCtxInit ContextInit;    // initialize the context data
    typedef NoMatch ContextMatch;
    typedef void Setter;
    typedef void EncodingCallback;
    typedef void ContextSwap;		// swap a context with another one
    typedef void SizeSetter;            // set the size of the element
};

struct ChoiceBase : public ElementBase {
    typedef void ContextStorer; // store the context data on pop.
};

class SequenceBase : public ElementBase {
public:
    static const int kMinOccurs = 1;
    static const int kMaxOccurs = 1;
    typedef void ContextStorer; // store the context data on pop.
};

#include "base/proto_impl.h"

template<class Derived>
class ProtoElement : public ElementBase {
public:
    typedef void SequenceLength;

    template <typename T>
    static void Writer(T *msg, uint8_t *data, size_t size) {
        typedef typename Derived::Setter setter_t;
        typedef typename mpl::if_<
            mpl::equal_to<
                mpl::int_<Derived::kSize>, mpl::int_<-1> >,
            detail::VariableLengthWriter<typename Derived::Setter, T>,
            detail::ApplyGetter<setter_t>
        >::type writer_t;
        writer_t writer;
        writer(data, Derived::kSize, msg);
    }

    template <typename T>
    static int Parse(const uint8_t *data, size_t size, ParseContext *context,
                     T *obj) {
        typedef typename mpl::if_<
            mpl::greater<
                mpl::int_<Derived::kSize>, mpl::int_<0> >,
            detail::SizeComparer<Derived>,
            detail::NopComparer
        >::type cmp_t;

        cmp_t cmp;
        if (!cmp(size)) {
            PROTO_DEBUG("Error: cmp(size) failed");
            context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                    TYPE_NAME(Derived), data,
                    Derived::kSize > 0 ? Derived::kSize : context->size());
            return -1;
        }
        if (!Derived::Verifier(obj, data, size, context)) {
            PROTO_DEBUG(TYPE_NAME(Derived) << " Verifier failed");
            context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                    TYPE_NAME(Derived), data,
                    Derived::kSize > 0 ? Derived::kSize : context->size());
            return -1;
        }

        typedef typename Derived::ContextInit ctx_init_t;
        ctx_init_t initializer;
        initializer(obj);

        detail::SequenceLengthSetter<typename Derived::SequenceLength, T> slen;
        int res = slen(context, data, Derived::kSize, size, obj);
        if (res < 0) {
            PROTO_DEBUG(TYPE_NAME(Derived) << ": error: Length Setter failed");
            context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                              TYPE_NAME(Derived), data, context->lensize());
            return -1;
        }

        typedef typename mpl::if_<boost::is_same<typename Derived::SizeSetter, void>,
            typename mpl::if_<
                mpl::equal_to<mpl::int_<Derived::kSize>, mpl::int_<-1> >,
                detail::VariableLengthSetter<typename Derived::Setter, T>,
                typename mpl::if_<
                    mpl::greater<mpl::int_<Derived::kSize>, mpl::int_<0> >,
                    detail::FixedLengthSetter<Derived, T>,
                    detail::NopSetter<Derived, T>
                >::type
            >::type,
            detail::LengthSizeSetter<typename Derived::SizeSetter, T>
        >::type setter_t;

        setter_t setter;
        res = setter(data, size, context, obj);
        if (res < 0) {
            context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                    TYPE_NAME(Derived), data,
                    Derived::kSize > 0 ? Derived::kSize : context->size());
        }
        return res;
    }

    template <typename T>
    static int Encode(EncodeContext *context, const T *msg, uint8_t *data,
                      size_t size) {
        typedef typename
            mpl::if_<boost::is_same<typename Derived::SizeSetter, void>,
            typename mpl::if_<mpl::equal_to<mpl::int_<Derived::kSize>, mpl::int_<-1> >,
                     detail::VarLengthSizeValue<typename Derived::Setter, T>,
                     detail::FixedLengthSizeValue<Derived> >::type,
            typename Derived::SizeSetter
        >::type size_value_t;

        int element_size = size_value_t::get(msg);
        if (data == NULL) {
            context->advance(element_size);
            return element_size;
        }
        assert(element_size >= 0);
        if (size < (size_t) element_size) {
            return -1;
        }

        // Setter overrides SequenceLength. Do not register a sequence length
        // callback if the element has defined a Setter.
        typename mpl::if_<
            boost::is_same<typename Derived::Setter, void>,
            detail::SequenceLengthAddCallback<typename Derived::SequenceLength>,
            detail::SequenceLengthAddCallback<void> >::type slen;
        slen(&ProtoElement::SequenceLengthWriteLen, context, data, element_size);

        detail::AddCallback<typename Derived::EncodingCallback> cbadd;
        cbadd(context, data, element_size);

        detail::SaveOffset<typename Derived::SaveOffset>()(context);

        Derived::Writer(msg, data, size);

        context->advance(element_size);
        return element_size;
    }

private:
    static void SequenceLengthWriteLen(EncodeContext *context, uint8_t *data,
                                       int offset, int element_size) {
        int length = context->length() - offset - element_size;
        put_value(data, element_size, length);
    }
};

template <typename Setter, typename T>
struct ChoiceSetter {
    void operator()(T *obj, int &value) {
        Setter::set(obj, value);
    }
};

template <typename T>
struct ChoiceSetter<void, T> {
    void operator()(T* obj, int value) { }
};

template <class Derived>
class ProtoChoice : public ChoiceBase {
public:
    template <typename T>
    static int Parse(const uint8_t *data, size_t size, ParseContext *context,
                     T *obj) {
        int advance = Derived::kSize;
        int value = -1;

        if (size < (size_t) advance) {
            context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                              TYPE_NAME(Derived), data, advance);
            return -1;
        }

        value = get_value(data, advance);

        data += advance;
        size -= advance;
        context->advance(advance);

        typedef typename mpl::if_<
            boost::is_same<typename Derived::Setter, void>,
            ChoiceSetter<void, T>,
            ChoiceSetter<typename Derived::Setter, T>
        >::type choice_setter_t;

        choice_setter_t setter;
        setter(obj, value);

        int result = ParseChoice(data, size, value, context, obj);
        if (result < 0) {
            PROTO_DEBUG(TYPE_NAME(Derived) << " ParseChoice failed");
            if (result == -2) {
                context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                              TYPE_NAME(Derived), data - advance, advance);
            }
            return result;
        }
        advance += result;
        return advance;
    }

    template <typename T>
    static int Encode(EncodeContext *context, const T *msg, uint8_t *data,
                      size_t size) {
        typedef typename Derived::Choice choice_t;
        int result = 0;
        ChoiceEncoder<T> encoder(context, msg, data, size, &result);
        mpl::for_each<choice_t>(encoder);
        return result;
    }

private:
    template <typename T>
    struct ChoiceMatcher {
        ChoiceMatcher(const uint8_t *data, size_t size, int value,
                      ParseContext *context, T *obj, int *resultp)
            : data(data), size(size), value(value), context(context),
              obj(obj), resultp(resultp), found(false) {
        }
        template <typename U>
        void operator()(U x) {
            if (found) return;
            if (U::first::value != -1 && U::first::value != value) {
                return;
            }
            found = true;
            typedef detail::DescendentParser<Derived, typename U::second>
                parser_t;
            *resultp = parser_t::Parse(data, size, context, obj);
        }
        const uint8_t *data;
        size_t size;
        int value;
        ParseContext *context;
        T *obj;
        int *resultp;
        bool found;
    };
    template <typename T>
    static int ParseChoice(const uint8_t *data, size_t size, int value,
                           ParseContext *context, T *obj) {
        int result = -2;
        ChoiceMatcher<T> match(data, size, value, context, obj, &result);
        mpl::for_each<typename Derived::Choice>(match);
        return result;
    }

    template <typename T>
    struct ChoiceEncoder {
        ChoiceEncoder(EncodeContext *context, const T *msg, uint8_t *data, int size,
                      int *resultp)
            : context(context), msg(msg), data(data), size(size),
            resultp(resultp) {
        }
        template <typename U, typename CtxType>
        struct EncoderTrue {
            int operator()(int opt, EncodeContext *context, const CtxType *msg,
                           uint8_t *data, int size) {
                if (Derived::kSize) {
                    if (data != NULL) {
                        if (size < Derived::kSize) return -1;
                        put_value(data, Derived::kSize, opt);
                        data += Derived::kSize;
                        size -= Derived::kSize;
                    }
                    context->advance(Derived::kSize);
                }
                int result = U::Encode(context, msg, data, size);
                if (result >= 0) {
                    result += Derived::kSize;
                }
                return result;
            }
        };
        template <typename U, typename CtxType>
        struct EncoderSetter {
            int operator()(int opt, EncodeContext *context, const CtxType *msg,
                    uint8_t *data, int size) {
                if (Derived::Setter::get(msg) == opt) {
                    return EncoderTrue<U, CtxType>()(opt, context, msg, data, size);
                }
                return 0;
            }
        };
        template <typename U>
        struct EncoderMatch {
            int operator()(int opt, EncodeContext *context, const T *msg,
                           uint8_t *data, int size) {
                typename U::ContextMatch matcher;
                if (matcher.match(msg)) {
                    return EncoderTrue<U, T>()(opt, context, msg, data, size);
                }
                return 0;
            }
        };
        template <typename U>
        struct EncoderRunTime {
            int operator()(int opt, EncodeContext *context, const T *msg,
                           uint8_t *data, int size) {
                if (typeid(*msg) == typeid(typename U::ContextType)) {
                    typedef typename U::ContextType ctx_t;
                    const ctx_t *ctx = static_cast<const ctx_t *>(msg);

                    typedef typename mpl::if_<
                        boost::is_same<typename Derived::Setter, void>,
                        EncoderTrue<U, ctx_t>,
                        EncoderSetter<U, ctx_t>
                    >::type encoder;
                    return encoder()(opt, context, ctx, data, size);
            	}
                return 0;
            }
        };
        struct EncoderNil {
            int operator()(int opt, EncodeContext *context, T *msg,
                           uint8_t *data, int size) {
                return 0;
            }
        };
        template <typename U>
        void operator()(U x) {
            if (*resultp < 0) {
                return;
            }
            // The choice element can be determined by:
            // 1. ContextType of the descendent or
            // 2. ContextMatch type of the descendent
            // In the case of ContextType match, the match can be either
            // performed at compile type (in case of exact match) or at run
            // type using RTTI.
            typedef typename mpl::if_<
                boost::is_same<typename U::second::ContextType, T>,
                EncoderTrue<typename U::second, T>,
                typename mpl::if_<
                    boost::is_same<typename U::second::ContextType, void>,
                    EncoderMatch<typename U::second>,
                    typename mpl::if_<
                        boost::is_base_of<T, typename U::second::ContextType>,
                          EncoderRunTime<typename U::second>,
                          EncoderNil
                        >::type
                    >::type
            >::type choice_t;
            choice_t choice;
            int result = choice(U::first::value, context, msg, data, size);
            if (result < 0) {
                *resultp = result;
            } else {
                *resultp += result;
            }
        }
    private:
        EncodeContext *context;
        const T *msg;
        uint8_t *data;
        int size;
        int *resultp;
    };
};

template <class Derived>
class ProtoSequence : public SequenceBase {
public:
    template <typename T>
    struct SequenceParser {
        SequenceParser(const uint8_t *data, size_t size, ParseContext *context,
            T *obj, int *resultp)
            : data(data), size(size), context(context), obj(obj),
              resultp(resultp) {
        }

        template <typename U>
        void operator()(U x) {
            if (*resultp < 0) {
                return;
            }
            typedef detail::DescendentParser<Derived, U> parser_t;
            size_t prev_size = context->size();
            int result = parser_t::Parse(data, size, context, obj);
            if (result < 0) {
                *resultp = result;
                return;
            }

            data += result;
            size -= result;
            context->advance(result);
            *resultp += result;

            if (context->size() != prev_size) {
                size = context->size();
                context->set_total_size();
            } else {
                context->set_size(prev_size - result);
            }
        }

        const uint8_t *data;
        size_t size;
        ParseContext *context;
        T *obj;
        int *resultp;
    };

    template <typename T>
    static int Parse(const uint8_t *data, size_t size, ParseContext *context,
                     T *obj) {
        int min = Derived::kMinOccurs;
        if (min == 0 && size == 0) {
            return 0;
        }
        if (!Derived::Verifier(obj, data, size, context)) {
            PROTO_DEBUG(TYPE_NAME(Derived) << " Verifier failed");
            context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                    TYPE_NAME(Derived), data,
                    Derived::kSize > 0 ? Derived::kSize : context->size());
            return -1;
        }
        int lensize = Derived::kSize;
        int length = size;
        if (lensize) {
            if (size < (size_t) lensize) {
                PROTO_DEBUG("Error: size = " << size << " lensize = " << lensize);
                context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                                  TYPE_NAME(Derived), data, lensize);
                return -1;
            }
            // TODO: options for length (include or exclude length field).
            length = get_value(data, lensize);
            assert(length >= 0);
            size -= lensize;
            if ((size_t) length > size) {
                PROTO_DEBUG("Error: length = " << length << " size = " << size);
                context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                                  TYPE_NAME(Derived), data, lensize);
                return -1;
            }
            data += lensize;
            context->advance(lensize);
        }
        int result = lensize;
        int max = Derived::kMaxOccurs;
        for (int i = 0; (max == -1 || i < max) && (length > 0); i++) {
            int sublen = 0;
            typedef typename Derived::ContextStorer ctx_access_t;
            typedef typename
            detail::ContextElementType<ctx_access_t>::ValueType child_obj_t;
            typedef typename mpl::if_<boost::is_same<typename Derived::ContextSwap, void>,
                    detail::ContextPush<T, child_obj_t>,
                    detail::NoContextPush<T>
            >::type ContextPush;
            ContextPush pushfn;
            typedef typename mpl::if_<boost::is_same<child_obj_t, void>,
                        T, child_obj_t>::type ctx_t;
            ctx_t *child_obj = pushfn(context, obj);

            SequenceParser<ctx_t> parser(data, length, context, child_obj, &sublen);
            mpl::for_each<typename Derived::Sequence>(parser);
            if (sublen < 0) {
                PROTO_DEBUG(TYPE_NAME(Derived) << ": error: sublen " << sublen);
                return -1;
            }
            if (sublen < (int)(context->size() + context->lensize())) {
                PROTO_DEBUG(TYPE_NAME(Derived) << ": error: sublen " << sublen
                        << " < " << context->size() << "+" << context->lensize());
                context->SetError(Derived::kErrorCode, Derived::kErrorSubcode,
                        TYPE_NAME(Derived), data,
                        context->size()+context->lensize());
                return -1;
            }
            result += sublen;
            data += sublen;
            length -= sublen;

            typedef typename mpl::if_<boost::is_same<typename Derived::ContextSwap, void>,
                    detail::ContextPop<Derived, T, child_obj_t>,
                    detail::ContextPop<Derived, void, void>
            >::type ContextPop;
            ContextPop popfn;
            popfn(context, obj);
        }
        return result;
    }

    template <typename T>
    struct SingleEncoder {
        typedef typename Derived::Sequence sequence_t;
        int operator()(EncodeContext *context, const T *msg, uint8_t *data,
                       size_t size) {
            int result = 0;
            SequenceEncoder<T> encoder(context, msg, data, size, &result);
            mpl::for_each<sequence_t>(encoder);
            return result;
        }
    };

    template <typename T>
    struct ListEncoder {
        typedef typename Derived::Sequence sequence_t;
        int operator()(EncodeContext *context, const T *msg, uint8_t *data,
                       size_t size) {
            typedef typename Derived::ContextStorer ctx_access_t;
            typedef typename
            detail::ContextElementType<ctx_access_t>::ValueType child_obj_t;
            int result = 0;

            detail::ContextIterator<ctx_access_t> iter(msg);
            while (iter.HasNext(msg)) {
                child_obj_t *child_obj = iter.Next();
                int subres = 0;
                context->Push();
                SequenceEncoder<child_obj_t> encoder(context, child_obj,
                                                     data, size, &subres);
                mpl::for_each<sequence_t>(encoder);
                if (subres < 0) {
                    result = subres;
                    break;
                }
                result += subres;
                if (data != NULL) {
                    data += subres;
                    size -= subres;
                }
                context->Pop(data != NULL);
            }
            return result;
        }
    };

    template <typename T>
    static int Encode(EncodeContext *context, const T *msg, uint8_t *data,
                      size_t size) {
        context->Push();
        detail::SaveOffset<typename Derived::SaveOffset>()(context);
        if (Derived::kSize > 0) {
            // context push callback
            if (data != NULL) {
                context->AddCallback(&SequenceLengthWriteLen, data, Derived::kSize);
                data += Derived::kSize;
                size -= Derived::kSize;
            }
            context->advance(Derived::kSize);
        }
        typedef typename mpl::if_<
            mpl::or_<
                mpl::equal_to<mpl::int_<Derived::kMaxOccurs>,
                              mpl::int_<-1> >,
                mpl::greater<mpl::int_<Derived::kMaxOccurs>,
                             mpl::int_<1> >
                >,
            ListEncoder<T>,
            SingleEncoder<T> >::type encoder_t;
        encoder_t encoder;
        int result = encoder(context, msg, data, size);
        if (result >= 0) {
            result += Derived::kSize;
            context->Pop(data != NULL);
        }
        return result;
    }

private:
    template <typename T>
    struct SequenceEncoder {
        SequenceEncoder(EncodeContext *context, const T *msg, uint8_t *data,
                        size_t size, int *resultp)
        : context(context), msg(msg), data(data), size(size),
          resultp(resultp) {
        }
        template <typename U>
        void operator()(U element) {
            if (*resultp < 0) {
                return;
            }
            int res = U::Encode(context, msg, data, size);
            if (res < 0) {
                *resultp = res;
            } else {
                *resultp += res;
            }
            if (data != NULL) {
                data += res;
                size -= res;
            }
        }

    private:
        EncodeContext *context;
        const T *msg;
        uint8_t *data;
        size_t size;
        int *resultp;
    };

    static void SequenceLengthWriteLen(EncodeContext *context, uint8_t *data,
                                       int offset, int arg) {
        int length = context->length() - Derived::kSize;
        put_value(data, Derived::kSize, length);
    }
};

#endif
