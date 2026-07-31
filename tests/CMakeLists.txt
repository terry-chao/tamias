add_executable(tamias_tests smoke_tests.cpp)
target_link_libraries(tamias_tests
  PRIVATE
    tamias::io
    tamias::view
    tamias::modeling
    tamias::render
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(tamias_tests)
