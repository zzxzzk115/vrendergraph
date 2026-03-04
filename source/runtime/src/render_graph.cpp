#include "vrendergraph/render_graph.hpp"

#include <stdexcept>
#include <unordered_map>

namespace vrendergraph
{
    RenderGraph::RenderGraph(const RenderGraphRegistry& registry, ImportFn importer) :
        m_Registry(registry), m_Importer(std::move(importer))
    {}

    void RenderGraph::build(FrameGraph& fg, FrameGraphBlackboard& blackboard, const RenderGraphDesc& desc) const
    {
        std::unordered_map<std::string, FrameGraphResource> resourceTable;
        resourceTable.reserve(desc.resources.size() + desc.passes.size() * 4);

        // 1) Import declared external resources
        for (const auto& r : desc.resources)
        {
            if (!m_Importer)
                throw std::runtime_error("vrendergraph: importer is not set, but external resources exist");

            FrameGraphResource handle = m_Importer(fg, r.name);
            if (!fg.isValid(handle))
                throw std::runtime_error("vrendergraph: importer returned invalid resource for: " + r.name);

            resourceTable[r.name] = handle;
        }

        // 2) Passes
        for (const auto& p : desc.passes)
        {
            if (!p.enabled)
                continue;

            const auto& def = m_Registry.get(p.type);

            // Build slot->handle map for inputs
            std::unordered_map<std::string, FrameGraphResource> inputs;
            inputs.reserve(p.inputs.size());
            for (const auto& [slot, resName] : p.inputs)
            {
                auto it = resourceTable.find(resName);
                if (it == resourceTable.end())
                    throw std::runtime_error("vrendergraph: pass '" + p.id + "' missing input resource: " + resName);
                inputs.emplace(slot, it->second);
            }

            // Pass writes slot->handle outputs here
            std::unordered_map<std::string, FrameGraphResource> outputs;
            outputs.reserve(p.outputs.size());

            PassBuildContext ctx(inputs, outputs);
            def.setup(fg, blackboard, p.params, ctx);

            // Publish outputs into resource table by declared mapping slot->resourceName
            for (const auto& [slot, resName] : p.outputs)
            {
                auto it = outputs.find(slot);
                if (it == outputs.end())
                    throw std::runtime_error("vrendergraph: pass '" + p.id + "' did not produce output slot: " + slot);

                if (!fg.isValid(it->second))
                    throw std::runtime_error("vrendergraph: pass '" + p.id +
                                             "' produced invalid resource for slot: " + slot);

                resourceTable[resName] = it->second;
            }
        }
    }
} // namespace vrendergraph
