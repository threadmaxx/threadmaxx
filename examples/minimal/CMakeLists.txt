add_executable(threadmaxx_minimal
    main.cpp
    MyGame.cpp
    MovementSystem.cpp
    SpawnerSystem.cpp
    ConsoleRenderer.cpp
)

target_link_libraries(threadmaxx_minimal PRIVATE threadmaxx::threadmaxx)

set_target_properties(threadmaxx_minimal PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
