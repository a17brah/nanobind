#define NB_TYPE_CASTER(type, descr)                                            \
protected:                                                                     \
    type value;                                                                \
                                                                               \
public:                                                                        \
    static constexpr auto cname = descr;                                       \
    static handle cast(type *p, rv_policy policy, handle parent) {   \
        if (!p)                                                                \
            return none().release();                                           \
        return cast(*p, policy, parent);                                       \
    }                                                                          \
    operator type *() { return &value; }                                       \
    operator type &() { return value; }                                        \
    template <typename T_> using cast_op_type = cast_op_type<T_>;

NAMESPACE_BEGIN(NB_NAMESPACE)

/// Approach used to cast a previously unknown C++ instance into a Python object
enum class rv_policy {
    /** This is the default return value policy, which falls back to the policy
        rv_policy::take_ownership when the return value is a pointer.
        Otherwise, it uses return_value::move or return_value::copy for rvalue
        and lvalue references, respectively. See below for a description of what
        all of these different policies do. */
    automatic,

    /** As above, but use policy rv_policy::reference when the return
        value is a pointer. This is the default conversion policy for function
        arguments when calling Python functions manually from C++ code (i.e. via
        handle::operator()). You probably won't need to use this. */
    automatic_reference,

    /** Reference an existing object (i.e. do not create a new copy) and take
        ownership. Python will call the destructor and delete operator when the
        object’s reference count reaches zero. Undefined behavior ensues when
        the C++ side does the same.. */
    take_ownership,

    /** Create a new copy of the returned object, which will be owned by
        Python. This policy is comparably safe because the lifetimes of the two
        instances are decoupled. */
    copy,

    /** Use std::move to move the return value contents into a new instance
        that will be owned by Python. This policy is comparably safe because the
        lifetimes of the two instances (move source and destination) are
        decoupled. */
    move,

    /** Reference an existing object, but do not take ownership. The C++ side
        is responsible for managing the object’s lifetime and deallocating it
        when it is no longer used. Warning: undefined behavior will ensue when
        the C++ side deletes an object that is still referenced and used by
        Python. */
    reference,

    /** This policy only applies to methods and properties. It references the
        object without taking ownership similar to the above
        rv_policy::reference policy. In contrast to that policy, the
        function or property’s implicit this argument (called the parent) is
        considered to be the the owner of the return value (the child).
        nanobind then couples the lifetime of the parent to the child via a
        reference relationship that ensures that the parent cannot be garbage
        collected while Python is still using the child. More advanced
        variations of this scheme are also possible using combinations of
        rv_policy::reference and the keep_alive call policy */
    reference_internal
};

NAMESPACE_BEGIN(detail)

template <typename T, typename SFINAE = int> struct type_caster;
template <typename T> using make_caster = type_caster<intrinsic_t<T>>;

template <typename T>
using cast_op_type =
    std::conditional_t<std::is_pointer_v<std::remove_reference_t<T>>,
                       typename std::add_pointer_t<intrinsic_t<T>>,
                       typename std::add_lvalue_reference_t<intrinsic_t<T>>>;

template <typename T>
struct type_caster<T, enable_if_t<std::is_arithmetic_v<T> && !is_std_char_v<T>>> {
    using T0 = std::conditional_t<sizeof(T) <= sizeof(long), long, long long>;
    using T1 = std::conditional_t<std::is_signed_v<T>, T0, std::make_unsigned_t<T0>>;
    using Tp = std::conditional_t<std::is_floating_point_v<T>, double, T1>;
public:
    bool load(handle src, bool convert) noexcept {
        Tp value_p;

        if (!src.is_valid())
            return false;

        if constexpr (std::is_floating_point_v<T>) {
            if (!convert && !PyFloat_Check(src.ptr()))
                return false;
            value_p = (Tp) PyFloat_AsDouble(src.ptr());
        } else {
            if (!convert && !PyLong_Check(src.ptr()))
                return false;

            if constexpr (std::is_unsigned_v<Tp>) {
                value_p = sizeof(T) <= sizeof(long)
                              ? (Tp) PyLong_AsUnsignedLong(src.ptr())
                              : (Tp) PyLong_AsUnsignedLongLong(src.ptr());
            } else {
                value_p = sizeof(T) <= sizeof(long)
                              ? (Tp) PyLong_AsLong(src.ptr())
                              : (Tp) PyLong_AsLongLong(src.ptr());
            }

            if constexpr (sizeof(Tp) != sizeof(T)) {
                if (value_p != (Tp) (T) value_p)
                    return false;
            }
        }

        if (value_p == Tp(-1) && PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }

        value = (T) value_p;
        return true;
    }

    static handle cast(T src, rv_policy /* policy */,
                       handle /* parent */) noexcept {
        if constexpr (std::is_floating_point_v<T>)
            return PyFloat_FromDouble((double) src);
        else if constexpr (std::is_unsigned_v<T> && sizeof(T) <= sizeof(long))
            return PyLong_FromUnsignedLong((unsigned long) src);
        else if constexpr (std::is_signed_v<T> && sizeof(T) <= sizeof(long))
            return PyLong_FromLong((long) src);
        else if constexpr (std::is_unsigned_v<T>)
            return PyLong_FromUnsignedLongLong((unsigned long long) src);
        else if constexpr (std::is_signed_v<T>)
            return PyLong_FromLongLong((long long) src);
        else
            fail("invalid number cast");
    }

    NB_TYPE_CASTER(T, const_name<std::is_integral_v<T>>("int", "float"));
};

template <> struct type_caster<std::nullptr_t> {
    bool load(handle src, bool) noexcept {
        if (src && src.is_none())
            return true;
        return false;
    }

    static handle cast(std::nullptr_t, rv_policy /* policy */,
                       handle /* parent */) noexcept {
        return none().inc_ref();
    }

    NB_TYPE_CASTER(std::nullptr_t, const_name("None"));
};

template <> struct type_caster<bool> {
    bool load(handle src, bool) noexcept {
        if (src.ptr() == Py_True) {
            value = true;
            return true;
        } else if (src.ptr() == Py_False) {
            value = false;
            return true;
        } else {
            return false;
        }
    }

    static handle cast(bool src, rv_policy /* policy */,
                       handle /* parent */) noexcept {
        return handle(src ? Py_True : Py_False).inc_ref();
    }

    NB_TYPE_CASTER(bool, const_name("bool"));
};

template <typename T1, typename T2> struct type_caster<std::pair<T1, T2>> {
    using T = std::pair<T1, T2>;
    using C1 = make_caster<T1>;
    using C2 = make_caster<T2>;

    NB_TYPE_CASTER(T, const_name("Tuple[") + concat(C1::cname, C2::cname) +
                          const_name("]"))

    bool load(handle src, bool convert) noexcept {
        PyObject *o[2];

        if (!seq_size_fetch(src.ptr(), 2, o))
            return false;

        C1 c1;
        C2 c2;

        if (!c1.load(o[0], convert))
            goto fail;
        if (!c2.load(o[1], convert))
            goto fail;

        value.first  = std::move(c1.value);
        value.second = std::move(c2.value);

        return true;

    fail:
        Py_DECREF(o[0]);
        Py_DECREF(o[1]);
        return false;
    }

    static handle cast(const T &value, rv_policy policy,
                       handle parent) noexcept {
        object o1 = steal(C1::cast(value.first,  policy, parent));
        if (!o1.is_valid())
            return handle();

        object o2 = steal(C2::cast(value.second, policy, parent));
        if (!o2.is_valid())
            return handle();

        PyObject *r = PyTuple_New(2);
        PyTuple_SET_ITEM(r, 0, o1.release().ptr());
        PyTuple_SET_ITEM(r, 1, o2.release().ptr());
        return r;
    }
};

template <typename T>
struct type_caster<T, enable_if_t<std::is_base_of_v<handle, T>>> {
public:
    NB_TYPE_CASTER(T, T::cname)

    bool load(handle src, bool convert) noexcept {
        if (!isinstance<T>(src))
            return false;

        if constexpr (std::is_same_v<T, handle>)
            value = src;
        else
            value = borrow<T>(src);

        return true;
    }

    static handle cast(const handle &src, rv_policy /* policy */,
                       handle /* parent */) noexcept {
        return src.inc_ref();
    }
};

template <typename T, typename SFINAE> struct type_caster {
    static constexpr auto cname = const_name<T>();

    NB_INLINE bool load(handle src, bool convert) noexcept {
        return detail::type_get(&typeid(T), src.ptr(), convert, (void **) &value);
    }

    NB_INLINE static handle cast(const T *p, rv_policy policy,
                                 handle parent) noexcept {
        if (policy == rv_policy::automatic)
            policy = rv_policy::take_ownership;
        else if (policy == rv_policy::automatic_reference)
            policy = rv_policy::reference;

        return cast_impl(p, policy, parent);
    }

    NB_INLINE static handle cast(const T &p, rv_policy policy,
                                 handle parent) noexcept {
        if (policy == rv_policy::automatic ||
            policy == rv_policy::automatic_reference)
            policy = rv_policy::copy;

        return cast_impl(&p, policy, parent);
    }

    NB_INLINE static handle cast(const T &&p, rv_policy /* policy */,
                                 handle parent) noexcept {
        return cast_impl(&p, rv_policy::move, parent);
    }

    NB_INLINE static handle cast_impl(const T *p, rv_policy policy,
                                      handle parent) noexcept {
        return detail::type_put(&typeid(T), (void *) p, policy, parent.ptr());
    }

    operator T*() { return value; }
    operator T&() { return *value; }

    template <typename T_> using cast_op_type = cast_op_type<T_>;

private:
    T *value;
};


NAMESPACE_END(detail)

template <typename T>
object cast(T &&value, rv_policy policy = rv_policy::move,
            handle parent = handle()) {
    detail::make_caster<T> caster;
    return steal(caster.cast(std::forward<T>(value), policy, parent));
}

NAMESPACE_BEGIN(detail)

template <typename Policy> template <typename T>
accessor<Policy>& accessor<Policy>::operator=(T &&value) {
    Policy::set(m_obj, m_key, cast((detail::forward_t<T>) value));
    return *this;
}

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)