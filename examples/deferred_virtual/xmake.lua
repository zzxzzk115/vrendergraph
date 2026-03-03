target("vrendergraph_example_deferred_virtual")
	set_kind("binary")
	add_files("main.cpp")

	add_deps("vrendergraph")

	-- copy scene.json to output dir
	after_build(function (target)
		os.cp("$(scriptdir)/scene.json", path.join(target:targetdir(), "scene.json"))
	end)