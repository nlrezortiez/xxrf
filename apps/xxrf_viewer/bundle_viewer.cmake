if(NOT DEFINED XXRF_BUNDLE_DIR OR NOT DEFINED XXRF_VIEWER_EXE)
  message(FATAL_ERROR "xxrf_bundle_viewer requires XXRF_BUNDLE_DIR and XXRF_VIEWER_EXE")
endif()

file(REMOVE_RECURSE "${XXRF_BUNDLE_DIR}")
file(MAKE_DIRECTORY "${XXRF_BUNDLE_DIR}")

file(COPY "${XXRF_VIEWER_EXE}" DESTINATION "${XXRF_BUNDLE_DIR}")

if(DEFINED XXRF_ASSETS_DIR AND EXISTS "${XXRF_ASSETS_DIR}")
  file(COPY "${XXRF_ASSETS_DIR}" DESTINATION "${XXRF_BUNDLE_DIR}")
endif()

if(DEFINED XXRF_BUNDLE_README AND EXISTS "${XXRF_BUNDLE_README}")
  file(COPY "${XXRF_BUNDLE_README}" DESTINATION "${XXRF_BUNDLE_DIR}")
endif()

string(REPLACE "|" ";" _search_dirs "${XXRF_RUNTIME_DIRS}")
list(REMOVE_DUPLICATES _search_dirs)

file(GET_RUNTIME_DEPENDENCIES
  RESOLVED_DEPENDENCIES_VAR _resolved_deps
  UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
  EXECUTABLES "${XXRF_VIEWER_EXE}"
  DIRECTORIES ${_search_dirs}
  POST_EXCLUDE_REGEXES
    ".*[Ww]indows[\\\\/][Ss]ystem32[\\\\/].*"
    ".*api-ms-win-.*"
    ".*ext-ms-.*"
)

foreach(_dep IN LISTS _resolved_deps)
  file(COPY "${_dep}" DESTINATION "${XXRF_BUNDLE_DIR}")
endforeach()

if(_unresolved_deps)
  message(WARNING "xxrf_bundle_viewer unresolved runtime dependencies: ${_unresolved_deps}")
endif()
