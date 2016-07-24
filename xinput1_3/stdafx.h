#pragma once

#include <WinSDKVer.h>

#define _WIN32_WINNT _WIN32_WINNT_WIN8

#include <SDKDDKVer.h>

#define DIRECTINPUT_VERSION 0x0800

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>

#define WIN32_LEAN_AND_MEAN
#define STRICT
#define NOMINMAX

#include <windows.h>
#include <process.h>
#include <xinput.h>
#include <dinput.h>
#include <dbt.h>

#include "utils.h"