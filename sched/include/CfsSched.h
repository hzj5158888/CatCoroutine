//
// Created by hzj on 25-1-11.
//
#pragma once

// 防止头文件循环引用

namespace co {
	enum CO_PRIORITY
	{
		PRIORITY_LOWEST = 19,
		PRIORITY_BACKGROUND = 10,
		PRIORITY_NORMAL = 0,
		PRIORITY_HIGH = -6,
		PRIORITY_VERY_HIGH = -10,
		PRIORITY_URGENT = -16,
		PRIORITY_REAL_TIME = -19,
		PRIORITY_HIGHEST = -20,
	};
}
