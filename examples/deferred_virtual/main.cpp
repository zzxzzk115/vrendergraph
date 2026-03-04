#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <nlohmann/json.hpp>
#include <vrendergraph/vrendergraph.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace vrendergraph;

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
// outputs: gbuffer, depth (re-export for convenience)
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
    static void registerSelf(vrendergraph::RenderGraphRegistry& registry)
    {
        registry.registerPass({
            .type = "Lighting",
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
            .type = "Tonemap",
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

//
// ----------------------------------------------------------------------------
// Utility
// ----------------------------------------------------------------------------
//

static nlohmann::json loadJsonFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
        throw std::runtime_error("Failed to open json: " + path);

    nlohmann::json j;
    ifs >> j;
    return j;
}

//
// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
//

int main(int argc, char** argv)
{
    try
    {
        const std::string scenePath = (argc >= 2) ? argv[1] : "scene.json";

        auto j    = loadJsonFile(scenePath);
        auto desc = vrendergraph::loadRenderGraph(j);

        FrameGraph           fg;
        FrameGraphBlackboard bb;

        // Importer
        auto importer = [](FrameGraph& fg, std::string_view name) -> FrameGraphResource {
            if (name == "backbuffer")
            {
                FakeTexture::Desc d;
                return fg.import(std::string(name), d, FakeTexture {});
            }

            throw std::runtime_error("Unknown imported resource: " + std::string(name));
        };

        // Register passes
        vrendergraph::RenderGraphRegistry registry;

        DepthPrePass::registerSelf(registry);
        GBufferPass::registerSelf(registry);
        LightingPass::registerSelf(registry);
        ToneMappingPass::registerSelf(registry);
        PresentPass::registerSelf(registry);

        // Build graph
        vrendergraph::RenderGraph rg(registry, importer);
        rg.build(fg, bb, desc);

        fg.compile();

        {
            std::ofstream ofs("rendergraph.dot");
            ofs << fg;
        }

        fg.execute();

        std::cout << "Done.\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}