#pragma once
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <algorithm> 
#include <string>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <fstream>
#include <ppltasks.h>
#include <windowsx.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <TlHelp32.h>
#include <thread>
#include <filesystem>
#include <cctype>    
#include <iomanip>
#include <random>
#include <sstream>
#include <locale>
#include <cstdint>
#include <d2d1_1.h>
#include <dwrite.h>

#ifdef DrawText
#undef DrawText
#endif
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#ifdef GetObject
#undef GetObject
#endif
#ifdef SendMessage
#undef SendMessage
#endif
#ifdef GetUserName
#undef GetUserName
#endif
#ifdef CreateFont
#undef CreateFont
#endif
#ifdef Button
#undef Button
#endif
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
// Unified Persistant Logging (Implementation in Main.cpp)
extern void BaseLog(const char* fmt, ...);
#define LOG(fmt, ...) BaseLog(fmt, ##__VA_ARGS__)
#define DEBUG_INFO
#include "json.hpp"
using json = nlohmann::json;
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "Dwrite")
#pragma comment(lib, "windowscodecs.lib")
#include "Vector.h"
#include "../Colour.h"

#include "XorStr.h"
#include "Memory.h"
#include "CheatFunction.h"