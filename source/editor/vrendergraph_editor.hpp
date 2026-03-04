#pragma once

// vrendergraph_editor.hpp
// -----------------------------------------------------------------------------
// ImGui + NodeEditor helper for editing vrendergraph RenderGraphDesc.
//
// Goals:
// - Header-only with stb-style IMPLEMENTATION switch.
// - Editor state (node pos/collapsed + view) is stored into RenderGraphDesc::meta.
// - Undo/Redo is *not* implemented here; this header only provides hooks.
// - Runtime vrendergraph does not depend on this header.
//
// Usage:
//   // In exactly one .cpp:
//   #define VRENDERGRAPH_EDITOR_IMPLEMENTATION
//   #include <vrendergraph/vrendergraph_editor.hpp>
//
//   // Everywhere else:
//   #include <vrendergraph/vrendergraph_editor.hpp>
// -----------------------------------------------------------------------------

#include "vrendergraph/registry.hpp"
#include "vrendergraph/render_graph_desc.hpp"

#include <imgui.h>
#include <imgui_node_editor/imgui_node_editor.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vrendergraph::editor
{
    namespace ed = ax::NodeEditor;

    // ---------------------------------------------------------------------
    // Undo hooks (engine-owned)
    // ---------------------------------------------------------------------

    struct UndoInterface
    {
        // Called at the beginning/end of one editor action.
        std::function<void()> beginAction;
        std::function<void()> endAction;

        // Called when the editor wants the engine to capture a snapshot.
        // Typical policy: call once per action (create/delete/link/etc.).
        std::function<void()> captureSnapshot;

        void begin() const
        {
            if (beginAction)
                beginAction();
        }
        void end() const
        {
            if (endAction)
                endAction();
        }
        void snapshot() const
        {
            if (captureSnapshot)
                captureSnapshot();
        }
        explicit operator bool() const
        {
            return static_cast<bool>(captureSnapshot) || static_cast<bool>(beginAction) || static_cast<bool>(endAction);
        }
    };

    // ---------------------------------------------------------------------
    // Minimal persisted UI state (stored in RenderGraphDesc::meta["editor"])
    // ---------------------------------------------------------------------

    struct NodeUiState
    {
        ImVec2 pos {0.0f, 0.0f};
        bool   hasPos    = false;
        bool   collapsed = false;
    };

    struct ViewUiState
    {
        float  zoom = 1.0f;
        ImVec2 pan {0.0f, 0.0f};
        bool   hasView = false;
    };

    struct EditorUiState
    {
        // passId -> ui
        std::unordered_map<std::string, NodeUiState> nodes;
        ViewUiState                                  view;

        // Arbitrary tool-specific data.
        nlohmann::json extra;
    };

    void loadEditorUiState(const RenderGraphDesc& desc, EditorUiState& outState);
    void storeEditorUiState(RenderGraphDesc& desc, const EditorUiState& state);

    // ---------------------------------------------------------------------
    // RenderGraphEditor
    // ---------------------------------------------------------------------

    class RenderGraphEditor
    {
    public:
        struct Config
        {
            // Optional external undo manager hooks.
            UndoInterface undo {};

            // If null, RenderGraphEditor will create an internal node editor context.
            // If provided, RenderGraphEditor will use it (and not destroy it).
            ed::EditorContext* nodeEditorContext = nullptr;

            // meta key under desc.meta to store ui state (default: "editor").
            std::string metaKey = "editor";
        };

        explicit RenderGraphEditor(const RenderGraphRegistry& registry, Config* cfg = nullptr);
        ~RenderGraphEditor();

        RenderGraphEditor(const RenderGraphEditor&)            = delete;
        RenderGraphEditor& operator=(const RenderGraphEditor&) = delete;

        // Sets/updates the graph to edit. Editor UI state is loaded from desc.meta.
        void setGraph(RenderGraphDesc* desc);

        // Returns the currently bound graph.
        RenderGraphDesc* graph() const { return m_Graph; }

        // Draws the editor. Safe to call every frame.
        void draw(const char* dockspaceName = "RenderGraph");

        // Writes the current UI state back into desc.meta.
        void flushMeta();

        // True if the graph structure or pass fields were modified.
        bool isDirty() const { return m_Dirty; }
        void clearDirty() { m_Dirty = false; }

        // Builds a stable, topologically sorted pass list from current DAG connections.
        // Notes:
        // - This is intended for editor-side generation of runtime-friendly passes[].
        // - Sorting uses current passes[] order as a stable tiebreaker.
        std::vector<std::string> buildTopoOrderPassIds(std::string* outError = nullptr) const;

        // Applies topo order to desc->passes (in-place). Returns false on cycle.
        bool applyTopoOrder(std::string* outError = nullptr);

    private:
        // ---- internal ids ----
        struct PinKey
        {
            std::string passId;
            std::string slot;
            bool        isInput = false;
        };

        struct PinKeyHash
        {
            size_t operator()(const PinKey& k) const noexcept
            {
                std::hash<std::string> hs;
                size_t                 h = hs(k.passId);
                h ^= hs(k.slot) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                h ^= std::hash<bool> {}(k.isInput) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                return h;
            }
        };

        struct PinKeyEq
        {
            bool operator()(const PinKey& a, const PinKey& b) const noexcept
            {
                return a.isInput == b.isInput && a.passId == b.passId && a.slot == b.slot;
            }
        };

        // ---- ui helpers ----
        static uint32_t fnv1a32(std::string_view s);
        int             ensureNodeId(std::string_view passId);
        int             ensurePinId(const PinKey& k);

        void loadMeta();
        void saveMeta();

        void drawToolbar();
        void drawCanvas();
        void drawInspector();
        void drawCreatePassPopup();
        void drawResourcePanel();

        void beginUndoAction();
        void commitUndoAction();

        // ---- graph mutation ----
        PassDecl*       findPass(std::string_view id);
        const PassDecl* findPass(std::string_view id) const;

        std::string allocPassId(std::string_view type) const;
        void        addPass(std::string_view type);
        void        removePass(std::string_view passId);

        // Link create/delete
        struct ResRef
        {
            std::string passId;
            std::string slot;
        };
        static std::optional<ResRef> parseResRef(std::string_view s);
        static std::string           makeResRef(std::string_view passId, std::string_view slot);

        bool createLink(const PinKey& fromOutput, const PinKey& toInput, std::string* outError = nullptr);
        void removeInputLink(std::string_view toPassId, std::string_view toSlot);

        // ---- state ----
        const RenderGraphRegistry& m_Registry;
        Config                     m_Cfg;
        RenderGraphDesc*           m_Graph = nullptr;
        bool                       m_Dirty = false;

        // selection
        std::string m_SelectedPassId;

        // undo action grouping
        bool m_InUndoAction = false;

        // node editor context
        ed::EditorContext* m_EdCtx    = nullptr;
        bool               m_OwnEdCtx = false;

        // cached ui state
        EditorUiState m_Ui;

        // ids
        std::unordered_map<std::string, int>                  m_NodeIds;
        std::unordered_map<PinKey, int, PinKeyHash, PinKeyEq> m_PinIds;
        std::unordered_map<std::string, int>                  m_ResourceNodeIds;

        // popup
        bool m_OpenCreatePopup = false;
    };

} // namespace vrendergraph::editor

#ifdef VRENDERGRAPH_EDITOR_IMPLEMENTATION

#include <unordered_set>

namespace vrendergraph::editor
{
    // ---------------------------------------------------------------------
    // meta helpers
    // ---------------------------------------------------------------------

    void loadEditorUiState(const RenderGraphDesc& desc, EditorUiState& outState)
    {
        outState = {};

        if (desc.meta.is_null() || desc.meta.empty())
            return;

        if (!desc.meta.contains("editor"))
            return;

        const auto& ej = desc.meta.at("editor");
        if (!ej.is_object())
            return;

        if (ej.contains("nodes") && ej.at("nodes").is_object())
        {
            const auto& nj = ej.at("nodes");
            for (auto it = nj.begin(); it != nj.end(); ++it)
            {
                NodeUiState st;
                if (it.value().is_object())
                {
                    const auto& v = it.value();
                    if (v.contains("pos") && v.at("pos").is_array() && v.at("pos").size() == 2)
                    {
                        st.pos.x  = v.at("pos").at(0).get<float>();
                        st.pos.y  = v.at("pos").at(1).get<float>();
                        st.hasPos = true;
                    }
                    st.collapsed = v.value("collapsed", false);
                }
                outState.nodes[it.key()] = st;
            }
        }

        if (ej.contains("view") && ej.at("view").is_object())
        {
            const auto& vj     = ej.at("view");
            outState.view.zoom = vj.value("zoom", 1.0f);
            if (vj.contains("pan") && vj.at("pan").is_array() && vj.at("pan").size() == 2)
            {
                outState.view.pan.x   = vj.at("pan").at(0).get<float>();
                outState.view.pan.y   = vj.at("pan").at(1).get<float>();
                outState.view.hasView = true;
            }
            else
            {
                outState.view.pan.x   = vj.value("panX", 0.0f);
                outState.view.pan.y   = vj.value("panY", 0.0f);
                outState.view.hasView = vj.contains("panX") || vj.contains("panY") || vj.contains("zoom");
            }
        }

        if (ej.contains("extra"))
            outState.extra = ej.at("extra");
    }

    void storeEditorUiState(RenderGraphDesc& desc, const EditorUiState& state)
    {
        nlohmann::json ej = nlohmann::json::object();

        nlohmann::json nj = nlohmann::json::object();
        for (const auto& [passId, st] : state.nodes)
        {
            nlohmann::json v = nlohmann::json::object();
            if (st.hasPos)
                v["pos"] = nlohmann::json::array({st.pos.x, st.pos.y});
            if (st.collapsed)
                v["collapsed"] = true;
            if (!v.empty())
                nj[passId] = std::move(v);
        }
        if (!nj.empty())
            ej["nodes"] = std::move(nj);

        if (state.view.hasView)
        {
            nlohmann::json vj = nlohmann::json::object();
            vj["zoom"]        = state.view.zoom;
            vj["pan"]         = nlohmann::json::array({state.view.pan.x, state.view.pan.y});
            ej["view"]        = std::move(vj);
        }

        if (!state.extra.is_null() && !state.extra.empty())
            ej["extra"] = state.extra;

        if (desc.meta.is_null() || !desc.meta.is_object())
            desc.meta = nlohmann::json::object();

        if (!ej.empty())
            desc.meta["editor"] = std::move(ej);
        else if (desc.meta.contains("editor"))
            desc.meta.erase("editor");
    }

    // ---------------------------------------------------------------------
    // RenderGraphEditor
    // ---------------------------------------------------------------------

    uint32_t RenderGraphEditor::fnv1a32(std::string_view s)
    {
        uint32_t h = 2166136261u;
        for (unsigned char c : s)
        {
            h ^= uint32_t(c);
            h *= 16777619u;
        }
        // Avoid zero ids (NodeEditor treats 0 as invalid sometimes)
        if (h == 0)
            h = 1;
        return h;
    }

    RenderGraphEditor::RenderGraphEditor(const RenderGraphRegistry& registry, Config* cfg) :
        m_Registry(registry), m_Cfg(cfg ? *cfg : Config {})
    {
        if (m_Cfg.nodeEditorContext)
        {
            m_EdCtx    = m_Cfg.nodeEditorContext;
            m_OwnEdCtx = false;
        }
        else
        {
            m_EdCtx    = ed::CreateEditor();
            m_OwnEdCtx = true;
        }
    }

    RenderGraphEditor::~RenderGraphEditor()
    {
        if (m_OwnEdCtx && m_EdCtx)
            ed::DestroyEditor(m_EdCtx);
        m_EdCtx = nullptr;
    }

    void RenderGraphEditor::setGraph(RenderGraphDesc* desc)
    {
        m_Graph = desc;
        m_SelectedPassId.clear();
        m_NodeIds.clear();
        m_PinIds.clear();
        m_Dirty = false;
        if (m_Graph)
            loadMeta();
        else
            m_Ui = {};
    }

    void RenderGraphEditor::flushMeta()
    {
        if (!m_Graph)
            return;
        saveMeta();
    }

    void RenderGraphEditor::loadMeta()
    {
        if (!m_Graph)
            return;
        // For now the metaKey is reserved for future expansion; we store under "editor".
        loadEditorUiState(*m_Graph, m_Ui);
    }

    void RenderGraphEditor::saveMeta()
    {
        if (!m_Graph)
            return;

        // Pull node positions from NodeEditor.
        for (auto& p : m_Graph->passes)
        {
            const int nid = ensureNodeId(p.id);
            if (ed::GetNodePosition(ed::NodeId(nid)).x != 0.0f || ed::GetNodePosition(ed::NodeId(nid)).y != 0.0f)
            {
                NodeUiState& st = m_Ui.nodes[p.id];
                st.pos          = ed::GetNodePosition(ed::NodeId(nid));
                st.hasPos       = true;
                // NodeEditor currently doesn't expose collapse state
                st.collapsed = false;
            }
        }

        // View state
        // NOTE: NodeEditor API differs across forks/versions.
        // We persist zoom universally; pan is kept from previous meta (or 0).
        m_Ui.view.zoom    = ed::GetCurrentZoom();
        m_Ui.view.hasView = true;

        storeEditorUiState(*m_Graph, m_Ui);
    }

    int RenderGraphEditor::ensureNodeId(std::string_view passId)
    {
        auto it = m_NodeIds.find(std::string(passId));
        if (it != m_NodeIds.end())
            return it->second;

        int id = int(fnv1a32(passId));
        // very unlikely collisions; resolve by probing
        std::unordered_set<int> used;
        used.reserve(m_NodeIds.size() + 1);
        for (auto& [_, v] : m_NodeIds)
            used.insert(v);
        while (used.contains(id))
            id += 1;

        m_NodeIds.emplace(std::string(passId), id);
        return id;
    }

    int RenderGraphEditor::ensurePinId(const PinKey& k)
    {
        auto it = m_PinIds.find(k);
        if (it != m_PinIds.end())
            return it->second;

        // Build a stable pin id.
        std::string s;
        s.reserve(k.passId.size() + k.slot.size() + 8);
        s.append(k.passId);
        s.push_back(':');
        s.append(k.slot);
        s.push_back(':');
        s.push_back(k.isInput ? 'I' : 'O');

        int id = int(fnv1a32(s));
        // ensure not colliding with nodes/pins
        std::unordered_set<int> used;
        used.reserve(m_NodeIds.size() + m_PinIds.size() + 8);
        for (auto& [_, v] : m_NodeIds)
            used.insert(v);
        for (auto& [_, v] : m_PinIds)
            used.insert(v);
        while (used.contains(id))
            id += 1;

        m_PinIds.emplace(k, id);
        return id;
    }

    PassDecl* RenderGraphEditor::findPass(std::string_view id)
    {
        if (!m_Graph)
            return nullptr;
        for (auto& p : m_Graph->passes)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    const PassDecl* RenderGraphEditor::findPass(std::string_view id) const
    {
        if (!m_Graph)
            return nullptr;
        for (auto& p : m_Graph->passes)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    std::string RenderGraphEditor::allocPassId(std::string_view type) const
    {
        // type_0, type_1, ...
        uint32_t idx = 0;
        while (true)
        {
            std::string id = std::string(type) + "_" + std::to_string(idx);
            if (!findPass(id))
                return id;
            ++idx;
        }
    }

    void RenderGraphEditor::beginUndoAction()
    {
        if (m_InUndoAction)
            return;
        m_InUndoAction = true;
        m_Cfg.undo.begin();
    }

    void RenderGraphEditor::commitUndoAction()
    {
        if (!m_InUndoAction)
            return;
        m_InUndoAction = false;
        m_Cfg.undo.end();
    }

    void RenderGraphEditor::addPass(std::string_view type)
    {
        if (!m_Graph)
            return;
        beginUndoAction();
        m_Cfg.undo.snapshot();

        PassDecl p;
        p.type    = std::string(type);
        p.id      = allocPassId(type);
        p.enabled = true;

        // auto-create slots from registry
        auto def = m_Registry.get(type);
        for (auto& in : def.inputs)
            p.inputs[in] = "";
        for (auto& out : def.outputs)
            p.outputs[out] = p.id + "." + out;

        m_Graph->passes.push_back(std::move(p));

        // Give it a default position near the mouse.
        const int nid = ensureNodeId(m_Graph->passes.back().id);
        ed::SetNodePosition(ed::NodeId(nid), ed::ScreenToCanvas(ImGui::GetMousePos()));

        m_Dirty = true;
        commitUndoAction();
    }

    void RenderGraphEditor::removePass(std::string_view passId)
    {
        if (!m_Graph)
            return;
        beginUndoAction();
        m_Cfg.undo.snapshot();

        // Remove incoming refs
        for (auto& p : m_Graph->passes)
        {
            for (auto it = p.inputs.begin(); it != p.inputs.end();)
            {
                auto rr = parseResRef(it->second);
                if (rr && rr->passId == passId)
                    it = p.inputs.erase(it);
                else
                    ++it;
            }
        }

        // Remove the pass
        m_Graph->passes.erase(std::remove_if(m_Graph->passes.begin(),
                                             m_Graph->passes.end(),
                                             [&](const PassDecl& p) { return p.id == passId; }),
                              m_Graph->passes.end());

        m_SelectedPassId.clear();
        m_Dirty = true;
        commitUndoAction();
    }

    std::optional<RenderGraphEditor::ResRef> RenderGraphEditor::parseResRef(std::string_view s)
    {
        const auto dot = s.find('.');
        if (dot == std::string_view::npos)
        {
            if (!s.empty() && s[0] == '@')
            {
                ResRef r;
                r.passId = std::string(s);
                r.slot   = "out";
                return r;
            }
            return std::nullopt;
        }
        if (dot == 0 || dot + 1 >= s.size())
            return std::nullopt;
        ResRef r;
        r.passId = std::string(s.substr(0, dot));
        r.slot   = std::string(s.substr(dot + 1));
        return r;
    }

    std::string RenderGraphEditor::makeResRef(std::string_view passId, std::string_view slot)
    {
        std::string s;
        s.reserve(passId.size() + slot.size() + 1);
        s.append(passId);
        s.push_back('.');
        s.append(slot);
        return s;
    }

    bool RenderGraphEditor::createLink(const PinKey& fromOutput, const PinKey& toInput, std::string* outError)
    {
        if (!m_Graph)
            return false;
        if (fromOutput.isInput || !toInput.isInput)
        {
            if (outError)
                *outError = "invalid link direction";
            return false;
        }
        if (fromOutput.passId == toInput.passId)
        {
            if (outError)
                *outError = "cannot link pass to itself";
            return false;
        }

        auto* dst = findPass(toInput.passId);
        auto* src = findPass(fromOutput.passId);
        if (!dst || !src)
        {
            if (outError)
                *outError = "unknown pass";
            return false;
        }

        beginUndoAction();
        m_Cfg.undo.snapshot();

        const std::string rr      = makeResRef(fromOutput.passId, fromOutput.slot);
        dst->inputs[toInput.slot] = rr;
        // Ensure outputs has an entry for the slot (editor convenience)
        src->outputs[fromOutput.slot] = rr;

        m_Dirty = true;
        commitUndoAction();
        return true;
    }

    void RenderGraphEditor::removeInputLink(std::string_view toPassId, std::string_view toSlot)
    {
        if (!m_Graph)
            return;
        auto* dst = findPass(toPassId);
        if (!dst)
            return;

        beginUndoAction();
        m_Cfg.undo.snapshot();

        dst->inputs.erase(std::string(toSlot));
        m_Dirty = true;
        commitUndoAction();
    }

    std::vector<std::string> RenderGraphEditor::buildTopoOrderPassIds(std::string* outError) const
    {
        std::vector<std::string> out;
        if (!m_Graph)
            return out;

        // Build indegree + adjacency from inputs (passId.slot refs)
        std::unordered_map<std::string, int>                      indeg;
        std::unordered_map<std::string, std::vector<std::string>> adj;
        indeg.reserve(m_Graph->passes.size());
        adj.reserve(m_Graph->passes.size());

        std::unordered_map<std::string, int> order;
        order.reserve(m_Graph->passes.size());
        for (int i = 0; i < (int)m_Graph->passes.size(); ++i)
        {
            const auto& p = m_Graph->passes[i];
            indeg[p.id]   = 0;
            order[p.id]   = i;
        }

        for (const auto& p : m_Graph->passes)
        {
            for (const auto& [_, srcRef] : p.inputs)
            {
                auto rr = parseResRef(srcRef);
                if (!rr)
                    continue;
                if (!findPass(rr->passId))
                    continue;
                // edge rr->passId -> p.id
                adj[rr->passId].push_back(p.id);
                indeg[p.id] += 1;
            }
        }

        // Kahn with stable order
        std::vector<std::string> ready;
        ready.reserve(m_Graph->passes.size());
        for (auto& [id, d] : indeg)
            if (d == 0)
                ready.push_back(id);

        auto readySort = [&] {
            std::sort(ready.begin(), ready.end(), [&](const std::string& a, const std::string& b) {
                return order[a] < order[b];
            });
        };
        readySort();

        while (!ready.empty())
        {
            std::string n = ready.front();
            ready.erase(ready.begin());
            out.push_back(n);

            auto it = adj.find(n);
            if (it == adj.end())
                continue;
            for (const auto& m : it->second)
            {
                indeg[m] -= 1;
                if (indeg[m] == 0)
                {
                    ready.push_back(m);
                    readySort();
                }
            }
        }

        if (out.size() != m_Graph->passes.size())
        {
            if (outError)
                *outError = "cycle detected";
            out.clear();
        }
        return out;
    }

    bool RenderGraphEditor::applyTopoOrder(std::string* outError)
    {
        if (!m_Graph)
            return true;
        auto ids = buildTopoOrderPassIds(outError);
        if (ids.empty() && !m_Graph->passes.empty())
            return false;

        std::unordered_map<std::string, PassDecl> map;
        map.reserve(m_Graph->passes.size());
        for (auto& p : m_Graph->passes)
            map.emplace(p.id, std::move(p));

        std::vector<PassDecl> ordered;
        ordered.reserve(ids.size());
        for (auto& id : ids)
            ordered.push_back(std::move(map.at(id)));

        beginUndoAction();
        m_Cfg.undo.snapshot();
        m_Graph->passes = std::move(ordered);
        m_Dirty         = true;
        commitUndoAction();
        return true;
    }

    void RenderGraphEditor::draw(const char* dockspaceName)
    {
        if (!m_Graph)
        {
            ImGui::TextUnformatted("RenderGraphEditor: no graph bound");
            return;
        }

        ed::SetCurrentEditor(m_EdCtx);

        ImGui::Begin(dockspaceName);
        drawToolbar();
        ImGui::Separator();

        ImGui::Columns(2, nullptr, true);
        drawCanvas();
        ImGui::NextColumn();
        drawInspector();
        ImGui::Columns(1);

        drawCreatePassPopup();
        ImGui::End();

        // Persist ui state each frame (cheap) so crashes don't lose layout.
        flushMeta();
    }

    void RenderGraphEditor::drawToolbar()
    {
        if (ImGui::Button("Add Pass"))
            m_OpenCreatePopup = true;
        ImGui::SameLine();
        if (ImGui::Button("Topo Order"))
        {
            std::string err;
            if (!applyTopoOrder(&err))
            {
                ImGui::OpenPopup("vrg_topo_error");
            }
        }

        if (ImGui::BeginPopupModal("vrg_topo_error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Failed to apply topo order (cycle detected)");
            if (ImGui::Button("OK"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void RenderGraphEditor::drawCanvas()
    {
        ed::Begin("Canvas", ImVec2(0, 0));

        // Create resource nodes (permanent)
        if (m_Graph && !m_Graph->resources.empty())
        {
            for (const auto& [name, _, __] : m_Graph->resources)
            {
                int nid;

                auto it = m_ResourceNodeIds.find(name);
                if (it == m_ResourceNodeIds.end())
                {
                    nid                     = int(fnv1a32(std::string("res:") + name));
                    m_ResourceNodeIds[name] = nid;
                }
                else
                {
                    nid = it->second;
                }

                ed::BeginNode(ed::NodeId(nid));

                ImGui::TextDisabled("%s", name.c_str());
                ImGui::Separator();

                PinKey pk {name, "out", false};
                int    pid = ensurePinId(pk);

                ed::BeginPin(ed::PinId(pid), ed::PinKind::Output);
                ImGui::Text("out");
                ed::EndPin();

                ed::EndNode();
            }
        }

        // Create nodes
        for (auto& p : m_Graph->passes)
        {
            const int nid = ensureNodeId(p.id);
            ed::BeginNode(ed::NodeId(nid));

            // header
            ImGui::Text("%s", p.id.c_str());
            ImGui::TextDisabled("%s", p.type.c_str());
            ImGui::Separator();

            // inputs
            for (const auto& [slot, src] : p.inputs)
            {
                PinKey pk {p.id, slot, true};
                int    pid = ensurePinId(pk);

                ed::BeginPin(ed::PinId(pid), ed::PinKind::Input);
                ImGui::Text("%s", slot.c_str());
                ed::EndPin();
            }

            ImGui::Spacing();

            // outputs
            for (const auto& [slot, res] : p.outputs)
            {
                PinKey pk {p.id, slot, false};
                int    pid = ensurePinId(pk);

                ed::BeginPin(ed::PinId(pid), ed::PinKind::Output);

                ImGui::Indent(40);
                ImGui::Text("%s", slot.c_str());
                ImGui::Unindent(40);

                ed::EndPin();
            }

            ed::EndNode();

            // Apply meta position once
            if (auto it = m_Ui.nodes.find(p.id); it != m_Ui.nodes.end())
            {
                if (it->second.hasPos)
                {
                    ed::SetNodePosition(ed::NodeId(nid), it->second.pos);
                    it->second.hasPos = false; // apply once
                }
            }
        }

        // Draw links from inputs
        int linkIdBase = 100000;
        for (const auto& p : m_Graph->passes)
        {
            for (const auto& [inSlot, srcRef] : p.inputs)
            {
                auto rr = parseResRef(srcRef);
                if (!rr)
                    continue;

                // allow resource nodes
                bool isResource = false;

                if (!findPass(rr->passId))
                {
                    if (m_Graph)
                    {
                        for (const auto& r : m_Graph->resources)
                        {
                            if (r.name == rr->passId)
                            {
                                isResource = true;
                                break;
                            }
                        }
                    }

                    if (!isResource)
                        continue;
                }

                PinKey    from {rr->passId, rr->slot, false};
                PinKey    to {p.id, inSlot, true};
                const int fromId = ensurePinId(from);
                const int toId   = ensurePinId(to);

                // Stable link id
                const int lid = int(fnv1a32(srcRef + "->" + p.id + "." + inSlot)) + linkIdBase;
                ed::Link(ed::LinkId(lid), ed::PinId(fromId), ed::PinId(toId));
            }
        }

        // Interaction: selection (poll every frame)
        m_SelectedPassId.clear();

        ed::NodeId nodes[1];
        int        count = ed::GetSelectedNodes(nodes, 1);

        if (count > 0)
        {
            int nodeId = nodes[0].Get();

            for (auto& [pid, nid] : m_NodeIds)
            {
                if (nid == nodeId)
                {
                    m_SelectedPassId = pid;
                    break;
                }
            }
        }

        // Create new link
        if (ed::BeginCreate())
        {
            ed::PinId a, b;
            if (ed::QueryNewLink(&a, &b))
            {
                // Find pin keys
                std::optional<PinKey> ka, kb;
                for (const auto& [k, id] : m_PinIds)
                {
                    if (id == (int)a.Get())
                        ka = k;
                    if (id == (int)b.Get())
                        kb = k;
                }

                if (ka && kb)
                {
                    // Normalize direction
                    PinKey from = *ka;
                    PinKey to   = *kb;
                    if (from.isInput && !to.isInput)
                        std::swap(from, to);

                    std::string err;
                    if (!from.isInput && to.isInput && createLink(from, to, &err))
                    {
                        ed::AcceptNewItem();
                    }
                    else
                    {
                        ed::RejectNewItem();
                    }
                }
            }
        }
        ed::EndCreate();

        // Delete links/nodes
        if (ed::BeginDelete())
        {
            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId))
            {
                if (ed::AcceptDeletedItem())
                {
                    // We don't have a reverse map from linkId; rebuild from current inputs by matching pins.
                    // NodeEditor provides endpoints:
                    ed::PinId a, b;
                    if (ed::GetLinkPins(linkId, &a, &b))
                    {
                        std::optional<PinKey> ka, kb;
                        for (const auto& [k, id] : m_PinIds)
                        {
                            if (id == (int)a.Get())
                                ka = k;
                            if (id == (int)b.Get())
                                kb = k;
                        }
                        if (ka && kb)
                        {
                            PinKey from = *ka;
                            PinKey to   = *kb;
                            if (from.isInput && !to.isInput)
                                std::swap(from, to);
                            if (!from.isInput && to.isInput)
                                removeInputLink(to.passId, to.slot);
                        }
                    }
                }
            }

            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId))
            {
                if (ed::AcceptDeletedItem())
                {
                    std::string passId;
                    for (auto& [pid, nid] : m_NodeIds)
                        if (nid == (int)nodeId.Get())
                            passId = pid;

                    // check resource node
                    bool isResource = false;
                    for (auto& [rid, nid] : m_ResourceNodeIds)
                    {
                        if (nid == (int)nodeId.Get())
                        {
                            isResource = true;
                            break;
                        }
                    }

                    if (!passId.empty() && !isResource)
                        removePass(passId);
                }
            }
        }
        ed::EndDelete();

        ed::End();
    }

    void RenderGraphEditor::drawInspector()
    {
        ImGui::TextUnformatted("Inspector");
        ImGui::Separator();

        if (m_SelectedPassId.empty())
        {
            ImGui::TextDisabled("Select a node to edit.");
            return;
        }

        auto* pass = findPass(m_SelectedPassId);
        if (!pass)
        {
            ImGui::TextDisabled("Invalid selection.");
            return;
        }

        ImGui::Text("%s", pass->id.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", pass->type.c_str());

        bool enabled = pass->enabled;
        if (ImGui::Checkbox("Enabled", &enabled))
        {
            beginUndoAction();
            m_Cfg.undo.snapshot();
            pass->enabled = enabled;
            m_Dirty       = true;
            commitUndoAction();
        }

        if (ImGui::Button("Delete Pass"))
        {
            removePass(pass->id);
            return;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Inputs");
        for (auto it = pass->inputs.begin(); it != pass->inputs.end();)
        {
            ImGui::PushID(it->first.c_str());
            ImGui::Text("%s", it->first.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", it->second.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                removeInputLink(pass->id, it->first);
                it = pass->inputs.begin();
                ImGui::PopID();
                continue;
            }
            ImGui::PopID();
            ++it;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Outputs");
        for (auto& [k, v] : pass->outputs)
        {
            ImGui::Text("%s", k.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", v.c_str());
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Params (JSON)");
        const std::string paramsStr = pass->params.raw().dump(2);
        ImGui::BeginChild("##params", ImVec2(-1, 200), true);
        ImGui::TextUnformatted(paramsStr.c_str());
        ImGui::EndChild();
        ImGui::TextDisabled("(Editing params is engine-specific; this view is read-only.)");
    }

    void RenderGraphEditor::drawCreatePassPopup()
    {
        if (m_OpenCreatePopup)
        {
            ImGui::OpenPopup("vrg_create_pass");
            m_OpenCreatePopup = false;
        }

        if (ImGui::BeginPopup("vrg_create_pass"))
        {
            ImGui::TextUnformatted("Add Pass");
            ImGui::Separator();

            auto types = m_Registry.listTypes();
            std::sort(types.begin(), types.end(), [](auto a, auto b) { return a < b; });
            for (auto t : types)
            {
                if (ImGui::MenuItem(std::string(t).c_str()))
                {
                    addPass(t);
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }

            ImGui::EndPopup();
        }
    }

    void RenderGraphEditor::drawResourcePanel()
    {
        // Reserved for future: show imported resources as special nodes.
    }

} // namespace vrendergraph::editor

#endif // VRENDERGRAPH_EDITOR_IMPLEMENTATION
