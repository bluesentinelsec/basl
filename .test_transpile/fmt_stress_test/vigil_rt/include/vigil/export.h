#ifndef VIGIL_EXPORT_H
#define VIGIL_EXPORT_H

#ifdef _WIN32
#if defined(VIGIL_SHARED)
#if defined(VIGIL_EXPORTS)
#define VIGIL_API __declspec(dllexport)
#else
#define VIGIL_API __declspec(dllimport)
#endif
#else
#define VIGIL_API
#endif
#else
#define VIGIL_API
#endif

#endif
