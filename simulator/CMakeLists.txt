add_executable(sapiens-sim src/main.cpp)
target_include_directories(sapiens-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sapiens-sim PRIVATE sapiensstore)  # sapiensstore PUBLIC-links sapienscore

# Link SDL2 (target form preferred; fall back to variables on older configs).
target_compile_definitions(sapiens-sim PRIVATE SAPIENS_WITH_SDL)
if(TARGET SDL2::SDL2)
  target_link_libraries(sapiens-sim PRIVATE SDL2::SDL2)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(sapiens-sim PRIVATE SDL2::SDL2main)
  endif()
else()
  target_include_directories(sapiens-sim PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(sapiens-sim PRIVATE ${SDL2_LIBRARIES})
endif()
