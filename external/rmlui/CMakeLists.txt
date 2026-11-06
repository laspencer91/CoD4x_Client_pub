# Frozen source list: additions arrive only through a reviewed vendor update.
file(GLOB_RECURSE RMLUI_CORE_SOURCES CONFIGURE_DEPENDS "vendor/Source/Core/*.cpp")
add_library(rmlui_core STATIC ${RMLUI_CORE_SOURCES})
target_include_directories(rmlui_core PUBLIC vendor/Include)
target_compile_features(rmlui_core PUBLIC cxx_std_17)
target_compile_definitions(rmlui_core PUBLIC RMLUI_STATIC_LIB PRIVATE
    RMLUI_FONT_ENGINE_FREETYPE RMLUI_VERSION="6.3")
target_compile_options(rmlui_core PRIVATE -m32 -O2)
target_link_libraries(rmlui_core PUBLIC freetype)

file(GLOB RMLUI_DEBUGGER_SOURCES CONFIGURE_DEPENDS "vendor/Source/Debugger/*.cpp")
add_library(rmlui_debugger STATIC ${RMLUI_DEBUGGER_SOURCES})
target_compile_options(rmlui_debugger PRIVATE -m32 -O2)
target_link_libraries(rmlui_debugger PUBLIC rmlui_core)
