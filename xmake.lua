set_project("RandomMapGen")
set_version("1.1.0")
set_xmakever("3.0.6")

add_rules("mode.debug", "mode.release")
set_languages("c++20")
set_optimize("fastest")

add_requires("nlohmann_json v3.12.0")
add_requires("openssl3 3.5.5")

target("rmg_native")
    set_kind("static")
    set_default(false)
    set_warnings("all", "extra")
    add_packages("nlohmann_json", "openssl3", {public = true})
    add_includedirs("include", "third_party", "third_party/ra2formats/include", {public = true})
    add_files(
        "src/cliff_pieces.cpp",
        "src/ini_config.cpp",
        "src/map_evaluation.cpp",
        "src/map_types.cpp",
        "src/map_writer.cpp",
        "src/native_api.cpp",
        "src/native_game_options.cpp",
        "src/native_generator.cpp",
        "src/native_resource.cpp",
        "src/native_theater_catalog.cpp",
        "src/native_trace.cpp",
        "src/randomizer.cpp",
        "src/shore_pieces.cpp",
        "src/tile_catalog.cpp",
        "third_party/pack_codec/pack_encoding.cpp",
        "third_party/ra2formats/src/ini.cpp",
        "third_party/ra2formats/src/mix.cpp",
        "third_party/ra2formats/src/palette.cpp",
        "third_party/ra2formats/src/tmp.cpp")
    if is_plat("windows") and is_arch("x64") then
        add_rules("asm")
        set_toolset("as", "nasm")
        add_asflags("-f", "win64", {force = true})
        add_defines("RMG_HAS_NATIVE_X87_HELPER")
        add_files("src/native_x87_math.asm")
        add_cxxflags("/utf-8", {tools = {"cl", "clang_cl"}})
    elseif is_plat("linux") and is_arch("x86_64") then
        add_rules("asm")
        add_defines("RMG_HAS_NATIVE_X87_HELPER")
        add_files("src/native_x87_math.S")
    end
target_end()

target("rmg")
    set_kind("binary")
    add_deps("rmg_native")
    add_files("src/main.cpp")
    add_defines('RMG_GENERATOR_VERSION="1.1.0-native"')
    set_rundir(os.scriptdir())
    if is_plat("windows") then
        add_cxxflags("/utf-8", {tools = {"cl", "clang_cl"}})
    end
target_end()

target("rmg_tests")
    set_kind("binary")
    set_default(false)
    add_deps("rmg_native")
    add_files("tests/selftest.cpp")
    if is_plat("windows") then
        add_cxxflags("/utf-8", {tools = {"cl", "clang_cl"}})
    end
target_end()
