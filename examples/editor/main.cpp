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

struct FrameInfo
{
    uint32_t width;
    uint32_t height;
};

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

class DepthPrePass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "Depth_Pre",

            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                    const auto& frame = bb.get<FrameInfo>();

                    FakeTexture::Desc desc;
                    desc.width  = frame.width;
                    desc.height = frame.height;
                    std::cout << "DepthPrePass: " << desc.width << "x" << desc.height << "\n";

                    struct Data
                    {
                        FrameGraphResource depth;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "DepthPre",

                        [desc](FrameGraph::Builder& builder, Data& data) {
                            data.depth = builder.create<FakeTexture>("Depth", desc);
                            data.depth = builder.write(data.depth);
                        },

                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[Depth]\n"; });

                    ctx.setOutput("depth", pass.depth);
                },

            .outputs = {"depth"},
        });
    }
};

class GBufferPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "GBuffer",

            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock& pb, PassBuildContext& ctx) {
                    auto depth = ctx.getInput("depth");

                    const auto& frame = bb.get<FrameInfo>();

                    FakeTexture::Desc desc;
                    desc.width  = frame.width;
                    desc.height = frame.height;

                    bool writeVelocity = pb.get<bool>("velocity", true);
                    std::cout << "GBuffer write velocity: " << writeVelocity << "\n";

                    struct Data
                    {
                        FrameGraphResource depth;
                        FrameGraphResource gpos;
                        FrameGraphResource gnorm;
                        FrameGraphResource galbedo;
                        FrameGraphResource velocity;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "GBuffer",

                        [depth, desc, writeVelocity](FrameGraph::Builder& builder, Data& data) {
                            data.depth = builder.read(depth);

                            data.gpos    = builder.create<FakeTexture>("gPosition", desc);
                            data.gnorm   = builder.create<FakeTexture>("gNormal", desc);
                            data.galbedo = builder.create<FakeTexture>("gAlbedo", desc);

                            data.gpos    = builder.write(data.gpos);
                            data.gnorm   = builder.write(data.gnorm);
                            data.galbedo = builder.write(data.galbedo);

                            if (writeVelocity)
                            {
                                data.velocity = builder.create<FakeTexture>("velocity", desc);
                                data.velocity = builder.write(data.velocity);
                            }
                        },

                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[GBuffer]\n"; });

                    ctx.setOutput("gPosition", pass.gpos);
                    ctx.setOutput("gNormal", pass.gnorm);
                    ctx.setOutput("gAlbedo", pass.galbedo);
                },

            .inputs  = {"depth"},
            .outputs = {"gPosition", "gNormal", "gAlbedo"},

            .params =
                {
                    {
                        .name         = "velocity",
                        .type         = ParamType::eBoolean,
                        .defaultValue = true,
                    },
                },
        });
    }
};

class SSAOPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "SSAO",

            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock& pb, PassBuildContext& ctx) {
                    auto gpos  = ctx.getInput("gPosition");
                    auto gnorm = ctx.getInput("gNormal");
                    auto depth = ctx.getInput("depth");

                    const auto& frame = bb.get<FrameInfo>();

                    FakeTexture::Desc desc;
                    desc.width  = frame.width;
                    desc.height = frame.height;

                    float radius = pb.get<float>("radius", 0.5f);
                    std::cout << "SSAO radius: " << radius << "\n";

                    struct Data
                    {
                        FrameGraphResource ao;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "SSAO",

                        [gpos, gnorm, depth, desc](FrameGraph::Builder& builder, Data& data) {
                            builder.read(gpos);
                            builder.read(gnorm);
                            builder.read(depth);

                            data.ao = builder.create<FakeTexture>("ao", desc);
                            data.ao = builder.write(data.ao);
                        },

                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[SSAO]\n"; });

                    ctx.setOutput("ao", pass.ao);
                },

            .inputs  = {"gPosition", "gNormal", "depth"},
            .outputs = {"ao"},

            .params =
                {
                    {
                        .name         = "radius",
                        .type         = ParamType::eFloat,
                        .defaultValue = 0.5f,
                        .minValue     = 0.0f,
                        .maxValue     = 1.0f,
                    },
                },
        });
    }
};

class LightingPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "Lighting",

            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock& pb, PassBuildContext& ctx) {
                    auto gpos  = ctx.getInput("gPosition");
                    auto gnorm = ctx.getInput("gNormal");
                    auto galb  = ctx.getInput("gAlbedo");
                    auto ao    = ctx.getInput("ao");

                    const auto& frame = bb.get<FrameInfo>();

                    FakeTexture::Desc desc;
                    desc.width  = frame.width;
                    desc.height = frame.height;

                    float ambient = pb.get<float>("ambient", 0.05f);
                    std::cout << "Lighting ambient: " << ambient << "\n";

                    struct Data
                    {
                        FrameGraphResource hdr;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "Lighting",

                        [gpos, gnorm, galb, ao, desc](FrameGraph::Builder& builder, Data& data) {
                            builder.read(gpos);
                            builder.read(gnorm);
                            builder.read(galb);
                            builder.read(ao);

                            data.hdr = builder.create<FakeTexture>("hdr", desc);
                            data.hdr = builder.write(data.hdr);
                        },

                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[Lighting]\n"; });

                    ctx.setOutput("hdr", pass.hdr);
                },

            .inputs  = {"gPosition", "gNormal", "gAlbedo", "ao"},
            .outputs = {"hdr"},

            .params =
                {
                    {.name         = "ambient",
                     .type         = ParamType::eFloat,
                     .defaultValue = 0.05f,
                     .minValue     = 0.0f,
                     .maxValue     = 1.0f},
                },
        });
    }
};

class TonemapPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "Tonemap",

            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock& pb, PassBuildContext& ctx) {
                    auto hdr = ctx.getInput("hdr");

                    const auto& frame = bb.get<FrameInfo>();

                    FakeTexture::Desc desc;
                    desc.width  = frame.width;
                    desc.height = frame.height;

                    float exposure = pb.get<float>("exposure", 1.0f);
                    std::cout << "Tonemap exposure: " << exposure << "\n";

                    struct Data
                    {
                        FrameGraphResource ldr;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "Tonemap",

                        [hdr, desc](FrameGraph::Builder& builder, Data& data) {
                            builder.read(hdr);

                            data.ldr = builder.create<FakeTexture>("ldr", desc);
                            data.ldr = builder.write(data.ldr);
                        },

                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[Tonemap]\n"; });

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
                        .maxValue     = 10.0f,
                    },
                },
        });
    }
};

class PresentPass
{
public:
    static void registerSelf(RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "Present",

            .setup =
                [](FrameGraph& fg, FrameGraphBlackboard&, const ParamBlock&, PassBuildContext& ctx) {
                    auto color = ctx.getInput("color");
                    auto back  = ctx.getInput("backbuffer");

                    struct Data
                    {
                        FrameGraphResource back;
                        FrameGraphResource color;
                    };

                    const auto& pass = fg.addCallbackPass<Data>(
                        "Present",

                        [color, back](FrameGraph::Builder& builder, Data& data) {
                            data.color = builder.read(color);
                            data.back  = builder.write(back);
                            builder.setSideEffect();
                        },

                        [](const Data&, FrameGraphPassResources&, void*) { std::cout << "[Present]\n"; });

                    // ctx.setOutput("backbuffer", pass.back);
                },

            .inputs = {"color", "backbuffer"},
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
    TonemapPass::registerSelf(registry);
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

            FrameInfo frame {1280, 720};
            bb.add<FrameInfo>() = frame;

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