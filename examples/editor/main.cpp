#include <GLFW/glfw3.h>

#include <glad/glad.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <imgui_node_editor/imgui_node_editor.h>

#define VRENDERGRAPH_EDITOR_IMPLEMENTATION
#include <vrendergraph_editor.hpp>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <vrendergraph/vrendergraph.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace ed = ax::NodeEditor;
using namespace vrendergraph;

// -----------------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------------

static nlohmann::json loadJsonFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
        throw std::runtime_error("Failed to open json: " + path);

    nlohmann::json j;
    ifs >> j;
    return j;
}

// -----------------------------------------------------------------------------
// Fake FrameGraph resource
// -----------------------------------------------------------------------------

struct FakeTexture
{
    struct Desc
    {
        uint32_t width  = 1280;
        uint32_t height = 720;
    };

    void create(const Desc&, void*) {}
    void destroy(const Desc&, void*) {}

    static const char* toString(const Desc&) { return "FakeTexture"; }
};

// -----------------------------------------------------------------------------
// Blackboard resources
// -----------------------------------------------------------------------------

struct DepthData
{
    FrameGraphResource depth;
};

struct GBufferData
{
    FrameGraphResource depth;
    FrameGraphResource gbuffer;
};

struct SceneColorData
{
    FrameGraphResource hdr;
};

struct BackbufferData
{
    FrameGraphResource backbuffer;
};

// -----------------------------------------------------------------------------
// DepthPrePass
// outputs: depth
// -----------------------------------------------------------------------------

class DepthPrePass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "depth_pre",
            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                    const auto& pass = fg.addCallbackPass<DepthData>(
                        "DepthPrePass",
                        [](FrameGraph::Builder& builder, DepthData& data) {
                            data.depth = builder.create<FakeTexture>("DepthBuffer", {1280, 720});
                            data.depth = builder.write(data.depth);
                        },
                        [](const DepthData&, FrameGraphPassResources&, void*) { std::cout << "[DepthPrePass]\n"; });

                    if (bb.has<DepthData>())
                        bb.get<DepthData>() = pass;
                    else
                        bb.add<DepthData>() = pass;

                    // --- publish output pin for editor/json ---
                    ctx.setOutput("depth", pass.depth);
                },
            .outputs = {"depth"},
        });
    }
};

// -----------------------------------------------------------------------------
// GBufferPass
// inputs : depth
// outputs: gbuffer, depth (re-export for convenience)
// -----------------------------------------------------------------------------

class GBufferPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "gbuffer",
            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                    // pull input from graph connection
                    DepthData depth {};
                    depth.depth = ctx.getInput("depth");

                    if (bb.has<DepthData>())
                        bb.get<DepthData>() = depth;
                    else
                        bb.add<DepthData>() = depth;

                    const auto& pass = fg.addCallbackPass<GBufferData>(
                        "GBufferPass",
                        [depth](FrameGraph::Builder& builder, GBufferData& data) {
                            data.depth = builder.read(depth.depth);

                            data.gbuffer = builder.create<FakeTexture>("GBuffer", {1280, 720});
                            data.gbuffer = builder.write(data.gbuffer);

                            // re-export depth handle (common pattern)
                            data.depth = builder.write(builder.read(data.depth));
                        },
                        [](const GBufferData&, FrameGraphPassResources&, void*) { std::cout << "[GBufferPass]\n"; });

                    if (bb.has<GBufferData>())
                        bb.get<GBufferData>() = pass;
                    else
                        bb.add<GBufferData>() = pass;

                    ctx.setOutput("gbuffer", pass.gbuffer);
                    ctx.setOutput("depth", pass.depth);
                },
            .inputs  = {"depth"},
            .outputs = {"gbuffer", "depth"},
        });
    }
};

// -----------------------------------------------------------------------------
// LightingPass
// inputs : gbuffer
// outputs: hdr
// -----------------------------------------------------------------------------

class LightingPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "lighting",
            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                    GBufferData gbuf {};
                    gbuf.gbuffer = ctx.getInput("gbuffer");

                    if (bb.has<GBufferData>())
                        bb.get<GBufferData>() = gbuf;
                    else
                        bb.add<GBufferData>() = gbuf;

                    const auto& pass = fg.addCallbackPass<SceneColorData>(
                        "LightingPass",
                        [gbuf](FrameGraph::Builder& builder, SceneColorData& data) {
                            builder.read(gbuf.gbuffer);

                            data.hdr = builder.create<FakeTexture>("SceneHDR", {1280, 720});
                            data.hdr = builder.write(data.hdr);
                        },
                        [](const SceneColorData&, FrameGraphPassResources&, void*) {
                            std::cout << "[LightingPass]\n";
                        });

                    if (bb.has<SceneColorData>())
                        bb.get<SceneColorData>() = pass;
                    else
                        bb.add<SceneColorData>() = pass;

                    ctx.setOutput("hdr", pass.hdr);
                },
            .inputs  = {"gbuffer"},
            .outputs = {"hdr"},
        });
    }
};

// -----------------------------------------------------------------------------
// ToneMappingPass
// inputs : hdr
// outputs: ldr
// -----------------------------------------------------------------------------

class ToneMappingPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "tonemap",
            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                    SceneColorData sc {};
                    sc.hdr = ctx.getInput("hdr");

                    if (bb.has<SceneColorData>())
                        bb.get<SceneColorData>() = sc;
                    else
                        bb.add<SceneColorData>() = sc;

                    struct Data
                    {
                        FrameGraphResource ldr;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "ToneMappingPass",
                        [sc](FrameGraph::Builder& builder, Data& data) {
                            builder.read(sc.hdr);

                            data.ldr = builder.create<FakeTexture>("SceneLDR", {1280, 720});
                            data.ldr = builder.write(data.ldr);
                        },
                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[ToneMappingPass]\n"; });

                    // publish output
                    ctx.setOutput("ldr", pass.ldr);
                },
            .inputs  = {"hdr"},
            .outputs = {"ldr"},
        });
    }
};

// -----------------------------------------------------------------------------
// PresentPass
// inputs : color, backbuffer
// outputs: backbuffer
// side-effect pass
// -----------------------------------------------------------------------------

class PresentPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "present",
            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                    const auto color      = ctx.getInput("color");
                    auto       backbuffer = ctx.getInput("backbuffer");

                    // keep in blackboard (optional)
                    BackbufferData bbData {backbuffer};
                    if (bb.has<BackbufferData>())
                        bb.get<BackbufferData>() = bbData;
                    else
                        bb.add<BackbufferData>() = bbData;

                    struct Data
                    {
                        FrameGraphResource backbuffer;
                        FrameGraphResource color;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "PresentPass",
                        [color, backbuffer](FrameGraph::Builder& builder, Data& data) {
                            data.color = builder.read(color);

                            // In a real renderer, this is "write backbuffer as RT"
                            data.backbuffer = builder.write(backbuffer);
                            builder.setSideEffect();
                        },
                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[PresentPass]\n"; });

                    ctx.setOutput("backbuffer", pass.backbuffer);
                },
            .inputs  = {"color", "backbuffer"},
            .outputs = {"backbuffer"},
        });
    }
};

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

static const char* choose_imgui_glsl_version() { return "#version 330"; }

static void setup_glfw_hints()
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
}

int main()
{
    if (!glfwInit())
        return -1;

    setup_glfw_hints();

    GLFWwindow* window = glfwCreateWindow(1280, 720, "vrendergraph editor", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        std::cout << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(choose_imgui_glsl_version());

    ed::Config edConfig;
    auto*      edCtx = ed::CreateEditor(&edConfig);

    // ---- Register passes ----
    RenderGraphRegistry registry;
    DepthPrePass::registerSelf(registry);
    GBufferPass::registerSelf(registry);
    LightingPass::registerSelf(registry);
    ToneMappingPass::registerSelf(registry);
    PresentPass::registerSelf(registry);

    // ---- Load scene.json (if missing -> empty graph) ----
    RenderGraphDesc desc;
    try
    {
        auto j = loadJsonFile("scene.json");
        desc   = vrendergraph::loadRenderGraph(j);
    }
    catch (const std::exception& e)
    {
        std::cout << "[warn] scene.json not loaded: " << e.what() << "\n";
        desc = {};
    }

    editor::RenderGraphEditor editor(registry);
    editor.setGraph(&desc);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ed::SetCurrentEditor(edCtx);
        editor.draw();
        ed::SetCurrentEditor(nullptr);

        ImGui::Begin("Runtime");

        if (ImGui::Button("Run Graph"))
        {
            std::cout << "\n--- Execute RenderGraph ---\n";

            FrameGraph           fg;
            FrameGraphBlackboard bb;

            // Importer: supports @backbuffer
            auto importer =
                [](FrameGraph& fg, std::string_view name, const nlohmann::json& rdesc) -> FrameGraphResource {
                if (name == "@backbuffer" || name == "backbuffer")
                {
                    FakeTexture::Desc d;
                    d.width  = rdesc.value("width", 1280u);
                    d.height = rdesc.value("height", 720u);

                    // We don't really use id in FakeTexture, so just construct default.
                    return fg.import(std::string(name), d, FakeTexture {});
                }
                throw std::runtime_error(std::string("Unknown imported resource: ") + std::string(name));
            };

            RenderGraph rg(registry, importer);

            try
            {
                rg.build(fg, bb, *editor.graph());
                fg.compile();

                std::ofstream("rendergraph.dot") << fg;
                fg.execute();
            }
            catch (const std::exception& e)
            {
                std::cout << "[error] build/execute failed: " << e.what() << "\n";
            }

            std::cout << "---------------------------\n";
        }

        if (ImGui::Button("Save scene.json"))
        {
            try
            {
                // flush meta every frame already happens in editor.draw(), but call once for clarity
                editor.flushMeta();

                nlohmann::json out = vrendergraph::saveRenderGraph(desc);
                std::ofstream("scene.json") << out.dump(2);
                std::cout << "[info] wrote scene.json\n";
            }
            catch (const std::exception& e)
            {
                std::cout << "[error] save failed: " << e.what() << "\n";
            }
        }

        ImGui::End();

        ImGui::Render();

        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);

        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ed::DestroyEditor(edCtx);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}