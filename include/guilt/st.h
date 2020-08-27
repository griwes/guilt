#include <cassert>
#include <memory>
#include <optional>
#include <queue>
#include <tuple>
#include <variant>

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

namespace st
{
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
                    std::forward<F>(f)(std::as_const(_state));
                }

                _continuations.push_back(std::forward<F>(f));
            }

            const T & get_value() const
            {
                assert(_state.index() == 1);
                return std::get<1>(_state).value;
            }

        private:
            void _invoke_continuations()
            {
                auto cs = std::move(_continuations);
                for (auto && c : cs)
                {
                    c(std::as_const(_state));
                }
            }

            std::variant<std::monostate, value_type, std::exception_ptr> _state;

            using Continuation = reaver::unique_function<void(decltype(_state))>;
            std::vector<Continuation> _continuations;
        };
    }

    template<typename T>
    struct future_promise_pair;

    template<typename T>
    class future;

    template<typename T = void>
    class promise_base
    {
        using State = detail::shared_state<::guilt::detail::replace_void_t<T>>;

        promise_base(std::shared_ptr<State> state) : _state{ state }
        {
        }

    public:
        template<typename U>
        friend future_promise_pair<U> make_manual_promise();

        promise_base() : promise_base(std::make_shared<State>())
        {
        }

        void set_value(::guilt::detail::replace_void_t<T> val = {})
        {
            _state->set_value(std::move(val));
        }

        void set_exception(std::exception_ptr ex)
        {
            _state->set_exception(ex);
        }

        coro::suspend_never initial_suspend()
        {
            return {};
        }
        coro::suspend_never final_suspend()
        {
            return {};
        }

        auto get_return_object()
        {
            return future<T>{ _state };
        }

        void unhandled_exception()
        {
            set_exception(std::current_exception());
        }

    private:
        std::shared_ptr<State> _state;
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

    template<typename T>
    future_promise_pair<T> make_manual_promise()
    {
        using State = detail::shared_state<::guilt::detail::replace_void_t<T>>;
        auto state = std::make_shared<State>();
        return { state, state };
    }

    template<typename T = void>
    class future
    {
        future(std::shared_ptr<detail::shared_state<::guilt::detail::replace_void_t<T>>> state)
            : _state{ state }
        {
        }

    public:
        using value_type = T;
        using promise_type = promise<T>;

        template<typename U>
        friend future_promise_pair<U> make_manual_promise();

        template<typename U>
        friend class promise_base;

        future() = delete;

        bool is_ready() const
        {
            return _state->is_ready();
        }

        bool await_ready() const
        {
            // always suspend
            // otherwise the produced graph is always going to be eager, thanks to not suspending initially
            return false;
        }

        void await_suspend(coro::coroutine_handle<> h)
        {
            then([h = std::move(h)](auto &&... args) mutable { h(); });
        }

        ::guilt::detail::replace_void_t<T> await_resume()
        {
            return _state->get_value();
        }

        template<typename F>
        auto then(F && f)
            -> future<decltype(::guilt::detail::void_call(std::declval<::guilt::detail::replace_void_t<T>>(),
                std::forward<F>(f)))>
        {
            using U = decltype(::guilt::detail::void_call(
                std::declval<::guilt::detail::replace_void_t<T>>(), std::forward<F>(f)));
            auto [future, promise] = make_manual_promise<U>();

            _state->add_continuation(
                [f = std::forward<F>(f), promise = std::move(promise), keepalive = _state](
                    const auto & state) mutable {
                    switch (state.index())
                    {
                        case 0:
                            assert(false && "this is not supposed to happen");

                        case 1:
                            global_execution_context().get_executor().execute(
                                [f = std::move(f),
                                    promise = std::move(promise),
                                    &value = std::get<1>(state).value,
                                    keepalive]() mutable {
                                    try
                                    {
                                        if constexpr (std::is_void_v<U>)
                                        {
                                            ::guilt::detail::void_call(value, std::move(f));
                                            promise.set_value({});
                                        }
                                        else
                                        {
                                            promise.set_value(std::move(f)(value));
                                        }
                                    }
                                    catch (...)
                                    {
                                        promise.set_exception(std::current_exception());
                                    }
                                });

                            break;

                        case 2:
                            try
                            {
                                std::rethrow_exception(std::get<2>(state));
                            }
                            catch (...)
                            {
                                std::terminate();
                            }
                    }
                });

            return std::move(future);
        }

    private:
        std::shared_ptr<detail::shared_state<::guilt::detail::replace_void_t<T>>> _state;
    };

    template<typename T>
    struct future_promise_pair
    {
        class future<T> future;
        class promise<T> promise;
    };

    template<typename T = void>
    future<T> make_ready_future(::guilt::detail::replace_void_t<T> val = {})
    {
        auto [future, promise] = make_manual_promise<T>();
        promise.set_value(std::move(val));
        return std::move(future);
    }

    template<typename... Ts>
    future<std::tuple<::guilt::detail::replace_void_t<typename Ts::value_type>...>> when_all(Ts... ts)
    {
        co_return { (co_await ts)... };
    }

}
}
