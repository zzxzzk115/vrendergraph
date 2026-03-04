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
//   #include <vrendergraph_editor.hpp>
//
//   // Everywhere else:
//   #include <vrendergraph_editor.hpp>
// -----------------------------------------------------------------------------
//
// NOTE:
//   This version uses imnodes internally (instead of ax::NodeEditor).
//   Public API is kept stable; internal types are adapted via a small 'ed' shim.
// -----------------------------------------------------------------------------

#include "vrendergraph/registry.hpp"
#include "vrendergraph/render_graph_desc.hpp"

#include <imgui.h>
#include <imnodes/imnodes.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vrendergraph::editor
{
    // ---------------------------------------------------------------------
    // Compatibility shim for previous ax::NodeEditor-facing API surface.
    //
    // We keep:
    //   - Config::nodeEditorContext field name & 'ed::EditorContext*' type
    // while internally mapping to imnodes' EditorContext.
    // ---------------------------------------------------------------------
    namespace ed
    {
        using EditorContext = ImNodesEditorContext;

        struct NodeId
        {
            int v    = 0;
            NodeId() = default;
            explicit NodeId(int x) : v(x) {}
            int Get() const { return v; }
        };
        struct PinId
        {
            int v   = 0;
            PinId() = default;
            explicit PinId(int x) : v(x) {}
            int Get() const { return v; }
        };
        struct LinkId
        {
            int v    = 0;
            LinkId() = default;
            explicit LinkId(int x) : v(x) {}
            int Get() const { return v; }
        };
    } // namespace ed

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
        const RenderGraphDesc* graph()
        {
            std::string error;
            if (m_Dirty)
                applyTopoOrder(&error);
            return m_Graph;
        }

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
            std::string passId; // pass id OR resource name (e.g. "@backbuffer")
            std::string slot;   // input/output slot name; resources use "out"
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

        // Inspector removed: everything is done via context menus.
        // void drawInspector();

        void drawCreatePassPopup();
        void drawResourcePanel();

        // context menus
        void drawContextMenus();
        void drawBackgroundContextMenu();
        void drawNodeContextMenu();
        void drawLinkContextMenu();

        void beginUndoAction();
        void commitUndoAction();

        // ---- graph mutation ----
        PassDecl*       findPass(std::string_view id);
        const PassDecl* findPass(std::string_view id) const;

        std::string allocPassId(std::string_view type) const;
        void        addPass(std::string_view type);
        void        removePass(std::string_view passId);

        // Resource nodes
        void addResource(std::string_view name);
        void removeResource(std::string_view name);

        // Link create/delete
        struct ResRef
        {
            std::string passId; // pass id OR resource name ("@backbuffer")
            std::string slot;   // pass output slot, or "out" for resources
        };
        static std::optional<ResRef> parseResRef(std::string_view s);

        // IMPORTANT:
        // - Pass outputs are referenced as "passId.slot".
        // - Resources are referenced as "@resource" (NO ".out") in RenderGraphDesc.
        static std::string makeResRef(std::string_view passId, std::string_view slot);

        bool createLink(const PinKey& fromOutput, const PinKey& toInput, std::string* outError = nullptr);

        // IMPORTANT: slots are fixed by registry.
        // Removing a link must NOT erase the slot key; it only clears the reference string.
        void removeInputLink(std::string_view toPassId, std::string_view toSlot);

        // helper: find input endpoint by linkId (computed deterministically)
        bool findInputByLinkId(int linkId, std::string& outToPassId, std::string& outToSlot) const;

        // helper: reverse map attribute id -> PinKey
        std::optional<PinKey> findPinKeyByAttrId(int attrId) const;

        // helper: is a name a listed resource?
        bool isListedResource(std::string_view name) const;

        // ---- state ----
        const RenderGraphRegistry& m_Registry;
        Config                     m_Cfg;
        RenderGraphDesc*           m_Graph = nullptr;
        bool                       m_Dirty = false;

        // selection
        std::string m_SelectedPassId;

        // undo action grouping
        bool m_InUndoAction = false;

        // node editor context (imnodes editor context)
        ed::EditorContext* m_EdCtx    = nullptr;
        bool               m_OwnEdCtx = false;

        // cached ui state
        EditorUiState m_Ui;

        // ids
        std::unordered_map<std::string, int>                  m_NodeIds;
        std::unordered_map<PinKey, int, PinKeyHash, PinKeyEq> m_PinIds;
        std::unordered_map<std::string, int>                  m_ResourceNodeIds;

        // context menu state
        int m_ContextNodeId = 0;
        int m_ContextLinkId = 0;

        // popup
        bool m_OpenCreatePopup = false;

        // apply-once flags
        bool m_AppliedViewOnce = false;

        // spawn position for Add Pass (screen-space)
        ImVec2 m_SpawnNodeScreenPos {0, 0};
        bool   m_HasSpawnPos = false;
    };

} // namespace vrendergraph::editor

#ifdef VRENDERGRAPH_EDITOR_IMPLEMENTATION

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
    // Node style helpers
    // ---------------------------------------------------------------------

    static ImU32 nodeColorFromType(std::string_view type, bool isResource = false)
    {
        // Resource nodes → fixed orange
        if (isResource)
            return IM_COL32(230, 140, 40, 255);

        // Contains depth -> fixed gray
        // convert to lowercase for case-insensitive search
        std::string typeLower(type);
        std::transform(
            typeLower.begin(), typeLower.end(), typeLower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (typeLower.find("depth") != std::string_view::npos)
            return IM_COL32(180, 180, 180, 255);

        // Present → fixed green
        if (type == "Present")
            return IM_COL32(80, 200, 120, 255);

        // FNV1a hash
        uint32_t h = 2166136261u;
        for (char c : type)
        {
            h ^= uint32_t(c);
            h *= 16777619u;
        }

        // map hash → hue
        float hue = float(h % 360) / 360.0f;

        // avoid very dark colors
        const float saturation = 0.55f;
        const float value      = 0.85f;

        ImVec4 outColor;
        outColor.w = 1.0f;

        ImGui::ColorConvertHSVtoRGB(hue, saturation, value, outColor.x, outColor.y, outColor.z);

        return ImGui::ColorConvertFloat4ToU32(outColor);
    }

    void pushNodeTitleColors(std::string_view type, bool isResource = false)
    {
        ImU32 base = nodeColorFromType(type, isResource);

        ImVec4 c = ImGui::ColorConvertU32ToFloat4(base);

        ImVec4 hovered  = c;
        ImVec4 selected = c;

        hovered.x = std::min(hovered.x + 0.08f, 1.0f);
        hovered.y = std::min(hovered.y + 0.08f, 1.0f);
        hovered.z = std::min(hovered.z + 0.08f, 1.0f);

        selected.x = std::min(selected.x + 0.15f, 1.0f);
        selected.y = std::min(selected.y + 0.15f, 1.0f);
        selected.z = std::min(selected.z + 0.15f, 1.0f);

        ImNodes::PushColorStyle(ImNodesCol_TitleBar, base);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, ImGui::ColorConvertFloat4ToU32(hovered));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, ImGui::ColorConvertFloat4ToU32(selected));
    }

    void popNodeTitleColors()
    {
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    void textOutlined(const char* text, uint32_t fontSize = 18)
    {
        ImVec2 pos  = ImGui::GetCursorScreenPos();
        auto*  draw = ImGui::GetWindowDrawList();

        ImU32 outline          = IM_COL32(0, 0, 0, 255);
        ImU32 main             = ImGui::GetColorU32(ImGuiCol_Text);
        auto  font             = ImGui::GetFont();
        auto  internalFontSize = ImGui::GetFontSize();

        draw->AddText(font, fontSize, ImVec2(pos.x - 1, pos.y), outline, text);
        draw->AddText(font, fontSize, ImVec2(pos.x + 1, pos.y), outline, text);
        draw->AddText(font, fontSize, ImVec2(pos.x, pos.y - 1), outline, text);
        draw->AddText(font, fontSize, ImVec2(pos.x, pos.y + 1), outline, text);

        draw->AddText(font, fontSize, pos, main, text);

        auto scale = fontSize / internalFontSize;
        auto size  = ImGui::CalcTextSize(text);
        ImGui::Dummy(ImVec2(size.x * scale, size.y * scale));
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
        // Avoid zero ids (some node libs treat 0 as invalid)
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
            m_EdCtx    = ImNodes::EditorContextCreate();
            m_OwnEdCtx = true;
        }
    }

    RenderGraphEditor::~RenderGraphEditor()
    {
        if (m_OwnEdCtx && m_EdCtx)
            ImNodes::EditorContextFree(m_EdCtx);
        m_EdCtx = nullptr;
    }

    void RenderGraphEditor::setGraph(RenderGraphDesc* desc)
    {
        m_Graph = desc;

        m_SelectedPassId.clear();
        m_NodeIds.clear();
        m_PinIds.clear();
        m_ResourceNodeIds.clear();

        m_Dirty = false;

        m_AppliedViewOnce = false;
        m_HasSpawnPos     = false;

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

        if (m_EdCtx)
            ImNodes::EditorContextSet(m_EdCtx);

        // Pull node positions from imnodes.
        for (auto& p : m_Graph->passes)
        {
            const int    nid = ensureNodeId(p.id);
            NodeUiState& st  = m_Ui.nodes[p.id];
            st.pos           = ImNodes::GetNodeGridSpacePos(nid);
            st.hasPos        = true;
            // imnodes collapse state is not persisted here
            st.collapsed = false;
        }

        // View: panning is supported via editor context.
        if (m_EdCtx)
        {
            m_Ui.view.pan     = ImNodes::EditorContextGetPanning();
            m_Ui.view.zoom    = 1.0f; // imnodes doesn't expose zoom in the public API
            m_Ui.view.hasView = true;
        }

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

        // Build a stable attribute id.
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

    bool RenderGraphEditor::isListedResource(std::string_view name) const
    {
        if (!m_Graph)
            return false;
        for (const auto& r : m_Graph->resources)
        {
            if (r.name == name)
                return true;
        }
        return false;
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
            p.inputs[in] = ""; // fixed slot, empty means unconnected
        for (auto& out : def.outputs)
            p.outputs[out] = p.id + "." + out;

        // initialize default params
        for (auto& param : def.params)
        {
            p.params.raw()[param.name] = param.defaultValue;
        }

        m_Graph->passes.push_back(std::move(p));

        const std::string& newId = m_Graph->passes.back().id;
        const int          nid   = ensureNodeId(newId);

        // Place the node at the popup click position if available.
        if (m_HasSpawnPos)
        {
            ImNodes::SetNodeScreenSpacePos(nid, m_SpawnNodeScreenPos);
            m_HasSpawnPos = false;
        }
        else
        {
            // fallback
            ImVec2 pan =
                (m_EdCtx ? (ImNodes::EditorContextSet(m_EdCtx), ImNodes::EditorContextGetPanning()) : ImVec2(0, 0));
            ImVec2 mp = ImGui::GetMousePos();
            ImNodes::SetNodeGridSpacePos(nid, ImVec2(mp.x - pan.x, mp.y - pan.y));
        }

        m_Dirty = true;
        commitUndoAction();
    }

    void RenderGraphEditor::removePass(std::string_view passId)
    {
        if (!m_Graph)
            return;

        beginUndoAction();
        m_Cfg.undo.snapshot();

        // Remove incoming refs (but keep slots fixed)
        for (auto& p : m_Graph->passes)
        {
            for (auto& [slot, srcRef] : p.inputs)
            {
                auto rr = parseResRef(srcRef);
                if (rr && rr->passId == passId)
                    srcRef.clear();
            }
        }

        // Remove the pass
        m_Graph->passes.erase(std::remove_if(m_Graph->passes.begin(),
                                             m_Graph->passes.end(),
                                             [&](const PassDecl& p) { return p.id == passId; }),
                              m_Graph->passes.end());

        if (m_SelectedPassId == passId)
            m_SelectedPassId.clear();

        m_Dirty = true;
        commitUndoAction();
    }

    void RenderGraphEditor::addResource(std::string_view name)
    {
        if (!m_Graph)
            return;

        if (name.empty())
            return;

        // Do not add duplicates
        for (const auto& r : m_Graph->resources)
            if (r.name == name)
                return;

        beginUndoAction();
        m_Cfg.undo.snapshot();

        ResourceDecl r;
        r.name = std::string(name);
        m_Graph->resources.push_back(std::move(r));

        // ensure a stable node id entry is created (optional)
        const int nid = int(fnv1a32(std::string("res:") + std::string(name)));
        m_ResourceNodeIds.emplace(std::string(name), nid);

        m_Dirty = true;
        commitUndoAction();
    }

    void RenderGraphEditor::removeResource(std::string_view name)
    {
        if (!m_Graph)
            return;

        if (name.empty())
            return;

        beginUndoAction();
        m_Cfg.undo.snapshot();

        // Clear any pass inputs that point to this resource (keep slots fixed)
        for (auto& p : m_Graph->passes)
        {
            for (auto& [slot, srcRef] : p.inputs)
            {
                if (srcRef == name)
                    srcRef.clear();
            }
        }

        m_Graph->resources.erase(std::remove_if(m_Graph->resources.begin(),
                                                m_Graph->resources.end(),
                                                [&](const ResourceDecl& r) { return r.name == name; }),
                                 m_Graph->resources.end());

        m_ResourceNodeIds.erase(std::string(name));

        m_Dirty = true;
        commitUndoAction();
    }
    std::optional<RenderGraphEditor::ResRef> RenderGraphEditor::parseResRef(std::string_view s)
    {
        // Resource references have NO '.' and map to a single output pin named "out".
        // Pass output references use the form "passId.slot".
        const auto dot = s.find('.');
        if (dot == std::string_view::npos)
        {
            if (s.empty())
                return std::nullopt;

            ResRef r;
            r.passId = std::string(s);
            r.slot   = "out";
            return r;
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
        // Resources are referenced by NAME only (no ".out") in RenderGraphDesc inputs.
        // Internally we still treat them as a single output pin with slot "out".
        if (slot == "out")
            return std::string(passId);

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

        // Directionality:
        //   output -> input
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
        if (!dst)
        {
            if (outError)
                *outError = "unknown destination pass";
            return false;
        }

        // Validate: destination slot must exist (fixed by registry)
        if (dst->inputs.find(toInput.slot) == dst->inputs.end())
        {
            if (outError)
                *outError = "unknown destination slot";
            return false;
        }

        // Source can be a pass or a resource node.
        const bool srcIsPass = (findPass(fromOutput.passId) != nullptr);
        const bool srcIsResource =
            (!fromOutput.passId.empty() && fromOutput.passId[0] == '@') || isListedResource(fromOutput.passId);

        if (!srcIsPass && !srcIsResource)
        {
            if (outError)
                *outError = "unknown source pass/resource";
            return false;
        }

        // For resources, only allow slot "out".
        if (!srcIsPass)
        {
            if (fromOutput.slot != "out")
            {
                if (outError)
                    *outError = "invalid resource slot";
                return false;
            }
        }
        else
        {
            // For passes, validate output slot exists.
            auto* src = findPass(fromOutput.passId);
            if (!src || src->outputs.find(fromOutput.slot) == src->outputs.end())
            {
                if (outError)
                    *outError = "unknown source output slot";
                return false;
            }
        }

        beginUndoAction();
        m_Cfg.undo.snapshot();

        const std::string rr = makeResRef(fromOutput.passId, fromOutput.slot);

        // IMPORTANT: fixed slots, so overwrite the ref string.
        dst->inputs[toInput.slot] = rr;

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

        // Keep the slot key; clear the reference string.
        auto it = dst->inputs.find(std::string(toSlot));
        if (it != dst->inputs.end())
            it->second.clear();

        m_Dirty = true;
        commitUndoAction();
    }

    std::optional<RenderGraphEditor::PinKey> RenderGraphEditor::findPinKeyByAttrId(int attrId) const
    {
        for (const auto& [k, id] : m_PinIds)
        {
            if (id == attrId)
                return k;
        }
        return std::nullopt;
    }

    bool RenderGraphEditor::findInputByLinkId(int linkId, std::string& outToPassId, std::string& outToSlot) const
    {
        if (!m_Graph)
            return false;

        const int linkIdBase = 100000;

        for (const auto& p : m_Graph->passes)
        {
            for (const auto& [inSlot, srcRef] : p.inputs)
            {
                if (srcRef.empty())
                    continue;

                const int lid = int(fnv1a32(srcRef + "->" + p.id + "." + inSlot)) + linkIdBase;
                if (lid == linkId)
                {
                    outToPassId = p.id;
                    outToSlot   = inSlot;
                    return true;
                }
            }
        }
        return false;
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
                if (srcRef.empty())
                    continue;

                auto rr = parseResRef(srcRef);
                if (!rr)
                    continue;

                // Only create dependency edges for pass->pass (resource nodes don't impose ordering).
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

        if (m_EdCtx)
            ImNodes::EditorContextSet(m_EdCtx);

        ImGui::Begin(dockspaceName);
        drawToolbar();
        ImGui::Separator();

        drawCanvas();

        drawCreatePassPopup();
        ImGui::End();

        // Persist ui state each frame (cheap) so crashes don't lose layout.
        flushMeta();
    }

    void RenderGraphEditor::drawToolbar()
    {
        // All editing operations are available via right click menus.
        // Keep topo order as a toolbar button since it's a global action.
        if (ImGui::Button("Topo Order"))
        {
            std::string err;
            if (!applyTopoOrder(&err))
                ImGui::OpenPopup("vrg_topo_error");
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
        // Apply saved view panning once per setGraph.
        if (!m_AppliedViewOnce && m_Ui.view.hasView && m_EdCtx)
        {
            ImNodes::EditorContextSet(m_EdCtx);
            ImNodes::EditorContextResetPanning(m_Ui.view.pan);
            m_AppliedViewOnce = true;
        }

        ImNodes::BeginNodeEditor();

        // -----------------------------------------------------------------
        // Resource nodes (permanent)
        // -----------------------------------------------------------------
        if (m_Graph && !m_Graph->resources.empty())
        {
            for (const auto& r : m_Graph->resources)
            {
                const std::string& name = r.name;

                int  nid = 0;
                auto it  = m_ResourceNodeIds.find(name);
                if (it == m_ResourceNodeIds.end())
                {
                    nid                     = int(fnv1a32(std::string("res:") + name));
                    m_ResourceNodeIds[name] = nid;
                }
                else
                {
                    nid = it->second;
                }

                pushNodeTitleColors("resource", true);
                ImNodes::BeginNode(nid);

                ImNodes::BeginNodeTitleBar();
                textOutlined(name.c_str());
                ImNodes::EndNodeTitleBar();

                // Resources have a single output pin.
                PinKey pk {name, "out", false};
                int    pid = ensurePinId(pk);

                ImNodes::BeginOutputAttribute(pid, ImNodesPinShape_CircleFilled);
                ImGui::TextUnformatted("out");
                ImNodes::EndOutputAttribute();

                ImNodes::EndNode();
                popNodeTitleColors();
            }
        }

        // -----------------------------------------------------------------
        // Pass nodes
        // -----------------------------------------------------------------
        for (auto& p : m_Graph->passes)
        {
            const int nid = ensureNodeId(p.id);

            const float node_width = 140.0f;

            pushNodeTitleColors(p.type);
            ImNodes::BeginNode(nid);

            // -------------------------------------------------------------
            // Title
            // -------------------------------------------------------------
            ImNodes::BeginNodeTitleBar();

            // try get the last number from id (from GBuffer_0 to get 0)
            // if the id ends with number N, generate a new alias N+1 (GBuffer #1)
            std::string alias          = p.id;
            const auto  lastUnderscore = alias.find_last_of('_');
            if (lastUnderscore != std::string::npos)
            {
                const auto& suffix = alias.substr(lastUnderscore + 1);
                if (std::all_of(suffix.begin(), suffix.end(), ::isdigit))
                {
                    alias = alias.substr(0, lastUnderscore) + " #" + std::to_string(std::stoi(suffix) + 1);
                }
            }
            textOutlined(alias.c_str());
            ImNodes::EndNodeTitleBar();

            // enforce consistent width
            ImGui::Dummy(ImVec2(node_width, 0.f));

            // -------------------------------------------------------------
            // Parameters
            // -------------------------------------------------------------
            auto def = m_Registry.get(p.type);

            for (auto& param : def.params)
            {
                auto& j = p.params.raw();

                if (!j.contains(param.name))
                    j[param.name] = param.defaultValue;

                if (param.type == ParamType::eFloat)
                {
                    float v = j[param.name];

                    // Support optional min/max for better UX, but allow unbounded values by default.
                    std::optional<float> min, max;
                    if (param.minValue.has_value())
                    {
                        min = param.minValue.value();
                    }
                    if (param.maxValue.has_value())
                    {
                        max = param.maxValue.value();
                    }

                    const float label_width = ImGui::CalcTextSize(param.name.c_str()).x;

                    ImGui::TextUnformatted(param.name.c_str());
                    ImGui::SameLine();

                    ImGui::PushItemWidth(node_width - label_width - 12.f);

                    bool changed = false;
                    if (min.has_value() && max.has_value())
                    {
                        changed = ImGui::DragFloat("##param_float_value", &v, 0.01f, min.value(), max.value());
                    }
                    else
                    {
                        changed = ImGui::DragFloat("##param_float_value", &v, 0.01f);
                    }

                    if (changed)
                    {
                        beginUndoAction();
                        m_Cfg.undo.snapshot();

                        j[param.name] = v;
                        m_Dirty       = true;

                        commitUndoAction();
                    }

                    ImGui::PopItemWidth();
                }
                else if (param.type == ParamType::eInt)
                {
                    int v = j[param.name];

                    // Support optional min/max for better UX, but allow unbounded values by default.
                    std::optional<int> min, max;
                    if (param.minValue.has_value())
                    {
                        min = int(param.minValue.value());
                    }
                    if (param.maxValue.has_value())
                    {
                        max = int(param.maxValue.value());
                    }

                    const float label_width = ImGui::CalcTextSize(param.name.c_str()).x;

                    ImGui::TextUnformatted(param.name.c_str());
                    ImGui::SameLine();

                    ImGui::PushItemWidth(node_width - label_width - 12.f);

                    bool changed = false;
                    if (min.has_value() && max.has_value())
                    {
                        changed = ImGui::DragInt("##value", &v, 1, min.value(), max.value());
                    }
                    else
                    {
                        changed = ImGui::DragInt("##param_int_value", &v, 1);
                    }

                    if (changed)
                    {
                        beginUndoAction();
                        m_Cfg.undo.snapshot();

                        j[param.name] = v;
                        m_Dirty       = true;

                        commitUndoAction();
                    }

                    ImGui::PopItemWidth();
                }
                else if (param.type == ParamType::eBoolean)
                {
                    bool v = j[param.name];

                    const float label_width = ImGui::CalcTextSize(param.name.c_str()).x;

                    ImGui::TextUnformatted(param.name.c_str());
                    ImGui::SameLine();

                    ImGui::PushItemWidth(node_width - label_width - 12.f);

                    bool changed = ImGui::Checkbox("##param_boolean_value", &v);

                    if (changed)
                    {
                        beginUndoAction();
                        m_Cfg.undo.snapshot();

                        j[param.name] = v;
                        m_Dirty       = true;

                        commitUndoAction();
                    }

                    ImGui::PopItemWidth();
                }
                else if (param.type == ParamType::eString)
                {
                    std::string v = j[param.name];

                    const float label_width = ImGui::CalcTextSize(param.name.c_str()).x;

                    ImGui::TextUnformatted(param.name.c_str());
                    ImGui::SameLine();

                    ImGui::PushItemWidth(node_width - label_width - 12.f);

                    char buf[256];
                    std::strncpy(buf, v.c_str(), sizeof(buf));
                    bool changed = ImGui::InputText("##param_str_value", buf, sizeof(buf));

                    if (changed)
                    {
                        beginUndoAction();
                        m_Cfg.undo.snapshot();

                        j[param.name] = std::string(buf);
                        m_Dirty       = true;

                        commitUndoAction();
                    }

                    ImGui::PopItemWidth();
                }
            }

            if (!def.params.empty())
                ImGui::Spacing();

            // -------------------------------------------------------------
            // Inputs
            // -------------------------------------------------------------
            for (const auto& [slot, src] : p.inputs)
            {
                (void)src;

                PinKey pk {p.id, slot, true};
                int    pid = ensurePinId(pk);

                ImNodes::BeginInputAttribute(pid, ImNodesPinShape_CircleFilled);

                ImGui::TextUnformatted(slot.c_str());

                ImNodes::EndInputAttribute();
            }

            if (!p.inputs.empty())
                ImGui::Spacing();

            // -------------------------------------------------------------
            // Outputs
            // -------------------------------------------------------------
            for (const auto& [slot, res] : p.outputs)
            {
                (void)res;

                PinKey pk {p.id, slot, false};
                int    pid = ensurePinId(pk);

                const float label_width = ImGui::CalcTextSize(slot.c_str()).x;

                ImNodes::BeginOutputAttribute(pid, ImNodesPinShape_CircleFilled);

                ImGui::Indent(node_width - label_width);
                ImGui::TextUnformatted(slot.c_str());

                ImNodes::EndOutputAttribute();
            }

            ImNodes::EndNode();
            popNodeTitleColors();

            // -------------------------------------------------------------
            // Apply saved position
            // -------------------------------------------------------------
            if (auto it = m_Ui.nodes.find(p.id); it != m_Ui.nodes.end())
            {
                if (it->second.hasPos)
                {
                    ImNodes::SetNodeGridSpacePos(nid, it->second.pos);
                    it->second.hasPos = false;
                }
            }
        }

        // -----------------------------------------------------------------
        // Links (RenderGraphDesc is the source of truth)
        // -----------------------------------------------------------------
        const int linkIdBase = 100000;

        for (const auto& p : m_Graph->passes)
        {
            for (const auto& [inSlot, srcRef] : p.inputs)
            {
                if (srcRef.empty())
                    continue;

                auto rr = parseResRef(srcRef);
                if (!rr)
                    continue;

                // allow resource nodes: if rr->passId isn't a pass, it must be a listed resource
                const bool srcIsPass = (findPass(rr->passId) != nullptr);
                if (!srcIsPass)
                {
                    if (!isListedResource(rr->passId) && !(!rr->passId.empty() && rr->passId[0] == '@'))
                        continue;
                }

                PinKey from {rr->passId, rr->slot, false};
                PinKey to {p.id, inSlot, true};

                const int fromId = ensurePinId(from);
                const int toId   = ensurePinId(to);

                const int lid = int(fnv1a32(srcRef + "->" + p.id + "." + inSlot)) + linkIdBase;
                ImNodes::Link(lid, fromId, toId);
            }
        }

        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);

        ImNodes::EndNodeEditor();

        // -----------------------------------------------------------------
        // Post-frame interaction queries (imnodes: call after EndNodeEditor)
        // -----------------------------------------------------------------

        // Selection (poll every frame)
        m_SelectedPassId.clear();
        if (ImNodes::NumSelectedNodes() > 0)
        {
            std::vector<int> sel;
            sel.resize((size_t)ImNodes::NumSelectedNodes());
            ImNodes::GetSelectedNodes(sel.data());
            const int nodeId = sel[0];

            for (auto& [pid, nid] : m_NodeIds)
            {
                if (nid == nodeId)
                {
                    m_SelectedPassId = pid;
                    break;
                }
            }
        }

        // Create new link (directional: output -> input)
        {
            int a = 0, b = 0;
            if (ImNodes::IsLinkCreated(&a, &b))
            {
                auto ka = findPinKeyByAttrId(a);
                auto kb = findPinKeyByAttrId(b);

                if (ka && kb)
                {
                    PinKey from = *ka;
                    PinKey to   = *kb;

                    // Normalize: output -> input
                    if (from.isInput && !to.isInput)
                        std::swap(from, to);

                    if (!from.isInput && to.isInput)
                    {
                        std::string err;
                        createLink(from, to, &err);
                    }
                }
            }
        }

        // Delete link
        {
            int destroyedLink = 0;
            if (ImNodes::IsLinkDestroyed(&destroyedLink))
            {
                std::string toPass, toSlot;
                if (findInputByLinkId(destroyedLink, toPass, toSlot))
                    removeInputLink(toPass, toSlot);
            }
        }

        // Context menus (background/node/link)
        drawContextMenus();
    }

    void RenderGraphEditor::drawContextMenus()
    {
        // Determine hovered element (imnodes query must happen after EndNodeEditor).
        int hoveredNode = 0;
        int hoveredLink = 0;

        const bool nodeHovered   = ImNodes::IsNodeHovered(&hoveredNode);
        const bool linkHovered   = ImNodes::IsLinkHovered(&hoveredLink);
        const bool editorHovered = ImNodes::IsEditorHovered();

        // Match imnodes example:
        //   require window focus + editor hovered + no item hovered
        const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        const bool openPopup = windowFocused && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
        if (openPopup)
        {
            // Priority: link > node > background
            if (linkHovered)
            {
                m_ContextLinkId = hoveredLink;
                ImGui::OpenPopup("vrg_link_menu");
            }
            else if (nodeHovered)
            {
                m_ContextNodeId = hoveredNode;
                ImGui::OpenPopup("vrg_node_menu");
            }
            else
            {
                // background menu: store click position for spawning nodes
                m_SpawnNodeScreenPos = ImGui::GetMousePos();
                m_HasSpawnPos        = true;
                ImGui::OpenPopup("vrg_bg_menu");
            }
        }

        drawBackgroundContextMenu();
        drawNodeContextMenu();
        drawLinkContextMenu();
    }

    void RenderGraphEditor::drawBackgroundContextMenu()
    {
        if (ImGui::BeginPopup("vrg_bg_menu"))
        {
            // Use the opening popup mouse pos as the spawn point (same as imnodes example).
            m_SpawnNodeScreenPos = ImGui::GetMousePosOnOpeningCurrentPopup();
            m_HasSpawnPos        = true;

            if (ImGui::BeginMenu("Add Resource"))
            {
                auto names = m_Registry.listResources();
                std::sort(names.begin(), names.end(), [](auto a, auto b) { return a < b; });

                for (auto n : names)
                {
                    // show disabled if already exists
                    const bool exists = isListedResource(n);
                    if (ImGui::MenuItem(std::string(n).c_str(), nullptr, false, !exists))
                    {
                        addResource(n);
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                }

                // Allow custom resources (not registered) as a fallback.
                ImGui::Separator();
                static char customName[128] = {};
                ImGui::InputText("Name", customName, sizeof(customName));
                if (ImGui::Button("Add") && customName[0] != 0)
                {
                    addResource(std::string_view(customName));
                    customName[0] = 0;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Add Pass"))
            {
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

                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Topo Order"))
            {
                std::string err;
                if (!applyTopoOrder(&err))
                    ImGui::OpenPopup("vrg_topo_error");
            }

            ImGui::EndPopup();
        }
    }

    void RenderGraphEditor::drawNodeContextMenu()
    {
        if (ImGui::BeginPopup("vrg_node_menu"))
        {
            // map node id -> passId (ignore resource nodes)
            std::string passId;
            for (auto& [pid, nid] : m_NodeIds)
            {
                if (nid == m_ContextNodeId)
                {
                    passId = pid;
                    break;
                }
            }

            // check resource node
            std::string resourceName;
            for (auto& [rid, nid] : m_ResourceNodeIds)
            {
                if (nid == m_ContextNodeId)
                {
                    resourceName = rid;
                    break;
                }
            }

            if (!resourceName.empty())
            {
                if (ImGui::MenuItem("Delete Resource"))
                {
                    removeResource(resourceName);
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
                return;
            }

            if (!passId.empty())
            {
                auto* pass = findPass(passId);
                if (pass)
                {
                    const bool enabled = pass->enabled;
                    if (ImGui::MenuItem("Enabled", nullptr, enabled))
                    {
                        beginUndoAction();
                        m_Cfg.undo.snapshot();
                        pass->enabled = !enabled;
                        m_Dirty       = true;
                        commitUndoAction();
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Delete Pass"))
                    {
                        removePass(passId);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }

            ImGui::EndPopup();
        }
    }

    void RenderGraphEditor::drawLinkContextMenu()
    {
        if (ImGui::BeginPopup("vrg_link_menu"))
        {
            if (ImGui::MenuItem("Delete Link"))
            {
                std::string toPass, toSlot;
                if (findInputByLinkId(m_ContextLinkId, toPass, toSlot))
                    removeInputLink(toPass, toSlot);

                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void RenderGraphEditor::drawCreatePassPopup()
    {
        // Kept for compatibility with old workflow; not required if only context menus are used.
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