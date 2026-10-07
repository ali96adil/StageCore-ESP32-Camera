if(STAGECORE_CAMERA_V2_FOUNDATION_C1 OR STAGECORE_CAMERA_V2_FOUNDATION_C2)
  idf_component_register(
    SRCS
      "v2_foundation/main.cpp"
      "v2_foundation/config_store.cpp"
      "v2_foundation/device_identity.cpp"
      "v2_foundation/trusted_clock.cpp"
      "v2_foundation/hub_discovery.cpp"
      "v2_foundation/hub_security.cpp"
      "v2_foundation/network_station.cpp"
      "v2_foundation/stage_device_runtime.cpp"
    INCLUDE_DIRS
      "v2_foundation"
  )
else()
  idf_component_register()
endif()
