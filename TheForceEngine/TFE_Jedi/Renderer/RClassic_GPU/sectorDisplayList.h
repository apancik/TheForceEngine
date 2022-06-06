#pragma once
//////////////////////////////////////////////////////////////////////
// Display list for rendering sector geometry
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_System/memoryPool.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include "sbuffer.h"

namespace TFE_Jedi
{
	void sdisplayList_init(s32 posIndex, s32 dataIndex, s32 planesIndex);
	void sdisplayList_destroy();

	void sdisplayList_clear();
	void sdisplayList_finish();

	void sdisplayList_addCaps(RSector* curSector);
	void sdisplayList_addSegment(RSector* curSector, SegmentClipped* wallSeg);
	void sdisplayList_addPortal(Vec3f p0, Vec3f p1);
	void sdisplayList_draw();

	s32  sdisplayList_getSize();
}  // TFE_Jedi
