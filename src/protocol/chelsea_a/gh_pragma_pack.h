/**
 ****************************************************************************************
 *
 * @file    gh_pragma_pack.h
 * @author  GOODIX GH Driver Team
 * @brief   Header file containing pragma pack macros for cross-platform compatibility.
 *
 ****************************************************************************************
 * @attention
  #####Copyright (c) 2024 GOODIX
  All rights reserved.
 ****************************************************************************************
 */

#ifndef __GH_PRAGMA_PACK_H__
#define __GH_PRAGMA_PACK_H__

#if defined(_MSC_VER)
    // Microsoft Visual C++
    #define GH_PACK_BEGIN __pragma(pack(push, 1))
    #define GH_PACK_END   __pragma(pack(pop))
    #define GH_PACKED
#elif defined(__GNUC__) || defined(__clang__)
    // GCC or Clang
    #define GH_PACK_BEGIN
    #define GH_PACK_END
    #define GH_PACKED __attribute__((packed))
#else
    // Other compilers - fallback
    #define GH_PACK_BEGIN
    #define GH_PACK_END
    #define GH_PACKED
#endif

#endif /* __GH_PRAGMA_PACK_H__ */