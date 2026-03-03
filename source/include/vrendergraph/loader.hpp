#pragma once

#include "vrendergraph/render_graph_desc.hpp"

#include <nlohmann/json.hpp>

namespace vultra::rg
{
    RenderGraphDesc loadRenderGraph(const nlohmann::json& j);
}
