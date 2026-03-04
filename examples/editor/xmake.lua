add_requires("imgui", {configs = { opengl3 = true, glfw = true}})
add_requires("glad", {configs = { api = "gl=3.3"}})
add_requires("glfw")

target("imgui-ext")
    set_kind("static")
    add_headerfiles("imgui-ext/**.h")
    add_files("imgui-ext/**.cpp")
    add_includedirs("imgui-ext", {public = true}) -- public: let other targets to auto include
    add_packages("imgui", {public = true})
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

target("vrendergraph_example_editor")
	set_kind("binary")
	add_files("main.cpp")

	add_deps("vrendergraph", "vrendergraph_editor", "imgui-ext")
    add_packages("glad", "glfw")

    -- copy scene.json to output dir
	after_build(function (target)
		os.cp("$(scriptdir)/scene.json", path.join(target:targetdir(), "scene.json"))
        os.cp("$(scriptdir)/imgui.ini", path.join(target:targetdir(), "imgui.ini"))
	end)

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vrendergraph_example_editor")