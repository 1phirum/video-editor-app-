# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles\\cutpro_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\cutpro_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\cutpro_backend_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\cutpro_backend_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\cutpro_backend_tests_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\cutpro_backend_tests_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\cutpro_subtitles_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\cutpro_subtitles_autogen.dir\\ParseCache.txt"
  "cutpro_autogen"
  "cutpro_backend_autogen"
  "cutpro_backend_tests_autogen"
  "cutpro_subtitles_autogen"
  )
endif()
