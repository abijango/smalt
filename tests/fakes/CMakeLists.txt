# Owned by task 00. Test doubles shared across tasks.
add_library(smalt_test_fakes STATIC fake_nickel.h fake_nickel.cpp)
target_include_directories(smalt_test_fakes PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(smalt_test_fakes PUBLIC smalt_common Qt5::Core)

smalt_add_qtest(fakes.fake_nickel SOURCES tst_fake_nickel.cpp DEPS smalt_test_fakes)
