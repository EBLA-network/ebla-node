add_executable(eblad main.cpp)
target_link_libraries(eblad PRIVATE
    app
)

set(EBLAD_INSTALL_DIR "/usr/local/bin" CACHE PATH "Ebla application install directory")
install(PROGRAMS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/eblad DESTINATION ${EBLAD_INSTALL_DIR} COMPONENT eblad)