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

    nlohmann::json saveRenderGraph(const RenderGraphDesc& desc)
    {
        nlohmann::json j;

        if (!desc.resources.empty())
        {
            nlohmann::json rj = nlohmann::json::object();
            for (const auto& r : desc.resources)
            {
                // Preserve opaque desc block, but ensure the name key is stable.
                nlohmann::json obj = r.desc;
                obj["imported"]    = r.imported;
                rj[r.name]         = std::move(obj);
            }
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
