add_requires("nlohmann_json", "fg")

target("vrendergraph")
    -- set target kind: static library
    set_kind("static")

    -- add include dir
    add_includedirs("include", {public = true}) -- public: let other targets to auto include

    -- add header files
    add_headerfiles("include/(vrendergraph/**.hpp)")

    -- add source files
    add_files("src/**.cpp")

    add_packages("nlohmann_json", "fg", {public = true})