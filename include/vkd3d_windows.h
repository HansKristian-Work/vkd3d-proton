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
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr)    ((HRESULT)(hr) < 0)

#define _HRESULT_TYPEDEF_(x) ((HRESULT)x)

#define S_OK    _HRESULT_TYPEDEF_(0)
#define S_FALSE _HRESULT_TYPEDEF_(1)

#define E_NOTIMPL     _HRESULT_TYPEDEF_(0x80004001)
#define E_NOINTERFACE _HRESULT_TYPEDEF_(0x80004002)
#define E_POINTER     _HRESULT_TYPEDEF_(0x80004003)
#define E_ABORT       _HRESULT_TYPEDEF_(0x80004004)
#define E_FAIL        _HRESULT_TYPEDEF_(0x80004005)
#define E_OUTOFMEMORY _HRESULT_TYPEDEF_(0x8007000E)
#define E_INVALIDARG  _HRESULT_TYPEDEF_(0x80070057)

/* Basic types */
typedef unsigned char BYTE;
typedef unsigned int DWORD;
typedef int INT;
typedef unsigned int UINT;
typedef int LONG;
typedef unsigned int ULONG;
typedef float FLOAT;
typedef LONG BOOL;

# ifndef __WIDL__
#  include <stdint.h>

typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int64_t INT64;
typedef uint64_t UINT64;

typedef uintptr_t ULONG_PTR;
typedef intptr_t LONG_PTR;
# else
typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;

typedef unsigned long UINT64;
typedef long INT64;

typedef long LONG_PTR;
typedef unsigned long ULONG_PTR;
# endif

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

#endif  /* __VKD3D_WINDOWS_H */
