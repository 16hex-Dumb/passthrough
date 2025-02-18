#include "pass_through.hpp"
#include "loadup.hpp"
#include "../common.hpp"

#define DRIVER_NAME "WriteMemoryDriver"
#define DRIVER_FILE_NAME "WriteMemoryDriver.sys"

namespace PassThrough
{
	inline bool operator==(const RECT& l, const RECT& r)
	{
		return l.left == r.left && l.top == r.top &&
			l.right == r.right && l.bottom == r.bottom;
	}

	namespace Driver
	{
		HANDLE device_handle{};

		bool unload()
		{
			if (device_handle && device_handle != INVALID_HANDLE_VALUE)
			{
				CloseHandle(device_handle);
				device_handle = nullptr;
			}
			NTSTATUS status = driver::unload(DRIVER_NAME);
			if (!NT_SUCCESS(status))
			{
				std::printf("failed to unload driver:%0x\n", status);
				return false;
			}
			return true;
		}

		bool load()
		{
			char path[MAX_PATH]{};
			GetModuleFileNameA(nullptr, path, MAX_PATH);
			char* last_slash = strrchr(path, '\\');
			if (!last_slash)
				return false;
			*last_slash = '\0';

			std::string driver_path = path;
			driver_path += "\\" DRIVER_FILE_NAME;

			NTSTATUS status = driver::load(driver_path, DRIVER_NAME);
			if (status == STATUS_IMAGE_ALREADY_LOADED)
			{
				unload();
				SwitchToThread();
				return load();
			}
			if (!NT_SUCCESS(status))
			{
				std::printf("failed to load driver:%0x\n", status);
				return false;
			}
			return true;
		}

		bool open_device_handle()
		{
			device_handle = CreateFileA("\\\\.\\" DRIVER_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if (device_handle == INVALID_HANDLE_VALUE)
			{
				std::printf("failed to open device handle\n");
				return false;
			}
			std::printf("device handle:%llx\n", device_handle);
			return true;
		}

		bool force_write_memory(const void* addr, const void* data, size_t size)
		{
			WriteRequest req{ reinterpret_cast<uint64_t>(addr), reinterpret_cast<uint64_t>(data), static_cast<uint32_t>(size) };
			NTSTATUS status{};
			DeviceIoControl(device_handle, FoceWriteMemoryCTL, &req, sizeof(req), &status, sizeof(status), nullptr, nullptr);
			return NT_SUCCESS(status);
		}
	}

	using ValidateHwnd_t = uintptr_t(WINAPI*)(HWND);
	ValidateHwnd_t validate_hwnd{};
	bool find_valid_hwnd_func()
	{
		auto start = reinterpret_cast<uint8_t*>(&IsChild);
		auto end = start + 256;
		for (auto i = start; i < end; i++)
		{
			// call    ?ValidateHwnd@@YAPEAUtagWND@@PEAUHWND__@@@Z ;
			if (*i == 0xE8)
			{
				validate_hwnd = reinterpret_cast<ValidateHwnd_t>(i + *reinterpret_cast<int32_t*>(i + 1) + 5);
				return true;
			}
		}
		return false;
	}
	
	std::uintptr_t find_hrgn_clip_pointer(HWND hwnd)
	{
		RECT window_rect{};
		GetWindowRect(hwnd, &window_rect);
		std::printf("rc %d %d %d %d\n", window_rect.left, window_rect.top, window_rect.right, window_rect.bottom);

		auto pwnd = validate_hwnd(hwnd);
		std::printf("pwnd:%llx\n", pwnd);

		for (size_t i = 0; i < 512; i++)
		{
			HRGN hrgn_clip = *reinterpret_cast<HRGN*>(pwnd + i);
			RECT clip_rc;
			if (!hrgn_clip || !GetRgnBox(hrgn_clip, &clip_rc))
				continue;
			std::printf("rgn %0x: %d %d %d %d\n", i, clip_rc.left, clip_rc.top, clip_rc.right, clip_rc.bottom);
			if (window_rect == clip_rc)
			{
				return pwnd + i;
			}
		}
		return -1;
	}

	uintptr_t hrgn_clip_ptr{};
	uintptr_t original_hrgn_clip{};
	bool initialize(HWND hwnd)
	{
		if(!find_valid_hwnd_func())
		{
			std::printf("failed to find ValidateHwnd function\n");
			return false;
		}
		hrgn_clip_ptr = find_hrgn_clip_pointer(hwnd);
		if (hrgn_clip_ptr == -1)
		{
			std::printf("failed to find hrgn clip pointer\n");
			return false;
		}
		original_hrgn_clip = hrgn_clip_ptr;
		std::printf("hrgn_clip_ptr:%llx\n", hrgn_clip_ptr);
		return true;
	}

	bool start()
	{
		HRGN fake_clip = reinterpret_cast<HRGN>(-1);
		WriteRequest req{ hrgn_clip_ptr, reinterpret_cast<uint64_t>(&fake_clip), sizeof(fake_clip) };

		NTSTATUS status{};
		DeviceIoControl(Driver::device_handle, FoceWriteMemoryCTL, &req, sizeof(WriteRequest), &status, sizeof(status), nullptr, nullptr);
		return NT_SUCCESS(status);
	}

	bool stop()
	{
		WriteRequest req{ hrgn_clip_ptr, reinterpret_cast<uint64_t>(&original_hrgn_clip), sizeof(original_hrgn_clip) };
		NTSTATUS status{};
		DeviceIoControl(Driver::device_handle, FoceWriteMemoryCTL, &req, sizeof(WriteRequest), &status, sizeof(status), nullptr, nullptr);
		return NT_SUCCESS(status);
	}
}