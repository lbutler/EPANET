# CMakeLists.txt - report replication test harness (repgen)
#
# Builds a command line tool that runs an EPANET simulation and writes both
# the engine's native report and a replica generated purely from public
# toolkit API calls, so the two can be diffed to find API gaps.

add_executable(repgen
    main.c
    collect.c
    inp_sniff.c
    render_text.c
)
target_include_directories(repgen PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(repgen LINK_PUBLIC epanet2)
if(NOT MSVC)
    target_link_libraries(repgen PUBLIC m)
endif()
