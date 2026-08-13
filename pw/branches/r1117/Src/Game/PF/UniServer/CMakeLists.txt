cmake_minimum_required(VERSION 3.15)
project(UniServerApp C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============================================================
# Directory layout
# ============================================================
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH _p1)
cmake_path(GET _p1 PARENT_PATH _p2)
cmake_path(GET _p2 PARENT_PATH _p3)
cmake_path(GET _p3 PARENT_PATH PROJECT_ROOT)

set(SRC_DIR ${PROJECT_ROOT}/Src)
set(VENDOR ${PROJECT_ROOT}/Vendor)
set(AUTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/UniServerApp.auto/UniServerApp)

message(STATUS "PROJECT_ROOT: ${PROJECT_ROOT}")
message(STATUS "SRC_DIR: ${SRC_DIR}")
message(STATUS "VENDOR: ${VENDOR}")

# ============================================================
# Find system packages
# ============================================================
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# ============================================================
# Common compile definitions
# ============================================================
set(COMMON_DEFS
    NIV_LINUX_PLATFORM
    NIV_LINUX
    DO_NOT_USE_DLLMAIN
    BOOST_ALL_NO_LIB
    TRANSPORT_LAYER_COMPONENT
    NI_FORCE_DISABLE_INLINE_PROFILER
    NI_DISABLE_CRASHRPT
    VISUAL_CUTTED
    SERVER_DB
    _DO_ASSERT
    DB_TYPES_REGISTRATOR_SILENT
    LOG_THREAD_EXIT
    CHECK_TOWN_CONSISTENCY
    CURL_STATICLIB
    _HAS_ITERATOR_DEBUGGING=0
    STATIC_LIB
)

# ============================================================
# Common include directories
# ============================================================
set(COMMON_INCLUDES
    ${SRC_DIR}
    ${PROJECT_ROOT}
    ${PROJECT_ROOT}/..
    ${VENDOR}
    ${VENDOR}/boost
    ${VENDOR}/Terabit
    ${VENDOR}/Terabit/app
    ${VENDOR}/OpenSSL/include
    ${VENDOR}/zlib
    ${VENDOR}/Thrift
    ${VENDOR}/Thrift/thrift-0.9.1
    ${VENDOR}/Thrift/thrift-0.9.1/lib/cpp/src
    ${VENDOR}/mongoose
    ${VENDOR}/UnixEnv
    ${VENDOR}/DirectX/Include
    ${VENDOR}/MD4
    ${VENDOR}/wsdlpull
    ${VENDOR}/wsdlpull/src
    ${VENDOR}/JsonCpp
    ${VENDOR}/JsonCpp/include
    ${VENDOR}/tinyxml
    ${VENDOR}/tinyxml/src
    ${SRC_DIR}/Game/PF
    ${SRC_DIR}/Server
    ${SRC_DIR}/Server/NetworkAIO
    ${SRC_DIR}/System
    ${SRC_DIR}/Network
    ${SRC_DIR}/Core
    ${SRC_DIR}/libdb
    ${SRC_DIR}/MemoryLib
    ${SRC_DIR}/Server/RPC
    ${SRC_DIR}/Server/ServerAppBase
    ${SRC_DIR}/Server/Coordinator
    ${SRC_DIR}/Server/ClusterTypes
    ${SRC_DIR}/Server/Monitoring
    ${SRC_DIR}/Server/Monitoring/MonitoringSvc
    ${SRC_DIR}/Server/Monitoring/PerfCounterProvider
    ${SRC_DIR}/Server/ClusterAdmin
    ${SRC_DIR}/Server/MatchMaking
    ${SRC_DIR}/Server/Relay
    ${SRC_DIR}/Server/Relay/RelayBalancer
    ${SRC_DIR}/Server/Chat
    ${SRC_DIR}/Server/Chat/ChatSvc
    ${SRC_DIR}/Server/UserManager
    ${SRC_DIR}/Server/UserManager/UserManagerSvc
    ${SRC_DIR}/Server/ZZima
    ${SRC_DIR}/Server/WebClient
    ${SRC_DIR}/Server/WebServer
    ${SRC_DIR}/Server/HttpGateway
    ${SRC_DIR}/Server/RdpTransport
    ${SRC_DIR}/Server/NewLogin
    ${SRC_DIR}/Server/ClientControl
    ${SRC_DIR}/Server/Db
    ${SRC_DIR}/Server/AppFramework
    ${SRC_DIR}/Server/DebugVarUpdate
    ${SRC_DIR}/Server/LogMonitorAgent
    ${SRC_DIR}/Network/RUDP
    ${SRC_DIR}/System/FileSystem
    ${SRC_DIR}/System/Pointers
    ${SRC_DIR}/System/BlockData
    ${SRC_DIR}/System/Node
    ${SRC_DIR}/System/Math
    ${SRC_DIR}/System/InlineProfiler3
    ${SRC_DIR}/Game/PF/Server
    ${SRC_DIR}/Game/PF/Server/Roll
    ${SRC_DIR}/Game/PF/Server/LobbyPvx
    ${SRC_DIR}/Game/PF/Server/LobbySocial
    ${SRC_DIR}/Game/PF/Server/LiveMMaking
    ${SRC_DIR}/Game/PF/Server/GameSession
    ${SRC_DIR}/Game/PF/Server/GameChatController
    ${SRC_DIR}/Game/PF/Server/Accounting
    ${SRC_DIR}/Game/PF/Server/Statistic
    ${SRC_DIR}/Game/PF/Server/TourneyCfg
    ${SRC_DIR}/Game/PF/Server/Censorship
    ${SRC_DIR}/Game/PF/Client/LobbyPvx
    ${SRC_DIR}/Game/PF/HybridServer
    ${SRC_DIR}/Game/PF/UniServer
)

# ============================================================
# 1. Build ACE 5.x from vendored sources
# ============================================================
message(STATUS "=== Building ACE 5.x ===")

file(GLOB ACE_SRCS ${VENDOR}/ACE_wrappers/ace/*.cpp)
list(FILTER ACE_SRCS EXCLUDE REGEX ".*FoxReactor.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*GUI.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*JNI.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*MAC.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*BEOS.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Vx.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Doomsday.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*TAO.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Win.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*windows.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*OS_NS_win.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Solaris.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*AIX.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*HP.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Tru64.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*OSF.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*VMS.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*NT.*")

# Exclude SPIPE/UPIPE files that fail with GCC 15, but keep SPIPE_Addr
list(FILTER ACE_SRCS EXCLUDE REGEX ".*SPIPE_Stream.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*SPIPE_Acceptor.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*SPIPE_Connector.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*SPIPE_stubs.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*UPIPE.*")

# Exclude files that are known to fail with GCC 15
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Caching_Strategies_T.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Cleanup_Strategies_T.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Service.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*SSLIOP.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*SSL_SOCK.*")
list(FILTER ACE_SRCS EXCLUDE REGEX ".*Date_Time.*")

list(LENGTH ACE_SRCS ACE_COUNT)
message(STATUS "ACE sources: ${ACE_COUNT}")

add_library(ace_vendor STATIC ${ACE_SRCS})
target_include_directories(ace_vendor PUBLIC
    ${VENDOR}/ACE_wrappers
)
target_compile_definitions(ace_vendor PUBLIC
    ${COMMON_DEFS}
    ACE_HAS_EXCEPTIONS
    _REENTRANT
    # Suppress warnings-as-errors for old code
)
target_compile_options(ace_vendor PRIVATE
    -fpermissive
    -Wno-class-memaccess
    -Wno-implicit-int
    -Wno-narrowing
    -Wno-unused-variable
    -Wno-unused-parameter
    -Wno-pragma-once-outside-header
    -Wno-old-style-cast
    -Wno-invalid-offsetof
    -Wno-deprecated-declarations
    -Wno-ignored-qualifiers
    -Wno-reorder
)
target_link_libraries(ace_vendor PRIVATE ${CMAKE_THREAD_LIBS_INIT} rt dl)

# ============================================================
# 2. Build Terabit from vendored sources
# ============================================================
message(STATUS "=== Building Terabit ===")

file(GLOB_RECURSE TERABIT_SRCS
    ${VENDOR}/Terabit/*.cpp
    ${VENDOR}/Terabit/*.c
)
list(FILTER TERABIT_SRCS EXCLUDE REGEX ".*[Ww]in.*")
list(FILTER TERABIT_SRCS EXCLUDE REGEX ".*windows.*")
list(FILTER TERABIT_SRCS EXCLUDE REGEX ".*Win32.*")
list(FILTER TERABIT_SRCS EXCLUDE REGEX ".*SSL.*")
list(FILTER TERABIT_SRCS EXCLUDE REGEX ".*ssl.*")
list(FILTER TERABIT_SRCS EXCLUDE REGEX ".*examples.*")
list(FILTER TERABIT_SRCS EXCLUDE REGEX ".*tests.*")
list(FILTER TERABIT_SRCS EXCLUDE REGEX ".*test.*")

list(LENGTH TERABIT_SRCS TERABIT_COUNT)
message(STATUS "Terabit sources: ${TERABIT_COUNT}")

add_library(terabit STATIC ${TERABIT_SRCS})
target_include_directories(terabit PUBLIC
    ${VENDOR}/Terabit
    ${VENDOR}/Terabit/app
    ${VENDOR}/ACE_wrappers
)
target_compile_definitions(terabit PUBLIC
    ${COMMON_DEFS}
    ACE_LIB_TEXT=ACE_TEXT
)
target_compile_options(terabit PRIVATE
    -fpermissive
    -Wno-class-memaccess
    -Wno-pragma-once-outside-header
    -Wno-old-style-cast
)
target_link_libraries(terabit PRIVATE ace_vendor ${CMAKE_THREAD_LIBS_INIT})

# ============================================================
# 3. Use system Thrift (0.22.0) + compatibility shim for 0.9.1
# ============================================================
message(STATUS "=== Using system Thrift ===")

find_library(THRIFT_LIB NAMES thrift PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
find_path(THRIFT_INCLUDE_DIR NAMES thrift/Thrift.h PATHS /usr/include)

if(THRIFT_LIB AND THRIFT_INCLUDE_DIR)
    message(STATUS "System Thrift found: ${THRIFT_LIB}")
    add_library(thrift_vendor INTERFACE)
    target_include_directories(thrift_vendor INTERFACE
        ${THRIFT_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/thrift_compat  # cxxfunctional.h shim
    )
    target_link_libraries(thrift_vendor INTERFACE ${THRIFT_LIB})
else()
    message(WARNING "System Thrift not found, using INTERFACE library")
    add_library(thrift_vendor INTERFACE)
endif()

# ============================================================
# 4. Build mongoose (vendored)
# ============================================================
message(STATUS "=== Building mongoose ===")

if(EXISTS ${VENDOR}/mongoose/mongoose.c)
    add_library(mongoose STATIC ${VENDOR}/mongoose/mongoose.c)
    target_compile_definitions(mongoose PRIVATE USE_SOCKETPAIR=1 USE_POLL=1)
    if(EXISTS ${VENDOR}/mongoose/mongoose_linux_prelude.h)
        target_compile_options(mongoose PRIVATE "-include" "${VENDOR}/mongoose/mongoose_linux_prelude.h")
    endif()
else()
    add_library(mongoose INTERFACE)
endif()

# ============================================================
# 5. Build wsdlpull (vendored)
# ============================================================
message(STATUS "=== Building wsdlpull ===")

file(GLOB_RECURSE WSDLPULL_SRCS ${VENDOR}/wsdlpull/src/*.cpp)
list(FILTER WSDLPULL_SRCS EXCLUDE REGEX ".*win32.*")
list(FILTER WSDLPULL_SRCS EXCLUDE REGEX ".*[Ww]in.*")
list(LENGTH WSDLPULL_SRCS WSDLPULL_COUNT)
message(STATUS "wsdlpull sources: ${WSDLPULL_COUNT}")

if(WSDLPULL_SRCS)
    add_library(wsdlpull STATIC ${WSDLPULL_SRCS})
    target_include_directories(wsdlpull PUBLIC ${VENDOR}/wsdlpull ${VENDOR}/wsdlpull/src)
    target_compile_definitions(wsdlpull PUBLIC ${COMMON_DEFS} PACKAGE_VERSION="1.0")
    target_compile_options(wsdlpull PRIVATE -fpermissive)
else()
    add_library(wsdlpull INTERFACE)
endif()

# ============================================================
# 6. Build JsonCpp (vendored)
# ============================================================
message(STATUS "=== Building JsonCpp ===")

file(GLOB_RECURSE JSONCPP_SRCS ${VENDOR}/JsonCpp/src/lib_json/*.cpp)
list(LENGTH JSONCPP_SRCS JSONCPP_COUNT)
message(STATUS "JsonCpp sources: ${JSONCPP_COUNT}")

if(JSONCPP_SRCS)
    add_library(jsoncpp_vendor STATIC ${JSONCPP_SRCS})
    target_include_directories(jsoncpp_vendor PUBLIC ${VENDOR}/JsonCpp ${VENDOR}/JsonCpp/include)
    target_compile_options(jsoncpp_vendor PRIVATE -fpermissive)
else()
    add_library(jsoncpp_vendor INTERFACE)
    find_library(JSONCPP_LIB jsoncpp PATHS /usr/lib/x86_64-linux-gnu)
    if(JSONCPP_LIB)
        target_link_libraries(jsoncpp_vendor INTERFACE ${JSONCPP_LIB})
    endif()
endif()

# ============================================================
# 7. Build tinyxml (vendored)
# ============================================================
message(STATUS "=== Building tinyxml ===")

set(TINYXML_SRC ${VENDOR}/tinyxml/tinyxml.cpp)
set(TINYXML_XML_SRC ${VENDOR}/tinyxml/xmlutils.cpp)
if(EXISTS ${TINYXML_SRC})
    add_library(tinyxml_vendor STATIC ${TINYXML_SRC} ${TINYXML_XML_SRC})
    target_include_directories(tinyxml_vendor PUBLIC ${VENDOR}/tinyxml)
else()
    add_library(tinyxml_vendor INTERFACE)
    find_library(TINYXML_LIB tinyxml PATHS /usr/lib/x86_64-linux-gnu)
    if(TINYXML_LIB)
        target_link_libraries(tinyxml_vendor INTERFACE ${TINYXML_LIB})
    endif()
endif()

# ============================================================
# 8. Build MD4 (vendored - only md4c.c)
# ============================================================
message(STATUS "=== Building MD4 ===")

set(MD4_SRCS ${VENDOR}/MD4/md4c.c)
if(EXISTS ${MD4_SRCS})
    add_library(md4_vendor STATIC ${MD4_SRCS})
    target_include_directories(md4_vendor PUBLIC ${VENDOR}/MD4)
    target_compile_definitions(md4_vendor PRIVATE PROTOTYPES=1)
else()
    add_library(md4_vendor INTERFACE)
endif()

# ============================================================
# 9. Build UnixEnv (vendored compat layer)
# ============================================================
message(STATUS "=== Building UnixEnv ===")

file(GLOB UNIXENV_SRCS ${VENDOR}/UnixEnv/*.cpp ${VENDOR}/UnixEnv/*.c)
if(UNIXENV_SRCS)
    add_library(unixenv STATIC ${UNIXENV_SRCS})
    target_include_directories(unixenv PUBLIC ${VENDOR}/UnixEnv ${VENDOR}/UnixEnv/include)
else()
    add_library(unixenv INTERFACE)
endif()

# ============================================================
# ============================================================
# 11. Collect UniServerApp project sources
# ============================================================
message(STATUS "=== Collecting UniServerApp sources ===")

set(ALL_SOURCES "")

file(STRINGS ${AUTO_DIR}/CMakeLists.txt CMK_INCLUDES REGEX "^include\\(")
foreach(INCLUDE_LINE ${CMK_INCLUDES})
    string(REGEX MATCH "include\\((.+\\.cmake)\\)" _ "${INCLUDE_LINE}")
    set(CMAKE_FILE "${CMAKE_MATCH_1}")
    if(NOT CMAKE_FILE)
        continue()
    endif()
    if(NOT IS_ABSOLUTE ${CMAKE_FILE})
        set(CMAKE_FILE ${AUTO_DIR}/${CMAKE_FILE})
    endif()
    get_filename_component(CMAKE_FILE_ABS ${CMAKE_FILE} ABSOLUTE)
    if(NOT EXISTS ${CMAKE_FILE_ABS})
        continue()
    endif()
    file(READ ${CMAKE_FILE_ABS} CMK_CONTENT)
    string(REGEX MATCH "set\\(SOURCES_[^ ]+ (.+)\\)" _ "${CMK_CONTENT}")
    if(CMAKE_MATCH_1)
        separate_arguments(SRC_LIST UNIX_COMMAND "${CMAKE_MATCH_1}")
        foreach(SRC ${SRC_LIST})
            string(STRIP "${SRC}" SRC)
            if(SRC STREQUAL "")
                continue()
            endif()
            get_filename_component(CMK_DIR ${CMAKE_FILE_ABS} DIRECTORY)
            set(FULL_SRC ${CMK_DIR}/${SRC})
            get_filename_component(FULL_SRC_ABS ${FULL_SRC} ABSOLUTE)
            if(NOT EXISTS ${FULL_SRC_ABS})
                continue()
            endif()
            string(TOLOWER "${FULL_SRC}" FULL_SRC_LOWER)
            if(FULL_SRC_LOWER MATCHES ".*win" OR FULL_SRC_LOWER MATCHES ".*windows")
                continue()
            endif()
            list(APPEND ALL_SOURCES ${FULL_SRC_ABS})
        endforeach()
    endif()
endforeach()

set(MAIN_CPP ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
if(EXISTS ${MAIN_CPP})
    list(APPEND ALL_SOURCES ${MAIN_CPP})
endif()

list(REMOVE_DUPLICATES ALL_SOURCES)
list(LENGTH ALL_SOURCES SRC_COUNT)
message(STATUS "Collected ${SRC_COUNT} UniServerApp source files")

# ============================================================
# 12. Main executable
# ============================================================
message(STATUS "=== Creating UniServerApp target ===")

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${COMMON_INCLUDES}
    ${VENDOR}/ACE_wrappers
    ${VENDOR}/boost
    ${OPENSSL_INCLUDE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PUBLIC ${COMMON_DEFS})

target_compile_options(${PROJECT_NAME} PRIVATE
    -fpermissive
    -Wno-class-memaccess
    -Wno-pragma-once-outside-header
    -Wno-old-style-cast
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ace_vendor           # ACE 5.x (vendored)
    terabit              # Terabit/TProactor (vendored)
    thrift_vendor        # Thrift 0.9.1 (vendored)
    mongoose             # mongoose (vendored)
    wsdlpull             # wsdlpull (vendored)
    jsoncpp_vendor       # JsonCpp (vendored or system)
    tinyxml_vendor       # tinyxml (vendored or system)
    md4_vendor           # MD4 (vendored)
    unixenv              # UnixEnv (vendored)
    ${CURL_LIBRARIES}    # system curl
    ${OPENSSL_LIBRARIES} # system openssl
    ${ZLIB_LIBRARIES}    # system zlib
    ${CMAKE_THREAD_LIBS_INIT}
    dl
    rt
    pthread
)

# ============================================================
# Compiler flags
# ============================================================
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Configuration complete")
