#pragma once

#include "vrendergraph/builder_context.hpp"
#include "vrendergraph/param_block.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vultra::rg
{
    using PassSetupFn = std::function<void(FrameGraph&, FrameGraphBlackboard&, const ParamBlock&, PassBuildContext&)>;

    struct PassDefinition
    {
        std::string type;
        PassSetupFn setup;
    };

    class RenderGraphRegistry
    {
    public:
        void                  registerPass(PassDefinition def);
        const PassDefinition& get(std::string_view type) const;
        bool                  contains(std::string_view type) const;

    private:
        std::unordered_map<std::string, PassDefinition> m_Definitions;
    };
} // namespace vultra::rg
