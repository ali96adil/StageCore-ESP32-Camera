if(STAGECORE_CAMERA_V2_FOUNDATION_C1 OR STAGECORE_CAMERA_V2_FOUNDATION_C2)
  set(STAGECORE_CAMERA_V2_SRCS
    "v2_foundation/main.cpp"
    "v2_foundation/config_store.cpp"
    "v2_foundation/network_station.cpp"
    "v2_foundation/stage_device_runtime.cpp"
  )
  if(STAGECORE_CAMERA_V2_FOUNDATION_C2)
    list(APPEND STAGECORE_CAMERA_V2_SRCS
      "v2_foundation/provisioning.cpp"
    )
  endif()
  if(STAGECORE_CAMERA_V2_FOUNDATION_C3)
    list(APPEND STAGECORE_CAMERA_V2_SRCS
      "v2_foundation/camera_service.cpp"
    )
  endif()
  idf_component_register(
    SRCS ${STAGECORE_CAMERA_V2_SRCS}
    INCLUDE_DIRS
      "."
      "v2_foundation"
    REQUIRES
      stagecore_foundation
  )
else()
  idf_component_register()
endif()
