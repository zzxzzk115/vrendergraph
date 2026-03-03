#pragma once

#include "vrendergraph/registry.hpp"
#include "vrendergraph/render_graph_desc.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <functional>

namespace vultra::rg
{
    using ImportFn = std::function<
        FrameGraphResource(FrameGraph&, std::string_view resourceName, const nlohmann::json& resourceDesc)>;

    class RenderGraph
    {
    public:
        RenderGraph(const RenderGraphRegistry& registry, ImportFn importer);

        void build(FrameGraph& fg, FrameGraphBlackboard& blackboard, const RenderGraphDesc& desc) const;

    private:
        const RenderGraphRegistry& m_Registry;
        ImportFn                   m_Importer;
    };
} // namespace vultra::rg
