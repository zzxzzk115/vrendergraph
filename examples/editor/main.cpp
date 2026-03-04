#include <GLFW/glfw3.h>

#include <glad/glad.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// --- imnodes ---
#include <imnodes/imnodes.h>

#define VRENDERGRAPH_EDITOR_IMPLEMENTATION
#include <vrendergraph_editor.hpp>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <vrendergraph/vrendergraph.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
    FrameGraphResource gPosition;
    FrameGraphResource gNormal;
    FrameGraphResource gAlbedo;
};

struct SSAOData
{
    FrameGraphResource ao;
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
            .type = "Depth_Pre",
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
// outputs: gPosition, gNormal, gAlbedo
// -----------------------------------------------------------------------------

class GBufferPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "GBuffer",
            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
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

                            data.gPosition = builder.create<FakeTexture>("GPosition", {1280, 720});
                            data.gNormal   = builder.create<FakeTexture>("GNormal", {1280, 720});
                            data.gAlbedo   = builder.create<FakeTexture>("GAlbedo", {1280, 720});

                            data.gPosition = builder.write(data.gPosition);
                            data.gNormal   = builder.write(data.gNormal);
                            data.gAlbedo   = builder.write(data.gAlbedo);
                        },

                        [](const GBufferData&, FrameGraphPassResources&, void*) { std::cout << "[GBufferPass]\n"; });

                    if (bb.has<GBufferData>())
                        bb.get<GBufferData>() = pass;
                    else
                        bb.add<GBufferData>() = pass;

                    ctx.setOutput("gPosition", pass.gPosition);
                    ctx.setOutput("gNormal", pass.gNormal);
                    ctx.setOutput("gAlbedo", pass.gAlbedo);
                },

            .inputs  = {"depth"},
            .outputs = {"gPosition", "gNormal", "gAlbedo"},
        });
    }
};

// -----------------------------------------------------------------------------
// SSAOPass
// inputs : gPosition, gNormal
// outputs: ao
// -----------------------------------------------------------------------------
class SSAOPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "SSAO",

            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                    auto gpos  = ctx.getInput("gPosition");
                    auto gnorm = ctx.getInput("gNormal");

                    const auto& pass = fg.addCallbackPass<SSAOData>(
                        "SSAO",

                        [gpos, gnorm](FrameGraph::Builder& builder, SSAOData& data) {
                            builder.read(gpos);
                            builder.read(gnorm);

                            data.ao = builder.create<FakeTexture>("SSAO", {1280, 720});
                            data.ao = builder.write(data.ao);
                        },

                        [](const SSAOData&, FrameGraphPassResources&, void*) { std::cout << "[SSAO]\n"; });

                    if (bb.has<SSAOData>())
                        bb.get<SSAOData>() = pass;
                    else
                        bb.add<SSAOData>() = pass;

                    ctx.setOutput("ao", pass.ao);
                },

            .inputs  = {"gPosition", "gNormal"},
            .outputs = {"ao"},
        });
    }
};

// -----------------------------------------------------------------------------
// LightingPass
// inputs : gPosition, gNormal, gAlbedo, ao
// outputs: hdr
// -----------------------------------------------------------------------------

class LightingPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({

            .type = "Lighting",

            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard&, const ParamBlock&, PassBuildContext& ctx) {
                    auto gpos   = ctx.getInput("gPosition");
                    auto gnorm  = ctx.getInput("gNormal");
                    auto albedo = ctx.getInput("gAlbedo");
                    auto ao     = ctx.getInput("ao");

                    struct Data
                    {
                        FrameGraphResource hdr;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "LightingPass",

                        [gpos, gnorm, albedo, ao](FrameGraph::Builder& builder, Data& data) {
                            builder.read(gpos);
                            builder.read(gnorm);
                            builder.read(albedo);
                            builder.read(ao);

                            data.hdr = builder.create<FakeTexture>("SceneHDR", {1280, 720});
                            data.hdr = builder.write(data.hdr);
                        },

                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[LightingPass]\n"; });

                    ctx.setOutput("hdr", pass.hdr);
                },

            .inputs  = {"gPosition", "gNormal", "gAlbedo", "ao"},
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
            .type = "Tonemap",
            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock& paramBlock, PassBuildContext& ctx) {
                    auto exposure = paramBlock.get<float>("exposure", 1.0f);
                    auto aces     = paramBlock.get<bool>("aces", false);
                    std::cout << "ToneMappingPass param: exposure = " << exposure
                              << ", aces = " << (aces ? "true" : "false") << "\n";

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
                        [sc, exposure](FrameGraph::Builder& builder, Data& data) {
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
            .params =
                {
                    {
                        .name         = "exposure",
                        .type         = ParamType::eFloat,
                        .defaultValue = 1.0f,
                        .minValue     = 0.0f,
                        .maxValue     = 5.0f,
                    },
                    {.name = "aces", .type = ParamType::eBoolean, .defaultValue = false},
                },
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
            .type = "Present",
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

    // ---- imnodes init (REQUIRED) ----
    ImNodes::CreateContext();
    ImNodes::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(choose_imgui_glsl_version());

    // ---- Register passes ----
    RenderGraphRegistry registry;
    DepthPrePass::registerSelf(registry);
    GBufferPass::registerSelf(registry);
    SSAOPass::registerSelf(registry);
    LightingPass::registerSelf(registry);
    ToneMappingPass::registerSelf(registry);
    PresentPass::registerSelf(registry);
    registry.registerResource("@backbuffer");

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

        editor.draw();

        ImGui::Begin("Runtime");

        if (ImGui::Button("Run Graph"))
        {
            std::cout << "\n--- Execute RenderGraph ---\n";

            FrameGraph           fg;
            FrameGraphBlackboard bb;

            using ProviderFn = std::function<FrameGraphResource(FrameGraph&)>;

            std::unordered_map<std::string, ProviderFn> providers;

            auto updateExternalProviders = [&]() {
                providers["@backbuffer"] = [&](FrameGraph& fg) -> FrameGraphResource {
                    FakeTexture::Desc d {1280, 720};
                    return fg.import("@backbuffer", d, FakeTexture {});
                };
            };

            auto importer = [&](FrameGraph& fg, std::string_view name) -> FrameGraphResource {
                auto it = providers.find(std::string(name));
                if (it == providers.end())
                    throw std::runtime_error("Unknown imported resource: " + std::string(name));
                return it->second(fg);
            };

            updateExternalProviders();

            RenderGraph rg(registry, importer);

            try
            {
                // Calculate the time to build + execute the graph, which includes running all setup() functions and
                // callbacks.

                // Unit: microseconds (us)
                auto start = std::chrono::high_resolution_clock::now();

                rg.build(fg, bb, *editor.graph());
                fg.compile();

#ifndef NDEBUG
                std::ofstream("rendergraph.dot") << fg;
#endif

                fg.execute();

                auto end  = std::chrono::high_resolution_clock::now();
                auto diff = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                std::cout << "RenderGraph execution time: " << diff << " us\n";
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    // ---- imnodes shutdown (REQUIRED) ----
    ImNodes::DestroyContext();

    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}