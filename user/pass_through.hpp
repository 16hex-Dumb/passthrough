#pragma once
#include <Windows.h>
namespace PassThrough
{
	namespace Driver
	{
		bool load();
		bool open_device_handle();
		bool unload();
	}

	bool initialize(HWND hwnd);
	bool start();
	bool stop();
}