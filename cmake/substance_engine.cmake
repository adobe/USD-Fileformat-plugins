set(SUBSTANCE_TARGETS)
list(APPEND SUBSTANCE_TARGETS Substance::Framework)
list(APPEND SUBSTANCE_TARGETS Substance::Linker)

# Find the package first so we can check which blend libraries are actually
# present in the SDK (e.g. universal packages ship cpu_blend instead of
# neon_blend/sse2_blend).
if(NOT TARGET Substance::Framework)
    find_package(substance CONFIG REQUIRED)
endif()

if(WIN32)
  list(APPEND SUBSTANCE_TARGETS Substance::sse2_blend)
elseif(LINUX)
  list(APPEND SUBSTANCE_TARGETS Substance::sse2_blend)
elseif(APPLE)
  if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    if(NOT sse2_blend_REL MATCHES "NOTFOUND")
      list(APPEND SUBSTANCE_TARGETS Substance::sse2_blend)
    else()
      list(APPEND SUBSTANCE_TARGETS Substance::cpu_blend)
    endif()
  elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    # Universal SDK packages ship cpu_blend rather than neon_blend.
    if(NOT neon_blend_REL MATCHES "NOTFOUND")
      list(APPEND SUBSTANCE_TARGETS Substance::neon_blend)
    else()
      list(APPEND SUBSTANCE_TARGETS Substance::cpu_blend)
    endif()
  else()
    # Universal (multi-arch) build or unspecified architecture.
    list(APPEND SUBSTANCE_TARGETS Substance::cpu_blend)
  endif()
endif()

if(USDSBSAR_ENABLE_INSTALL)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(
      FILES $<TARGET_FILE:Substance::sse2_blend> $<TARGET_FILE:Substance::ogl3_blend> $<TARGET_FILE:Substance::Linker>
        DESTINATION lib
        COMPONENT Runtime
     )

    if(TARGET Substance::vk_blend AND USDSBSAR_ENABLE_VULKAN)
      install(
        FILES $<TARGET_FILE:Substance::vk_blend
        DESTINATION lib
        COMPONENT Runtime
      )
    endif()
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    install(
        FILES $<TARGET_FILE:Substance::sse2_blend> $<TARGET_FILE:Substance::d3d11_blend> $<TARGET_FILE:Substance::Linker>
        DESTINATION bin
        COMPONENT Runtime
    )
    if(TARGET Substance::vk_blend AND USDSBSAR_ENABLE_VULKAN)
      install(
        FILES $<TARGET_FILE:Substance::vk_blend
        DESTINATION bin
        COMPONENT Runtime
      )
    endif()
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
      install(
        FILES $<TARGET_FILE:Substance::sse2_blend>
        DESTINATION lib
        COMPONENT Runtime
      )
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
      if(NOT neon_blend_REL MATCHES "NOTFOUND")
        install(
          FILES $<TARGET_FILE:Substance::neon_blend>
          DESTINATION lib
          COMPONENT Runtime
        )
      else()
        install(
          FILES $<TARGET_FILE:Substance::cpu_blend>
          DESTINATION lib
          COMPONENT Runtime
        )
      endif()
    else()
      # Universal build
      install(
        FILES $<TARGET_FILE:Substance::cpu_blend>
        DESTINATION lib
        COMPONENT Runtime
      )
    endif()

    install(
      FILES $<TARGET_FILE:Substance::mtl_blend> $<TARGET_FILE:Substance::ogl3_blend> $<TARGET_FILE:Substance::Linker>
      DESTINATION lib
      COMPONENT Runtime
    )

    if(TARGET Substance::vk_blend AND USDSBSAR_ENABLE_VULKAN)
      install(
        FILES $<TARGET_FILE:Substance::vk_blend
        DESTINATION lib
        COMPONENT Runtime
      )
    endif()
  endif()
endif()
