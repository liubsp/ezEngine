ez_cmake_init()

ez_requires(EZ_3RDPARTY_JOLT_SUPPORT)

get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME_WE)
ez_create_target(APPLICATION ${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME}
  PRIVATE
  TestFramework
  JoltPlugin
  JoltPluginTest_Plugin
)

ez_ci_add_test(${PROJECT_NAME})
