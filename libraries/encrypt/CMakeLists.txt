file( GLOB HEADERS "include/decent/encrypt/*.hpp" )

add_library( decent_encrypt
             encryptionutils.cpp
             custodyutils.cpp
             ${HEADERS} )

target_link_libraries( decent_encrypt
    PUBLIC fc gmp pbc ${CRYPTOPP_NAME} )

target_include_directories( decent_encrypt
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

add_executable( test_encrypt test_encryption_utils.cpp )
target_link_libraries( test_encrypt decent_encrypt )

#add_executable( test_pbc_benchmark test_pbc_benchmark.cpp )
#target_link_libraries( test_pbc_benchmark decent_encrypt )

#install( TARGETS
#   decent_encrypt
#
#   RUNTIME DESTINATION bin
#   LIBRARY DESTINATION lib
#   ARCHIVE DESTINATION lib
#)
