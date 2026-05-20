#pragma once

#include "DeviceLost.h"

#if defined(PW_LINUX_DB_BOOTSTRAP) || defined(PW_LINUX_NULL_RENDER)

namespace Render
{

struct OcclusionQuery
{
  enum
  {
    UNUSED,
    ISSUEDBEGIN,
    ISSUEDEND,
  } state;

  OcclusionQuery(void)
    : state(UNUSED)
  {
  }
};

class OcclusionQueriesBank
{
public:
  enum CameraID
  {
    CID_MAIN,
    CID_SHADOW,
    CID_WATER,
  } cameraID;

  OcclusionQueriesBank(void)
    : cameraID(CID_MAIN)
  {
  }

  ~OcclusionQueriesBank(void)
  {
  }

  int BeginNextQuery() { return 0; }
  int EndNextQuery() { return 0; }
  int GetLatestResults() { return -1; }

  static void OnFrameStart() {}
};

class OcclusionQueries : public DeviceLostHandler
{
  OcclusionQueriesBank bank;

protected:
  OcclusionQueries() {}

public:
  OcclusionQueriesBank& Get() { return bank; }
  void Clear() {}

  virtual void OnDeviceLost() {}
  virtual void OnDeviceReset() {}

  enum UseMode
  {
    QUM_NONE = 0,
    QUM_CHECK = 1,
    QUM_ISSUE = QUM_CHECK<<1,
    QUM_CHECK_AND_ISSUE = QUM_CHECK | QUM_ISSUE
  };

  static void SetUseMode(UseMode) {}
  static UseMode GetUseMode() { return QUM_NONE; }
  static void SetCurrentCameraID(OcclusionQueriesBank::CameraID) {}
};

}

#else

#include <d3d9.h>
#include "DxIntrusivePtr.h"

namespace Render
{

struct OcclusionQuery
{
  DXQueryRef query;
  enum 
  {
    UNUSED,
    ISSUEDBEGIN,
    ISSUEDEND,
  } state;
public:
  OcclusionQuery(void) : state(UNUSED) {}
};


class OcclusionQueriesBank
{
  StaticArray<OcclusionQuery, 2> queries;
  UINT head;
  UINT lastResult, lastFinishedQuery;
  UINT currentFrame;

  static UINT s_currentFrame;

public:
  enum CameraID
  {
    CID_MAIN,
    CID_SHADOW,
    CID_WATER,
  } cameraID;

  OcclusionQueriesBank(void);
  ~OcclusionQueriesBank(void);

  HRESULT BeginNextQuery();
  HRESULT EndNextQuery();
  int GetLatestResults();

  static void OnFrameStart() { ++s_currentFrame; }
};


class OcclusionQueries : public DeviceLostHandler
{
  typedef vector<OcclusionQueriesBank> VQueries;
  VQueries banks;

protected:
  OcclusionQueries() {}

public:
  // Get (or create) OcclusionQueriesBank for desired camera.
  OcclusionQueriesBank& Get();
  void Clear() { banks.clear(); }

  virtual void OnDeviceLost()  { Clear(); }
  virtual void OnDeviceReset() {}

  enum UseMode
  {
    QUM_NONE = 0,
    QUM_CHECK = 1,
    QUM_ISSUE = QUM_CHECK<<1,
    QUM_CHECK_AND_ISSUE = QUM_CHECK | QUM_ISSUE
  };

  static void SetUseMode(UseMode _useMode);
  static UseMode GetUseMode();

  //static UseMode s_useMode;
  static void SetCurrentCameraID(OcclusionQueriesBank::CameraID _id);
};

};

#endif
