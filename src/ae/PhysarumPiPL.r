#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "PhysarumVersion.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif

resource 'PiPL' (16000) {
	{
		Kind {
			AEEffect
		},
		Name {
			PHYSARUM_NAME
		},
		Category {
			PHYSARUM_CATEGORY
		},
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #endif
#endif
		AE_PiPL_Version {
			2,
			0
		},
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		AE_Effect_Version {
			PHYSARUM_PIPL_VERSION
		},
		AE_Effect_Info_Flags {
			0
		},
		AE_Effect_Global_OutFlags {
			PHYSARUM_OUTFLAGS
		},
		AE_Effect_Global_OutFlags_2 {
			PHYSARUM_OUTFLAGS2
		},
		AE_Effect_Match_Name {
			PHYSARUM_MATCH_NAME
		},
		AE_Reserved_Info {
			0
		},
		AE_Effect_Support_URL {
			PHYSARUM_SUPPORT_URL
		}
	}
};
