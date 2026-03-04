# vrendergraph Editor Integration Example

Below is a minimal example showing how any application can integrate the **vrendergraph editor**.

The editor is **header-only** and depends on:

- ImGui
- ax::NodeEditor

The runtime (`vrendergraph`) itself has **no UI dependency**.

---

## 1. Include editor

In **one translation unit**:

```cpp
#define VRENDERGRAPH_EDITOR_IMPLEMENTATION
#include <vrendergraph/vrendergraph_editor.hpp>
```

In other files:

```cpp
#include <vrendergraph/vrendergraph_editor.hpp>
```

---

## 2. Register passes

Applications register render passes via `RenderGraphRegistry`.

```cpp
#include <vrendergraph/vrendergraph.hpp>

using namespace vultra::rg;

static void registerPasses(RenderGraphRegistry& registry)
{
    registry.registerPass({
        .type = "texture_value",
        .setup = [](FrameGraph&, FrameGraphBlackboard&, const ParamBlock&)
        {
            // runtime logic
        }
    });

    registry.registerPass({
        .type = "lighting",
        .setup = [](FrameGraph&, FrameGraphBlackboard&, const ParamBlock&)
        {
        }
    });

    registry.registerPass({
        .type = "present",
        .setup = [](FrameGraph&, FrameGraphBlackboard&, const ParamBlock&)
        {
        }
    });
}
```

---

## 3. Create editor

The editor accepts an optional **Undo interface**.

```cpp
#include <vrendergraph/vrendergraph_editor.hpp>

using namespace vultra::rg;
using namespace vultra::rg::editor;

UndoInterface undo{
    .beginAction = [](){ std::cout << "undo begin\n"; },
    .captureSnapshot = [](){ std::cout << "undo snapshot\n"; },
    .endAction = [](){ std::cout << "undo end\n"; }
};

RenderGraphRegistry registry;
registerPasses(registry);

RenderGraphEditor editor(registry, {.undo = undo});
```

---

## 4. Load / save graphs

```cpp
RenderGraphDesc graph;

// load JSON
{
    std::ifstream f("graph.json");
    if (f.good())
    {
        nlohmann::json j;
        f >> j;

        graph = loadRenderGraph(j);
        editor.load(graph);
    }
}

// save JSON
{
    auto desc = editor.build();

    nlohmann::json j = saveRenderGraph(desc);

    std::ofstream("graph.json") << j.dump(2);
}
```

---

## 5. Draw editor

```cpp
// Before main-loop
ImNodes::CreateContext();

// Main-loop
while(...)
{
    editor.draw();
}

// After main-loop
ImNodes::DestroyContext();
```

---

## 6. Compile graph

When the user clicks **Compile**, convert the editor graph into runtime passes.

```cpp
RenderGraphDesc desc = editor.build();

RenderGraph runtime(registry, importer);

FrameGraph fg;
FrameGraphBlackboard bb;

runtime.build(fg, bb, desc);
```

The resulting `FrameGraph` can then be executed by your renderer.
