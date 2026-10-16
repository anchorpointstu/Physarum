/* Single source of truth for the plugin identity, version and global flags.
   Included by both PhysarumPiPL.r and the C++ code; Physarum.cpp static_asserts
   that the hex flag values match the SDK enum expressions. Plain C preprocessor only. */

#ifndef PHYSARUM_VERSION_H
#define PHYSARUM_VERSION_H

#define PHYSARUM_NAME "Physarum"
#define PHYSARUM_MATCH_NAME "APS Physarum"
#define PHYSARUM_CATEGORY "Simulation"
#define PHYSARUM_SUPPORT_URL "https://anchorpoint.studio"

#define PHYSARUM_MAJOR 1
#define PHYSARUM_MINOR 0
#define PHYSARUM_BUG 0
#define PHYSARUM_BUILD 1

/* PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1) */
#define PHYSARUM_PIPL_VERSION 524289

/* PF_OutFlag_WIDE_TIME_INPUT | PF_OutFlag_NON_PARAM_VARY | PF_OutFlag_DEEP_COLOR_AWARE |
   PF_OutFlag_SEND_UPDATE_PARAMS_UI */
#define PHYSARUM_OUTFLAGS 0x06000006

/* PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG | PF_OutFlag2_SUPPORTS_SMART_RENDER |
   PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT |
   PF_OutFlag2_I_MIX_GUID_DEPENDENCIES */
#define PHYSARUM_OUTFLAGS2 0x00221408

#endif
