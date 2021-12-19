#include "guilt/annotated.h"

guilt::annotated_task<> task_right(guilt::context);

guilt::annotated_task<> base_task(guilt::context ctx)
{
    static auto ret = [](guilt::context ctx) -> guilt::annotated_task<>
    {
        co_await guilt::describe_function{ "base_task" };
        ctx = co_await guilt::describe_region{ "create base task " };
        co_await task_right(ctx);
        co_return;
    }(ctx);

    return ret;
}

guilt::annotated_task<> task_left(guilt::context ctx)
{
    co_await guilt::describe_function{ "task_left" };
    ctx = co_await guilt::describe_region{ "wait for base work" };

    co_await base_task(ctx);
    co_return;
}

guilt::annotated_task<> task_right(guilt::context ctx)
{
    static auto ret = [](guilt::context ctx) -> guilt::annotated_task<>
    {
        co_await guilt::describe_function{ "task_right" };
        ctx = co_await guilt::describe_region{ "wait for base work" };

        co_await base_task(ctx);
        co_return;
    }(ctx);

    return ret;
}

guilt::annotated_task<> create_work(guilt::context ctx)
{
    co_await guilt::describe_function{ "create work" };

    ctx = co_await guilt::describe_region{ "start the work" };
    auto all = guilt::when_all(ctx, task_left(ctx), task_right(ctx));

    ctx = co_await guilt::describe_region{ "await the dependencies" };
    co_await guilt::when_all(ctx, std::move(all));

    co_return;
}

int main()
{
    guilt::dependency_graph graph;
    auto main_cluster = graph.add_cluster("main()");
    auto main_node = graph.add_node(main_cluster, "main()");

    auto task = create_work({ &graph, main_cluster, main_node });
    assert(!task.is_ready());
    task.start();
    guilt::global_execution_context().handle_all_until([&] { return task.is_ready(); });
    assert(task.is_ready());
    try
    {
        auto result = task.await_resume();

        std::cout << graph.to_graphviz() << std::endl;
    }
    catch (const guilt::dependency_cycle & cycle)
    {
        std::cout << cycle.full_graph_graphviz() << std::endl;
    }
}
