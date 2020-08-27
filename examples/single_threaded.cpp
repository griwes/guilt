#include "guilt/st.h"

guilt::st::future_promise_pair<void> base_future()
{
    static guilt::st::future_promise_pair<void> ret = guilt::st::make_manual_promise<void>();
    return ret;
}

guilt::st::future<> task_left()
{
    co_await base_future().future;
    co_return;
}

guilt::st::future<> task_right()
{
    co_await base_future().future;
    co_return;
}

guilt::st::future<> create_work()
{
    auto fut = guilt::st::when_all(task_left(), task_right());
    base_future().promise.set_value();
    co_await fut;
    co_return;
}

int main()
{
    auto fut = create_work();
    assert(!fut.is_ready());
    guilt::st::global_execution_context().handle_all_until([&] { return fut.is_ready(); });
    assert(fut.is_ready());
}
