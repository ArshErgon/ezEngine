######################################
### OpenXR support
######################################

set (EZ_BUILD_OPENXR OFF CACHE BOOL "Whether support for OpenXR should be added")

######################################
### ez_requires_openxr()
######################################

macro(ez_requires_openxr)

	ez_requires_windows()
	ez_requires(EZ_BUILD_OPENXR)
	# While counter-intuitive, we need to find the package here so that the PUBLIC inherited
	# target_sources using generator expressions can be resolved in the dependant projects.
	find_package(ezOpenXR REQUIRED)

endmacro()

######################################
### ez_link_target_openxr(<target>)
######################################

function(ez_link_target_openxr TARGET_NAME)

	ez_requires_openxr()

	find_package(ezOpenXR REQUIRED)

	if (EZOPENXR_FOUND)
		target_link_libraries(${TARGET_NAME} PRIVATE ezOpenXR::Loader)

        get_target_property(_dll_location ezOpenXR::Loader IMPORTED_LOCATION)
		if (NOT _dll_location STREQUAL "")
			add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ezOpenXR::Loader> $<TARGET_FILE_DIR:${TARGET_NAME}>)
		endif()
		unset(_dll_location)
		ez_uwp_add_import_to_sources(${TARGET_NAME} ezOpenXR::Loader)
      
	endif()

endfunction()

