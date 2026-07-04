#pragma once
#include "stdafx.h"
#include "infra.h"
namespace mod {
	namespace counters {
		void LoadMapData();
		void InitMapStats();
		void StatSuccess(int event_type, int count, bool is_new);
		int  GetCategoryCurrent(int category); // cumulative collected (0..CategoryCount-1)
		int  GetCategoryMax(int category);     // cumulative available
	}
}