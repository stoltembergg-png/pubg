#pragma once
#include <Windows.h>

namespace DiscordHijack {
	inline HWND FindDiscordOverlay() {
		// Find Discord's overlay window by class and name (legacy approach)
		HWND discordHwnd = FindWindowA("Chrome_WidgetWin_1", "Discord Overlay");
		
		if (discordHwnd) {
			// Make sure it's visible and updated
			UpdateWindow(discordHwnd);
			ShowWindow(discordHwnd, SW_SHOW);
		}

		return discordHwnd;
	}
}
