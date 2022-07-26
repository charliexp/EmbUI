// This framework originaly based on JeeUI2 lib used under MIT License Copyright (c) 2019 Marsel Akhkamov
// then re-written and named by (c) 2020 Anton Zolotarev (obliterator) (https://github.com/anton-zolotarev)
// also many thanks to Vortigont (https://github.com/vortigont), kDn (https://github.com/DmytroKorniienko)
// and others people

#pragma once

// Global macro's and framework libs
#include <Arduino.h>
#include "constants.h"

// STRING Macro
#ifndef __STRINGIFY
 #define __STRINGIFY(a) #a
#endif
#define TOSTRING(x) __STRINGIFY(x)

// LOG macro's
#if defined(EMBUI_DEBUG)
  #ifndef EMBUI_DEBUG_PORT
    #define EMBUI_DEBUG_PORT Serial
  #endif

  #define LOG(func, ...) EMBUI_DEBUG_PORT.func(__VA_ARGS__)
  #define LOG_CALL(call...) { call; }
#else
  #define LOG(func, ...) ;
  #define LOG_CALL(call...) ;
#endif

typedef std::function<void(void)> callback_function_t;