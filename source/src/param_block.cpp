#include "vrendergraph/param_block.hpp"

namespace vrendergraph
{
    ParamBlock::ParamBlock(nlohmann::json data) : m_Data(std::move(data)) {}

    bool ParamBlock::has(std::string_view key) const { return m_Data.contains(std::string(key)); }
} // namespace vrendergraph
