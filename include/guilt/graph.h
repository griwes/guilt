#include <string>
#include <unordered_set>
#include <vector>

#include <cassert>
#include <fstream>
#include <iostream>

namespace guilt
{
struct node_id
{
    std::size_t id;

    friend bool operator<(const node_id & lhs, const node_id & rhs)
    {
        return lhs.id < rhs.id;
    }

    friend bool operator==(const node_id & lhs, const node_id & rhs)
    {
        return lhs.id == rhs.id;
    }
};

struct node_id_hash
{
    std::size_t operator()(const node_id & id) const
    {
        return std::hash<std::size_t>()(id.id);
    }
};

using node_id_set = std::unordered_set<node_id, node_id_hash>;

struct cluster_id
{
    std::size_t id;
};

class dependency_graph;

class dependency_cycle : std::exception
{
public:
    std::string to_graphviz() const;

private:
    friend class dependency_graph;

    dependency_cycle(const dependency_graph * graph, node_id from, node_id to)
        : _graph{ graph }, _from{ from }, _to{ to }
    {
    }

    const dependency_graph * _graph;
    node_id _from;
    node_id _to;
};

class dependency_graph
{
public:
    friend class dependency_cycle;

    cluster_id add_cluster(std::string name, std::string description = "")
    {
        cluster_id ret = { _clusters.size() };
        _clusters.push_back(cluster{ ret, std::move(name), std::move(description) });
        return ret;
    }

    cluster_id add_cluster(cluster_id parent, std::string name, std::string description = "")
    {
        auto ret = add_cluster(std::move(name), std::move(description));
        _clusters.at(parent.id).child_clusters.push_back(ret);
        _clusters[ret.id].parent = parent;
        return ret;
    }

    node_id add_node(std::string name, std::string description = "")
    {
        node_id ret = { _nodes.size() };
        _nodes.push_back(node{ ret, std::move(name), std::move(description) });
        return ret;
    }

    node_id add_node(cluster_id parent, std::string name, std::string description = "")
    {
        auto ret = add_node(std::move(name), std::move(description));
        _clusters.at(parent.id).child_nodes.push_back(ret);
        return ret;
    }

    void add_edge(node_id from, node_id to)
    {
        if (_check_for_cycle(from, to))
        {
            throw dependency_cycle{ this, from, to };
        }

        auto e = edge{ from, to };
        _edges.insert(std::upper_bound(_edges.begin(), _edges.end(), e), e);
    }

    struct graph_filter_between
    {
        node_id from;
        node_id to;
    };

    std::string to_graphviz() const
    {
        std::string ret = _graphviz_header();

        for (auto && node : _nodes)
        {
            auto id_str = std::to_string(node.id.id);
            ret += "    node_" + id_str + " [ label = \"" + node.name + " (#" + id_str + ")\n"
                + node.description + "\" ];\n";
        }

        ret += "\n";

        auto print_cluster = [&](auto && self, const cluster & c, std::string prefix) -> void {
            auto id_str = std::to_string(c.id.id);

            ret += prefix + "subgraph cluster_" + id_str + " {\n";
            ret += prefix + "    label = \"" + c.name + " (#" + id_str + ")\n" + c.description + "\";\n\n";

            for (auto && child : c.child_clusters)
            {
                self(self, _clusters.at(child.id), prefix + "    ");
            }

            for (auto && child : c.child_nodes)
            {
                ret += prefix + "    node_" + std::to_string(child.id) + ";\n";
            }

            ret += prefix + "}\n";
        };

        for (auto && cluster : _clusters)
        {
            if (cluster.parent)
            {
                continue;
            }

            print_cluster(print_cluster, cluster, "    ");
        }

        ret += "\n";

        for (auto && edge : _edges)
        {
            ret +=
                "    node_" + std::to_string(edge.from.id) + " -> node_" + std::to_string(edge.to.id) + ";\n";
        }

        ret += _graphviz_footer();

        return ret;
    }

    std::string to_graphviz(graph_filter_between filter) const
    {
        std::string ret;

        ret += _graphviz_header();
        ret += _generate_graphviz(_get_filtered_nodes(filter));
        ret += _graphviz_footer();

        return ret;
    }

private:
    std::string _graphviz_header() const
    {
        return
            R"header(
digraph {
    rankdir = "TB";
    newrank = "true";

)header";
    }

    std::string _graphviz_footer() const
    {
        return "}";
    }

    std::string _generate_graphviz(node_id_set filtered_node_ids) const
    {
        std::string ret;

        for (auto && node_id : filtered_node_ids)
        {
            auto id_str = std::to_string(node_id.id);
            ret += "    node_" + id_str + " [ label = \"" + _nodes[node_id.id].name + " (#" + id_str + ")\n"
                + _nodes[node_id.id].description + "\" ];\n";
        }

        ret += "\n";

        auto print_cluster = [&](auto && self, const cluster & c, std::string prefix) -> void {
            auto id_str = std::to_string(c.id.id);

            ret += prefix + "subgraph cluster_" + id_str + " {\n";
            ret += prefix + "    label = \"" + c.name + " (#" + id_str + ")\\n" + c.description + "\";\n\n";

            for (auto && child : c.child_clusters)
            {
                self(self, _clusters.at(child.id), prefix + "    ");
            }

            for (auto && child : c.child_nodes)
            {
                if (filtered_node_ids.find(child) != filtered_node_ids.end())
                {
                    ret += prefix + "    node_" + std::to_string(child.id) + ";\n";
                }
            }

            ret += prefix + "}\n";
        };

        for (auto && cluster : _clusters)
        {
            if (cluster.parent)
            {
                continue;
            }

            print_cluster(print_cluster, cluster, "    ");
        }

        ret += "\n";

        for (auto && edge : _edges)
        {
            if (filtered_node_ids.find(edge.from) != filtered_node_ids.end()
                && filtered_node_ids.find(edge.to) != filtered_node_ids.end())
            {
                ret += "    node_" + std::to_string(edge.from.id) + " -> node_" + std::to_string(edge.to.id)
                    + ";\n";
            }
        }

        return ret;
    }

    bool _check_for_cycle(node_id from, node_id to) const
    {
        node_id_set nodes_reachable_from_to{ to };
        node_id_set nodes_to_check{ to };

        auto reachable_node = [&](node_id id) {
            if (nodes_reachable_from_to.insert(id).second)
            {
                nodes_to_check.insert(id);
            }
        };

        while (!nodes_to_check.empty())
        {
            auto b = nodes_to_check.begin();
            auto current_from = *b;
            nodes_to_check.erase(b);

            auto [begin, end] = std::equal_range(
                _edges.begin(), _edges.end(), edge{ current_from, {} }, [](auto && lhs, auto && rhs) {
                    return lhs.from.id < rhs.from.id;
                });

            while (begin != end)
            {
                auto current_to = (begin++)->to;

                if (current_to.id == from.id)
                {
                    return true;
                }

                reachable_node(current_to);
            }
        }

        return false;
    }

    node_id_set _get_filtered_nodes(graph_filter_between filter) const
    {
        node_id_set nodes_included{ filter.from, filter.to };
        std::vector<std::vector<node_id>> paths_to_check{ { filter.to } };

        while (!paths_to_check.empty())
        {
            auto path = std::move(paths_to_check.back());
            paths_to_check.pop_back();

            auto tail_id = path.back();

            auto [begin, end] = std::equal_range(
                _edges.begin(), _edges.end(), edge{ tail_id, {} }, [](auto && lhs, auto && rhs) {
                    return lhs.from.id < rhs.from.id;
                });

            while (begin != end)
            {
                auto current_id = (begin++)->to;

                if (current_id == filter.to || nodes_included.find(current_id) != nodes_included.end())
                {
                    nodes_included.insert(path.begin(), path.end());
                    continue;
                }

                paths_to_check.push_back(path);
                paths_to_check.back().push_back(current_id);
            }
        }

        return nodes_included;
    }

    struct node
    {
        node_id id;
        std::string name;
        std::string description;
    };

    struct edge
    {
        node_id from;
        node_id to;

        friend bool operator<(const edge & lhs, const edge & rhs)
        {
            return lhs.from < rhs.from || lhs.to < rhs.to;
        }

        friend bool operator==(const edge & lhs, const edge & rhs)
        {
            return lhs.from == rhs.from && lhs.to == rhs.to;
        }
    };

    struct cluster
    {
        cluster_id id;
        std::string name;
        std::string description;

        std::vector<cluster_id> child_clusters = {};
        std::vector<node_id> child_nodes = {};
        std::optional<cluster_id> parent = {};
    };

    std::vector<node> _nodes;
    std::vector<edge> _edges;
    std::vector<cluster> _clusters;
};

inline std::string dependency_cycle::to_graphviz() const
{
    std::string ret;

    ret += _graph->_graphviz_header();

    ret += _graph->_generate_graphviz(
        _graph->_get_filtered_nodes(dependency_graph::graph_filter_between{ _from, _to }));
    ret += "    node_" + std::to_string(_from.id) + " -> node_" + std::to_string(_to.id)
        + " [ style = \"dashed\" color = \"red\" constraint = \"false\" ];\n";

    ret += _graph->_graphviz_footer();

    return ret;
}
}
