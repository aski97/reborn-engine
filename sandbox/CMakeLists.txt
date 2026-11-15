add_executable(sandbox
    src/main.c
)

target_link_libraries(sandbox PRIVATE Reborn::Engine)

reborn_set_compiler_options(sandbox)
