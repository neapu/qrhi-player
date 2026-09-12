#pragma once

#ifdef _WIN32
#ifdef CONTROLLER_LIBRARY
#define CONTROLLER_EXPORT __declspec(dllexport)
#else
#define CONTROLLER_EXPORT __declspec(dllimport)
#endif
#else
#define CONTROLLER_EXPORT
#endif
