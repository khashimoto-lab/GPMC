#pragma once

#include "utils.hpp"
#include "graph.hpp"

namespace sspp {
namespace decomp {
TreeDecomposition Treedecomp(const Graph& graph, double time, string tmp_dir);
TreeDecomposition TreedecompFromFile(std::ifstream& in);
} // namespace decomp
} // namespace sspp
