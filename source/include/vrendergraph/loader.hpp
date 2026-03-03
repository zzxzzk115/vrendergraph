#pragma once

#include "vrendergraph/render_graph_desc.hpp"

#include <nlohmann/json.hpp>

namespace vrendergraph
{
    RenderGraphDesc loadRenderGraph(const nlohmann::json& j);
}
