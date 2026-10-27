# Checks the git submodules before the third-party build starts: stops when one
# isn't checked out, warns when one isn't at the commit this repo records.
# Never updates them itself; that changes the checkout and needs the network.

file(STRINGS "${PROJECT_SOURCE_DIR}/.gitmodules" _tachyon_submodule_lines REGEX "^[ \t]*path[ \t]*=")
foreach(_line IN LISTS _tachyon_submodule_lines)
  string(REGEX REPLACE "^[ \t]*path[ \t]*=[ \t]*" "" _path "${_line}")
  string(STRIP "${_path}" _path)
  # googletest is only needed for the tests.
  if(_path MATCHES "googletest$" AND NOT TACHYON_BUILD_TESTS)
    continue()
  endif()
  file(GLOB _contents "${PROJECT_SOURCE_DIR}/${_path}/*")
  if(NOT _contents)
    message(FATAL_ERROR
      "${_path} is empty: the git submodules aren't checked out.\n"
      "Run: git submodule update --init --recursive")
  endif()
endforeach()

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" submodule status
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE _status
    ERROR_QUIET
    RESULT_VARIABLE _rc)
  if(_rc EQUAL 0)
    string(REPLACE "\n" ";" _status "${_status}")
    foreach(_line IN LISTS _status)
      # A leading '+' means the checkout differs from the recorded commit.
      if(_line MATCHES "^\\+[0-9a-f]+ ([^ ]+)")
        message(WARNING
          "${CMAKE_MATCH_1} is not at the commit this repository records. "
          "If that isn't on purpose, run: git submodule update --recursive")
      endif()
    endforeach()
  endif()
endif()
