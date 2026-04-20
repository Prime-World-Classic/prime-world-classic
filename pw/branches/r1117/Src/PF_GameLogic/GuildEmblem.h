#ifndef GUILDEMBLEM_H_5D67B548_52C0_4AEA
#define GUILDEMBLEM_H_5D67B548_52C0_4AEA

#pragma warning( disable: 4530 )
#include "System/JobThread.h"
#include <sstream>

namespace NGameX
{

class GuildEmblem : public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_1( GuildEmblem, BaseObjectST );

  nstl::vector<StrongMT<threading::JobThread>> downloadJobs;
  nstl::map<uint64_t, nstl::vector<byte>> data;
  threading::Mutex mutex;

public:
  GuildEmblem();

  void StartDownload( uint64_t guildAuid, uint id, const nstl::string& url );
  bool IsDownloaded();
  void SetData( uint64_t guildAuid, const nstl::vector<byte>& _data );
  bool GetData( uint64_t guildAuid, nstl::vector<byte>& _data );
};

}

#endif //#define GUILDEMBLEM_H_5D67B548_52C0_4AEA