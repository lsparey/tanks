find_program(GLSLC_EXECUTABLE glslc REQUIRED)

# compile_shaders(<target> <shader-source-files...>)
# Compiles each GLSL source in shaders/ to SPIR-V (same name + .spv) next to
# it, and makes <target> depend on the compiled outputs so shader edits
# trigger recompilation as part of the normal build.
function(compile_shaders TARGET_NAME)
    set(SHADER_OUTPUTS)
    foreach(SHADER_SOURCE ${ARGN})
        set(SHADER_INPUT "${CMAKE_SOURCE_DIR}/shaders/${SHADER_SOURCE}")
        set(SHADER_OUTPUT "${CMAKE_SOURCE_DIR}/shaders/${SHADER_SOURCE}.spv")
        set(SHADER_OPTIONS --target-env=vulkan1.3)
        if(SHADER_SOURCE STREQUAL "foliage_depth.frag")
            # Use core OpKill for discard. SPIR-V 1.6 emits demotion, whose
            # optional Vulkan feature is not required by our renderer.
            list(APPEND SHADER_OPTIONS --target-spv=spv1.5)
        endif()
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_OPTIONS} ${SHADER_INPUT} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_INPUT}
            COMMENT "Compiling shader ${SHADER_SOURCE}"
            VERBATIM
        )
        list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
    endforeach()

    add_custom_target(${TARGET_NAME}_shaders DEPENDS ${SHADER_OUTPUTS})
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_shaders)
endfunction()
