# Included from the isolated copied engine tree after its native target setup.
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The staged host currently supports Windows x64 only")
endif()
if(NOT DEFINED ZC255_PHASE_ROOT OR NOT DEFINED ZC255_WORKSPACE_ROOT)
  message(FATAL_ERROR "Use phase-host/build_standalone.py to prepare this target")
endif()
add_compile_definitions(ALLEGRO_LEGACY_NO_MAGIC_MAIN STRICT NOMINMAX)
if(NOT DEFINED ZC255_HOST_SOURCE_ROOT)
  set(ZC255_HOST_SOURCE_ROOT "${ZC255_PHASE_ROOT}/src")
endif()
target_include_directories(zplayer BEFORE PRIVATE
  "${ZC255_PHASE_ROOT}/src" "${ZC255_WORKSPACE_ROOT}/src" "${CMAKE_CURRENT_SOURCE_DIR}/src/zc")
target_include_directories(zcbase BEFORE PRIVATE "${ZC255_PHASE_ROOT}/src" "${ZC255_WORKSPACE_ROOT}/src")
target_include_directories(zcgui BEFORE PRIVATE "${ZC255_PHASE_ROOT}/src" "${ZC255_WORKSPACE_ROOT}/src")
if(ZC255_LIFECYCLE_CANDIDATE)
  foreach(target zplayer zcbase zcgui zcsound zconsolelogger)
    target_include_directories(${target} BEFORE PRIVATE "${ZC255_HOST_SOURCE_ROOT}")
  endforeach()
endif()
target_sources(zplayer PRIVATE
  "${ZC255_HOST_SOURCE_ROOT}/engine_host.cpp"
  "${ZC255_HOST_SOURCE_ROOT}/native_history_policy.cpp"
  "${ZC255_HOST_SOURCE_ROOT}/player_bridge.cpp"
  "${ZC255_HOST_SOURCE_ROOT}/player_audio.cpp"
  "${ZC255_HOST_SOURCE_ROOT}/libretro_host.cpp")
if(ZC255_SPRITE_SELFTEST)
  target_sources(zplayer PRIVATE "${ZC255_HOST_SOURCE_ROOT}/sprite_exception_selftest.cpp")
endif()
if(ZC255_OWNER_GATE)
  target_sources(zplayer PRIVATE "${ZC255_HOST_SOURCE_ROOT}/lifecycle_runtime_gate.cpp")
endif()
if(ZC255_EMBEDDED_ASSETS)
  enable_language(RC)
  set(PUBLIC_ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../assets")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/embedded_assets.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/embedded_assets.rc" @ONLY)
  target_sources(zplayer PRIVATE "${ZC255_HOST_SOURCE_ROOT}/materializer.cpp"
      "${CMAKE_CURRENT_BINARY_DIR}/embedded_assets.rc")
endif()
if(NOT DEFINED ZC255_RUNTIME_ROOT)
  set(ZC255_RUNTIME_ROOT "${ZC255_PHASE_ROOT}/runtime")
endif()
if(ZC255_DEPENDENCY_PROFILE MATCHES "^(minimal-static|music-static|allegro-static)$")
  target_link_libraries(nfd PRIVATE ole32 shell32 uuid)
  foreach(dependency nfd asmjit)
    get_target_property(dependency_type ${dependency} TYPE)
    if(NOT dependency_type STREQUAL "STATIC_LIBRARY")
      message(FATAL_ERROR "${dependency} did not become a static library")
    endif()
  endforeach()
endif()
if(ZC255_DEPENDENCY_PROFILE MATCHES "^(music-static|allegro-static)$")
  get_target_property(gme_type gme TYPE)
  if(NOT gme_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "GME did not become a static library")
  endif()
endif()
if(ZC255_DEPENDENCY_PROFILE STREQUAL "allegro-static")
  foreach(dependency allegro allegro_audio allegro_acodec allegro_font allegro_image
      allegro_primitives allegro_dialog allegro_main allegro-legacy)
    if(NOT TARGET ${dependency})
      message(FATAL_ERROR "Required Allegro target missing: ${dependency}")
    endif()
    get_target_property(dependency_type ${dependency} TYPE)
    if(NOT dependency_type STREQUAL "STATIC_LIBRARY")
      message(FATAL_ERROR "${dependency} did not become a static library")
    endif()
  endforeach()
  # Upstream static flags are PRIVATE. Export them to all engine consumers too.
  target_compile_definitions(allegro_headers INTERFACE ALLEGRO_STATICLINK)
  target_compile_definitions(allegro_with_legacy INTERFACE
      ALLEGRO_STATICLINK ALLEGRO_LEGACY_STATICLINK)
  target_compile_definitions(allegro-legacy PUBLIC
      ALLEGRO_STATICLINK ALLEGRO_LEGACY_STATICLINK)
endif()
set_target_properties(zplayer PROPERTIES PREFIX "" OUTPUT_NAME "zc255_libretro"
  RUNTIME_OUTPUT_DIRECTORY "${ZC255_RUNTIME_ROOT}"
  LIBRARY_OUTPUT_DIRECTORY "${ZC255_RUNTIME_ROOT}")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set_target_properties(zplayer PROPERTIES
    "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${ZC255_RUNTIME_ROOT}"
    "LIBRARY_OUTPUT_DIRECTORY_${config_upper}" "${ZC255_RUNTIME_ROOT}")
endforeach()

# Frame-owned enhanced music; all decoder code remains inside the core DLL.
target_sources(zplayer PRIVATE
    "${ZC255_HOST_SOURCE_ROOT}/enhanced_music.cpp"
    "${ZC255_HOST_SOURCE_ROOT}/pcm_decoder.cpp"
    "${ZC255_HOST_SOURCE_ROOT}/vendor/stb_vorbis.c")
target_include_directories(zplayer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dumb/include" "${CMAKE_CURRENT_SOURCE_DIR}/include/minimp3")
target_link_libraries(zplayer PRIVATE bcrypt dumb gme)
