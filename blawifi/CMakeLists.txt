# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Project-specific Kconfig changes layered on top of IDF's built-in defaults.
# Keep this file small and commented — every line should be a deliberate
# deviation from the IDF baseline.
set(SDKCONFIG_DEFAULTS "sdkconfig.overrides")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(blawifi)
