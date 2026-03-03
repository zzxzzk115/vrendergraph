#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

#include <nlohmann/json.hpp>
#include <vrendergraph/vrendergraph.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

//
// ----------------------------------------------------------------------------
// Virtual FrameGraph resource (from FrameGraph tests)
// ----------------------------------------------------------------------------
//

struct FrameGraphTexture
{
    struct Desc
    {
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    FrameGraphTexture() = default;
    explicit FrameGraphTexture(int32_t aId) : id(aId) {}
    FrameGraphTexture(FrameGraphTexture&&) noexcept = default;

    void create(const Desc&, void*) { id = ++s_LastId; }
    void destroy(const Desc&, void*) {}

    void preRead(const Desc&, uint32_t, void*) const {}

    static const char* toString(const Desc&) { return "<I>texture</I>"; }

    int32_t               id       = -1;
    inline static int32_t s_LastId = 0;
};

//
// ----------------------------------------------------------------------------
// Resource data structures (libvultra-style blackboard resources)
// ----------------------------------------------------------------------------
//

struct GBufferData
{
    FrameGraphResource albedo;
    FrameGraphResource normal;
    FrameGraphResource depth;
};

struct SceneColorData
{
    FrameGraphResource hdr;
};

//
// ----------------------------------------------------------------------------
// Pass 1 : GBufferPass (OOP style)
// ----------------------------------------------------------------------------
//

class GBufferPass
{
public:
    static void addPass(FrameGraph& fg, FrameGraphBlackboard& bb)
    {
        const auto& pass = fg.addCallbackPass<GBufferData>(
            "GBufferPass",
            [](FrameGraph::Builder& builder, GBufferData& data) {
                data.albedo = builder.create<FrameGraphTexture>("GBuffer.Albedo", {1280, 720});
                data.albedo = builder.write(data.albedo);

                data.normal = builder.create<FrameGraphTexture>("GBuffer.Normal", {1280, 720});
                data.normal = builder.write(data.normal);

                data.depth = builder.create<FrameGraphTexture>("GBuffer.Depth", {1280, 720});
                data.depth = builder.write(data.depth);
            },
            [](const GBufferData& data, FrameGraphPassResources& res, void*) {
                std::cout << "[GBuffer] albedo=" << res.get<FrameGraphTexture>(data.albedo).id
                          << " normal=" << res.get<FrameGraphTexture>(data.normal).id
                          << " depth=" << res.get<FrameGraphTexture>(data.depth).id << "\n";
            });

        if (bb.has<GBufferData>())
            bb.get<GBufferData>() = pass;
        else
            bb.add<GBufferData>() = pass;
    }

    static void registerSelf(vrendergraph::RenderGraphRegistry& registry, GBufferPass& self)
    {
        using namespace vrendergraph;

        registry.registerPass(
            {.type  = "gbuffer",
             .setup = [&self](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                 self.addPass(fg, bb);

                 const auto& out = bb.get<GBufferData>();

                 ctx.setOutput("albedo", out.albedo);
                 ctx.setOutput("normal", out.normal);
                 ctx.setOutput("depth", out.depth);
             }});
    }
};

//
// ----------------------------------------------------------------------------
// Pass 2 : LightingPass (OOP style)
// ----------------------------------------------------------------------------
//

class LightingPass
{
public:
    static void addPass(FrameGraph& fg, FrameGraphBlackboard& bb, bool enableIBL)
    {
        const auto& gbuf = bb.get<GBufferData>();

        const auto& pass = fg.addCallbackPass<SceneColorData>(
            "LightingPass",
            [gbuf](FrameGraph::Builder& builder, SceneColorData& data) {
                builder.read(gbuf.albedo);
                builder.read(gbuf.normal);
                builder.read(gbuf.depth);

                data.hdr = builder.create<FrameGraphTexture>("SceneColor.HDR", {1280, 720});
                data.hdr = builder.write(data.hdr);
            },
            [enableIBL](const SceneColorData& data, FrameGraphPassResources& res, void*) {
                std::cout << "[Lighting] hdr=" << res.get<FrameGraphTexture>(data.hdr).id << " IBL=" << enableIBL
                          << "\n";
            });

        if (bb.has<SceneColorData>())
            bb.get<SceneColorData>() = pass;
        else
            bb.add<SceneColorData>() = pass;
    }

    static void registerSelf(vrendergraph::RenderGraphRegistry& registry, LightingPass& self)
    {
        using namespace vrendergraph;

        registry.registerPass(
            {.type = "lighting",
             .setup =
                 [&self](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock& params, PassBuildContext& ctx) {
                     GBufferData gbuf;
                     gbuf.albedo = ctx.getInput("albedo");
                     gbuf.normal = ctx.getInput("normal");
                     gbuf.depth  = ctx.getInput("depth");

                     if (bb.has<GBufferData>())
                         bb.get<GBufferData>() = gbuf;
                     else
                         bb.add<GBufferData>() = gbuf;

                     bool enableIBL = params.get<bool>("enableIBL", true);

                     self.addPass(fg, bb, enableIBL);

                     const auto& out = bb.get<SceneColorData>();
                     ctx.setOutput("hdr", out.hdr);
                 }});
    }
};

class ToneMappingPass
{
public:
    static void addPass(FrameGraph& fg, FrameGraphBlackboard& bb)
    {
        struct ToneMapData
        {
            FrameGraphResource ldr;
        };

        const auto& sc = bb.get<SceneColorData>();

        const auto& pass = fg.addCallbackPass<ToneMapData>(
            "ToneMappingPass",
            [sc](FrameGraph::Builder& builder, ToneMapData& data) {
                builder.read(sc.hdr);

                data.ldr = builder.create<FrameGraphTexture>("SceneColor.LDR", {1280, 720});

                data.ldr = builder.write(data.ldr);
            },
            [](const ToneMapData& data, FrameGraphPassResources& res, void*) {
                std::cout << "[ToneMap] ldr=" << res.get<FrameGraphTexture>(data.ldr).id << "\n";
            });

        SceneColorData out;
        out.hdr = pass.ldr;

        bb.get<SceneColorData>() = out;
    }

    static void registerSelf(vrendergraph::RenderGraphRegistry& registry, ToneMappingPass& self)
    {
        using namespace vrendergraph;

        registry.registerPass(
            {.type  = "tonemap",
             .setup = [&self](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                 SceneColorData sc;
                 sc.hdr = ctx.getInput("hdr");

                 if (bb.has<SceneColorData>())
                     bb.get<SceneColorData>() = sc;
                 else
                     bb.add<SceneColorData>() = sc;

                 self.addPass(fg, bb);

                 ctx.setOutput("ldr", bb.get<SceneColorData>().hdr);
             }});
    }
};

class FXAAPass
{
public:
    static void addPass(FrameGraph& fg, FrameGraphBlackboard& bb)
    {
        struct FXAAData
        {
            FrameGraphResource output;
        };

        const auto& sc = bb.get<SceneColorData>();

        const auto& pass = fg.addCallbackPass<FXAAData>(
            "FXAAPass",
            [sc](FrameGraph::Builder& builder, FXAAData& data) {
                builder.read(sc.hdr);

                data.output = builder.create<FrameGraphTexture>("SceneColor.FXAA", {1280, 720});

                data.output = builder.write(data.output);
            },
            [](const FXAAData& data, FrameGraphPassResources& res, void*) {
                std::cout << "[FXAA] output=" << res.get<FrameGraphTexture>(data.output).id << "\n";
            });

        SceneColorData out;
        out.hdr = pass.output;

        bb.get<SceneColorData>() = out;
    }

    static void registerSelf(vrendergraph::RenderGraphRegistry& registry, FXAAPass& self)
    {
        using namespace vrendergraph;

        registry.registerPass(
            {.type  = "fxaa",
             .setup = [&self](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                 SceneColorData sc;
                 sc.hdr = ctx.getInput("input");

                 if (bb.has<SceneColorData>())
                     bb.get<SceneColorData>() = sc;
                 else
                     bb.add<SceneColorData>() = sc;

                 self.addPass(fg, bb);

                 ctx.setOutput("output", bb.get<SceneColorData>().hdr);
             }});
    }
};

class PresentPass
{
public:
    static void addPass(FrameGraph& fg, FrameGraphBlackboard& bb, FrameGraphResource& backbuffer)
    {
        const auto& sc = bb.get<SceneColorData>();

        fg.addCallbackPass<int>(
            "PresentPass",
            [sc, &backbuffer](FrameGraph::Builder& builder, int&) {
                builder.read(sc.hdr);
                backbuffer = builder.write(backbuffer);
                builder.setSideEffect();
            },
            [](const int&, FrameGraphPassResources&, void*) { std::cout << "[Present]\n"; });
    }

    static void registerSelf(vrendergraph::RenderGraphRegistry& registry, PresentPass& self)
    {
        using namespace vrendergraph;

        registry.registerPass(
            {.type  = "present",
             .setup = [&self](FrameGraph& fg, FrameGraphBlackboard& bb, const ParamBlock&, PassBuildContext& ctx) {
                 SceneColorData sc;
                 sc.hdr = ctx.getInput("color");

                 if (bb.has<SceneColorData>())
                     bb.get<SceneColorData>() = sc;
                 else
                     bb.add<SceneColorData>() = sc;

                 auto backbuffer = ctx.getInput("backbuffer");

                 self.addPass(fg, bb, backbuffer);

                 ctx.setOutput("backbuffer", backbuffer);
             }});
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
        auto importer = [](FrameGraph& fg, std::string_view name, const nlohmann::json& rdesc) -> FrameGraphResource {
            if (name == "backbuffer")
            {
                FrameGraphTexture::Desc d;
                d.width  = rdesc.value("width", 1280);
                d.height = rdesc.value("height", 720);
                int id   = rdesc.value("backbuffer_id", 777);

                return fg.import(std::string(name), d, FrameGraphTexture {id});
            }

            throw std::runtime_error("Unknown imported resource: " + std::string(name));
        };

        // Register passes
        vrendergraph::RenderGraphRegistry registry;

        GBufferPass     gbuffer;
        LightingPass    lighting;
        ToneMappingPass tonemap;
        FXAAPass        fxaa;
        PresentPass     present;

        GBufferPass::registerSelf(registry, gbuffer);
        LightingPass::registerSelf(registry, lighting);
        ToneMappingPass::registerSelf(registry, tonemap);
        FXAAPass::registerSelf(registry, fxaa);
        PresentPass::registerSelf(registry, present);

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