add_executable(test_scan test_scan.cpp)
target_link_libraries(test_scan PRIVATE jsync_core)
target_compile_options(test_scan PRIVATE -Wall -Wextra)
add_test(NAME scan COMMAND test_scan)

add_executable(test_hash test_hash.cpp)
target_link_libraries(test_hash PRIVATE jsync_core)
target_compile_options(test_hash PRIVATE -Wall -Wextra)
add_test(NAME hash COMMAND test_hash)
