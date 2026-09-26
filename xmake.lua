add_rules("mode.debug", "mode.release")

set_project("PortalShapes")
set_version("1.7.1")
set_languages("cxx20")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")

add_requires("preloader_android 0.2.3")

target("PortalShapes")
    set_kind("shared")
    set_basename("PortalShapes")
    set_symbols("hidden")

    add_files("src/main.cpp")
    add_files("src/mod/*.cpp")
    add_files("src/mod/mc/*.cpp")

    add_includedirs("src", {public = true})
    add_includedirs("include", {public = true})
    add_defines("PortalShapes_VERSION=\"1.7.1\"")
    add_packages("preloader_android")

    add_cxxflags(
        "-Oz",
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-semantic-interposition",
        "-fno-ident",
        "-fno-rtti",
        "-fomit-frame-pointer",
        "-fdeclspec",
        "-Wno-unused-parameter",
        {force = true}
    )

    add_ldflags(
        "-Wl,-z,max-page-size=16384",
        "-Wl,--gc-sections",
        "-Wl,--icf=safe",
        "-Wl,--as-needed",
        "-Wl,--strip-all",
        "-Wl,-z,relro",
        "-Wl,-z,now",
        "-Wl,--pack-dyn-relocs=android",
        {force = true}
    )

    add_syslinks("dl", "android", "log")
