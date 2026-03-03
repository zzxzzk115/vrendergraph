#include "vrendergraph/param_block.hpp"

namespace vultra::rg
{
    ParamBlock::ParamBlock(nlohmann::json data) : m_Data(std::move(data)) {}

    bool ParamBlock::has(std::string_view key) const { return m_Data.contains(std::string(key)); }
} // namespace vultra::rg
