cmake_minimum_required(VERSION 3.10)
project(Terabit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include paths - vendor ACE 5.7 headers (ABI-compatible with project code)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/../ACE_wrappers  # vendor ACE 5.7
)

# Compiler flags matching the main project
add_compile_options(-Wall -Wextra -Wno-implicit-fallthrough -fpermissive)
add_compile_definitions(AIO_ROOT)

# No pre-include needed - vendor ACE 5.7 is compatible out of the box

# ============================================================
# TProactor - POSIX sources only
# ============================================================
set(TPROACTOR_SOURCES
    TProactor/Proactor.cpp
    TProactor/Proactor_Impl.cpp
    TProactor/Asynch_IO.cpp
    TProactor/Asynch_IO_Impl.cpp
    TProactor/Sock_Addr.cpp
    TProactor/POSIX_Asynch_IO.cpp
    TProactor/POSIX_Proactor.cpp
    TProactor/POSIX_AIO_Config.cpp
    TProactor/POSIX_AIO_Dispatcher.cpp
    TProactor/POSIX_AIO_Emulation.cpp
    TProactor/POSIX_AIO_Emulation_Provider.cpp
    TProactor/POSIX_AIO_FD_Info.cpp
    TProactor/POSIX_AIO_Processor.cpp
    TProactor/POSIX_AIO_Provider.cpp
    TProactor/POSIX_AIO_LINUX_NAIO_Provider.cpp
    TProactor/POSIX_AIO_Event_Poll_Provider.cpp
    TProactor/POSIX_AIO_Poll_Provider.cpp
    TProactor/POSIX_AIO_Select_Provider.cpp
    TProactor/POSIX_AIO_STD_Provider.cpp
    TProactor/Asynch_File_Transmitter.cpp
)

add_library(TProactor SHARED ${TPROACTOR_SOURCES})
target_link_libraries(TProactor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ACE_wrappers/lib/libACE.so pthread rt)
set_target_properties(TProactor PROPERTIES PREFIX "lib")
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../ACE_wrappers/lib)

# ============================================================
# IOTerabit
# ============================================================
set(IOTERABIT_SOURCES
    app/IOTerabit/AsynchChannel.cpp
    app/IOTerabit/BaseAcceptor.cpp
    app/IOTerabit/BaseConnector.cpp
    app/IOTerabit/BaseThreadPool.cpp
    app/IOTerabit/ChannelAcceptor.cpp
    app/IOTerabit/ChannelConnector.cpp
    app/IOTerabit/ChannelManager.cpp
    app/IOTerabit/IOThreadPool.cpp
    app/IOTerabit/MessageBlockFactory.cpp
    app/IOTerabit/Protocol.cpp
    app/IOTerabit/RefCntProtocol.cpp
    app/IOTerabit/TcpChannel.cpp
    app/IOTerabit/WrkThreadPool_T.cpp
)

add_library(IOTerabit SHARED ${IOTERABIT_SOURCES})
target_link_libraries(IOTerabit PRIVATE TProactor ACE pthread)
set_target_properties(IOTerabit PROPERTIES PREFIX "lib")
