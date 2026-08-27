# CMake generated Testfile for 
# Source directory: C:/premier-pro/python-app
# Build directory: C:/premier-pro/python-app/build-verify
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[cutpro_backend_tests]=] "C:/premier-pro/python-app/build-verify/bin/cutpro_backend_tests.exe")
set_tests_properties([=[cutpro_backend_tests]=] PROPERTIES  ENVIRONMENT "CUTPRO_CACHE_DIR=C:/premier-pro/python-app/build-verify/test-cache;CUTPRO_SETTINGS_FILE=C:/premier-pro/python-app/build-verify/test-settings.ini" _BACKTRACE_TRIPLES "C:/premier-pro/python-app/CMakeLists.txt;153;add_test;C:/premier-pro/python-app/CMakeLists.txt;0;")
