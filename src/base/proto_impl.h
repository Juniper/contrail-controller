/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE_PROTO_IMPL_H__
#define __BASE_PROTO_IMPL_H__

#include <vector>

namespace detail {

extern bool debug_;
#define PROTO_DEBUG(args...) if (detail::debug_) LOG(DEBUG, ##args)

template <typename T>
struct ApplySetter {
    template <typename U>
    void operator()(const uint8_t *data, int element_size, U *obj) {
        int value = get_value(data, element_size);
        T::set(obj, value);
    }
};

template <>
struct ApplySetter<void> {
    void operator()(const uint8_t *data, int element_size, void *obj) {
    }
};

template <typename T>
struct ApplyGetter {
    template <typename U>
    void operator()(uint8_t *data, int element_size, U *obj) {
        uint64_t value = T::get(obj);
        put_value(data, element_size, value);
    }
};
template <>
struct ApplyGetter<void> {
    template <typename U>
    void operator()(uint8_t *data, int element_size, U *obj) {
        memset(data, 0, element_size);
    }
};

template <typename Accessor, typename T>
struct VariableLengthWriter {
    template <typename Iterator>
    int Copy(Iterator begin, Iterator end, uint8_t *data) {
        int count = 0;
        for (Iterator iter = begin; iter != end; ++iter) {
            put_value(data, sizeof(*iter), *iter);
            data += sizeof(*iter);
            count+= sizeof(*iter);
        }
        return count;
    }
    int operator()(uint8_t *data, int element_size, T *obj) {
        int count = Copy(Accessor::begin(obj), Accessor::end(obj), data);
        return count;
    }
};
template <typename T>
struct VariableLengthWriter<void, T> {
    int operator()(uint8_t *data, int element_size, T *obj) {
        return 0;
    }
};

template <typename P, typename C>
struct ContextPush {
    C * operator()(ParseContext *context, P *obj) {
        C *child_obj = new C;
        context->Push(child_obj);
	return child_obj;
    }
};
template <typename P>
struct ContextPush<P, void> {
    P *operator()(ParseContext *context, P *obj) {
        context->ReleaseData();
        context->Push(obj);
        return obj;
    }
};

template <>
struct ContextPush<void, void> {
    void *operator()(ParseContext *context, void *obj) {
        return obj;
    }
};
template <typename T>
struct NoContextPush {
    T *operator()(ParseContext *context, T *obj) {
        return obj;
    }
};

template<typename T, typename ChildContextType>
struct StoreContext {
    template <typename C>
    void operator()(C *obj, ParseObject *context_obj) {
    	ChildContextType *child_obj =
    			dynamic_cast<ChildContextType *>(context_obj);
    	assert(child_obj);
        T::insert(obj, child_obj);
    }
};

template <typename ChildContextType>
struct StoreContext<void, ChildContextType> {
    void operator()(void *obj, ParseObject *child_obj) {
        delete child_obj;
    }
};

template <typename Parent, typename T, typename ChildContextType>
struct ContextPop {
    void operator()(ParseContext *context, T *obj) {
        StoreContext<typename Parent::ContextStorer,
        			ChildContextType> storer;
        storer(obj, context->Pop());
    }
};

template<typename Parent, typename T>
struct ContextPop<Parent, T, void> {
    void operator()(ParseContext *context, void *obj) {
        context->SwapData(context->Pop());
    }
};

template<typename Parent>
struct ContextPop<Parent, void, void> {
    void operator()(ParseContext *context, void *obj) {
    }
};

// If the child's ContextType is void the obj is copied to child_obj.
template <typename Child, typename T, typename opt_ctx_t>
struct DescendentContextPush {
    opt_ctx_t *operator()(ParseContext *context, T *obj) {
        ContextPush<T, opt_ctx_t> pushfn;
        return pushfn(context, obj);
    }
};

template <typename Child, typename T>
struct DescendentContextPush<Child, T, void>
 {
    typedef T ctx_t;
    typedef void pctx_t;
    T *operator()(ParseContext *context, T *obj) {
        return obj;
    }
};
template <typename Child>
struct DescendentContextSwap {
    typedef typename Child::ContextType ctx_t;
    template <typename U>
        ctx_t *operator()(ParseContext *context, U *obj) {
            typedef typename Child::ContextSwap swap_t;
            ctx_t *nobj = swap_t()(obj);
            if (nobj == NULL) {
                // TODO:
            }
            if (nobj != obj) {
                context->SwapData(nobj);
            }
            return nobj;
        }
};

template <class Parent, typename Child>
struct DescendentParser {
    template <typename T>
    static int Parse(const uint8_t *data, size_t size, ParseContext *context,
                     T *obj) {
    	// If the child defines ContextType, this changes the type of the
    	// top of stack element and causes us to push a new context into the
    	// stack.
    	// Context push and pop is only relevant if the child doesn't
    	// define a context swap. If it does, the swap operation will
    	// replace the existing stack frame data object.
        typedef typename Child::ContextType opt_ctx_t;

        typedef typename boost::mpl::if_<
        		boost::is_same<opt_ctx_t, void>,
        		T, opt_ctx_t>::type ctx_t;

        typedef typename boost::mpl::if_<
    			boost::is_same<typename Child::ContextSwap, void>,
    			DescendentContextPush<Child, T, typename Child::ContextType>,
    			DescendentContextSwap<Child> >::type context_op_t;

        ctx_t *child_obj = context_op_t()(context, obj);

        int result = Child::Parse(data, size, context, child_obj);
        if (result < 0) return result;
        typedef typename boost::mpl::if_<
    			boost::is_same<typename Child::ContextSwap, void>,
    			opt_ctx_t,
    			void>::type push_ctx_t;
        typedef typename boost::mpl::if_<
                        boost::is_same<push_ctx_t, void>,
                        void, T>::type pctx_t;
        ContextPop<Parent, pctx_t, push_ctx_t> popfn;
        popfn(context, obj);
        return result;
    }
};

template <typename T, typename Context>
struct SequenceLengthSetter {
    int operator()(ParseContext *context, const uint8_t *data, size_t size,
                   size_t msgsize, Context *obj) {
        T t;
        int value = t(obj, data, size);
        context->set_lensize(size);
        if ((size_t) value > msgsize) {
            PROTO_DEBUG(TYPE_NAME(Context) << " Sequence length error: "
                       << value << " > " << msgsize);
            return -1;
        }
        context->set_size(value);
        return 0;
    }
};

template <typename Context>
struct SequenceLengthSetter<int, Context> {
    int operator()(ParseContext *context, const uint8_t *data, size_t size,
                   size_t msgsize, Context *obj) {
        int value = get_value(data, size);
        context->set_lensize(size);
        if ((size_t) value > msgsize) {
            PROTO_DEBUG(TYPE_NAME(Context) << " Sequence length error: "
                    << value << " > " << msgsize);
            return -1;
        }
        context->set_size(value);
        return 0;
    }
};

template <typename Context>
struct SequenceLengthSetter<void, Context> {
    int operator()(ParseContext *context, const uint8_t *data, size_t size,
                   size_t msgsize, Context *obj) {
        return 0;
    }
};

template<typename T>
struct SequenceLengthAddCallback {
    void operator()(EncodeContext::CallbackType cb, EncodeContext *context,
                    uint8_t *data, int arg) {
        context->AddCallback(cb, data, arg);
    }
};
template<>
struct SequenceLengthAddCallback<void> {
    void operator()(EncodeContext::CallbackType cb, EncodeContext *context,
                    uint8_t *data, int arg) {
    }
};

template<typename Accessor, typename T>
struct VariableLengthSetter {
    int operator()(const uint8_t *data, size_t size, ParseContext *context,
                   T *obj) {
        int element_size = context->size();
        // If element size is unknown, read till the end of buffer
        if (element_size < 0) element_size = size;
        else if (size < (size_t) element_size) {
            PROTO_DEBUG(TYPE_NAME(T) << " Variable Length Setter failed "
                       << size << " < " << element_size);
            return -1;
        }
        Accessor::set(obj, data, element_size);
        return element_size;
    }
};
template<typename T>
struct VariableLengthSetter<void, T> {
    int operator()(const uint8_t *data, size_t size, ParseContext *context,
                   T *obj) {
        return 0;
    }
};

template<typename SizeSetter, typename T>
struct LengthSizeSetter {
    int operator()(const uint8_t *data, size_t size, ParseContext *context,
                   T *obj) {
        return SizeSetter::get(obj);
    }
};

template<typename Derived, typename T>
struct FixedLengthSetter {
    int operator()(const uint8_t *data, size_t size, ParseContext *context,
                   T *obj) {
        typedef typename Derived::Setter setter_t;
        detail::ApplySetter<setter_t> setter;
        setter(data, Derived::kSize, obj);
        return Derived::kSize;
    }
};

template<typename Derived, typename T>
struct NopSetter {
    int operator()(const uint8_t *data, size_t size, ParseContext *context,
                   T *obj) {
        return Derived::kSize;
    }
};

template <typename Derived>
struct SizeComparer {
    bool operator()(int size) {
        return size >= Derived::kSize;
    }
};

struct NopComparer {
    bool operator()(int size) {
        return true;
    }
};

template <typename Setter, typename T>    
struct VarLengthSizeValue {
    static int get(const T *msg) {
        return Setter::size(msg);
    }
};
template <typename T>
struct VarLengthSizeValue<void, T> {
    static int get(const T *msg) {
        return 0;
    }
};
template <typename Derived>
struct FixedLengthSizeValue {
    static int get(const void *msg) {
        return Derived::kSize;
    }
};

template <typename T>
struct AddCallback {
    void operator()(EncodeContext *context, uint8_t *data, int arg) {
        context->AddCallback(&T::Callback, data, arg);
    }
};
template <>
struct AddCallback<void>{
    void operator()(EncodeContext *context, uint8_t *data, int arg) {
    }
};

template <typename ContextAccessor>
struct ContextElementType {
    typedef typename ContextAccessor::ValueType ValueType;
};
template <>
struct ContextElementType<void> {
    typedef void ValueType;
};

template <typename Accessor>
struct ContextIterator {
    typedef typename Accessor::ValueType ValueType;
    template <typename Obj>
    ContextIterator(const Obj *obj)
    : iter(accessor.begin(obj)) {
        
    }

    ValueType * Next() {
        ValueType *obj = *iter;
        ++iter;
        return obj;
    }

    template <typename Obj>
    bool HasNext(Obj *obj) const {
        return (iter != accessor.end(obj));
    }

private:
    Accessor accessor;
    typename Accessor::CollectionType::const_iterator iter;
};
template <>
struct ContextIterator<void> {
    ContextIterator(void *obj) {
    }
    void * Next() { return NULL; }
    bool HasNext(void *obj) const { return false; }
};

template <typename T>
struct SaveOffset {
    void operator()(EncodeContext *ctx) {
        ctx->SaveOffset(T()());
    }
};

template <>
struct SaveOffset<void> {
    void operator()(EncodeContext *ctx) { }
};
    
}  // detail


#endif
