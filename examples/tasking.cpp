#include "guilt/tasking.h"

auto & base_task()
{
    static auto ret = []() -> guilt::task<>
    {
        std::cout << "here" << std::endl;
        co_return;
    }();
    return ret;
}

guilt::task<> task_left()
{
    std::cout << "left" << std::endl;
    co_await base_task();
    std::cout << "left" << std::endl;
    co_return;
}

guilt::task<> task_right()
{
    std::cout << "right" << std::endl;
    co_await base_task();
    std::cout << "right" << std::endl;
    co_return;
}

guilt::task<> create_work()
{
    auto task = guilt::when_all(task_left(), task_right());
    co_await task;
    co_return;
}

int main()
{
    auto task = create_work();
    assert(!task.is_ready());
    task.start();
    guilt::global_execution_context().handle_all_until([&] { return task.is_ready(); });
    assert(task.is_ready());
}
