# Shared compile settings for every first-party target.
#
# Determinism (see docs/design/00-foundations.md): floating point must never be
# compiled in "fast" modes, because those let the compiler reorder arithmetic.
function(hsim_set_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /fp:precise /utf-8 /Zc:__cplusplus)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
    endif()
endfunction()
