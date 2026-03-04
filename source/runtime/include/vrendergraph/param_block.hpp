#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace vrendergraph
{
    class ParamBlock
    {
    public:
        ParamBlock() = default;
        explicit ParamBlock(nlohmann::json data);

        template<typename T>
        T get(std::string_view key, T defaultValue) const
        {
            if (!m_Data.contains(key))
                return defaultValue;
            return m_Data.at(key).get<T>();
        }

        bool                  has(std::string_view key) const;
        const nlohmann::json& raw() const noexcept { return m_Data; }

    private:
        nlohmann::json m_Data;
    };
} // namespace vrendergraph
