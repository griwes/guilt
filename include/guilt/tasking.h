#pragma once

#include <cassert>
#include <memory>
#include <optional>
#include <queue>
#include <tuple>
#include <variant>

#include <iostream>

// TODO: GCC appears to have a bug and not define the coroutine feature test macros correctly. If it did, the
// __has_includes would turn into `defined(__cpp_impl_coroutine)` for the IS and `defined(__cpp_coroutines)`
// for the TS.
#if __has_include(<coroutine>)
// C++20 coroutines
#include <coroutine>

namespace guilt
{
namespace coro = std;
}
#elif __has_include(<experimental/coroutine>)
// Coroutines TS
#include <experimental/coroutine>

namespace guilt
{
namespace coro = std::experimental;
}
#else
#error no coroutines support detected!
#endif

#include <reaver/function.h>

namespace guilt
{
namespace detail
{
    template<typename T>
    struct replace_void
    {
        using type = T;
    };

    template<>
    struct replace_void<void>
    {
        struct type
        {
        };
    };

    template<typename T>
    using replace_void_t = typename replace_void<T>::type;

    template<typename T, typename F>
    decltype(auto) void_call(T && val, F && f)
    {
        return std::forward<F>(f)(std::forward<T>(val));
    }

    template<typename F>
    auto void_call(replace_void_t<void>, F && f)
    {
        return std::forward<F>(f)();
    }
}

class execution_context
{
public:
    class executor
    {
    public:
        executor(execution_context * ctx) : _ctx{ ctx }
        {
        }

        template<typename F>
        void execute(F && f)
        {
            _ctx->_callbacks.push(std::forward<F>(f));
        }

    private:
        execution_context * _ctx = nullptr;
    };

    friend class executor;

    executor get_executor()
    {
        return executor{ this };
    }

    void handle_single()
    {
        _callbacks.front()();
        _callbacks.pop();
    }

    void handle_all()
    {
        while (_callbacks.size())
        {
            handle_single();
        }
    }

    template<typename F>
    void handle_all_until(F && f)
    {
        while (_callbacks.size() && !f())
        {
            handle_single();
        }
    }

private:
    using Callback = reaver::unique_function<void()>;
    std::queue<Callback> _callbacks;
};

execution_context & global_execution_context()
{
    static execution_context gl;
    return gl;
}

namespace detail
{
    template<typename T>
    class shared_state
    {
        struct value_type
        {
            T value;
        };

    public:
        void set_value(T t)
        {
            assert(_state.index() == 0);
            _state = value_type{ std::move(t) };
            _invoke_continuations();
        }

        void set_exception(std::exception_ptr ex)
        {
            assert(_state.index() == 0);
            _state = std::move(ex);
            _invoke_continuations();
        }

        bool is_ready() const
        {
            return _state.index() != 0;
        }

        template<typename F>
        void add_continuation(F && f)
        {
            if (is_ready())
            {
                std::forward<F>(f)();
            }

            _continuations.push_back(std::forward<F>(f));
        }

        const T & get_value() const
        {
            assert(_state.index() > 0);
            if (_state.index() == 1)
            {
                return std::get<1>(_state).value;
            }
            else
            {
                std::rethrow_exception(std::move(std::get<2>(_state)));
            }
        }

    private:
        void _invoke_continuations()
        {
            auto cs = std::move(_continuations);
            for (auto && c : cs)
            {
                c();
            }
        }

        std::variant<std::monostate, value_type, std::exception_ptr> _state;

        using Continuation = reaver::unique_function<void()>;
        std::vector<Continuation> _continuations;
    };
}

template<typename T>
class task;

template<typename T = void>
class promise_base
{
public:
    using task_type = task<T>;

    friend class task<T>;

    promise_base() : _self(coro::coroutine_handle<promise_base<T>>::from_promise(*this))
    {
    }

    promise_base(coro::coroutine_handle<> self) : _self(self)
    {
    }

    promise_base(const promise_base &) = delete;
    promise_base(promise_base &&) = delete;
    promise_base & operator=(const promise_base &) = delete;
    promise_base & operator=(promise_base &&) = delete;

    void set_value(::guilt::detail::replace_void_t<T> val = {})
    {
        _state.set_value(std::move(val));
    }

    void set_exception(std::exception_ptr ex)
    {
        _state.set_exception(ex);
    }

    coro::suspend_always initial_suspend() noexcept
    {
        return {};
    }

    coro::suspend_always final_suspend() noexcept
    {
        return {};
    }

    auto get_return_object()
    {
        return task<T>{ this };
    }

    void unhandled_exception()
    {
        set_exception(std::current_exception());
    }

private:
    detail::shared_state<::guilt::detail::replace_void_t<T>> _state;
    coro::coroutine_handle<> _self;
    std::size_t _refcount = 0;
};

template<typename T = void>
class promise : public promise_base<T>
{
public:
    using promise_base<T>::promise_base;

    void return_value(::guilt::detail::replace_void_t<T> t)
    {
        this->set_value(std::move(t));
    }
};

template<>
class promise<void> : public promise_base<void>
{
public:
    using promise_base<void>::promise_base;

    void return_void()
    {
        set_value({});
    }
};

template<typename T = void>
class task
{
    task(promise_base<T> * promise) : _promise{ promise }
    {
        _promise->_refcount += 1;
    }

public:
    using value_type = T;
    using promise_type = promise<T>;

    template<typename Other>
    using rebind = task<Other>;

    friend class promise_base<T>;

    task() = delete;

    ~task()
    {
        _release();
    }

    task(const task & other) : _promise{ other._promise }
    {
        _acquire();
    }

    task(task && other) : _promise{ std::exchange(other._promise, nullptr) }
    {
    }

    task & operator=(const task & other)
    {
        _release();
        _promise = other._promise;
        _acquire();
    }

    task & operator=(task && other)
    {
        _release();
        _promise = std::exchange(other._promise, nullptr);
    }

    bool is_ready() const
    {
        return _promise->_state.is_ready();
    }

    bool await_ready() const
    {
        return _promise->_state.is_ready();
    }

    auto await_suspend(coro::coroutine_handle<> h)
    {
        _promise->_state.add_continuation(
            [h = std::move(h)]() mutable
            { global_execution_context().get_executor().execute([h = std::move(h)]() mutable { h(); }); });
        return _promise->_self;
    }

    ::guilt::detail::replace_void_t<T> await_resume()
    {
        return _promise->_state.get_value();
    }

    void start()
    {
        global_execution_context().get_executor().execute([h = _promise->_self]() mutable { h(); });
    }

private:
    promise_base<T> * _promise;

    void _acquire()
    {
        assert(_promise);
        _promise->_refcount += 1;
    }

    void _release()
    {
        if (!_promise)
        {
            return;
        }

        _promise->_refcount -= 1;
        if (!_promise->_refcount)
        {
            _promise->_self.destroy();
        }
    }
};

template<typename T = void>
task<T> make_ready_task(::guilt::detail::replace_void_t<T> val = {})
{
    auto ret = [val = std::move(val)]() mutable -> task<T> { co_return std::move(val); };
    ret.start();
    return ret;
}

template<typename... Ts>
task<std::tuple<::guilt::detail::replace_void_t<typename Ts::value_type>...>> when_all(Ts... ts)
{
    co_return { (co_await ts)... };
}
}
