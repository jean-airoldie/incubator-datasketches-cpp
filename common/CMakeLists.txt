add_library(common INTERFACE)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()

target_include_directories(common
  INTERFACE
    include
)

set(common_HEADERS "include/serde.hpp")

install(FILES ${common_HEADERS}
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/DataSketches")

target_sources(common
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/MurmurHash3.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/serde.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/CommonUtil.hpp
)

