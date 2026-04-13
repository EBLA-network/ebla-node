add_executable(ebla-bootnode main.cpp)
target_link_libraries(ebla-bootnode PRIVATE
    cli
    p2p
)

set(EBLA_BOOTNODE_INSTALL_DIR "/usr/local/bin" CACHE PATH "Ebla bootnode install directory")
install(PROGRAMS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ebla-bootnode DESTINATION ${EBLA_BOOTNODE_INSTALL_DIR} COMPONENT ebla-bootnode)