add_executable(HomeCarouselTouchTest
  HomeCarouselTouchTest.cpp
)

target_include_directories(HomeCarouselTouchTest PRIVATE
  ${REPO_ROOT}/src/activities/home
)

target_link_libraries(HomeCarouselTouchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeCarouselTouchTest)

