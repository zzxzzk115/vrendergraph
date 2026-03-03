#include "vrendergraph/loader.hpp"

#include <stdexcept>

namespace vultra::rg
{
    static std::unordered_map<std::string, std::string> parseSlotMap(const nlohmann::json& j, const char* key)
    {
        std::unordered_map<std::string, std::string> out;
        if (!j.contains(key))
            return out;

        const auto& m = j.at(key);
        if (!m.is_object())
            throw std::runtime_error(std::string("vrendergraph: '") + key + "' must be an object");

        for (auto it = m.begin(); it != m.end(); ++it)
            out.emplace(it.key(), it.value().get<std::string>());

        return out;
    }

    RenderGraphDesc loadRenderGraph(const nlohmann::json& j)
    {
        RenderGraphDesc desc;

        if (j.contains("resources"))
        {
            const auto& res = j.at("resources");
            if (!res.is_object())
                throw std::runtime_error("vrendergraph: 'resources' must be an object");

            for (auto it = res.begin(); it != res.end(); ++it)
            {
                ResourceDecl rd;
                rd.name = it.key();

                const auto& rj = it.value();
                rd.imported    = rj.value("imported", false);
                rd.desc        = rj; // store opaque json block

                desc.resources.push_back(std::move(rd));
            }
        }

        if (!j.contains("passes") || !j.at("passes").is_array())
            throw std::runtime_error("vrendergraph: 'passes' must be an array");

        for (const auto& pj : j.at("passes"))
        {
            PassDecl p;
            p.id      = pj.value("id", "");
            p.type    = pj.value("type", "");
            p.enabled = pj.value("enabled", true);

            p.inputs  = parseSlotMap(pj, "inputs");
            p.outputs = parseSlotMap(pj, "outputs");
            p.params  = ParamBlock(pj.value("params", nlohmann::json {}));

            if (p.id.empty())
                p.id = p.type;

            desc.passes.push_back(std::move(p));
        }

        return desc;
    }
} // namespace vultra::rg
