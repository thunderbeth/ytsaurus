LIBRARY()

SRCS(
    yql_yt_profiling.cpp
)

PEERDIR(
    yql/essentials/utils/log
    yt/yql/providers/yt/provider
)

YQL_LAST_ABI_VERSION()

END()
