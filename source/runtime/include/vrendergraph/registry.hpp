#pragma once

#include "vrendergraph/builder_context.hpp"
#include "vrendergraph/param_block.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vrendergraph
{
    using PassSetupFn = std::function<void(FrameGraph&, FrameGraphBlackboard&, const ParamBlock&, PassBuildContext&)>;

    enum class ParamType
    {
        eFloat,
        eInt,
        eBoolean,
        eString
    };

    struct ParamDesc
    {
        std::string                   name;
        ParamType                     type;
        nlohmann::json                defaultValue;
        std::optional<nlohmann::json> minValue; // optional
        std::optional<nlohmann::json> maxValue; // optional
    };

    struct PassDefinition
    {
        std::string              type;
        PassSetupFn              setup;
        std::vector<std::string> inputs;  // slot names
        std::vector<std::string> outputs; // slot names
        std::vector<ParamDesc>   params;
    };

    class RenderGraphRegistry
    {
    public:
        void                     registerPass(PassDefinition def);
        const PassDefinition&    get(std::string_view type) const;
        bool                     contains(std::string_view type) const;
        std::vector<std::string> listTypes() const;

    private:
        std::unordered_map<std::string, PassDefinition> m_Definitions;
    };
} // namespace vrendergraph
