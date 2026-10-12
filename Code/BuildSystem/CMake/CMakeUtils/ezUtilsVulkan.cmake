# #####################################
# ## Vulkan support
# #####################################

set(EZ_BUILD_VULKAN ON CACHE BOOL "Whether to build Vulkan renderer support.")

# #####################################
# ## ez_requires_vulkan()
# #####################################
macro(ez_requires_vulkan)
	ez_requires(EZ_CMAKE_PLATFORM_SUPPORTS_VULKAN)
	ez_requires(EZ_BUILD_VULKAN)
	find_package(EzVulkan REQUIRED)
endmacro()

# #####################################
# ## ez_link_target_dxc(<target>)
# #####################################
function(ez_link_target_dxc TARGET_NAME)
	ez_requires_vulkan()

	find_package(EzVulkan REQUIRED)

	if(EZVULKAN_FOUND)
		target_link_libraries(${TARGET_NAME} PRIVATE EzVulkan::DXC)

		get_target_property(_dll_location EzVulkan::DXC IMPORTED_LOCATION)

		if(NOT _dll_location STREQUAL "")
			# Deployment must also run when the compiler is already linked but its imported DLL is missing.
			# TARGET_FILE_DIR does not add a target dependency (CMP0112); the copy can precede the build.
			add_custom_target(${TARGET_NAME}_DXCDeployment
				COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TARGET_NAME}>
				COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:EzVulkan::DXC> $<TARGET_FILE_DIR:${TARGET_NAME}>
				VERBATIM)
			add_dependencies(${TARGET_NAME} ${TARGET_NAME}_DXCDeployment)
		endif()

		unset(_dll_location)
	endif()
endfunction()
