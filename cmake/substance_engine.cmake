find_package(substance CONFIG REQUIRED)
set(SUBSTANCE_TARGETS)
list(APPEND SUBSTANCE_TARGETS Substance::Framework)
list(APPEND SUBSTANCE_TARGETS Substance::Linker)

if(WIN32)
  list(APPEND SUBSTANCE_TARGETS Substance::sse2_blend)
elseif(LINUX)
  list(APPEND SUBSTANCE_TARGETS Substance::sse2_blend)
elseif(APPLE)
  if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    list(APPEND SUBSTANCE_TARGETS Substance::sse2_blend)
  elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    list(APPEND SUBSTANCE_TARGETS Substance::neon_blend)
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
      install(
        FILES $<TARGET_FILE:Substance::neon_blend>
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
