set_project("luajit_ui")

add_rules("mode.debug", "mode.release")

add_requires("luajit", { configs = { shared = false } })

set_languages("c++17")
set_arch("x64")

local root = os.scriptdir()

target("luajit_ui")
    set_kind("binary")
    add_packages("luajit")

    add_files(
        "src/**.cpp",
        path.join(root, "vendor/ImGuiColorTextEdit/TextEditor.cpp"),
        path.join(root, "vendor/imgui/imgui.cpp"),
        path.join(root, "vendor/imgui/imgui_draw.cpp"),
        path.join(root, "vendor/imgui/imgui_tables.cpp"),
        path.join(root, "vendor/imgui/imgui_widgets.cpp"),
        path.join(root, "vendor/imgui/backends/imgui_impl_win32.cpp"),
        path.join(root, "vendor/imgui/backends/imgui_impl_dx9.cpp")
    )

    add_includedirs(
        "src",
        path.join(root, "vendor/imgui"),
        path.join(root, "vendor/imgui/backends"),
        path.join(root, "vendor/ImGuiColorTextEdit")
    )

    add_defines("UNICODE", "_UNICODE")

    if is_plat("windows") then
        add_defines("_WIN32_WINNT=0x0601", "NOMINMAX")
        add_syslinks("d3d9", "imm32", "comdlg32", "shell32", "dwmapi")
        add_cxflags("/utf-8", { force = true })
        add_ldflags("/SUBSYSTEM:WINDOWS", { force = true })
    end

    if is_mode("debug") then
        set_symbols("debug")
        set_optimize("none")
        set_runtimes("MDd")
    else
        set_symbols("none")
        set_optimize("fastest")
        set_runtimes("MD")
    end

    set_targetdir(path.join(root, "bin/$(mode)-windows-$(arch)"))
    set_objectdir(path.join(root, "bin-int/$(mode)-windows-$(arch)/luajit_ui"))
