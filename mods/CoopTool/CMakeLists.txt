# CoopTool (a mod built with the game into build/port/bin/mods/CoopTool; mod kit: modsdk/).
add_library(CoopTool SHARED cooptool.cpp)
target_compile_features(CoopTool PRIVATE cxx_std_20)
target_include_directories(CoopTool PRIVATE "${CMAKE_SOURCE_DIR}/modsdk/include")
set_target_properties(CoopTool PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${URBAN_MODS_OUTPUT_DIR}/CoopTool"
                                          LIBRARY_OUTPUT_DIRECTORY "${URBAN_MODS_OUTPUT_DIR}/CoopTool")
if(NOT WIN32)
    # CoopTool.so: only urban_mod_init exported (URBAN_MOD_EXPORT), its own code bound inside (urban exports its
    # symbols with -rdynamic).
    set_target_properties(CoopTool PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_link_options(CoopTool PRIVATE -Wl,-Bsymbolic)
endif()
add_custom_command(TARGET CoopTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/mod.json" "$<TARGET_FILE_DIR:CoopTool>/mod.json")
