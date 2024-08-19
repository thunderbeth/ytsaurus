LIBRARY()

INCLUDE(${ARCADIA_ROOT}/yt/ya_cpp.make.inc)

SRCS(
    fixed_growth_string_output.cpp
    test_memory_tracker.cpp
    test_server_host.cpp
    GLOBAL framework.cpp
)

IF (NOT USE_SYSTEM_STL)
    SRCS(
        test_proxy_service.cpp
    )
ENDIF()

PEERDIR(
    library/cpp/testing/gtest
    library/cpp/testing/hook
    yt/yt/build
    yt/yt/core
    yt/yt/core/http
    yt/yt/library/profiling/solomon
)

END()
