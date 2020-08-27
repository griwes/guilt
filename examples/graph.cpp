#include "guilt/graph.h"

int main()
{
    guilt::dependency_graph graph;

    auto cluster1 = graph.add_cluster("top level");
    auto cluster2 = graph.add_cluster(cluster1, "235");
    auto cluster3 = graph.add_cluster(cluster1, "46");

    auto node1 = graph.add_node("node 1", "this is a longer description");
    auto node2 = graph.add_node(cluster2, "node 2", "this is a description of node 2\nmay be line broken");
    auto node3 = graph.add_node(cluster2, "node 3", "this is the last node");
    auto node4 = graph.add_node(cluster3, "node 4");
    auto node5 = graph.add_node(cluster2, "node 5");
    auto node6 = graph.add_node(cluster3, "node 6");

    graph.add_edge(node1, node2);
    graph.add_edge(node2, node3);
    graph.add_edge(node3, node4);
    graph.add_edge(node2, node5);
    graph.add_edge(node4, node5);
    graph.add_edge(node5, node6);

    std::cout << "Full graph:\n";
    auto gv = graph.to_graphviz();
    std::cout << gv << std::endl;

    std::ofstream{ "success.dot" } << gv << std::endl;

    try
    {
        graph.add_edge(node6, node2);
    }
    catch (const guilt::dependency_cycle & cycle)
    {
        std::cout << "\nNodes inside cycle:\n";
        auto gv = cycle.to_graphviz();
        std::cout << gv << std::endl;

        std::ofstream{ "failure.dot" } << gv << std::endl;
    }
}

