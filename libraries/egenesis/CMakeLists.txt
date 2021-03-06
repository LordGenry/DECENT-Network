
add_library( graphene_egenesis_none
   egenesis_none.cpp
   include/graphene/egenesis/egenesis.hpp
   )

target_link_libraries( graphene_egenesis_none PRIVATE graphene_chain graphene_db decent_encrypt )
target_include_directories( graphene_egenesis_none
   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

add_executable( embed_genesis
   embed_genesis.cpp
   )

target_link_libraries( embed_genesis PRIVATE graphene_chain graphene_db graphene_app graphene_egenesis_none decent_encrypt )

set( embed_genesis_args
   -t "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_brief.cpp.tmpl---${CMAKE_CURRENT_BINARY_DIR}/egenesis_brief.cpp"
   -t "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_full.cpp.tmpl---${CMAKE_CURRENT_BINARY_DIR}/egenesis_full.cpp"
   )

if( GRAPHENE_EGENESIS_JSON )
   MESSAGE( STATUS "egenesis: " ${GRAPHENE_EGENESIS_JSON} )
   list( APPEND embed_genesis_args --genesis-json "${GRAPHENE_EGENESIS_JSON}" )
else( GRAPHENE_EGENESIS_JSON )
   MESSAGE( STATUS "egenesis: none" )
endif( GRAPHENE_EGENESIS_JSON )

MESSAGE( STATUS "embed_genesis_args: " ${embed_genesis_args} )

add_custom_command(
   OUTPUT
      "${CMAKE_CURRENT_BINARY_DIR}/egenesis_brief.cpp"
      "${CMAKE_CURRENT_BINARY_DIR}/egenesis_full.cpp"
   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
   COMMAND embed_genesis ${embed_genesis_args}
   DEPENDS
      "${GRAPHENE_EGENESIS_JSON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_brief.cpp.tmpl"
      "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_full.cpp.tmpl"
      embed_genesis )

add_library( graphene_egenesis_brief "${CMAKE_CURRENT_BINARY_DIR}/egenesis_brief.cpp" include/graphene/egenesis/egenesis.hpp )
add_library( graphene_egenesis_full  "${CMAKE_CURRENT_BINARY_DIR}/egenesis_full.cpp" include/graphene/egenesis/egenesis.hpp )
add_library( graphene_egenesis_decent "${CMAKE_CURRENT_SOURCE_DIR}/egenesis_decent.cpp" include/graphene/egenesis/egenesis.hpp )

target_link_libraries( graphene_egenesis_brief PRIVATE graphene_chain graphene_db decent_encrypt )
target_link_libraries( graphene_egenesis_full PRIVATE graphene_chain graphene_db decent_encrypt )
target_link_libraries( graphene_egenesis_decent PRIVATE graphene_chain graphene_db decent_encrypt )

target_include_directories( graphene_egenesis_brief
   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
target_include_directories( graphene_egenesis_full
   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
target_include_directories( graphene_egenesis_decent
   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

#install( TARGETS
#   graphene_egenesis_decent
#   #   embed_genesis graphene_egenesis_none graphene_egenesis_brief graphene_egenesis_full
#
#   RUNTIME DESTINATION bin
#   LIBRARY DESTINATION lib
#   ARCHIVE DESTINATION lib
#)
