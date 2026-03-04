#pragma once

#include "vrendergraph/render_graph_desc.hpp"

#include <nlohmann/json.hpp>

namespace vrendergraph
{
    RenderGraphDesc loadRenderGraph(const nlohmann::json& j);

    // Serialize to JSON.
    // Notes:
    // - Always writes: passes[]
    // - Writes resources{} only if non-empty
    // - Writes meta{} only if non-empty
    nlohmann::json saveRenderGraph(const RenderGraphDesc& desc);
} // namespace vrendergraph
