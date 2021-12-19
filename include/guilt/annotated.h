#pragma once

#include "graph.h"
#include "tasking.h"

#if __GNUC__ >= 11
#define HAS_SOURCE_LOCATION
#include <source_location>
#endif

namespace guilt
{
struct region_state
{
    node_id start_node;
    node_id end_node;
};

struct context
{
    dependency_graph * graph;
    cluster_id current_function;
    node_id current_node;
#ifdef HAS_SOURCE_LOCATION
    std::source_location loc;

    context(
        dependency_graph * graph,
        cluster_id current_function,
        node_id current_node,
        std::source_location loc = std::source_location::current())
        : graph(graph), current_function(current_function), current_node(current_node), loc(loc)
    {
    }

    context(const context & ctx, std::source_location loc = std::source_location::current())
        : graph(ctx.graph), current_function(ctx.current_function), current_node(ctx.current_node), loc(loc)
    {
    }

    context(context &&) = default;

    context & operator=(const context &) = default;
    context & operator=(context &&) = default;
#endif
};

namespace detail
{
    struct annotation_shared_state
    {
        annotation_shared_state(context ctx) : captured_context(std::move(ctx))
        {
        }

        context captured_context;

        std::optional<cluster_id> function = std::nullopt;
        std::optional<region_state> region = std::nullopt;

        bool use_predecessor = true;
        bool use_captured = false;
    };
}

template<typename T>
class annotated_task;

template<typename T>
class annotated_pair;

template<typename T>
class annotated_promise_base;

template<typename F>
struct functional_awaiter
{
    F f;

    bool await_ready() const
    {
        return true;
    }
    void await_suspend(coro::coroutine_handle<>)
    {
        assert(0);
    }
    [[nodiscard]] decltype(f()) await_resume()
    {
        return f();
    }
};

template<typename F>
functional_awaiter(F) -> functional_awaiter<F>;

struct describe_function
{
    std::string name;
    std::string description;

#ifdef HAS_SOURCE_LOCATION
    describe_function(
        std::string name,
        std::string description = "",
        std::source_location loc = std::source_location::current())
        : name(std::move(name)),
          description(
              std::string("at ") + loc.file_name() + ":" + std::to_string(loc.line()) + "\n" + description)
    {
    }
#endif
};

struct describe_region
{
    std::string name;
    std::string description;

#ifdef HAS_SOURCE_LOCATION
    describe_region(
        std::string name,
        std::string description = "",
        std::source_location loc = std::source_location::current())
        : name(std::move(name)),
          description(
              std::string("at ") + loc.file_name() + ":" + std::to_string(loc.line()) + "\n" + description)
    {
    }
#endif
};

constexpr inline struct get_promise_t
{
} get_promise;

constexpr inline struct get_context_t
{
} get_context;

constexpr inline struct inherit_function_t
{
} inherit_function;

template<typename T>
class annotated_promise_base
{
protected:
    template<typename>
    friend class annotated_promise_base;

    friend class annotated_task<T>;

public:
    using task_type = task<T>;
    using value_type = T;

    template<typename... Other>
    annotated_promise_base(context ctx, Other &&... other)
        : _state{ std::move(ctx) },
          _wrapped{ coro::coroutine_handle<annotated_promise_base<T>>::from_promise(*this) }
    {
    }

    template<typename Class, typename... Other>
    annotated_promise_base(Class &&, context ctx, Other &&... other)
        : _state{ std::move(ctx) },
          _wrapped{ coro::coroutine_handle<annotated_promise_base<T>>::from_promise(*this) }
    {
    }

protected:
    void _set_value(::guilt::detail::replace_void_t<value_type> val = {})
    {
        _wrapped.set_value(std::move(val));
    }

public:
    coro::suspend_never initial_suspend() noexcept
    {
        return {};
    }
    auto final_suspend() noexcept
    {
        return _wrapped.final_suspend();
    }

    auto get_return_object()
    {
        return annotated_task{ this, _wrapped.get_return_object() };
    }

    void unhandled_exception()
    {
        _wrapped.unhandled_exception();
    }

    template<typename U>
    auto await_transform(U && u)
    {
        assert(_already_suspended);
        return std::forward<U>(u);
    }

    auto await_transform(describe_function desc)
    {
        auto & [name, description] = desc;

        assert(!_state.function);
        _state.function = _state.captured_context.graph->add_cluster(std::move(name), std::move(description));

        return coro::suspend_never();
    }

    auto await_transform(describe_region desc)
    {
        auto & [name, description] = desc;

        assert(_state.function);
        auto old = std::move(_state.region);

        auto & graph = *_state.captured_context.graph;

        region_state current;
        current.start_node =
            graph.add_node(_state.function.value(), "begin: " + name, std::move(description));
        current.end_node = graph.add_node(_state.function.value(), "end: " + name, std::move(description));
        graph.add_edge(current.start_node, current.end_node, edge_type::flow);

        if (old)
        {
            graph.add_edge(old.value().end_node, current.start_node, edge_type::flow);
        }

        if (_state.use_predecessor)
        {
            graph.add_edge(_state.captured_context.current_node, current.start_node, edge_type::flow);
            _state.use_predecessor = false;
        }

        _state.region = std::move(current);

        struct awaitable
        {
            bool await_ready()
            {
                return already_suspended;
            }

            void await_suspend(coro::coroutine_handle<>)
            {
            }

            auto await_resume()
            {
                return self->get_context();
            }

            bool already_suspended;
            annotated_promise_base * self;
        };

        auto ret = awaitable{ _already_suspended, this };

        if (!_already_suspended)
        {
            _already_suspended = true;
        }

        return ret;
    }

    template<typename task>
    auto await_transform(
        annotated_task<task> & fut
#ifdef HAS_SOURCE_LOCATION
        ,
        std::source_location loc = std::source_location::current()
#endif
    );

    template<typename task>
    auto await_transform(
        annotated_task<task> && fut
#ifdef HAS_SOURCE_LOCATION
        ,
        std::source_location loc = std::source_location::current()
#endif
    );

    auto await_transform(get_promise_t)
    {
        auto fn = [&]() -> auto &
        {
            return *this;
        };
        return functional_awaiter{ fn };
    }

    auto await_transform(get_context_t)
    {
        return functional_awaiter{ [&] { return get_context(); } };
    }

    auto await_transform(inherit_function_t)
    {
        return functional_awaiter{ [&]
                                   {
                                       _state.function = _state.captured_context.current_function;
                                       _state.use_captured = true;
                                   } };
    }

    void set_description(std::string name, std::string description = "")
    {
    }

    void set_region(std::string name, std::string description = "")
    {
    }

    auto get_region() const
    {
        assert(_state.region);
        return *_state.region;
    }

    auto & get_graph() const
    {
        return *_state.captured_context.graph;
    }

    auto get_context() const
    {
        assert(_state.region);
        return context{ _state.captured_context.graph, _state.function.value(), _state.region->start_node };
    }

private:
    detail::annotation_shared_state _state;
    promise<T> _wrapped;
    bool _already_suspended = std::is_same<decltype(_wrapped.initial_suspend()), coro::suspend_never>::value;
};

template<typename T>
class annotated_promise : public annotated_promise_base<T>
{
public:
    using annotated_promise_base<T>::annotated_promise_base;

    void return_value(T val)
    {
        this->_set_value(std::move(val));
    }
};

template<>
class annotated_promise<void> : public annotated_promise_base<void>
{
public:
    using annotated_promise_base<void>::annotated_promise_base;

    void return_void()
    {
        this->_set_value({});
    };
};

template<typename T = void>
class annotated_task
{
public:
    using value_type = T;
    using promise_type = annotated_promise<T>;

    annotated_task(
        annotated_promise_base<T> * promise,
        task<T> wrapped,
        std::optional<node_id> node = std::nullopt)
        : _promise{ promise }, _wrapped{ std::move(wrapped) }, _node{ std::move(node) }
    {
    }

    bool is_ready() const
    {
        return _wrapped.is_ready();
    }

    bool await_ready() const
    {
        return _wrapped.await_ready();
    }

    auto await_suspend(coro::coroutine_handle<> h)
    {
        return _wrapped.await_suspend(std::move(h));
    }

    auto await_resume()
    {
        return _wrapped.await_resume();
    }

    void start()
    {
        coro::coroutine_handle<annotated_promise_base<T>>::from_promise(*_promise).resume();
    }

    auto get_node()
    {
        if (_node)
        {
            return *_node;
        }

        assert(_promise->_state.region);
        return _promise->_state.region->end_node;
    }

private:
    annotated_promise_base<T> * _promise = nullptr;
    task<T> _wrapped;
    std::optional<node_id> _node;
};

template<typename T>
template<typename U>
auto annotated_promise_base<T>::await_transform(
    annotated_task<U> & task
#ifdef HAS_SOURCE_LOCATION
    ,
    std::source_location loc
#endif
)
{
    std::string label;
#ifdef HAS_SOURCE_LOCATION
    label = std::string("at ") + loc.file_name() + ":" + std::to_string(loc.line());
#endif

    assert(_state.region);
    _state.captured_context.graph->add_edge(
        task.get_node(), _state.region->end_node, edge_type::depend, std::move(label));

    // Sigh. Should just do an auto& return type and replicate the rvalue version, but...
    // ...GCC moves out of the returned lvalue reference in generated code. Sigh.

    struct awaiter_t
    {
        annotated_task<U> & task;

        bool await_ready()
        {
            return task.await_ready();
        }
        auto await_suspend(coro::coroutine_handle<> h)
        {
            return task.await_suspend(std::move(h));
        }
        auto await_resume()
        {
            return task.await_resume();
        }
    };

    return awaiter_t{ task };
}

template<typename T>
template<typename U>
auto annotated_promise_base<T>::await_transform(
    annotated_task<U> && task
#ifdef HAS_SOURCE_LOCATION
    ,
    std::source_location loc
#endif
)
{
    std::string label;
#ifdef HAS_SOURCE_LOCATION
    label = std::string("at ") + loc.file_name() + ":" + std::to_string(loc.line());
#endif

    assert(_state.region);
    _state.captured_context.graph->add_edge(
        task.get_node(), _state.region->end_node, edge_type::depend, label);

    return std::move(task);
}

template<typename... Ts>
annotated_task<std::tuple<::guilt::detail::replace_void_t<typename Ts::value_type>...>> when_all(
    context ctx,
    Ts... ts)
{
    co_await inherit_function;
    (void)co_await describe_region{ "when_all",
                                    ""
#ifdef HAS_SOURCE_LOCATION
                                    ,
                                    ctx.loc
#endif
    };
    co_return { (co_await ts)... };
}
}
