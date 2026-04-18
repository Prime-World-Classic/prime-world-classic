#include "stdafx.h"

#if defined(PW_LINUX_NULL_RENDER)

#include <algorithm>

#include "renderer.h"
#include "batch.h"
#include "DXManager.h"

namespace Render
{

namespace
{

IBatchQueueLogger* s_pLogger = 0;
int s_materialSwitchCounter = 0;

struct CompareFarToNear
{
  static bool Less(const Batch* lhs, const Batch* rhs)
  {
    if (lhs->sortValue != rhs->sortValue)
    {
      return lhs->sortValue > rhs->sortValue;
    }

    return lhs->pMaterial->GetSubPriority() > rhs->pMaterial->GetSubPriority();
  }

  static void BatchProcess(Batch& batch)
  {
    batch.GetSortId();
  }
};

struct CompareSortId
{
  static bool Less(const Batch* lhs, const Batch* rhs)
  {
    const UINT lhsSortId = lhs->GetSortId();
    const UINT rhsSortId = rhs->GetSortId();

    if (lhsSortId == rhsSortId)
    {
      return lhs->pPrimitive < rhs->pPrimitive;
    }

    return lhsSortId < rhsSortId;
  }

  static void BatchProcess(Batch& batch)
  {
    batch.GetSortId();
  }
};

} // namespace

void Batch::SetWaterLevel(bool, float)
{
}

void Batch::Prepare() const
{
  if (!pMaterial)
  {
    return;
  }

  pMaterial->PrepareRenderer();
  ++s_materialSwitchCounter;
}

void Batch::Draw() const
{
  if (dontDraw)
  {
    return;
  }

  if (pMaterial)
  {
    pMaterial->PrepareRendererWithBatch(*this);
  }

  if (pOwner)
  {
    pOwner->PrepareRendererAfterMaterial(elementNumber);
  }

  if (pPrimitive)
  {
    pPrimitive->Bind();
    pPrimitive->Draw();
  }

  if (s_pLogger)
  {
    (*s_pLogger)(*this);
  }
}

void Batch::IssueQueryUP()
{
  dontDraw = false;
}

void Batch::IssueQuery()
{
  dontDraw = false;
}

void BatchQueue::SetRenderLogger(IBatchQueueLogger* pLogger)
{
  s_pLogger = pLogger;
}

bool BatchQueue::IsLoggingActive()
{
  return s_pLogger != 0;
}

BatchQueue::BatchQueue(Index ndx, BatchQueueSorter& sorter_, int maxSize)
  : elements()
  , sorter(sorter_)
  , priorities()
  , curSortingValue(0.0f)
  , pCurSHConsts(0)
  , index(ndx)
  , resourceManager(0)
{
  elements.reserve(maxSize);

  for (int i = 0; i < NDb::MATERIALPRIORITY_COUNT; ++i)
  {
    priorities[i].bSort = false;
    priorities[i].numBatches = 0;
    priorities[i].pBatchList = 0;
  }
}

BatchQueue::~BatchQueue()
{
}

void BatchQueue::EnableResourceManagment(bool enable)
{
  if (enable && !resourceManager.get())
  {
    resourceManager = std::auto_ptr<DXManager>(new DXManager());
    return;
  }

  if (!enable && resourceManager.get())
  {
    resourceManager = std::auto_ptr<DXManager>(0);
  }
}

bool BatchQueue::OnNextFrame()
{
  if (!resourceManager.get())
  {
    return false;
  }

  resourceManager->OnNextFrame();
  return true;
}

bool BatchQueue::ManageResources()
{
  if (!resourceManager.get())
  {
    return false;
  }

  resourceManager->Manage();
  return true;
}

void BatchQueue::SetSorting(int priority, bool sorting)
{
  ASSERT(0 <= priority && priority < NDb::MATERIALPRIORITY_COUNT);
  priorities[priority].bSort = sorting;
}

void BatchQueue::Clear()
{
  elements.resize(0);

  for (int i = 0; i < NDb::MATERIALPRIORITY_COUNT; ++i)
  {
    priorities[i].numBatches = 0;
    priorities[i].pBatchList = 0;
  }
}

void BatchQueue::Push(int priority,
                      const RenderComponent* pOwner,
                      const IRenderablePrimitive* pPrimitive,
                      unsigned int elementNumber,
                      BaseMaterial* pMaterial)
{
  if (pMaterial->GetAlternativeMaterial())
  {
    BaseMaterial* alternativeMaterial = static_cast<BaseMaterial*>(pMaterial->GetAlternativeMaterial());
    const int alternativePriority = pMaterial->IsAltPriorityEnabled() ? alternativeMaterial->GetAltPriority() : alternativeMaterial->GetPriority();
    PushInternal(alternativePriority, pOwner, pPrimitive, elementNumber, alternativeMaterial);

    if (pMaterial->IsAltPriorityEnabled() && alternativeMaterial->IsFadingAlternativeMaterial())
    {
      PushInternal(alternativeMaterial->GetBasePriority(), pOwner, pPrimitive, elementNumber, pMaterial);
    }

    return;
  }

  PushInternal(priority, pOwner, pPrimitive, elementNumber, pMaterial);
}

void BatchQueue::PushInternal(int priority,
                              const RenderComponent* pOwner,
                              const IRenderablePrimitive* pPrimitive,
                              unsigned int elementNumber,
                              BaseMaterial* pMaterial)
{
  ASSERT(0 <= priority && priority < NDb::MATERIALPRIORITY_COUNT);

  Batch& batch = elements.push_back();
  batch.pOwner = pOwner;
  batch.elementNumber = elementNumber;
  batch.pPrimitive = pPrimitive;
  batch.pMaterial = pMaterial;
  batch.sortValue = curSortingValue;
  batch.pSHConsts = pCurSHConsts;
  batch.dontDraw = false;
  batch.isInside = false;

  Priority& batchList = priorities[priority];
  batch.pNextBatch = batchList.pBatchList;
  batchList.pBatchList = &batch;
  ++batchList.numBatches;

  if (resourceManager.get())
  {
    resourceManager->AddGeometry(pPrimitive->GetBuffers());
    resourceManager->AddTextures(pMaterial);
  }
}

void BatchQueue::ReplaceMaterial(int priority, BaseMaterial* pMaterial)
{
  for (Batch* pBatch = priorities[priority].pBatchList; pBatch; pBatch = pBatch->pNextBatch)
  {
    pBatch->pMaterial = pMaterial;
  }
}

void BatchQueue::ReplaceMaterial(int beginPriority, int endPriority, BaseMaterial* pMaterial)
{
  ASSERT(0 <= beginPriority && beginPriority < endPriority && endPriority <= NDb::MATERIALPRIORITY_COUNT);

  for (int priority = beginPriority; priority < endPriority; ++priority)
  {
    ReplaceMaterial(priority, pMaterial);
  }
}

void BatchQueue::RenderBatchesPtrArray(UINT numBatches, Batch** pBatches)
{
  if (!pBatches)
  {
    return;
  }

  UINT currentSortId = 0;

  for (UINT i = 0; i < numBatches; ++i)
  {
    Batch& batch = *pBatches[i];
    const UINT sortId = batch.GetSortId();

    if ((sortId != currentSortId) || EqualIDs(sortId, 0))
    {
      batch.Prepare();
      currentSortId = sortId;
    }

    batch.Draw();
  }
}

void BatchQueue::RenderBatchesList(Batch* pBatch)
{
  UINT currentSortId = 0;

  for (; pBatch; pBatch = pBatch->pNextBatch)
  {
    const UINT sortId = pBatch->GetSortId();

    if ((sortId != currentSortId) || EqualIDs(sortId, 0))
    {
      pBatch->Prepare();
      currentSortId = sortId;
    }

    pBatch->Draw();
  }
}

void BatchQueue::Render_SpecialTransparency(int priority) const
{
  Render(priority);
}

void BatchQueue::Render(int priority) const
{
  const int numBatches = priorities[priority].numBatches;
  if (!numBatches)
  {
    return;
  }

  if (!priorities[priority].bSort)
  {
    RenderBatchesList(priorities[priority].pBatchList);
    return;
  }

  int numSortedBatches = 0;
  Batch** pSortedArray = sorter.Sort<CompareFarToNear>(numBatches, priorities[priority].pBatchList, numSortedBatches);
  RenderBatchesPtrArray(numSortedBatches, pSortedArray);
}

void BatchQueue::Render(int beginPriority, int endPriority) const
{
  ASSERT(0 <= beginPriority && beginPriority < endPriority && endPriority <= NDb::MATERIALPRIORITY_COUNT);

  for (int priority = beginPriority; priority < endPriority; ++priority)
  {
    Render(priority);
  }
}

void BatchQueue::DropMaterialSwitchCounter()
{
  s_materialSwitchCounter = 0;
}

int BatchQueue::GetMaterialSwitchCounter()
{
  return s_materialSwitchCounter;
}

template <class CMP_POLICY>
Batch** BatchQueueSorter::Sort(int numBatches, Batch* pList, int& numBatchesOut)
{
  batchPtrs.resize(0);
  batchPtrs.reserve(numBatches);

  for (Batch* pBatch = pList; pBatch; pBatch = pBatch->pNextBatch)
  {
    CMP_POLICY::BatchProcess(*pBatch);
    batchPtrs.push_back(pBatch);
  }

  if (!simpleCopy)
  {
    std::stable_sort(batchPtrs.begin(), batchPtrs.end(), CMP_POLICY::Less);
  }

  numBatchesOut = batchPtrs.size();
  return numBatchesOut ? &batchPtrs[0] : 0;
}

template Batch** BatchQueueSorter::Sort<CompareFarToNear>(int, Batch*, int&);
template Batch** BatchQueueSorter::Sort<CompareSortId>(int, Batch*, int&);

} // namespace Render

#endif
