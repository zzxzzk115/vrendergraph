#pragma once
#include "vrendergraph/param_block.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace vrendergraph
{
    struct ResourceDecl
    {
        // External engine-owned resource identifier.
        //
        // RenderGraph only records the name; the actual handle is provided at build time
        // through RenderGraph::ImportFn.
        std::string name;
    };

    struct PassDecl
    {
        std::string id;
        std::string type;
        bool        enabled = true;

        // slotName -> resourceName
        std::unordered_map<std::string, std::string> inputs;
        std::unordered_map<std::string, std::string> outputs;

        ParamBlock params;
    };

    struct RenderGraphDesc
    {
        std::vector<ResourceDecl> resources;
        std::vector<PassDecl>     passes;

        // Optional editor/runtime metadata.
        // - Runtime systems should ignore this field.
        // - Tools/editors can store node positions, zoom/pan, custom UI state, etc.
        nlohmann::json meta;
    };
} // namespace vrendergraph
