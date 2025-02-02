/*
 * Copyright (c) 2016-2023, National Institute of Information and Communications
 * Technology (NICT). All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the NICT nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NICT AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE NICT OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * cef_pit.h
 */

#ifndef __CEF_PIT_HEADER__
#define __CEF_PIT_HEADER__

/****************************************************************************************
 Include Files
 ****************************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>

#include <cefore/cef_hash.h>
#include <cefore/cef_define.h>
#include <cefore/cef_frame.h>

/****************************************************************************************
 Macros
 ****************************************************************************************/
#define CefC_PitEntryVersion_Max		2	/* Max number of versions that can be 		*/
											/* registered in 1 Down Face Entry (other than AnyVer) */

/****************************************************************************************
 Structure Declarations
 ****************************************************************************************/

/*------------------------------------------------------------------*/
/* Down Stream Face entry											*/
/*------------------------------------------------------------------*/
//0.8.3c S
/***** T_VERSION Information for PIT entry 	*****/
typedef struct CefT_Pit_Tversion {
	int 			tver_len;			/* T_VERSION Length					*/
	unsigned char*	tver_value;			/* T_VERSION Value    		 		*/
	struct CefT_Pit_Tversion* tvnext;	/* Next T_VERSION	 				*/
} CefT_Pit_Tversion;
//0.8.3c E

typedef struct CefT_Down_Faces {

	/*--------------------------------------------
		Variables related to Network
	----------------------------------------------*/
	uint16_t		faceid;					/* Face-ID 									*/
	uint64_t	 	lifetime_us;			/* Lifetime 								*/
	uint64_t		nonce;					/* Nonce 									*/
	struct CefT_Down_Faces* next;			/* pointer to next Down Stream Face entry 	*/
	int				tver_none;				/* No T_VERSION flag 0.8.3c */
	struct CefT_Pit_Tversion	tver;		/* T_VERSION List	 0.8.3c */

	/*--------------------------------------------
		Variables related to Content Store
	----------------------------------------------*/
	uint8_t			reply_f;				/* Reply Content Flag. To stop Interest	 	*/
	//0.8.3
	uint8_t				IR_Type;			/* InterestReturn Type 						*/
	unsigned int 		IR_len;				/* Length of IR_msg 						*/
	unsigned char* 		IR_msg;				/* InterestReturn msg 						*/

} CefT_Down_Faces;

/*------------------------------------------------------------------*/
/* Up Stream Face entry												*/
/*------------------------------------------------------------------*/

typedef struct CefT_Up_Faces {

	/*--------------------------------------------
		Variables related to Network
	----------------------------------------------*/
	uint16_t		faceid;					/* Face-ID 									*/
	struct CefT_Up_Faces* next;				/* pointer to next Up Stream Face entry 	*/

} CefT_Up_Faces;

/*------------------------------------------------------------------*/
/* PIT entry														*/
/*------------------------------------------------------------------*/

typedef struct {

	unsigned char* 		key;				/* Key of the PIT entry 					*/
	unsigned int 		klen;				/* Length of this key 						*/
	uint8_t				longlife_f;			/* set to not 0 if it shows Longlife PIT 	*/
	CefT_Down_Faces		dnfaces;			/* Down Stream Face entries 				*/
	unsigned int 		dnfacenum;			/* Number of Down Stream Face entries 		*/
	CefT_Up_Faces		upfaces;			/* Up Stream Face entry		 				*/
	uint8_t				stole_f;			/* sets to not 0 if it will be deleted	 	*/
	uint32_t 			hashv;				/* Hash value of this entry 				*/
	uint16_t 			tp_variant;			/* Transport Variant 						*/
	uint64_t	 		clean_us;			/* time to cleaning							*/
	CefT_Down_Faces		clean_dnfaces;		/* Down Stream Face entries to clean		*/
	uint64_t			nonce;				/* Nonce 									*/
	uint64_t 			adv_lifetime_us;	/* Advertised lifetime 						*/
	uint64_t 			drp_lifetime_us;
	//0.8.3
	uint8_t				hoplimit;			/* Hop Limit of Forwarding Interest 		*/
	int					PitType;			/* PitType									*/
	int64_t				Last_chunk_num;		/* Last Forward Object Chunk Number 		*/
	unsigned int 		KIDR_len;			/* KIDR_selector Len 						*/
	unsigned char* 		KIDR_selector;		/* KeyIdRestriction selector				*/
	unsigned int 		COBHR_len;			/* COBHR_selector Len 						*/
	unsigned char* 		COBHR_selector;		/* ContentObjectHashRestriction selector 	*/

#ifdef	CefC_PitEntryMutex
	pthread_mutex_t 	pe_mutex_pt;		/* mutex for thread safe for Pthread 		*/
#endif	// CefC_PitEntryMutex
} CefT_Pit_Entry;

/****************************************************************************************
 Global Variables
 ****************************************************************************************/



/****************************************************************************************
 Function Declarations
 ****************************************************************************************/

/*--------------------------------------------------------------------------------------
	Initialize the PIT module
----------------------------------------------------------------------------------------*/
void cef_pit_init (
	uint32_t reply_timeout,        /* PIT lifetime(seconds) at "full discovery request" */
	uint32_t symbolic_max_lt,       /* Symbolic Interest max Lifetime 0.8.3             */
	uint32_t regular_max_lt         /* Regular Interest max Lifetime 0.8.3              */
);
/*--------------------------------------------------------------------------------------
	Looks up and creates a PIT entry matching the specified Name
----------------------------------------------------------------------------------------*/
CefT_Pit_Entry* 							/* a PIT entry								*/
cef_pit_entry_lookup (
	CefT_Hash_Handle pit,					/* PIT										*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh					/* Parsed Option Header						*/
	, unsigned char* ccninfo_pit,			/* pit name for ccninfo ccninfo-03			*/
	int	ccninfo_pit_len						/* ccninfo pit length						*/
);
/*--------------------------------------------------------------------------------------
	Searches a PIT entry matching the specified Name
----------------------------------------------------------------------------------------*/
CefT_Pit_Entry* 							/* a PIT entry								*/
cef_pit_entry_search (
	CefT_Hash_Handle pit,					/* PIT										*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh					/* Parsed Option Header						*/
	, unsigned char* ccninfo_pit,			/* pit name for ccninfo ccninfo-03			*/
	int	ccninfo_pit_len						/* ccninfo pit length						*/
);
/*--------------------------------------------------------------------------------------
	Searches a PIT entry matching the specified Name
----------------------------------------------------------------------------------------*/
CefT_Pit_Entry* 							/* a PIT entry								*/
cef_pit_entry_search_specified_name (
	CefT_Hash_Handle pit,					/* PIT										*/
	unsigned char* sp_name,					/* specified Name							*/
	uint16_t sp_name_len,					/* length of Name							*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh,				/* Parsed Option Header						*/
	int match_type							/* 0:Exact, 1:Prefix						*/
);
/*--------------------------------------------------------------------------------------
	Searches a PIT(for App) entry matching the specified Name --- Prefix(Longest) Match
----------------------------------------------------------------------------------------*/
CefT_Pit_Entry* 							/* a PIT entry								*/
cef_pit_entry_search_specified_name_for_app (
	CefT_Hash_Handle pit,					/* PIT										*/
	unsigned char* sp_name,					/* specified Name							*/
	uint16_t sp_name_len,					/* length of Name							*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh					/* Parsed Option Header						*/
);

#ifdef CefC_Debug
void
cef_pit_entry_print (
	CefT_Hash_Handle pit					/* PIT										*/
);
#endif // CefC_Debug
/*--------------------------------------------------------------------------------------
	Looks up and creates the specified Down Face entry
----------------------------------------------------------------------------------------*/
int 										/* Returns 1 if the return entry is new	 	*/
cef_pit_entry_down_face_update (
	CefT_Pit_Entry* entry, 					/* PIT entry								*/
	uint16_t faceid,						/* Face-ID									*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh,				/* Parsed Option Header						*/
	unsigned char* msg,						/* cefore packet 							*/
	int		 Resend_method					/* Resend method 0.8.3						*/
);
/*--------------------------------------------------------------------------------------
	Looks up and creates the specified Up Face entry
----------------------------------------------------------------------------------------*/
int 										/* Returns 1 if the return entry is new	 	*/
cef_pit_entry_up_face_update (
	CefT_Pit_Entry* entry, 					/* PIT entry								*/
	uint16_t faceid,						/* Face-ID									*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh					/* Parsed Option Header						*/
);
/*--------------------------------------------------------------------------------------
	Free the specified PIT entry
----------------------------------------------------------------------------------------*/
void
cef_pit_entry_free (
	CefT_Hash_Handle pit,					/* PIT										*/
	CefT_Pit_Entry* entry 					/* PIT entry 								*/
);
#if 0
/*--------------------------------------------------------------------------------------
	Searches a PIT entry matching the specified Name for Cefore-Router
----------------------------------------------------------------------------------------*/
CefT_Pit_Entry* 							/* a PIT entry								*/
cefrt_pit_entry_search (
	CefT_Hash_Handle pit,					/* PIT										*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh					/* Parsed Option Header						*/
);
#endif
/*--------------------------------------------------------------------------------------
	Cleanups PIT entry which expires the lifetime
----------------------------------------------------------------------------------------*/
void
cef_pit_clean (
	CefT_Hash_Handle pit,					/* PIT										*/
	CefT_Pit_Entry* entry 					/* PIT entry 								*/
);
/*--------------------------------------------------------------------------------------
	Removes the specified FaceID from the specified PIT entry
----------------------------------------------------------------------------------------*/
void
cef_pit_down_faceid_remove (
	CefT_Pit_Entry* entry, 					/* PIT entry 								*/
	uint16_t faceid 						/* Face-ID									*/
);
//0.8.3
/*--------------------------------------------------------------------------------------
	Symbolic PIT Check
----------------------------------------------------------------------------------------*/
int
cef_pit_symbolic_pit_check (
	CefT_Hash_Handle pit,					/* PIT										*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh					/* Parsed Option Header						*/
);
/*--------------------------------------------------------------------------------------
	Searches a PIT entry matching the specified Name with chunk number
----------------------------------------------------------------------------------------*/
CefT_Pit_Entry* 							/* a PIT entry								*/
cef_pit_entry_search_with_chunk (
	CefT_Hash_Handle pit,					/* PIT										*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh					/* Parsed Option Header						*/
);
/*--------------------------------------------------------------------------------------
	Searches a PIT entry matching the specified Name without chunk number
----------------------------------------------------------------------------------------*/
CefT_Pit_Entry* 							/* a PIT entry								*/
cef_pit_entry_search_without_chunk (
	CefT_Hash_Handle pit,					/* PIT										*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh					/* Parsed Option Header						*/
);

/*--------------------------------------------------------------------------------------
	Set InterestReturn Info to DownFace
----------------------------------------------------------------------------------------*/
int
cef_pit_interest_return_set (
	CefT_Pit_Entry* entry, 					/* PIT entry 								*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh,				/* Parsed Option Header						*/
	uint16_t faceid, 						/* Face-ID									*/
	uint8_t				IR_Type,			/* InterestReturn Type 						*/
	unsigned int 		IR_len,				/* Length of IR_msg 						*/
	unsigned char* 		IR_msg				/* InterestReturn msg 						*/
) ;

/*--------------------------------------------------------------------------------------
	Search the version entry in specified Down Face entry
----------------------------------------------------------------------------------------*/
int											/* found entry = 1							*/
cef_pit_entry_down_face_ver_search (
	CefT_Down_Faces* dnface,				/* Down Face entry							*/
	int head_or_point_f,					/* 1: dnface is head of down face lest		*/
											/* 0: dnface is pointer of 1 entry			*/
	CefT_CcnMsg_MsgBdy* pm 					/* Parsed CEFORE message					*/
);
/*--------------------------------------------------------------------------------------
	Remove the version entry in specified Down Face entry
----------------------------------------------------------------------------------------*/
void
cef_pit_entry_down_face_ver_remove (
	CefT_Pit_Entry* pe, 					/* PIT entry								*/
	CefT_Down_Faces* dnface,				/* Down Face entry							*/
	CefT_CcnMsg_MsgBdy* pm 					/* Parsed CEFORE message					*/
);
/*--------------------------------------------------------------------------------------
	Searches a Up Face entry
----------------------------------------------------------------------------------------*/
CefT_Up_Faces*								/* Returns  Up Face info				 	*/
cef_pit_entry_up_face_search (
	CefT_Pit_Entry* entry, 					/* PIT entry 								*/
	uint16_t faceid 						/* Face-ID									*/
);
#endif // __CEF_PIT_HEADER__
