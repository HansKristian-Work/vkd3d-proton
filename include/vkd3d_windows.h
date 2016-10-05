/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __VKD3D_WINDOWS_H
#define __VKD3D_WINDOWS_H

#if !defined(_WIN32) || defined(__WIDL__)

/* HRESULT */
typedef int HRESULT;
# define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
# define FAILED(hr)    ((HRESULT)(hr) < 0)

# define _HRESULT_TYPEDEF_(x) ((HRESULT)x)

# define S_OK    _HRESULT_TYPEDEF_(0)
# define S_FALSE _HRESULT_TYPEDEF_(1)

# define E_NOTIMPL     _HRESULT_TYPEDEF_(0x80004001)
# define E_NOINTERFACE _HRESULT_TYPEDEF_(0x80004002)
# define E_POINTER     _HRESULT_TYPEDEF_(0x80004003)
# define E_ABORT       _HRESULT_TYPEDEF_(0x80004004)
# define E_FAIL        _HRESULT_TYPEDEF_(0x80004005)
# define E_OUTOFMEMORY _HRESULT_TYPEDEF_(0x8007000E)
# define E_INVALIDARG  _HRESULT_TYPEDEF_(0x80070057)

/* Basic types */
typedef unsigned char BYTE;
typedef unsigned int DWORD;
typedef int INT;
typedef unsigned int UINT;
typedef int LONG;
typedef unsigned int ULONG;
typedef float FLOAT;
typedef LONG BOOL;

/* Assuming LP64 model */
typedef char INT8;
typedef unsigned char UINT8;
typedef short INT16;
typedef unsigned short UINT16;
typedef int INT32;
typedef unsigned int UINT32;
# if defined(__x86_64__) || defined(__WIDL__)
typedef long INT64;
typedef unsigned long UINT64;
# else
typedef long long INT64;
typedef unsigned long long UINT64;
# endif
typedef long LONG_PTR;
typedef unsigned long ULONG_PTR;

typedef ULONG_PTR SIZE_T;

typedef unsigned short WCHAR;
typedef void *HANDLE;

/* GUID */
# ifdef __WIDL__
typedef struct
{
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
# else
typedef struct _GUID
{
    unsigned int Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
# endif

typedef GUID IID;

# ifdef INITGUID
#  ifndef __cplusplus
#   define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        const GUID name DECLSPEC_HIDDEN; \
        const GUID name = \
    { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 }}
#  else
#   define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID name DECLSPEC_HIDDEN; \
        EXTERN_C const GUID name = \
    { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 }}
#  endif
# else
#  define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID name DECLSPEC_HIDDEN;
# endif /* INITGUID */

typedef struct SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;
#endif  /* !defined(_WIN32) || defined(__WIDL__) */


#ifndef _WIN32
# include <stddef.h>
# include <stdlib.h>
# include <string.h>

# define COM_NO_WINDOWS_H

# define FORCEINLINE inline

# define CONTAINING_RECORD(address, type, field) \
        ((type *)((char *)(address) - offsetof(type, field)))

# ifdef __x86_64__
#  define WINAPI __attribute__((ms_abi))
#  define STDMETHODCALLTYPE __attribute__((ms_abi))
# else
#  define WINAPI __attribute__((__stdcall__))
#  define STDMETHODCALLTYPE __attribute__((__stdcall__))
# endif

# ifdef __GNUC__
#  define DECLSPEC_SELECTANY __attribute__((weak))
# endif

/* Macros for COM interfaces */
# define interface struct
# define BEGIN_INTERFACE
# define END_INTERFACE

# ifdef __cplusplus
#  define EXTERN_C extern "C"
# else
#  define EXTERN_C extern
# endif

# define CONST_VTBL const

# define TRUE 1
# define FALSE 0

# if defined(__cplusplus) && !defined(CINTERFACE)
#  define REFIID const IID &
#  define REFGUID const GUID &
# else
#  define REFIID const IID * const
#  define REFGUID const GUID * const
# endif

#if defined(__cplusplus) && !defined(CINTERFACE)
# define IsEqualGUID(guid1, guid2) (!memcmp(&(guid1), &(guid2), sizeof(GUID)))
#else
# define IsEqualGUID(guid1, guid2) (!memcmp(guid1, guid2, sizeof(GUID)))
#endif

#define WAIT_OBJECT_0 (0)
#define WAIT_TIMEOUT (1)
#define WAIT_FAILED (~0u)
#define INFINITE (~0u)

HANDLE WINAPI VKD3DCreateEvent(void);
BOOL WINAPI VKD3DSignalEvent(HANDLE event);
unsigned int WINAPI VKD3DWaitEvent(HANDLE event, unsigned int milliseconds);
void WINAPI VKD3DDestroyEvent(HANDLE event);

#elif !defined(__WIDL__)

# include <windows.h>

#endif  /* _WIN32 */


/* Nameless unions */
#ifndef __C89_NAMELESS
# ifdef NONAMELESSUNION
#  define __C89_NAMELESS
#  define __C89_NAMELESSUNIONNAME u
# else
#  define __C89_NAMELESS
#  define __C89_NAMELESSUNIONNAME
# endif /* NONAMELESSUNION */
#endif  /* __C89_NAMELESS */

/* Define DECLSPEC_HIDDEN */
#ifndef DECLSPEC_HIDDEN
# if defined(__MINGW32__)
#  define DECLSPEC_HIDDEN
# elif defined(__GNUC__)
#  define DECLSPEC_HIDDEN __attribute__((visibility("hidden")))
# else
#  define DECLSPEC_HIDDEN
# endif
#endif  /* DECLSPEC_HIDDEN */

/* Define min() & max() macros */
#ifndef min
# define min(a, b) (((a) <= (b)) ? (a) : (b))
#endif

#ifndef max
# define max(a, b) (((a) >= (b)) ? (a) : (b))
#endif

#endif  /* __VKD3D_WINDOWS_H */
