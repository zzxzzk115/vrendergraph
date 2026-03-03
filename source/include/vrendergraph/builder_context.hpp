#pragma once

#include <fg/FrameGraph.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace vultra::rg
{
    class PassBuildContext
    {
    public:
        PassBuildContext(const std::unordered_map<std::string, FrameGraphResource>& inputs,
                         std::unordered_map<std::string, FrameGraphResource>&       outputs) :
            m_Inputs(inputs), m_Outputs(outputs)
        {}

        [[nodiscard]] bool hasInput(std::string_view slot) const { return m_Inputs.contains(std::string(slot)); }

        [[nodiscard]] FrameGraphResource getInput(std::string_view slot) const
        {
            auto it = m_Inputs.find(std::string(slot));
            if (it == m_Inputs.end())
                return {};
            return it->second;
        }

        void setOutput(std::string_view slot, FrameGraphResource res) { m_Outputs[std::string(slot)] = res; }

        [[nodiscard]] bool hasOutput(std::string_view slot) const { return m_Outputs.contains(std::string(slot)); }

        [[nodiscard]] FrameGraphResource getOutput(std::string_view slot) const
        {
            auto it = m_Outputs.find(std::string(slot));
            if (it == m_Outputs.end())
                return {};
            return it->second;
        }

    private:
        const std::unordered_map<std::string, FrameGraphResource>& m_Inputs;
        std::unordered_map<std::string, FrameGraphResource>&       m_Outputs;
    };
} // namespace vultra::rg
