# -----------------------------------------------------------------------------
# reborn-cmd: sends command lines to a game started with --listen (ADR 0022)
# -----------------------------------------------------------------------------

add_executable(reborn-cmd reborn-cmd/reborn_cmd.c)
target_link_libraries(reborn-cmd PRIVATE Reborn::Engine)
# The engine's sockets live behind an internal header (platform/platform_socket.h): reused, not
# rewritten, so the client and the server share one implementation per OS.
target_include_directories(reborn-cmd PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
reborn_set_compiler_options(reborn-cmd)
