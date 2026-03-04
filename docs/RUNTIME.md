# vrendergraph Runtime Integration Example

This document shows how **any renderer or engine** can integrate the **vrendergraph runtime**.

The runtime layer has **no dependency on ImGui or the editor**.  
It simply converts a JSON graph description into a `FrameGraph` execution pipeline.

Typical workflow:

Editor (optional) → JSON → vrendergraph runtime → FrameGraph → Renderer

---

# 1. Include vrendergraph

```cpp
#include <vrendergraph/vrendergraph.hpp>
```

vrendergraph depends only on:

- C++23
- nlohmann::json
- the FrameGraph library used internally

---

# 2. Register render passes

Each application registers the passes that its renderer supports.

```cpp
#include <vrendergraph/vrendergraph.hpp>

using namespace vultra::rg;

static void registerPasses(RenderGraphRegistry& registry)
{
    registry.registerPass({
        .type = "gbuffer",
        .setup = [](FrameGraph& fg,
                    FrameGraphBlackboard& bb,
                    const ParamBlock& params)
        {
            // Add GBuffer pass to FrameGraph
        }
    });

    registry.registerPass({
        .type = "lighting",
        .setup = [](FrameGraph& fg,
                    FrameGraphBlackboard& bb,
                    const ParamBlock& params)
        {
            // Add lighting pass
        }
    });

    registry.registerPass({
        .type = "present",
        .setup = [](FrameGraph& fg,
                    FrameGraphBlackboard& bb,
                    const ParamBlock& params)
        {
            // Present to swapchain
        }
    });
}
```

Each `setup` function translates the abstract graph node into a **FrameGraph pass**.

---

# 3. Load a render graph

Render graphs are stored as JSON files.

```cpp
#include <fstream>
#include <nlohmann/json.hpp>

RenderGraphDesc loadGraph(const std::string& path)
{
    std::ifstream file(path);

    nlohmann::json j;
    file >> j;

    return loadRenderGraph(j);
}
```

---

# 4. Build the runtime graph

To execute the render graph, construct the runtime object and build a FrameGraph.

```cpp
RenderGraphRegistry registry;
registerPasses(registry);

RenderGraph runtime(registry, importer);

FrameGraph fg;
FrameGraphBlackboard blackboard;

RenderGraphDesc desc = loadGraph("pipeline.json");

runtime.build(fg, blackboard, desc);
```

At this stage the `FrameGraph` contains all passes required by the pipeline.

---

# 5. Execute the FrameGraph

The renderer executes the resulting FrameGraph normally.

```cpp
fg.compile();
fg.execute();
```

Actual execution may involve:

- command buffers
- GPU resources
- synchronization
- swapchain presentation

These details are handled by the renderer implementation.

---

# 6. Example JSON pipeline

```json
{
  "passes": [
    { "id": "gbuffer", "type": "gbuffer" },
    {
      "id": "lighting",
      "type": "lighting",
      "inputs": {
        "albedo": "gbuffer.albedo",
        "normal": "gbuffer.normal"
      }
    },
    {
      "id": "present",
      "type": "present",
      "inputs": {
        "color": "lighting.output"
      }
    }
  ]
}
```

The runtime only reads the `passes` array.  
Any `meta` data (such as editor layout) is ignored.

---

# 7. Typical runtime architecture

A typical renderer integration looks like:

```
Renderer
   │
   ├─ registerPasses()
   │
   ├─ load JSON render graph
   │
   ├─ vrendergraph.build()
   │
   └─ execute FrameGraph
```

This allows render pipelines to be **fully data-driven**.
