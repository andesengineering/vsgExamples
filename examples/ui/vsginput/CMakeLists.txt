set(SOURCES
    vsginput.cpp
)

add_executable(vsginput ${SOURCES})

target_link_libraries(vsginput vsg::vsg)
