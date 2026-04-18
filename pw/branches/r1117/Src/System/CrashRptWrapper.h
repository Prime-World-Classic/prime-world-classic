#ifndef CRASHRPTWRAPPER_H_INCLUDED
#define CRASHRPTWRAPPER_H_INCLUDED

namespace CrashRptWrapper
{

#if defined( NV_LINUX_PLATFORM )

inline void InstallForProcess( const char *, bool, bool, const char * = 0, const char * = 0, bool = false, bool = true ) {}
inline void UninstallFromProcess() {}
inline void InstallToCurrentThread() {}
inline void UninstallFromCurrentThread() {}
inline void AddFileToReport( const char *, const char * ) {}
inline void AddTagToReport( const char *, const char * ) {}

#else

void InstallForProcess( const char * uploadUrl, bool useBinaryEncoding, bool noGui, const char * productTitleOverride = 0, const char * privacyPolicyUrl = 0, bool enableLogging = false, bool sendQueuedReports = true );
void UninstallFromProcess();

#ifndef NI_DISABLE_CRASHRPT

//We cover per-thread functions only to reduce copy-paste
void InstallToCurrentThread();
void UninstallFromCurrentThread();

#else //NI_DISABLE_CRASHRPT

inline void InstallToCurrentThread() {}
inline void UninstallFromCurrentThread() {}

#endif //NI_DISABLE_CRASHRPT


void AddFileToReport( const char * filename, const char * description );

void AddTagToReport( const char * name, const char * value );

#endif

} //namespace CrashRptWrapper

#endif //CRASHRPTWRAPPER_H_INCLUDED
