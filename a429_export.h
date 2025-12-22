#ifndef A429_EXPORT_H
#define A429_EXPORT_H

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef A429_BUILD_DLL
    #ifdef __GNUC__
      #define A429_API __attribute__ ((dllexport))
    #else
      #define A429_API __declspec(dllexport)
    #endif
  #else
    #define A429_API __declspec(dllimport)
  #endif
#else
  #if __GNUC__ >= 4
    #define A429_API __attribute__ ((visibility ("default")))
  #else
    #define A429_API
  #endif
#endif

#endif // A429_EXPORT_H