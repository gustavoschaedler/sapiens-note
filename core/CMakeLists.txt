add_library(sapienscore STATIC
    src/Id.cpp
    src/NoteService.cpp
    src/AudioService.cpp
    src/AppController.cpp
    src/SyncClient.cpp
)

target_include_directories(sapienscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(sapienscore PUBLIC cxx_std_23)
