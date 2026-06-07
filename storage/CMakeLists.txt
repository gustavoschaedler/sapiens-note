add_library(sapiensstore STATIC src/SqliteNoteRepository.cpp)
target_include_directories(sapiensstore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sapiensstore PUBLIC sapienscore)

find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
  target_link_libraries(sapiensstore PUBLIC SQLite::SQLite3)
else()
  # Fall back to the system library (present in the macOS SDK / most Linux).
  target_link_libraries(sapiensstore PUBLIC sqlite3)
endif()
