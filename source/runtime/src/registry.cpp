#include "vrendergraph/registry.hpp"

#include <stdexcept>

namespace vrendergraph
{
    void RenderGraphRegistry::registerPass(PassDefinition def) { m_Definitions.emplace(def.type, std::move(def)); }

    const PassDefinition& RenderGraphRegistry::get(std::string_view type) const
    {
        auto it = m_Definitions.find(std::string(type));
        if (it == m_Definitions.end())
            throw std::runtime_error("vrendergraph: unknown pass type: " + std::string(type));
        return it->second;
    }

    bool RenderGraphRegistry::contains(std::string_view type) const
    {
        return m_Definitions.contains(std::string(type));
    }

    std::vector<std::string> RenderGraphRegistry::listTypes() const
    {
        std::vector<std::string> out;
        out.reserve(m_Definitions.size());

        for (const auto& [type, _] : m_Definitions)
            out.push_back(type);

        return out;
    }
} // namespace vrendergraph
