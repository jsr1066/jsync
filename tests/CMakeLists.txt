add_executable(test_scan test_scan.cpp)
target_link_libraries(test_scan PRIVATE jsync_core)
target_compile_options(test_scan PRIVATE -Wall -Wextra)
add_test(NAME scan COMMAND test_scan)

add_executable(test_hash test_hash.cpp)
target_link_libraries(test_hash PRIVATE jsync_core)
target_compile_options(test_hash PRIVATE -Wall -Wextra)
add_test(NAME hash COMMAND test_hash)

add_executable(test_read test_read.cpp)
target_link_libraries(test_read PRIVATE jsync_core)
target_compile_options(test_read PRIVATE -Wall -Wextra)
add_test(NAME read COMMAND test_read)

add_executable(test_write test_write.cpp)
target_link_libraries(test_write PRIVATE jsync_core)
target_compile_options(test_write PRIVATE -Wall -Wextra)
add_test(NAME write COMMAND test_write)

add_executable(test_peer test_peer.cpp)
target_link_libraries(test_peer PRIVATE jsync_core)
target_compile_options(test_peer PRIVATE -Wall -Wextra)
add_test(NAME peer COMMAND test_peer)

add_executable(test_wire test_wire.cpp)
target_link_libraries(test_wire PRIVATE jsync_core)
target_compile_options(test_wire PRIVATE -Wall -Wextra)
add_test(NAME wire COMMAND test_wire)

add_executable(test_database test_database.cpp)
target_link_libraries(test_database PRIVATE jsync_core)
target_compile_options(test_database PRIVATE -Wall -Wextra)
add_test(NAME database COMMAND test_database)
