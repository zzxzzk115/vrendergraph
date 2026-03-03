#include "vrendergraph/registry.hpp"

#include <stdexcept>

namespace vultra::rg
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
} // namespace vultra::rg
