#include "stdafx.h"
#include "AsyncMapStartup.h"
#include "MapStartup.h"

#include "PFWorld.h"

#include "System/InlineProfiler3/InlineProfiler3.h"

namespace NWorld
{
//////////////////////////////////////////////////////////////////////////

  typedef CObj<PFWorld> WorldPtr;

//////////////////////////////////////////////////////////////////////////

  class MapLoadingControllerImpl : public MapLoadingController
  {
  public:
    MapLoadingControllerImpl(volatile bool& isRunning)
      : running(isRunning)
    {
    }
    virtual bool CanLoad() const
    {
      return running;
    }
  private:
    volatile bool& running;
  };

//////////////////////////////////////////////////////////////////////////

  class WorldMapLoadingControllerGuard : public NonCopyable
  {
  public:
    explicit WorldMapLoadingControllerGuard(const WorldPtr& world, const MapLoadingControllerPtr& controller)
      : world(world)
    {
      if (world)
        world->SetMapLoadingController(controller);
    }
    ~WorldMapLoadingControllerGuard()
    {
      if (world)
        world->SetMapLoadingController(NULL);
    }
  private:
    WorldMapLoadingControllerGuard();

    const WorldPtr world;
  };

//////////////////////////////////////////////////////////////////////////

  class MapLoadingThreadJob
    : public BaseObjectMT
    , public threading::IThreadJob
  {
    NI_DECLARE_REFCOUNT_CLASS_2(MapLoadingThreadJob, threading::IThreadJob, BaseObjectMT);
  public:
    explicit MapLoadingThreadJob(MapLoadingJob* const job)
      : job(job)
    {

    }

    virtual void Work(volatile bool & isRunning)
    {
      NI_PROFILE_THREAD

      const WorldPtr world(job->GetWorld());
      const MapLoadingControllerPtr loadingController(new MapLoadingControllerImpl(isRunning));

      const WorldMapLoadingControllerGuard guard(world, loadingController);

      job->DoTheJob();
    }
  private:
    MapLoadingThreadJob();

    const StrongMT<MapLoadingJob> job;
  };

//////////////////////////////////////////////////////////////////////////

  bool MapLoader::IsThreaded()
  {
#if defined( PW_LINUX_DB_BOOTSTRAP )
    return false;
#else
    return !!NGlobal::GetVar("threaded_loading", 1L).GetInt64();
#endif
  }

  threading::JobThread* MapLoader::CreateMapLoadingThread(MapLoadingJob* const job)
  {
#if defined( PW_LINUX_DB_BOOTSTRAP )
    (void)job;
    return 0;
#else
    return new threading::JobThread(new MapLoadingThreadJob(job), "MapLoading", 30 * 60 * 1000);
#endif
  }

//////////////////////////////////////////////////////////////////////////
}

NI_DEFINE_REFCOUNT(NWorld::MapLoadingController);
NI_DEFINE_REFCOUNT(NWorld::MapLoadingThreadJob);
