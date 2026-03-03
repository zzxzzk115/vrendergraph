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
        std::string    name;
        bool           imported = false;
        nlohmann::json desc; // opaque (backend/app interprets)
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
    };
} // namespace vrendergraph
