#include "vrendergraph/loader.hpp"

#include <stdexcept>

namespace vrendergraph
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

        // Optional metadata for editor/tools.
        if (j.contains("meta"))
            desc.meta = j.at("meta");

        // -----------------------------------------------------------------
        // resources
        //
        // v2 schema:
        //   "resources": ["backbuffer", "history", ...]
        //
        // Backward compatibility (older schema):
        //   "resources": { "backbuffer": { ... }, "@backbuffer": { "imported": true, ... }, ... }
        // Only keys are consumed; per-resource desc/import flags are ignored.
        // -----------------------------------------------------------------
        if (j.contains("resources"))
        {
            const auto& res = j.at("resources");

            if (res.is_array())
            {
                for (const auto& v : res)
                {
                    if (v.is_string())
                    {
                        desc.resources.push_back(ResourceDecl {v.get<std::string>()});
                    }
                    else if (v.is_object())
                    {
                        // allow [{ "name": "backbuffer" }, ...]
                        const std::string name = v.value("name", "");
                        if (!name.empty())
                            desc.resources.push_back(ResourceDecl {name});
                    }
                    else
                    {
                        throw std::runtime_error("vrendergraph: 'resources' array elements must be strings or objects");
                    }
                }
            }
            else if (res.is_object())
            {
                // legacy: object map
                for (auto it = res.begin(); it != res.end(); ++it)
                {
                    ResourceDecl rd;
                    rd.name = it.key();
                    desc.resources.push_back(std::move(rd));
                }
            }
            else
            {
                throw std::runtime_error("vrendergraph: 'resources' must be an array or object");
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

    nlohmann::json saveRenderGraph(const RenderGraphDesc& desc)
    {
        nlohmann::json j;

        if (!desc.resources.empty())
        {
            nlohmann::json rj = nlohmann::json::array();
            for (const auto& r : desc.resources)
                rj.push_back(r.name);
            j["resources"] = std::move(rj);
        }

        nlohmann::json passes = nlohmann::json::array();
        for (const auto& p : desc.passes)
        {
            nlohmann::json pj;
            pj["id"]      = p.id;
            pj["type"]    = p.type;
            pj["enabled"] = p.enabled;

            if (!p.inputs.empty())
                pj["inputs"] = p.inputs;
            if (!p.outputs.empty())
                pj["outputs"] = p.outputs;

            if (!p.params.raw().is_null() && !p.params.raw().empty())
                pj["params"] = p.params.raw();

            passes.push_back(std::move(pj));
        }
        j["passes"] = std::move(passes);

        if (!desc.meta.is_null() && !desc.meta.empty())
            j["meta"] = desc.meta;

        return j;
    }
} // namespace vrendergraph
