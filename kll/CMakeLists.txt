add_library(kll OBJECT "")

add_library(${PROJECT_NAME}::KLL ALIAS kll)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()

target_include_directories(kll
  PUBLIC
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${COMMON_INCLUDE_DIR}
)

target_link_libraries(kll common)

set_target_properties(cpc PROPERTIES
  PUBLIC_HEADER "include/kll_sketch.hpp;include/kll_helper.hpp;include/kll_quantile_calculator.hpp;include/kll_wrapper.hpp"
  POSITION_INDEPENDENT_CODE ON
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
)


install(TARGETS kll
  EXPORT ${PROJECT_NAME}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/DataSketches
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/DataSketches
)

target_sources(kll
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kll_helper.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kll_sketch.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kll_quantile_calculator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kll_wrapper.hpp
  PRIVATE
    src/kll_wrapper.cpp
)
