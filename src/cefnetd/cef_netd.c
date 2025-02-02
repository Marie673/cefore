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
 * cef_netd.c
 */

#define __CEF_NETD_SOURECE__

// #define	DEB_CCNINFO
/****************************************************************************************
 Include Files
 ****************************************************************************************/
#ifdef __APPLE__
#define __APPLE_USE_RFC_3542
#endif //__APPLE
#include "cef_netd.h"
#include "cef_status.h"
#ifdef __APPLE__
#include <sys/socket.h>
#include <netinet/in.h>
#endif //__APPLE

#include <sys/ioctl.h>

#ifndef CefC_ContentStore
#define CefC_NDEF_ContentStore
#ifdef CefC_Conpub
#define CefC_ContentStore
#endif
#endif
#ifndef CefC_ContentStore
#ifdef CefC_CefnetdCache
#define CefC_ContentStore
#endif
#endif

#ifdef DEB_CCNINFO
#include <ctype.h>
#endif //DEB_CCNINFO

//#define	_DEBUG_BABEL_
//#define __T_VERSION__

/****************************************************************************************
 Macros
 ****************************************************************************************/

/* The number of processing function of message (invalid/Interest/Object) 	*/
#define CefC_Msg_Process_Num		7

#define CEFRTHASH8(_p, _seed, _limit)				\
	do {											\
		int _i0;									\
		unsigned char* _wp = (unsigned char*) _p;	\
		for (_i0 = 0 ; _i0 < _limit ; _i0++) {		\
			_seed = _seed * 33 + _wp[_i0];			\
		}											\
	} while (0)

typedef	enum	{
	CefC_Connection_Type_Udp = 0,
	CefC_Connection_Type_Tcp,
	CefC_Connection_Type_Csm,
	CefC_Connection_Type_Ccr,
	CefC_Connection_Type_Num,
	CefC_Connection_Type_Local = 99,
}	CefC_Connection_Type;


#define CefC_App_MatchType_Exact		0
#define CefC_App_MatchType_Prefix		1

/****************************************************************************************
 Structures Declaration
 ****************************************************************************************/

struct cef_hdr {
	uint8_t 	version;
	uint8_t 	type;
	uint16_t 	pkt_len;
	uint8_t		hoplimit;
	uint8_t		reserve1;
	uint8_t		reserve2;
	uint8_t 	hdr_len;
} __attribute__((__packed__));

typedef struct {
	uint16_t 		faceid;
	unsigned char 	name[CefC_Max_Length];
	uint16_t 		name_len;
	uint8_t 		match_type;				/* Exact or Prefix */
} CefT_App_Reg;

/****************************************************************************************
 State Variables
 ****************************************************************************************/

/* The flag which shows cefnetd is running. 	*/
static uint8_t cefnetd_running_f = 0;
static uint64_t stat_nopit_frames = 0;
static uint64_t stat_rcv_size_cnt = 0;
static uint64_t stat_rcv_size_sum = 0;
static uint64_t stat_rcv_size_min = 65536;
static uint64_t stat_rcv_size_max = 0;

static char root_user_name[CefC_Ctrl_User_Len] = {"root"};


#ifdef CefC_ContentStore
static uint64_t ccninfo_push_time = 0;
#endif // CefC_ContentStore

#ifdef CefC_Debug
static char cnd_dbg_msg[2048];
static int cef_dbg_loglv_finest = 0;
#endif // CefC_Debug

/****************************************************************************************
 Static Function Declaration
 ****************************************************************************************/
static	pthread_t cefnetd_cefstatus_th;

/*--------------------------------------------------------------------------------------
	Reads the config file
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_config_read (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
);
/*--------------------------------------------------------------------------------------
	Handles the control message
----------------------------------------------------------------------------------------*/
static int
cefnetd_input_control_message (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	unsigned char* msg, 						/* the received message(s)				*/
	int msg_size,								/* size of received message(s)			*/
	unsigned char** rspp,
	int fd
);
/*--------------------------------------------------------------------------------------
	Handles the message to reg/dereg application name
----------------------------------------------------------------------------------------*/
static int
cefnetd_input_app_reg_command (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh,				/* Parsed Option Header						*/
	uint16_t faceid
);
/*--------------------------------------------------------------------------------------
	Report xroute is changed to cefbabeld
----------------------------------------------------------------------------------------*/
static void
cefnetd_xroute_change_report (
	CefT_Netd_Handle* hdl,
	unsigned char* name,
	uint16_t name_len,
	int reg_f
);
/*--------------------------------------------------------------------------------------
	Handles the FIB request message
----------------------------------------------------------------------------------------*/
static int
cefnetd_fib_info_get (
	CefT_Netd_Handle* hdl,
	unsigned char** buff
);
/*--------------------------------------------------------------------------------------
	Handles the command from babeld
----------------------------------------------------------------------------------------*/
static int
cefnetd_babel_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	unsigned char* msg,
	int msg_len,
	unsigned char* buff
);
/*--------------------------------------------------------------------------------------
	Creates listening socket(s)
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_faces_init (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
);
static int
cefnetd_trim_line_string (
	const char* p1, 						/* target string for trimming 				*/
	char* p2,								/* name string after trimming				*/
	char* p3								/* value string after trimming				*/
);
/*--------------------------------------------------------------------------------------
	Closes faces
----------------------------------------------------------------------------------------*/
static int
cefnetd_faces_destroy (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
);
/*--------------------------------------------------------------------------------------
	Handles the input message
----------------------------------------------------------------------------------------*/
static int
cefnetd_udp_input_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int fd, 								/* FD which is polled POLLIN				*/
	int faceid								/* Face-ID that message arrived 			*/
);
/*--------------------------------------------------------------------------------------
	Handles the input message from the TCP listen socket
----------------------------------------------------------------------------------------*/
static int
cefnetd_tcp_input_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	int faceid									/* Face-ID that message arrived 		*/
);
/*--------------------------------------------------------------------------------------
	Handles the input message from Csmgr
----------------------------------------------------------------------------------------*/
static int
cefnetd_csm_input_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	int faceid									/* Face-ID that message arrived 		*/
);
/*--------------------------------------------------------------------------------------
	Handles the input message from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_input_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	int faceid									/* Face-ID that message arrived 		*/
);
static int									/* No care now								*/
(*cefnetd_input_process[CefC_Connection_Type_Num]) (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	int faceid									/* Face-ID that message arrived 		*/
) = {
	cefnetd_udp_input_process,					/* CefC_Connection_Type_Udp */
	cefnetd_tcp_input_process,					/* CefC_Connection_Type_Tcp */
	cefnetd_csm_input_process,					/* CefC_Connection_Type_Csm */
	cefnetd_ccr_input_process					/* CefC_Connection_Type_Ccr */
};

/*--------------------------------------------------------------------------------------
	Check Input Cefroute msg
----------------------------------------------------------------------------------------*/
static int
cefnetd_route_msg_check (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	unsigned char* msg,						/* received message to handle				*/
	int msg_size							/* size of received message(s)				*/
);

#ifdef CefC_Ccore
/*--------------------------------------------------------------------------------------
	Handles the operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_invalid_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_r_neighbor_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_r_fib_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_s_fib_add_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_s_fib_del_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_r_cache_prefix_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_r_cache_cap_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_s_cache_cap_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_s_lifetime_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_s_write_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_r_status_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_s_status_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_r_cache_chunk_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);
static int
cefnetd_ccr_s_cache_del_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
);

static int
cefnetd_ccr_s_fib_node_chack(
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	char* node
);

static int
(*cefnetd_ccr_operation_process[CcoreC_Ope_Num]) (
	CefT_Netd_Handle* hdl,
	int fd,
	unsigned char* msg,
	int msg_len
) = {
	cefnetd_ccr_invalid_process,
	cefnetd_ccr_r_neighbor_process,
	cefnetd_ccr_r_fib_process,
	cefnetd_ccr_s_fib_add_process,
	cefnetd_ccr_s_fib_del_process,
	cefnetd_ccr_r_cache_prefix_process,
	cefnetd_ccr_r_cache_cap_process,
	cefnetd_ccr_s_cache_cap_process,
	cefnetd_ccr_s_lifetime_process,
	cefnetd_ccr_s_write_process,
	cefnetd_ccr_r_status_process,
	cefnetd_ccr_s_status_process,
	cefnetd_ccr_r_cache_chunk_process,
	cefnetd_ccr_s_cache_del_process
};
#endif // CefC_Ccore

/*--------------------------------------------------------------------------------------
	Handles the received message(s)
----------------------------------------------------------------------------------------*/
static int									/* No care now								*/
cefnetd_input_message_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* the received message(s)					*/
	int msg_size,							/* size of received message(s)				*/
	char*	user_id
);
/*--------------------------------------------------------------------------------------
	Seeks the top of the frame from the receive buffer
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_messege_head_seek (
	CefT_Face* face,						/* the face structure						*/
	uint16_t* payload_len,
	uint16_t* header_len
);
/*--------------------------------------------------------------------------------------
	Handles the received Interest message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_interest_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);
/*--------------------------------------------------------------------------------------
	Handles the received Content Object message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_object_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);
/*--------------------------------------------------------------------------------------
	Handles the received InterestReturn
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_intreturn_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);
/*--------------------------------------------------------------------------------------
	Handles the received Ccninfo Request
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_ccninforeq_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);
/*--------------------------------------------------------------------------------------
	Handles the received Ccninfo Replay
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_ccninforep_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);
#ifdef CefC_ContentStore
/*--------------------------------------------------------------------------------------
	cefnetd cached Object process
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_cefcache_object_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	unsigned char* msg 						/* received message to handle				*/
);
#endif //CefC_ContentStore

static int									/* Returns a negative value if it fails 	*/
(*cefnetd_incoming_msg_process[CefC_Msg_Process_Num]) (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) = {
	cefnetd_incoming_interest_process,
	cefnetd_incoming_object_process,
	cefnetd_incoming_intreturn_process,
	cefnetd_incoming_ccninforeq_process,
	cefnetd_incoming_ccninforep_process
};
/*--------------------------------------------------------------------------------------
	Handles the received Piggyback message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_piggyback_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* pkt, 					/* received packet to handle				*/
	uint16_t msg_len, 						/* length of ccn message 					*/
	uint16_t header_len						/* length of fixed and option header 		*/
);
#ifdef CefC_ContentStore
/*--------------------------------------------------------------------------------------
	Handles the received message(s) from csmgr
----------------------------------------------------------------------------------------*/
static int										/* No care now							*/
cefnetd_input_message_from_csmgr_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	unsigned char* msg, 						/* the received message(s)				*/
	int msg_size								/* size of received message(s)			*/
);
/*--------------------------------------------------------------------------------------
	Seeks the top of the frame from the receive buffer
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_csmgr_messege_head_seek (
	CefT_Cs_Stat* cs_stat,
	uint16_t* payload_len,
	uint16_t* header_len
);
/*--------------------------------------------------------------------------------------
	Handles the received Interest message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_csmgr_interest_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);
/*--------------------------------------------------------------------------------------
	Handles the received Content Object message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_csmgr_object_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);
/*--------------------------------------------------------------------------------------
	Handles the received Cefping Request
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_csmgr_pingreq_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);
/*--------------------------------------------------------------------------------------
	Handles the received Ccninfo Request
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_csmgr_ccninforeq_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
);

static int									/* Returns a negative value if it fails 	*/
(*cefnetd_incoming_csmgr_msg_process[CefC_Msg_Process_Num]) (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) = {
	cefnetd_incoming_csmgr_interest_process,
	cefnetd_incoming_csmgr_object_process,
	cefnetd_incoming_intreturn_process,
	cefnetd_incoming_csmgr_ccninforeq_process,
	cefnetd_incoming_ccninforep_process,
	cefnetd_incoming_csmgr_pingreq_process
};

/*--------------------------------------------------------------------------------------
	Seeks the csmgr and creates the ccninfo response
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_external_cache_seek (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	CefT_CcnMsg_MsgBdy* pm,				/* Structure to set parsed CEFORE message	*/
	CefT_CcnMsg_OptHdr* poh
);

#endif // CefC_ContentStore
/*--------------------------------------------------------------------------------------
	Create and Send the FHR's ccninfo response
----------------------------------------------------------------------------------------*/
static void
cefnetd_FHR_Reply_process(
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	const unsigned char* name,				/* Report name								*/
	uint32_t name_len,						/* Report name length						*/
	CefT_CcnMsg_OptHdr* poh
);

/*--------------------------------------------------------------------------------------
	ccninfo loop check ccninfo-03
----------------------------------------------------------------------------------------*/
static	int
cefnetd_ccninfo_loop_check(
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	CefT_Parsed_Ccninfo* pci
);

/*--------------------------------------------------------------------------------------
	Create for ccninfo_pit ccninfo-03
----------------------------------------------------------------------------------------*/
#define		CCNINFO_REQ		0
#define		CCNINFO_REP		1
static int
cefnetd_ccninfo_pit_create(
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	CefT_Parsed_Ccninfo* pci,
	unsigned char* ccninfo_pit,
	int		req_or_rep,
	int		skip_option
);

/*--------------------------------------------------------------------------------------
	Clean PIT entries
----------------------------------------------------------------------------------------*/
static void
cefnetd_pit_cleanup (
	CefT_Netd_Handle* hdl, 						/* cefnetd handle						*/
	uint64_t nowt								/* current time (usec) 					*/
);
/*--------------------------------------------------------------------------------------
	Clean FIB entries
----------------------------------------------------------------------------------------*/
static void
cefnetd_fib_cleanup (
	CefT_Netd_Handle* hdl, 						/* cefnetd handle						*/
	uint64_t nowt								/* current time (usec) 					*/
);
/*--------------------------------------------------------------------------------------
	Handles the invalid command
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_invalid_command_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
);
/*--------------------------------------------------------------------------------------
	Handles the Link Request command
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_link_req_command_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
);
/*--------------------------------------------------------------------------------------
	Handles the Link Response command
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_link_res_command_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
);

static int									/* Returns a negative value if it fails 	*/
(*cefnetd_command_process[CefC_Cmd_Num]) (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
) = {
	cefnetd_invalid_command_process,
	cefnetd_link_req_command_process,
	cefnetd_link_res_command_process
};

/*--------------------------------------------------------------------------------------
	Creates the Command Filter(s)
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_command_filter_init (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
);
/*--------------------------------------------------------------------------------------
	Handles a command message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_command_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
);
/*--------------------------------------------------------------------------------------
	Accepts and receives the frame(s) from local face
----------------------------------------------------------------------------------------*/
static int									/* No care now								*/
cefnetd_input_from_local_process (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
);

/*--------------------------------------------------------------------------------------
	Handles the elements of TX queue
----------------------------------------------------------------------------------------*/
static int										/* No care now							*/
cefnetd_input_from_txque_process (
	CefT_Netd_Handle* hdl						/* cefnetd handle						*/
);
/*--------------------------------------------------------------------------------------
	check state of port use
----------------------------------------------------------------------------------------*/
static int										/* No care now							*/
	cefnetd_check_state_of_port_use (
	CefT_Netd_Handle* hdl						/* cefnetd handle						*/
);
/*--------------------------------------------------------------------------------------
	Prepares the UDP and TCP sockets to be polled
----------------------------------------------------------------------------------------*/
static int										/* number of the poll which has events	*/
cefnetd_poll_socket_prepare (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	struct pollfd fds[],
	CefC_Connection_Type fd_type[],
	int faceids[]
);
/*--------------------------------------------------------------------------------------
	Obtains my NodeID (IP Address)
----------------------------------------------------------------------------------------*/
static void
cefnetd_node_id_get (
	CefT_Netd_Handle* hdl						/* cefnetd handle						*/
);

/*--------------------------------------------------------------------------------------
	Obtains my NodeID (IP Address)
----------------------------------------------------------------------------------------*/
static int
cefnetd_matched_node_id_get (
	CefT_Netd_Handle* hdl,
	unsigned char* peer_node_id,
	int peer_node_id_len,
	unsigned char* node_id,
	unsigned int* responder_mtu

);

//0.8.3
/*--------------------------------------------------------------------------------------
	Handles the received Interest message has T_SELECTIVE
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_selective_interest_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh				/* Parsed Option Header						*/
);

#ifndef __PIN_NOT_USE__
static int
cefnetd_plugin_load (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
);
#endif

/* NodeName Check */
static int									/*  */
cefnetd_nodename_check (
	const char* in_name,					/* input NodeName							*/
	unsigned char* ot_name					/* buffer to set After Check NodeName		*/
);


#ifdef DEB_CCNINFO
/*--------------------------------------------------------------------------------------
	for debug
----------------------------------------------------------------------------------------*/
static void
cefnetd_dbg_cpi_print (
	CefT_Parsed_Ccninfo* pci
);
#endif

//0.8.3
static int
cefnetd_keyid_get (
	const unsigned char* msg,
	int msg_len,
	unsigned char *keyid_buff
);

static int
cefnetd_fwd_plugin_load (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
);

static int
cefnetd_continfo_process (
	CefT_Netd_Handle*		hdl,			/* cefnetd handle							*/
	int						faceid,			/* Face-ID where messages arrived at		*/
	int						peer_faceid,	/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char*			msg,			/* received message to handle				*/
	uint16_t				payload_len,	/* Payload Length of this message			*/
	uint16_t				header_len,		/* Header Length of this message			*/
	CefT_CcnMsg_MsgBdy*	pm,				/* Structure to set parsed CEFORE message	*/
	CefT_Parsed_Ccninfo*	pci				/* Structure to set parsed Ccninfo message	*/
);

static void
cefnetd_adv_route_process (
	CefT_Netd_Handle*		hdl,			/* cefnetd handle							*/
	CefT_CcnMsg_MsgBdy*	pm				/* Structure to set parsed CEFORE message	*/
);

/****************************************************************************************
 ****************************************************************************************/

/*--------------------------------------------------------------------------------------
	Creates and initialize the cefnetd handle
----------------------------------------------------------------------------------------*/
CefT_Netd_Handle* 								/* the created cefnetd handle			*/
cefnetd_handle_create (
	uint8_t 	node_type						/* Node Type (Router/Receiver....)		*/
) {
	CefT_Netd_Handle* hdl;
	int res;
	void* 	vret = NULL;
	char*	wp;
	char 	conf_path[1024];
	char 	sock_path[1024];

	/* Allocates a block of memory for cefnetd handle 	*/
	hdl = (CefT_Netd_Handle*) malloc (sizeof (CefT_Netd_Handle));
	if (hdl == NULL) {
		return (NULL);
	}
	memset (hdl, 0, sizeof (CefT_Netd_Handle));
	hdl->nowtus = cef_client_present_timeus_calc ();

	/* Assigns the Node Type							*/
	hdl->node_type = node_type;

	/* Assigns default values to the cefnetd handle 	*/
	hdl->port_num 		= CefC_Default_PortNum;
	hdl->fib_max_size 	= CefC_Default_FibSize;
	hdl->pit_max_size 	= CefC_Default_PitSize;
	hdl->sk_type 		= CefC_Default_Sktype;
	hdl->nbr_max_size 	= CefC_Default_NbrSize;
	hdl->nbr_mng_intv 	= CefC_Default_NbrInterval;
	hdl->nbr_mng_thread = CefC_Default_NbrThread;
	hdl->fwd_rate 		= CefC_Default_Cache_Send_Rate;
	hdl->babel_route 	= 0x03;		/* both 	*/
	hdl->cs_mode 		= 0;
//0.8.3.c ----- START
//	hdl->forwarding_info_strategy = CefC_Default_ForwardingInfoStrategy;
	hdl->forwarding_strategy = CefC_Default_ForwardingStrategy;
//0.8.3.c ----- END
	//2020
	hdl->app_fib_max_size		= CefC_Default_FibAppSize;
	hdl->app_pit_max_size		= CefC_Default_PitAppSize;
	hdl->My_Node_Name			= NULL;
	hdl->My_Node_Name_TLV		= NULL;
	hdl->My_Node_Name_TLV_len	= 0;

	hdl->ccninfo_access_policy = CefC_Default_CcninfoAccessPolicy;
	hdl->ccninfo_full_discovery = CefC_Default_CcninfoFullDiscovery;
	strcpy(hdl->ccninfo_valid_alg ,CefC_Default_CcninfoValidAlg);
	hdl->ccninfo_valid_type = CefC_T_CRC32C;	/* ccninfo-05 */
	strcpy(hdl->ccninfo_sha256_key_prfx ,CefC_Default_CcninfoSha256KeyPrfx);
	hdl->ccninfo_reply_timeout = CefC_Default_CcninfoReplyTimeout;

	//0.8.3
	hdl->IntrestRetrans			= CefC_IntRetrans_Type_RFC;
	hdl->Selective_fwd			= CefC_Default_SelectiveForward;
	hdl->SymbolicBack			= CefC_Default_SymbolicBackBuff;
	hdl->IR_Congesion			= CefC_Default_IR_Congesion;
	hdl->BW_Stat_interval		= CefC_Default_BANDWIDTH_STAT_INTERVAL;
	hdl->Symbolic_max_lifetime	= CefC_Default_SYMBOLIC_LIFETIME;
	hdl->Regular_max_lifetime	= CefC_Default_REGULAR_LIFETIME;
	hdl->Ex_Cache_Access		= CefC_Default_CSMGR_ACCESS_RW;
	strcpy( hdl->bw_stat_pin_name, "bw_stat" );
	hdl->Buffer_Cache_Time		= CefC_Default_BUFFER_CACHE_TIME * 1000;
	hdl->cefstatus_pipe_fd[0]	= -1;
	hdl->cefstatus_pipe_fd[1]	= -1;
	//202108
	hdl->IR_Option				= 0;	//Not
	memset (hdl->IR_enable, 0, sizeof (hdl->IR_enable));

	hdl->stat_recv_frames							 = 0;
	hdl->stat_send_frames							 = 0;
	hdl->stat_recv_interest							 = 0;
	hdl->stat_recv_interest_types[CefC_PIT_TYPE_Rgl] = 0;
	hdl->stat_recv_interest_types[CefC_PIT_TYPE_Sym] = 0;
	hdl->stat_recv_interest_types[CefC_PIT_TYPE_Sel] = 0;
	hdl->stat_send_interest							 = 0;
	hdl->stat_send_interest_types[CefC_PIT_TYPE_Rgl] = 0;
	hdl->stat_send_interest_types[CefC_PIT_TYPE_Sym] = 0;
	hdl->stat_send_interest_types[CefC_PIT_TYPE_Sel] = 0;

	//20220311
	hdl->Selective_max_range = CefC_Default_SELECTIVE_MAX;

	/* Initialize the frame module 						*/
	cef_client_config_dir_get (conf_path);
	cef_frame_init ();
	res = cef_valid_init (conf_path);
	if (res < 0) {
		cef_log_write (CefC_Log_Error, "Failed to read the cefnetd.key\n");
		return (NULL);
	}
	cef_log_write (CefC_Log_Info, "Loading cefnetd.key ... OK\n");

	/* Reads the config file 				*/
	hdl->port_num = cef_client_listen_port_get ();
	res = cefnetd_config_read (hdl);
	if (res < 0) {
		cef_log_write (CefC_Log_Error, "Failed to read the cefnetd.conf\n");
		return (NULL);
	}
	cef_log_write (CefC_Log_Info, "Loading cefnetd.conf ... OK\n");

	/* Initialize sha256 validation environment for ccninfo */
	if (hdl->ccninfo_valid_type == CefC_T_RSA_SHA256) {
		res = cef_valid_init_ccninfoRT (conf_path);
		if (res < 0) {
			return (NULL);
		}
	}
	/* Clear information used in authentication & authorization */
	hdl->ccninfousr_id_len = 0;
	memset (hdl->ccninfousr_node_id, 0, sizeof(hdl->ccninfousr_node_id));
	if (hdl->ccninfo_rcvdpub_key_bi != NULL) {
		free (hdl->ccninfo_rcvdpub_key_bi);
		hdl->ccninfo_rcvdpub_key_bi = NULL;
	}
	hdl->ccninfo_rcvdpub_key_bi_len = 0;

	/* check state of port use */
	res = cefnetd_check_state_of_port_use (hdl);
	if (res < 0) {
		char	tmp_sock_path[1024];
		char	tmp_msg[2048];
		cef_client_local_sock_name_get (tmp_sock_path);
		sprintf( tmp_msg, "Another cefnetd may be running with the same port. If not, remove the old socket file (%s) and restart cefnetd.\n", tmp_sock_path );
		cef_log_write (CefC_Log_Error, tmp_msg);
		return (NULL);
	}

	/* Creates listening socket(s)			*/
	res = cefnetd_faces_init (hdl);
	if (res < 0) {
		/* Delete UNIX domain socket file */
		cef_client_local_sock_name_get (sock_path);
		unlink (sock_path);
		if (hdl->babel_use_f) {
			cef_client_babel_sock_name_get (sock_path);
			unlink (sock_path);
		}
		return (NULL);
	}
	srand ((unsigned) time (NULL));
	hdl->cefrt_seed = (uint8_t)(rand () + 1);
	cef_log_write (CefC_Log_Info, "Creation the listen faces ... OK\n");

	/* Obtains my NodeID (IP Address) 	*/
	cefnetd_node_id_get (hdl);

	/* Creates and initialize FIB			*/
	hdl->fib = cef_hash_tbl_create_ext ((uint16_t) hdl->fib_max_size, CefC_Hash_Coef_FIB);
	cef_fib_init (hdl->fib,
				  hdl->nodeid4_num,
				  hdl->nodeid16_num,
				  hdl->nodeid4_c,
				  hdl->nodeid16_c,
				  hdl->port_num
	);
	cef_face_update_listen_faces (
		hdl->inudpfds, hdl->inudpfaces, &hdl->inudpfdc,
		hdl->intcpfds, hdl->intcpfaces, &hdl->intcpfdc);
	hdl->fib_clean_t = cef_client_present_timeus_calc () + 1000000;
	cef_log_write (CefC_Log_Info, "Creation FIB ... OK\n");

	/* Creates the Command Filter(s) 		*/
	cefnetd_command_filter_init (hdl);

	/* Creates PIT 							*/
	cef_pit_init (hdl->ccninfo_reply_timeout, hdl->Symbolic_max_lifetime, hdl->Regular_max_lifetime); //0.8.3
	hdl->pit = cef_lhash_tbl_create_ext (hdl->pit_max_size, CefC_Hash_Coef_PIT);
	hdl->pit_clean_t = cef_client_present_timeus_calc () + 1000000;
	cef_log_write (CefC_Log_Info, "Creation PIT ... OK\n");

	/* Prepares sockets for applications 	*/
	for (res = 0 ; res < CefC_App_Conn_Num ; res++) {
		hdl->app_fds[res] = -1;
		hdl->app_faces[res] = -1;
	}
	hdl->app_fds_num = 0;

#ifdef CefC_ContentStore
	/* Reads the config file 				*/
	hdl->cs_stat = cef_csmgr_stat_create (hdl->cs_mode);

	if (hdl->cs_stat == NULL) {
		cef_log_write (CefC_Log_Error, "Failed to init Content Store\n");
		/* Delete UNIX domain socket file */
		cef_client_local_sock_name_get (sock_path);
		unlink (sock_path);
		if (hdl->babel_use_f) {
			cef_client_babel_sock_name_get (sock_path);
			unlink (sock_path);
		}
		return (NULL);
	}
	if (hdl->cs_stat->cache_type != CefC_Default_Cache_Type) {
		cef_log_write (CefC_Log_Info, "Initialization Content Store ... OK\n");
	} else {
		cef_log_write (CefC_Log_Info, "Not use Content Store\n");
	}
	/* RW or RO */
	hdl->cs_stat->csmgr_access = hdl->Ex_Cache_Access;
	/* BUFFER_CACHE_TIME */
	hdl->cs_stat->buffer_cache_time = hdl->Buffer_Cache_Time;

	/* set send content rate 	*/
	hdl->send_rate = (double)(8.0 / (double) hdl->fwd_rate);
	hdl->send_next = 0;
#else // CefC_ContentStore
	hdl->cs_stat = NULL;
#endif // CefC_ContentStore

	/* Creates App Reg table 		*/
	hdl->app_reg = cef_hash_tbl_create_ext (hdl->app_fib_max_size, CefC_Hash_Coef_FIB);
	/* Creates App Reg PIT 			*/
	hdl->app_pit = cef_lhash_tbl_create_ext (hdl->app_pit_max_size, CefC_Hash_Coef_PIT);

	/* Inits the plugin 			*/
	cef_plugin_init (&(hdl->plugin_hdl));

#ifdef CefC_Mobility
	cef_mb_plugin_init (
		&hdl->plugin_hdl.mb, hdl->plugin_hdl.tx_que, hdl->plugin_hdl.tx_que_mp,
		hdl->rtts, hdl->nbr_num, (uint32_t)(hdl->nbr_mng_intv / 1000), &vret);
#endif // CefC_Mobility

	cef_tp_plugin_init (
		&hdl->plugin_hdl.tp, hdl->plugin_hdl.tx_que, hdl->plugin_hdl.tx_que_mp, vret);

	//0.8.3 libcefnetd_plugin
	hdl->bw_stat_hdl = (CefT_Plugin_Bw_Stat*)malloc( sizeof(CefT_Plugin_Bw_Stat) );
	memset( hdl->bw_stat_hdl, 0, sizeof(CefT_Plugin_Bw_Stat) );
#ifndef __PIN_NOT_USE__
	/* cefnetd.conf */
	if ( strcmp( hdl->bw_stat_pin_name, "None" ) != 0 ) {
		res = cefnetd_plugin_load(hdl);
		if ( res < 0 ) {
			/* NG */
			cefnetd_handle_destroy (hdl);
			return (NULL);
		}
		res = hdl->bw_stat_hdl->init( hdl->BW_Stat_interval );
		if ( res < 0 ) {
			/* NG */
			cefnetd_handle_destroy (hdl);
			return (NULL);
		}
	}
#endif

	/* Forwarding Strategy Plugin(libcefnetd_fwd_plugin) */
	hdl->fwd_strtgy_hdl = (CefT_Plugin_Fwd_Strtgy*) malloc (sizeof (CefT_Plugin_Fwd_Strtgy));
	memset( hdl->fwd_strtgy_hdl, 0, sizeof (CefT_Plugin_Fwd_Strtgy));
	res = cefnetd_fwd_plugin_load(hdl);
	if (res < 0) {
		cefnetd_handle_destroy (hdl);
		return (NULL);
	}
	if (hdl->fwd_strtgy_hdl->init) {
		res = hdl->fwd_strtgy_hdl->init();
		if (res < 0) {
			cefnetd_handle_destroy (hdl);
			return (NULL);
		}
	}

	/* Records the user which launched cefnetd 		*/
	wp = getenv ("USER");

	if (wp == NULL) {
		cefnetd_handle_destroy (hdl);
		cef_log_write (CefC_Log_Error,
			"Failed to obtain $USER launched cefnetd\n");
		return (NULL);
	}
	memset (hdl->launched_user_name, 0, CefC_Ctrl_User_Len);
	strcpy (hdl->launched_user_name, wp);

#ifdef CefC_Ccore
	hdl->rt_hdl = ccore_router_handle_create (conf_path);

	if (hdl->rt_hdl) {

		res = ccore_valid_cefnetd_init (conf_path);

		if (res < 0) {
			cef_log_write (CefC_Log_Error, "Failed to initialize validation for ccore\n");
			cefnetd_handle_destroy (hdl);
			return (NULL);
		}
		if (hdl->rt_hdl->sock != -1) {
			cef_log_write (CefC_Log_Info, "Initialization controller ... OK\n");
		} else {
			hdl->rt_hdl->reconnect_time = hdl->nowtus + 3000000;
		}
	}
#endif // CefC_Ccore

	//NodeName Check
	if ( hdl->My_Node_Name == NULL ) {
		char		addrstr[256];
		if ( hdl->top_nodeid_len == 4 ) {
			inet_ntop (AF_INET, hdl->top_nodeid, addrstr, sizeof (addrstr));
		} else if ( hdl->top_nodeid_len == 16 ) {
			inet_ntop (AF_INET6, hdl->top_nodeid, addrstr, sizeof (addrstr));
		}
		unsigned char	out_name[CefC_Max_Length];
		unsigned char	out_name_tlv[CefC_Max_Length];

		hdl->My_Node_Name = malloc( sizeof(char) * strlen(addrstr) + 1 );
		strcpy( hdl->My_Node_Name, addrstr );
		/* Convert Name TLV */
		strcpy( (char*)out_name, "ccnx:/" );
		strcat( (char*)out_name, hdl->My_Node_Name );
		res = cef_frame_conversion_uri_to_name ((char*)out_name, out_name_tlv);
		if ( res < 0 ) {
			/* Error */
			cef_log_write (CefC_Log_Error, "NODE_NAME contains characters that cannot be used.\n");
			return( NULL );
		} else {
			struct tlv_hdr name_tlv_hdr;
			name_tlv_hdr.type = htons (CefC_T_NAME);
			name_tlv_hdr.length = htons (res);
			hdl->My_Node_Name_TLV = (unsigned char*)malloc( res+CefC_S_TLF );
			hdl->My_Node_Name_TLV_len = res + CefC_S_TLF;
			memcpy( &hdl->My_Node_Name_TLV[0], &name_tlv_hdr, sizeof(struct tlv_hdr) );
			memcpy( &hdl->My_Node_Name_TLV[CefC_S_TLF], out_name_tlv, res );
		}
		cef_log_write (CefC_Log_Warn, "No NODE_NAME defined in cefnetd.conf; IP address is temporarily used as the node name.\n");
	}

	/*#####*/
	{	/* cefstatus_thread */
		int flags;
		/* Create socket for communication between cednetd and memory cache */
		if ( socketpair(AF_UNIX,SOCK_DGRAM, 0, hdl->cefstatus_pipe_fd) == -1 ) {
			cefnetd_handle_destroy (hdl);
			cef_log_write (CefC_Log_Error, "%s cefstatus pair socket creation error (%s)\n"
							, __func__, strerror(errno));
			return (NULL);
	  	}
		/* Set cefnetd side socket as non-blocking I/O */
		if ( (flags = fcntl(hdl->cefstatus_pipe_fd[0], F_GETFL, 0) ) < 0) {
			cefnetd_handle_destroy (hdl);
			cef_log_write (CefC_Log_Error, "%s fcntl error (%s)\n"
							, __func__, strerror(errno));
			return (NULL);
		}
		flags |= O_NONBLOCK;
		if (fcntl(hdl->cefstatus_pipe_fd[0], F_SETFL, flags) < 0) {
			cefnetd_handle_destroy (hdl);
			cef_log_write (CefC_Log_Error, "%s fcntl error (%s)\n"
							, __func__, strerror(errno));
			return (NULL);
		}

		if (pthread_create(&cefnetd_cefstatus_th, NULL
				, &cefnetd_cefstatus_thread, (hdl)) == -1) {
				cefnetd_handle_destroy (hdl);
				cef_log_write (CefC_Log_Error
							, "%s Failed to create the new thread(cefnetd_cefstatus_thread)\n"
							, __func__);
			return (NULL);
		}
	}

	/*#####*/

	return (hdl);
}
/*--------------------------------------------------------------------------------------
	Destroys the cefnetd handle
----------------------------------------------------------------------------------------*/
void
cefnetd_handle_destroy (
	CefT_Netd_Handle* hdl						/* cefnetd handle to destroy			*/
) {
	char sock_path[1024];

	/* destroy plugins 		*/
	cef_tp_plugin_destroy (hdl->plugin_hdl.tp);
	cef_plugin_destroy (&(hdl->plugin_hdl));

	//0.8.3
	if ( hdl->bw_stat_hdl->destroy ) {
		hdl->bw_stat_hdl->destroy();
	}

	if (hdl->fwd_strtgy_hdl->destroy) {
		hdl->fwd_strtgy_hdl->destroy();
	}
#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> Rx ContentObject = "FMTU64"\n", hdl->stat_recv_frames);
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> Tx ContentObject = "FMTU64"\n", hdl->stat_send_frames);
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> Rx Interest      = "FMTU64"\n", hdl->stat_recv_interest);
	cef_dbg_write (CefC_Dbg_Fine,
		"                        = RGL("FMTU64"), SYM("FMTU64"), SEL("FMTU64")\n",
									hdl->stat_recv_interest_types[0],
									hdl->stat_recv_interest_types[1],
									hdl->stat_recv_interest_types[2]);
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> Tx Interest      = "FMTU64"\n", hdl->stat_send_interest);
	cef_dbg_write (CefC_Dbg_Fine,
		"                        = RGL("FMTU64"), SYM("FMTU64"), SEL("FMTU64")\n",
									hdl->stat_send_interest_types[0],
									hdl->stat_send_interest_types[1],
									hdl->stat_send_interest_types[2]);
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> No PIT Frames    = "FMTU64"\n", stat_nopit_frames);
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> Frame Size Cnt   = "FMTU64"\n", stat_rcv_size_cnt);
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> Frame Size Sum   = "FMTU64"\n", stat_rcv_size_sum);
	if (stat_rcv_size_min > 65535) {
		stat_rcv_size_min = 0;
	}
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> Frame Size Min   = "FMTU64"\n", stat_rcv_size_min);
	cef_dbg_write (CefC_Dbg_Fine,
		"<STAT> Frame Size Max   = "FMTU64"\n", stat_rcv_size_max);
#endif // CefC_Debug

	cefnetd_faces_destroy (hdl);
	if (hdl->babel_use_f) {
		cef_client_babel_sock_name_get (sock_path);
		unlink (sock_path);
	}

#ifdef CefC_ContentStore
	cef_csmgr_stat_destroy (&hdl->cs_stat);
#endif // CefC_ContentStore

#ifdef CefC_Ccore
	ccore_handle_destroy (&hdl->rt_hdl);
#endif // CefC_Ccore
	free (hdl);

	cef_client_local_sock_name_get (sock_path);
	unlink (sock_path);

	cef_log_write (CefC_Log_Info, "Stop\n");
}
/*--------------------------------------------------------------------------------------
	Main Loop Function
----------------------------------------------------------------------------------------*/
void
cefnetd_event_dispatch (
	CefT_Netd_Handle* hdl 						/* cefnetd handle						*/
) {
	int i;
	int res;

	uint64_t nowt = cef_client_present_timeus_calc ();

	cef_log_write (CefC_Log_Info, "Running\n");
	cefnetd_running_f = 1;

	struct pollfd fds[CefC_Listen_Face_Max * 2];
	CefC_Connection_Type fd_type[CefC_Listen_Face_Max * 2];
	int faceids[CefC_Listen_Face_Max * 2];
	int fdnum;

#ifdef CefC_Ccore
	uint64_t ret_cnt = 5;
#endif
	while (cefnetd_running_f) {

		/* Calculates the present time 						*/
		nowt = cef_client_present_timeus_calc ();
		hdl->nowtus = nowt;

#ifdef CefC_Ccore
		if (hdl->rt_hdl) {
			/* Re-connect to ccored 		*/
			if ((hdl->rt_hdl->sock == -1) &&
				(hdl->nowtus > hdl->rt_hdl->reconnect_time)) {
				cef_log_write (CefC_Log_Info,
						"cefnetd is trying to connect with ccored ...\n");
				hdl->rt_hdl->sock =
					ccore_connect_tcp_to_ccored (
							hdl->rt_hdl->controller_id, hdl->rt_hdl->port_str);
				if (hdl->rt_hdl->sock != -1) {
					cef_log_write (CefC_Log_Info,
						"cefnetd connects to that ccored\n");
					hdl->rt_hdl->reconnect_time = 0;
					ret_cnt = 3;
				} else {
					cef_log_write (CefC_Log_Info,
						"cefnetd failed to connect with ccored\n");
					nowt = cef_client_present_timeus_calc ();
					hdl->nowtus = nowt;
					hdl->rt_hdl->reconnect_time = hdl->nowtus + 1000000 * ret_cnt;

					if (ret_cnt < 60) {
						ret_cnt += 5;
					}
				}
			}
		}
#endif // CefC_Ccore

		/* Cleans PIT entries 		*/
		cefnetd_pit_cleanup (hdl, nowt);

		/* Cleans FIB entries 		*/
		cefnetd_fib_cleanup (hdl, nowt);
		/* Accepts the TCP socket 	*/
		res = cef_face_accept_connect ();

		if ((res > 0) && (hdl->intcpfdc < CefC_Listen_Face_Max)) {
			hdl->intcpfaces[hdl->intcpfdc] = (uint16_t) res;
			hdl->intcpfds[hdl->intcpfdc].fd
				= cef_face_get_fd_from_faceid ((uint16_t) res);
			hdl->intcpfds[hdl->intcpfdc].events = POLLIN | POLLERR;
			hdl->intcpfdc++;
		}

		/* Receives the frame(s) from local process 		*/
		cefnetd_input_from_local_process (hdl);
		cef_face_update_listen_faces (
				hdl->inudpfds, hdl->inudpfaces, &hdl->inudpfdc,
				hdl->intcpfds, hdl->intcpfaces, &hdl->intcpfdc);
		/* Receives the frame(s) from the listen port 		*/
		fdnum = cefnetd_poll_socket_prepare (hdl, fds, fd_type, faceids);
		res = poll (fds, fdnum, 1);

		for (i = 0 ; res > 0 && i < fdnum ; i++) {

			if (fds[i].revents != 0) {
				res--;
				if (fds[i].revents & POLLIN) {
					if (fd_type[i] == CefC_Connection_Type_Local) {
						continue;
					}
					(*cefnetd_input_process[fd_type[i]]) (
										hdl, fds[i].fd, faceids[i]);
				}
#ifdef __APPLE__
				if (fds[i].revents & (POLLERR | POLLNVAL)) {
#else // __APPLE__
				if (fds[i].revents & (POLLERR | POLLNVAL | POLLHUP)) {
#endif // __APPLE__
					if (fd_type[i] < CefC_Connection_Type_Csm) {
						cef_face_close (faceids[i]);
						cef_fib_faceid_cleanup (hdl->fib);
					}
				}

			}
		}

		cefnetd_input_from_txque_process (hdl);

#ifdef CefC_ContentStore
		if ((hdl->cs_stat->cache_type != CefC_Cache_Type_None) &&
			(nowt > ccninfo_push_time)) {
			if (hdl->cs_stat->cache_type == CefC_Cache_Type_Excache) {
				cef_csmgr_excache_item_push (hdl->cs_stat);
				ccninfo_push_time = nowt + 500000;
			}
#ifdef	CefC_CefnetdCache
			else
			if (hdl->cs_stat->cache_type == CefC_Cache_Type_Localcache) {
				; /* NOP */
			}
#endif	//CefC_CefnetdCache
		}
#endif // CefC_ContentStore

	}

}
/*--------------------------------------------------------------------------------------
	Ccninfo Full discobery authentication & authorization
	NOTE: Stub function for future expansion
----------------------------------------------------------------------------------------*/
int											/* Returns 0 if authentication 				*/
											/* and authorization are OK 				*/
cefnetd_ccninfo_fulldiscovery_authNZ(
	uint16_t 		usr_id_len,
	unsigned char*  usr_node_id,
	int 			rcvdpub_key_bi_len,
	unsigned char* 	rcvdpub_key_bi
) {
	if (rcvdpub_key_bi_len == 0){
		return (-1);
	} else {
		return (0);
	}
}
/*--------------------------------------------------------------------------------------
	Check Ccninfo Relpy size
	  NOTE: When oversize, create a reply message of NO_SPECE
----------------------------------------------------------------------------------------*/
uint16_t
cefnetd_ccninfo_check_relpy_size(
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	unsigned char* msg, 					/* Reply message							*/
	uint16_t msg_len, 						/* Length of this message			*/
	uint16_t ccninfo_flag					/* ccninfo_flag ccninfo-05 */
){

	struct fixed_hdr* 	fixed_hp;
	struct tlv_hdr* 	tlv_ptr;
	uint16_t 			hdr_len;
	uint16_t 			pkt_len;
	uint16_t 			new_pkt_len;
	uint16_t 			dsc_len;
	uint16_t 			new_dsc_len;
	uint16_t 			name_len;
	uint16_t 			vald_len;

#define CCNINFO_MAX_REPLY_SIZE 1280

	if (msg_len > CCNINFO_MAX_REPLY_SIZE){
		/* Remove the Valation-related TLV from the message */
		msg_len = cef_valid_remove_valdsegs_fr_msg_forccninfo (msg, msg_len);
		if (msg_len <= CCNINFO_MAX_REPLY_SIZE){
			return (msg_len);
		}

		/***
                          1               2               3
	      0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
	     +---------------+---------------+---------------+---------------+
	     |    Version    |  PacketType   |         PacketLength          |
	     +---------------+---------------+-------------+-+---------------+
	     |    HopLimit   |   ReturnCode  |Reserved(MBZ)  | HeaderLength  |
	     +===============+===============+=============+=+===============+
	     |                                                               |
	     +                       Request block TLV                       +
	     |                                                               |
	     +---------------+---------------+---------------+---------------+
	     /                               .                               /
	     /                      n Report block TLVs                      /
	     /                               .                               /
	     +===============+===============+===============+===============+
	     |          T_DISCOVERY          |         MessageLength         |
	     +---------------+---------------+---------------+---------------+
	     |            T_NAME             |             Length            |
	     +---------------+---------------+---------------+---------------+
	     / Name segment TLVs (name prefix specified by ccninfo command) /
	     +---------------+---------------+---------------+---------------+
	     /                        Reply block TLV                        /
	     +---------------+---------------+---------------+---------------+
	     /                     Reply sub-block TLV 1                     /
	     /                               .                               /
		***/

		/* Obtains header length */
		fixed_hp 	= (struct fixed_hdr*) msg;
		hdr_len		= fixed_hp->hdr_len;
		pkt_len		= ntohs (fixed_hp->pkt_len);
		/* Obtains T_DISCOVERY Length 	*/
		tlv_ptr = (struct tlv_hdr*) &msg[hdr_len];
		dsc_len = ntohs (tlv_ptr->length);
		/* Obtains Validation Length 	*/
		vald_len = pkt_len - (dsc_len + CefC_S_TLF);
		/* Obtains T_NAME Length 	*/
		tlv_ptr = (struct tlv_hdr*)
					&msg[hdr_len+CefC_S_TLF /* T_DISCOVERY+MessageLength */];
		name_len = ntohs (tlv_ptr->length);

		/* Create NO_SPACE message */
		new_pkt_len = hdr_len
						+ CefC_S_TLF /* T_DISCOVERY+MessageLength	*/
						+ CefC_S_TLF /* T_NAME+Length				*/
						+ name_len;
		fixed_hp->pkt_len = htons (new_pkt_len);
		new_dsc_len = CefC_S_TLF /* T_NAME+Length				*/
						+ name_len;
		tlv_ptr = (struct tlv_hdr*) &msg[hdr_len];
		tlv_ptr->length = htons (new_dsc_len);
		msg[CefC_O_Fix_Type] 			= CefC_PT_REPLY;
		msg[CefC_O_Fix_Ccninfo_RetCode] 	= CefC_CtRc_NO_SPACE;

		if ( ccninfo_flag & CefC_CtOp_ReqValidation )	/* ccninfo-05 */
		{
		/* Validation */
		if ((new_pkt_len + vald_len) <= CCNINFO_MAX_REPLY_SIZE
			&& (hdl->ccninfo_valid_type != CefC_T_ALG_INVALID)) {
			CefT_Ccninfo_TLVs tlvs;
			tlvs.alg.valid_type = hdl->ccninfo_valid_type;
			new_pkt_len = cef_frame_ccninfo_vald_create_for_reply (msg, &tlvs);
		}
		}
		return (new_pkt_len);
	}
	return (msg_len);
}

/*--------------------------------------------------------------------------------------
	check state of port use
----------------------------------------------------------------------------------------*/
static int										/* No care now							*/
	cefnetd_check_state_of_port_use (
	CefT_Netd_Handle* hdl						/* cefnetd handle						*/
){
	char sock_path[1024];
	int rc = 0;
	struct stat sb = {0};

	cef_client_local_sock_name_get (sock_path);
	rc = stat (sock_path, &sb);
	if (rc == 0) {
		cef_log_write (CefC_Log_Warn, "%s (%s)\n", __func__, strerror (errno));
		return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------------------------
	Prepares the UDP and TCP sockets to be polled
----------------------------------------------------------------------------------------*/
static int										/* number of the poll which has events	*/
cefnetd_poll_socket_prepare (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	struct pollfd fds[],
	CefC_Connection_Type fd_type[],
	int faceids[]
) {
	int res = 0;
	int i;
	int n;

	for (i = 0 ; i < hdl->inudpfdc ; i++) {
		if (cef_face_check_active (hdl->inudpfaces[i]) > 0) {
			fds[res].events = POLLIN | POLLERR;
			fds[res].fd = hdl->inudpfds[i].fd;
			fd_type[res] = CefC_Connection_Type_Udp;
			faceids[res] = hdl->inudpfaces[i];
			res++;
		} else {
			for (n = i ; n < hdl->inudpfdc - 1 ; n++) {
				hdl->inudpfds[n].fd = hdl->inudpfds[n + 1].fd;
				hdl->inudpfaces[n] 	= hdl->inudpfaces[n + 1];
			}
			hdl->inudpfdc--;
			i--;
		}
	}

	for (i = 0 ; i < hdl->intcpfdc ; i++) {
		if (cef_face_check_active (hdl->intcpfaces[i]) > 0) {
			fds[res].events = POLLIN | POLLERR;
			fds[res].fd = hdl->intcpfds[i].fd;
			fd_type[res] = CefC_Connection_Type_Tcp;
			faceids[res] = hdl->intcpfaces[i];
			res++;
		} else {
			for (n = i ; n < hdl->intcpfdc - 1 ; n++) {
				hdl->intcpfds[n].fd = hdl->intcpfds[n + 1].fd;
				hdl->intcpfaces[n] 	= hdl->intcpfaces[n + 1];
			}
			hdl->intcpfdc--;
			i--;
		}
	}

#ifdef CefC_ContentStore
	if (hdl->cs_stat->local_sock != -1) {
		fds[res].events = POLLIN | POLLERR;
		fds[res].fd = hdl->cs_stat->local_sock;
		fd_type[res] = CefC_Connection_Type_Csm;
		faceids[res] = 0;
		res++;
	}

	if (hdl->cs_stat->tcp_sock != -1) {
		fds[res].events = POLLIN | POLLERR;
		fds[res].fd = hdl->cs_stat->tcp_sock;
		fd_type[res] = CefC_Connection_Type_Csm;
		faceids[res] = 0;
		res++;
	}
#endif // CefC_ContentStore

#ifdef CefC_Ccore
	if (hdl->rt_hdl) {
		if (hdl->rt_hdl->sock != -1) {
			fds[res].events = POLLIN | POLLERR;
			fds[res].fd = hdl->rt_hdl->sock;
			fd_type[res] = CefC_Connection_Type_Ccr;
			faceids[res] = 0;
			res++;
		}
	}
#endif // CefC_Ccore

	for (i = 0 ; i < hdl->app_fds_num ; i++) {
		if (hdl->app_fds[i] != -1) {
			fds[res].events = POLLIN | POLLERR;
			fds[res].fd = hdl->app_fds[i] ;
			fd_type[res] = CefC_Connection_Type_Local;
			faceids[res] = 0;
			res++;
		}
	}

	return (res);
}
/*--------------------------------------------------------------------------------------
	Handles the elements of TX queue
----------------------------------------------------------------------------------------*/
static int										/* No care now							*/
cefnetd_input_from_txque_process (
	CefT_Netd_Handle* hdl						/* cefnetd handle						*/
) {
	CefT_Tx_Elem* tx_elem;
	int i;
	unsigned char hoplimit;
	uint32_t seqnum;

#ifdef CefC_ContentStore
	CefT_Cs_Tx_Elem* cs_tx_elem;
	CefT_Cs_Stat* cs_stat = hdl->cs_stat;
	CefT_Cs_Tx_Elem_Cob* cob_temp;
	int send_cnt = 0;
	uint64_t nowt;
	uint64_t data_sum = 0;
#endif // CefC_ContentStore
	uint16_t new_buff_len = 0;

	while (1) {
		/* Pop one element from the TX Ring Queue 		*/
		tx_elem = (CefT_Tx_Elem*) cef_rngque_pop (hdl->plugin_hdl.tx_que);

		if (tx_elem != NULL) {

			if (tx_elem->type > CefC_Elem_Type_Object) {
				goto FREE_POOLED_BK;
			}

			if (tx_elem->msg[CefC_O_Fix_Type] == CefC_PT_INTEREST) {
				hoplimit = tx_elem->msg[CefC_O_Fix_HopLimit];
			} else {
				hoplimit = 1;
			}

			if (hoplimit < 1) {
				goto FREE_POOLED_BK;
			}

			for (i = 0 ; i < tx_elem->faceid_num ; i++) {
				if (cef_face_check_active (tx_elem->faceids[i]) > 0) {
					new_buff_len = tx_elem->msg_len;
					if (tx_elem->msg[CefC_O_Fix_Type] == CefC_PT_OBJECT) {
						seqnum = cef_face_get_seqnum_from_faceid (tx_elem->faceids[i]);
						new_buff_len =
							cef_frame_seqence_update (tx_elem->msg, seqnum);
					}
					cef_face_frame_send_forced (
						tx_elem->faceids[i], tx_elem->msg, new_buff_len);
					hdl->stat_send_frames++;
				}
			}

FREE_POOLED_BK:
			/* Free the pooled block 	*/
			cef_mpool_free (hdl->plugin_hdl.tx_que_mp, tx_elem);

		} else {
			break;
		}
	}

#ifdef CefC_ContentStore
	if ((hdl->cs_stat->cache_type != CefC_Default_Cache_Type) &&
		 ((nowt = cef_client_present_timeus_calc ()) > hdl->send_next)) {
		while (1) {
			/* Pop one element from the TX Ring Queue 		*/
			cs_tx_elem = (CefT_Cs_Tx_Elem*) cef_rngque_read (cs_stat->tx_que);

			if (cs_tx_elem == NULL) {
				break;
			}
			if (cs_tx_elem->type == CefC_Cs_Tx_Elem_Type_Cob) {
				cs_tx_elem = (CefT_Cs_Tx_Elem*) cef_rngque_pop (cs_stat->tx_que);

				/* send cob	*/
				cob_temp = (CefT_Cs_Tx_Elem_Cob*) cs_tx_elem->data;
				csmgr_cob_forward (
							cob_temp->faceid, cob_temp->cob.msg,
							cob_temp->cob.msg_len, cob_temp->cob.chunk_num);
				/* Free the pooled block 	*/
				data_sum += cob_temp->cob.msg_len;
				cef_mpool_free (cs_stat->tx_cob_mp, cob_temp);
				cef_mpool_free (cs_stat->tx_que_mp, cs_tx_elem);
				send_cnt++;
				hdl->stat_send_frames++;
			} else {
#ifdef CefC_Debug
				cef_dbg_write (CefC_Dbg_Fine,
					"Memcache queue read the unknown type message\n");
#endif // CefC_Debug
				cef_rngque_pop (cs_stat->tx_que);
				cef_mpool_free (cs_stat->tx_que_mp, cs_tx_elem);
			}
			if (send_cnt > CefC_Csmgr_Max_Send_Num) {
				/* Temporarily suspend	*/
				break;
			}
		}
		/* set next	*/
		hdl->send_next = nowt + (hdl->send_rate * data_sum);
	}
#endif // CefC_ContentStore

	return (1);
}

/*--------------------------------------------------------------------------------------
	Accepts and receives the frame(s) from local face
----------------------------------------------------------------------------------------*/
static int										/* No care now							*/
cefnetd_input_from_local_process (
	CefT_Netd_Handle* hdl						/* cefnetd handle						*/
) {
	int sock;
	int work_peer_sock;
	struct sockaddr_un peeraddr;
	socklen_t addrlen = (socklen_t) sizeof (peeraddr);
	int len;
	int flag;
	int i;
	int peer_faceid;
	unsigned char buff[CefC_Max_Length];
	unsigned char* rsp_msg;
	struct pollfd send_fds[1];
	char	user_id[512];
	rsp_msg = calloc(1, CefC_Max_Length*10);

	/* Obtains the FD for local face 	*/
	sock = cef_face_get_fd_from_faceid (CefC_Faceid_Local);

	/* Accepts the interrupt from local process */
	if ((work_peer_sock = accept (sock, (struct sockaddr*)&peeraddr, &addrlen)) < 0) {
		/* NOP */;
	} else {
		if (hdl->app_fds_num < CefC_App_Conn_Num) {
			hdl->app_fds[hdl->app_fds_num] = work_peer_sock;

			flag = fcntl (hdl->app_fds[hdl->app_fds_num], F_GETFL, 0);
			if (flag < 0) {
				cef_log_write (CefC_Log_Info,
					"<Fail> cefnetd_input_from_local_process (fcntl)\n");
				work_peer_sock = -1;
			} else {
				if (fcntl (hdl->app_fds[hdl->app_fds_num]
									, F_SETFL, flag | O_NONBLOCK) < 0) {
					cef_log_write (CefC_Log_Info,
						"<Fail> cefnetd_input_from_local_process (fcntl)\n");
					work_peer_sock = -1;
				}
			}
			if (work_peer_sock != -1) {
				peer_faceid = cef_face_lookup_local_faceid (work_peer_sock);

				if (peer_faceid < 0) {
					close (hdl->app_steps[hdl->app_fds_num]);
					hdl->app_fds[hdl->app_fds_num] = -1;
				} else {
					hdl->app_faces[hdl->app_fds_num] = peer_faceid;
					hdl->app_steps[hdl->app_fds_num] = 0;
					hdl->app_fds_num++;
				}
			} else {
				close (hdl->app_steps[hdl->app_fds_num]);
				hdl->app_fds[hdl->app_fds_num] = -1;
			}
		} else {
			close (work_peer_sock);
		}
	}

	/* Checks whether frame(s) arrivals from the active local faces */
	for (i = 0 ; i < hdl->app_fds_num ; i++) {
		len = recv (hdl->app_fds[i], buff, CefC_Max_Length, 0);

		if (len > 0) {
			hdl->app_steps[i] = 0;

			if (memcmp (buff, CefC_Ctrl, CefC_Ctrl_Len) == 0) {
				flag = cefnetd_input_control_message (
						hdl, buff, len, &rsp_msg, hdl->app_fds[i]);
				if (flag > 0) {
					send_fds[0].fd = hdl->app_fds[i];
					send_fds[0].events = POLLOUT | POLLERR;
					if (poll (send_fds, 1, 0) > 0) {
						if (send_fds[0].revents & (POLLERR | POLLNVAL | POLLHUP)) {
							cef_log_write (CefC_Log_Warn,
								"Failed to send to Local peer (%d)\n", send_fds[0].fd);
						} else {
							{
								int	fblocks;
								int rem_size;
								int counter;
								int fcntlfl;
								fcntlfl = fcntl (hdl->app_fds[i], F_GETFL, 0);
								fcntl (hdl->app_fds[i], F_SETFL, fcntlfl & ~O_NONBLOCK);
								fblocks = flag / 65535;
								rem_size = flag % 65535;
								for (counter=0; counter<fblocks; counter++){
									send (hdl->app_fds[i], &rsp_msg[counter*65535], 65535, 0);
								}
								if (rem_size != 0){
									send (hdl->app_fds[i], &rsp_msg[fblocks*65535], rem_size, 0);
								}
								fcntl (hdl->app_fds[i], F_SETFL, fcntlfl | O_NONBLOCK);
							}
						}
					}
				}
			} else if (memcmp (buff, CefC_Face_Close, len) == 0) {

				cef_face_close (hdl->app_faces[i]);
				hdl->app_fds[i] = -1;
				hdl->app_fds_num--;

				for (flag = i ; flag < hdl->app_fds_num ; flag++) {
					hdl->app_fds[flag] = hdl->app_fds[flag + 1];
					hdl->app_faces[flag] = hdl->app_faces[flag + 1];
					hdl->app_steps[flag] = hdl->app_steps[flag + 1];
				}
				i--;
			} else {
				cefnetd_input_message_process (
						hdl, CefC_Faceid_Local, hdl->app_faces[i], buff, len, user_id);
			}
		}
		else {
			struct pollfd fds[1];
			fds[0].fd = hdl->app_fds[i];
			fds[0].events = POLLIN | POLLERR;
			poll (fds, 1, 0);
			if ((fds[0].revents & POLLIN) && (fds[0].revents & POLLHUP)) {
				cef_face_close (hdl->app_faces[i]);
				hdl->app_fds[i] = -1;
				hdl->app_fds_num--;

				for (flag = i ; flag < hdl->app_fds_num ; flag++) {
					hdl->app_fds[flag] = hdl->app_fds[flag + 1];
					hdl->app_faces[flag] = hdl->app_faces[flag + 1];
					hdl->app_steps[flag] = hdl->app_steps[flag + 1];
				}
				i--;
			}
		}
	}

	if (!hdl->babel_use_f) {
		if (rsp_msg != NULL){
			free(rsp_msg);
			rsp_msg = NULL;
		}
		return (1);
	}

	/* Obtains the FD for local face 	*/
	sock = cef_face_get_fd_from_faceid (CefC_Faceid_ListenBabel);

	/* Accepts the interrupt from local process */
	if ((work_peer_sock = accept (sock, (struct sockaddr*)&peeraddr, &addrlen)) < 0) {
		/* NOP */;
	} else {
		if (hdl->babel_sock > 0) {
			cef_face_close (hdl->babel_face);
			hdl->babel_sock = -1;
			hdl->babel_face = -1;
		}
		flag = fcntl (work_peer_sock, F_GETFL, 0);
		if (flag < 0) {
			cef_log_write (CefC_Log_Info,
				"<Fail> cefnetd_input_from_local_process (fcntl)\n");
			work_peer_sock = -1;
		} else {
			if (fcntl (work_peer_sock, F_SETFL, flag | O_NONBLOCK) < 0) {
				cef_log_write (CefC_Log_Info,
					"<Fail> cefnetd_input_from_local_process (fcntl)\n");
				work_peer_sock = -1;
			}
		}
		if (work_peer_sock != -1) {
			peer_faceid = cef_face_lookup_local_faceid (work_peer_sock);

			if (peer_faceid < 0) {
				close (work_peer_sock);
			} else {
				hdl->babel_sock = work_peer_sock;
				hdl->babel_face = peer_faceid;
			}
		} else {
			close (work_peer_sock);
		}
	}

	if (hdl->babel_sock < 0) {
		if (rsp_msg != NULL){
			free(rsp_msg);
			rsp_msg = NULL;
		}
		return (1);
	}

	/* Checks whether frame(s) arrivals from the active local faces */
	len = recv (hdl->babel_sock, buff, CefC_Max_Length, 0);

	if (len > 0) {

		if (memcmp (buff, CefC_Ctrl, CefC_Ctrl_Len) == 0) {
			memset (rsp_msg, 0, CefC_Max_Length);
			flag = cefnetd_input_control_message (
					hdl, buff, len, &rsp_msg, hdl->babel_sock);
			if (flag > 0) {
				{
					int	fblocks;
					int rem_size;
					int counter;
					int fcntlfl;
					fcntlfl = fcntl (hdl->babel_sock, F_GETFL, 0);
					fcntl (hdl->babel_sock, F_SETFL, fcntlfl & ~O_NONBLOCK);
					fblocks = flag / 65535;
					rem_size = flag % 65535;
					for (counter=0; counter<fblocks; counter++){
						send (hdl->babel_sock, &rsp_msg[counter*65535], 65535, 0);
					}
					if (rem_size != 0){
						send (hdl->babel_sock, &rsp_msg[fblocks*65535], rem_size, 0);
					}
					fcntl (hdl->babel_sock, F_SETFL, fcntlfl | O_NONBLOCK);
				}
			}
		} else if (memcmp (buff, CefC_Face_Close, len) == 0) {
			cef_face_close (hdl->babel_face);
			hdl->babel_sock = -1;
			hdl->babel_face = -1;
		} else {
			/* NOP */;
		}
	}
	else {
		struct pollfd fds[1];
		int kerrno;
		kerrno = errno;
		fds[0].fd = hdl->babel_sock;
		fds[0].events = POLLIN | POLLERR;
		poll (fds, 1, 0);
		if (fds[0].revents & (POLLIN | POLLHUP)) {
			if((fds[0].revents == POLLIN) && (kerrno == EAGAIN || kerrno == EWOULDBLOCK)) {
				; // NOP
			} else {
				cef_face_close (hdl->babel_face);
				hdl->babel_sock = -1;
				hdl->babel_face = -1;
			}
		}
	}

	if (rsp_msg != NULL){
		free(rsp_msg);
		rsp_msg = NULL;
	}
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the message to reg/dereg application name
----------------------------------------------------------------------------------------*/
static int
cefnetd_input_app_reg_command (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh,				/* Parsed Option Header						*/
	uint16_t faceid
) {
	CefT_App_Reg* wp;
	CefT_Pit_Entry* pe = NULL;

#ifdef CefC_Debug
	memset (cnd_dbg_msg, 0, sizeof (cnd_dbg_msg));
	if (poh->app_reg_f == CefC_App_DeReg) {
		sprintf (cnd_dbg_msg, "Unreg the application name filter [");
	} else if (poh->app_reg_f == CefC_App_Reg){
		sprintf (cnd_dbg_msg, "Reg the application name filter [");
	} else if (poh->app_reg_f == CefC_App_RegPrefix) {
		sprintf (cnd_dbg_msg, "Reg(prefix) the application name filter [");
	} else if (poh->app_reg_f == CefC_App_RegPit) {
		sprintf (cnd_dbg_msg, "Reg(PIT) the application name filter [");
	} else if (poh->app_reg_f == CefC_App_DeRegPit) {
		sprintf (cnd_dbg_msg, "Unreg(PIT) the application name filter [");
	} else if (poh->app_reg_f == CefC_Dev_RegPit){
		sprintf (cnd_dbg_msg, "Reg(PIT) the develop name filter [");
	} else {
		sprintf (cnd_dbg_msg, "Unknown the application name filter [");
	}
	cef_dbg_buff_write_name (CefC_Dbg_Finer,
								(unsigned char*)cnd_dbg_msg, strlen(cnd_dbg_msg),
								pm->name, pm->name_len,
								(unsigned char*)" ]\n", strlen(" ]\n"));
#endif // CefC_Debug

	/* Max Check */
	if ( (poh->app_reg_f == CefC_App_Reg ) || (poh->app_reg_f == CefC_App_RegPrefix) ) {
		if( cef_hash_tbl_item_num_get(hdl->app_reg) == cef_hash_tbl_def_max_get(hdl->app_reg)) {
		cef_log_write (CefC_Log_Warn,
			"FIB(APP) table is full(FIB_SIZE_APP = %d)\n", cef_hash_tbl_def_max_get(hdl->app_reg));
		return (0);
		}
	} else if (poh->app_reg_f == CefC_App_RegPit) {
		if( cef_lhash_tbl_item_num_get(hdl->app_pit) == cef_lhash_tbl_def_max_get(hdl->app_pit)) {
		cef_log_write (CefC_Log_Warn,
			"PIT(APP) table is full(PIT_SIZE_APP = %d)\n", cef_lhash_tbl_def_max_get(hdl->app_pit));
		return (0);
		}
	}
	if (poh->app_reg_f == CefC_Dev_RegPit) {
		if (cef_lhash_tbl_item_num_get(hdl->pit) == cef_lhash_tbl_def_max_get(hdl->pit)) {
			cef_log_write (CefC_Log_Warn,
				"PIT table is full(PIT_SIZE = %d)\n", cef_lhash_tbl_def_max_get(hdl->pit));
			return (0);
		}
	}

	if (poh->app_reg_f == CefC_App_Reg || poh->app_reg_f == CefC_App_RegPrefix) {
		wp = (CefT_App_Reg*) malloc (sizeof (CefT_App_Reg));
		wp->faceid = faceid;
		wp->name_len = pm->name_len;
		memcpy (wp->name, pm->name, pm->name_len);
		if (poh->app_reg_f == CefC_App_Reg) {
			wp->match_type = CefC_App_MatchType_Exact;
		} else {
			wp->match_type = CefC_App_MatchType_Prefix;
		}
		if (CefC_Hash_Faile !=
				cef_hash_tbl_item_set_for_app (hdl->app_reg, pm->name,
											   pm->name_len, wp->match_type, wp)) {
			cefnetd_xroute_change_report (hdl, pm->name, pm->name_len, 1);
		}
		else{
			char uri[2048];
			cef_frame_conversion_name_to_string (pm->name, pm->name_len, uri, "ccnx");
			cef_log_write (CefC_Log_Warn, "This Name[%s] has already been registered or can't register any more, so SKIP register\n", uri);
			if (wp) {
				free(wp);
			}
		}
	} else if (poh->app_reg_f == CefC_App_DeReg) {
		wp = (CefT_App_Reg*)
				cef_hash_tbl_item_remove (hdl->app_reg, pm->name, pm->name_len);

		if (wp) {
			cefnetd_xroute_change_report (hdl, pm->name, pm->name_len, 0);
			free(wp);
		}
	} else if (poh->app_reg_f == CefC_App_RegPit) {
		unsigned char tmp_msg[1024];
		/* Searches a PIT entry matching this Command 	*/
		pe = cef_pit_entry_lookup (hdl->app_pit, pm, poh, NULL, 0);
		if (pe == NULL) {
			char uri[2048];
			cef_frame_conversion_name_to_string (pm->name, pm->name_len, uri, "ccnx");
			cef_log_write (CefC_Log_Warn, "This Name[%s] can't registered in PIT, so SKIP register\n", uri);
			return (1);
		}
		/* Updates the information of down face that this Command arrived 	*/
		cef_pit_entry_down_face_update (pe, faceid, pm, poh, tmp_msg, CefC_IntRetrans_Type_SUP);	//0.8.3
	} else if (poh->app_reg_f == CefC_App_DeRegPit) {
		/* Searches a PIT entry matching this Command 	*/
		pe = cef_pit_entry_search (hdl->app_pit, pm, poh, NULL, 0);
		if (pe != NULL) {
			cef_pit_entry_free (hdl->app_pit, pe);
		}
	} else if (poh->app_reg_f == CefC_Dev_RegPit) {
		int cnt;
		unsigned char tmp_msg[1024];
		int pit_max, pit_size;

		pit_size = cef_lhash_tbl_item_num_get(hdl->pit);
		pit_max = cef_lhash_tbl_def_max_get(hdl->pit);

		if ( pm->InterestType == CefC_PIT_TYPE_Sym ) {
			pe = cef_pit_entry_lookup (hdl->pit, pm, poh, NULL, 0);
			if (pe) {
				cef_pit_entry_down_face_update (pe, faceid, pm, poh, tmp_msg, CefC_IntRetrans_Type_SUP);
				/* Restore the length of the name */
				pm->name_len -= (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
			}
		} else {
			for (cnt = 0; cnt < poh->dev_reg_pit_num;cnt++) {
				if (pit_size < pit_max) {
					uint32_t chunk_num_wk;
					uint16_t chunk_len_wk;
					uint16_t ftvn_chunk;
					int idx = 0;

					chunk_num_wk = htonl(cnt);
					chunk_len_wk = htons(CefC_S_ChunkNum);
					ftvn_chunk = htons (CefC_T_CHUNK);

					memcpy (&(pm->name[pm->name_len+idx]), &ftvn_chunk, CefC_S_Type);
					idx += CefC_S_Type;
					memcpy (&(pm->name[pm->name_len+idx]), &chunk_len_wk, CefC_S_Length);
					idx += CefC_S_Length;
					memcpy (&(pm->name[pm->name_len+idx]), &chunk_num_wk, CefC_S_ChunkNum);
					pm->name_len += (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);

					pe = cef_pit_entry_lookup (hdl->pit, pm, poh, NULL, 0);
					if (pe) {
						cef_pit_entry_down_face_update (pe, faceid, pm, poh, tmp_msg, CefC_IntRetrans_Type_SUP);
						/* Restore the length of the name */
						pm->name_len -= (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
					} else {
						break;
					}
					pit_size++;
				} else {
					cef_log_write (CefC_Log_Warn, "PIT table is full(PIT_SIZE = %d)\n", pit_max);
					break;
				}
			}
		}
	}

	return (1);
}
/*--------------------------------------------------------------------------------------
	Report xroute is changed to cefbabeld
----------------------------------------------------------------------------------------*/
static void
cefnetd_xroute_change_report (
	CefT_Netd_Handle* hdl,
	unsigned char* name,
	uint16_t name_len,
	int reg_f
) {
	unsigned char buff[CefC_Max_Length];
	int rc;
	uint16_t length;

	if ((!hdl->babel_use_f) || (hdl->babel_sock < 0)) {
		return;
	}

	buff[0] = 0x03;
	length = sizeof (uint16_t) + name_len + 1;
	memcpy (&buff[1], &length, sizeof (uint16_t));

	buff[3] = (reg_f) ? 0x01 : 0x00;

	memcpy (&buff[4], &name_len, sizeof (uint16_t));
	memcpy (&buff[4 + sizeof (uint16_t)], name, name_len);

	rc = send (hdl->babel_sock, buff, length + 3, 0);
	if (rc < 0) {
		cef_face_close (hdl->babel_face);
		hdl->babel_sock = -1;
		hdl->babel_face = -1;
	}

	return;
}

/*--------------------------------------------------------------------------------------
	Handles the FIB request message
----------------------------------------------------------------------------------------*/
static int
cefnetd_fib_info_get (
	CefT_Netd_Handle* hdl,
	unsigned char** rsp_msgpp
) {
	uint32_t msg_len = 5; /* code(1byte)+length(4byte) */
	uint32_t length;
	CefT_App_Reg* aentry;
	uint32_t index = 0;
	CefT_Fib_Entry* fentry = NULL;
	int table_num;
	int i;
	char* buff;
	int buff_size;

	buff = (char*)*rsp_msgpp;
	buff_size = CefC_Max_Length;

	if (!hdl->babel_use_f) {
		buff[0] = 0x01;
		length = 0;
		memcpy (&buff[1], &length, sizeof (uint32_t));
		return (5);
	}

	do {
		aentry = (CefT_App_Reg*)
					cef_hash_tbl_item_check_from_index (hdl->app_reg, &index);

		if (aentry) {
			if ((msg_len + sizeof (uint16_t) + aentry->name_len) > buff_size){
				void *new = realloc(buff, buff_size+CefC_Max_Length);
				if (new == NULL) {
					buff[0] = 0x01;
					length = 0;
					memcpy (&buff[1], &length, sizeof (uint32_t));
					return (5);
				}
				buff = new;
				*rsp_msgpp = (unsigned char*)new;
				buff_size += CefC_Max_Length;
			}
			memcpy (&buff[msg_len], &aentry->name_len, sizeof (uint16_t));
			memcpy (&buff[msg_len + sizeof (uint16_t)], aentry->name, aentry->name_len);
			msg_len += sizeof (uint16_t) + aentry->name_len;
		}
		index++;
	} while (aentry);

	table_num = cef_hash_tbl_item_num_get (hdl->fib);
	index = 0;

	for (i = 0; i < table_num; i++) {
		fentry = (CefT_Fib_Entry*) cef_hash_tbl_elem_get (hdl->fib, &index);
		if (fentry == NULL) {
			break;
		}

		/* Face Check */
		{
			CefT_Fib_Face* faces = NULL;
			int			type_c = 0;
			int			type_s = 0;
			int			type_d = 0;

			faces = fentry->faces.next;
			while (faces != NULL) {

				if ( (faces->type >> 2) & 0x01 ) {
					type_c = 1;
				}
				if ( (faces->type >> 1) & 0x01 ) {
					type_s = 1;
				}
				if ( (faces->type) & 0x01 ) {
					type_d = 1;
				}
				faces = faces->next;
			}
			if ( (type_c == 0) && (type_s == 0) && (type_d == 1) ) {
				index++;
				continue;
			}
		}

		if ((msg_len + sizeof (uint16_t) + fentry->klen) > buff_size){
			void *new = realloc(buff, buff_size+CefC_Max_Length);
			if (new == NULL) {
				buff[0] = 0x01;
				length = 0;
				memcpy (&buff[1], &length, sizeof (uint32_t));
				return (5);
			}
			buff = new;
			*rsp_msgpp = (unsigned char*)new;
			buff_size += CefC_Max_Length;
		}
		memcpy (&buff[msg_len], &fentry->klen, sizeof (uint16_t));
		memcpy (&buff[msg_len + sizeof (uint16_t)], fentry->key, fentry->klen);
		msg_len += sizeof (uint16_t) + fentry->klen;
		index++;
	}

	buff[0] = 0x01;
	length = msg_len - 5;
	memcpy (&buff[1], &length, sizeof (uint32_t));

	return ((int) msg_len);
}
/*--------------------------------------------------------------------------------------
	Handles the command from babeld
----------------------------------------------------------------------------------------*/
static int
cefnetd_babel_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	unsigned char* msg,
	int msg_len,
	unsigned char* buff
) {
	uint8_t opetype;
	uint16_t index;
	struct tlv_hdr* tlv_hdr;
	uint16_t prefix_len, prefix_index;
	uint16_t node_len, node_index;
	uint16_t length;
	int rcu, rct;
	char uri[CefC_Max_Length];
//	int change_f;
	int change_f = 0;

	CefT_Fib_Metric		fib_metric;	//0.8.3c

	if (!hdl->babel_use_f) {
		return (0);
	}

#ifdef	_DEBUG_BABEL_
	{
		int dbg_x;
		fprintf (stderr, "[%s] Input msg_len:%d\n", __func__, msg_len );
		fprintf (stderr, "Msg [ ");
		for (dbg_x = 0 ; dbg_x < msg_len ; dbg_x++) {
			fprintf (stderr, "%02x ", msg[dbg_x]);
		}
		fprintf (stderr, "](%d)\n", msg_len);
	}
#endif

	memset( &fib_metric, 0x00, sizeof(CefT_Fib_Metric) );

	/*-----------------------------------------------------------
		Parses the command from babeld
	-------------------------------------------------------------*/
	switch (msg[0]) {
	case 'A': {
		opetype = 0x01;
		break;
	}
	case 'D': {
		opetype = 0x02;
		break;
	}
	default: {
		opetype = 0x00;
		break;
	}
	if (opetype == 0) {
		return (0);
	}
	}
	index = 1;

	memcpy (&length, &msg[index], sizeof (uint16_t));
	index += sizeof (uint16_t);

	/* Obtains the prefix			*/
	tlv_hdr = (struct tlv_hdr*) &msg[index];
	if (tlv_hdr->type != 0x0000) {
		return (0);
	}
	prefix_len   = tlv_hdr->length;
	prefix_index = index + sizeof (struct tlv_hdr);
	index += sizeof (struct tlv_hdr) + prefix_len;

	/* Obtains the nexthop			*/
	tlv_hdr = (struct tlv_hdr*) &msg[index];
#ifdef	_DEBUG_BABEL_
	fprintf (stderr, "tlv_hdr->type %d \n", tlv_hdr->type );
	fprintf (stderr, "tlv_hdr->length %d \n", tlv_hdr->length );
#endif
	if (tlv_hdr->type != 0x0001) {
		return (0);
	}
	node_len   = tlv_hdr->length;
	node_index = index + sizeof (struct tlv_hdr);
	index += sizeof (struct tlv_hdr) + node_len;

	//0.8.3 S
	if ( opetype == 0x01 ) {
		struct value16_tlv*	v16_tlv;
		//Metric
		index += sizeof (unsigned short);
		v16_tlv = (struct value16_tlv*) &msg[index];
#ifdef	_DEBUG_BABEL_
	fprintf (stderr, "index %d \n", index );
	fprintf (stderr, "v16_tlv->type %d \n", v16_tlv->type );
	fprintf (stderr, "v16_tlv->length %d \n", v16_tlv->length );
	fprintf (stderr, "v16_tlv->value %d \n", v16_tlv->value );
#endif
		fib_metric.cost = v16_tlv->value;

		index += sizeof (struct value16_tlv);
		v16_tlv = (struct value16_tlv*) &msg[index];
#ifdef	_DEBUG_BABEL_
	fprintf (stderr, "index %d \n", index );
	fprintf (stderr, "v16_tlv->type %d \n", v16_tlv->type );
	fprintf (stderr, "v16_tlv->length %d \n", v16_tlv->length );
	fprintf (stderr, "v16_tlv->value %d \n", v16_tlv->value );
#endif
		fib_metric.dummy_metric = v16_tlv->value;
	}
	//0.8.3 E

	/*-----------------------------------------------------------
		Creates the FIB route message
	-------------------------------------------------------------*/

	/* Set operation 		*/
	buff[0] = opetype;
	index = 2;

	/* Set URI		 		*/
	cef_frame_conversion_name_to_uri (&msg[prefix_index], prefix_len, uri);
	prefix_len = (uint16_t) strlen (uri);
	memcpy (&buff[index], &prefix_len, sizeof (uint16_t));
	index += sizeof (uint16_t);
	memcpy (&buff[index], uri, prefix_len);
	index += prefix_len;

	/* Set host		 		*/
	buff[index] = (uint8_t) node_len;
	index++;
	memcpy (&buff[index], &msg[node_index], node_len);
	index += node_len;
	rcu = rct = 0;

	/* Update FIB with TCP			*/
	if (hdl->babel_route & 0x01) {
		buff[1] = 0x01;
		rcu = cef_fib_route_msg_read (
//0.8.3c				hdl->fib, buff, index, CefC_Fib_Entry_Dynamic, &change_f);
				hdl->fib, buff, index, CefC_Fib_Entry_Dynamic, &change_f, &fib_metric);		//0.8.3c
	}

	/* Update FIB with UDP			*/
	if (hdl->babel_route & 0x02) {
		buff[1] = 0x02;
		rct = cef_fib_route_msg_read (
//0.8.3c				hdl->fib, buff, index, CefC_Fib_Entry_Dynamic, &change_f);
				hdl->fib, buff, index, CefC_Fib_Entry_Dynamic, &change_f, &fib_metric);		//0.8.3c
	}

	if (rcu + rct) {
		cef_face_update_listen_faces (
			hdl->inudpfds, hdl->inudpfaces, &hdl->inudpfdc,
			hdl->intcpfds, hdl->intcpfaces, &hdl->intcpfdc);
	}

	buff[0] = 0x02;
	buff[1] = 0x01;
	buff[2] = 0x01;

	return (3);
}
/*--------------------------------------------------------------------------------------
	Handles the control message
----------------------------------------------------------------------------------------*/
static int
cefnetd_input_control_message (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	unsigned char* msg, 						/* the received message(s)				*/
	int msg_size,								/* size of received message(s)			*/
	unsigned char** rspp,
	int fd
) {
	int index;
	int res = 0;
	int rc, change_f;
	unsigned char name[CefC_Max_Length];
	int name_len;
	CefT_Cefstatus_Msg	cefstaus_msg;
	uint16_t	out_opt;

	if (memcmp (&msg[CefC_Ctrl_Len], CefC_Ctrl_Kill, CefC_Ctrl_Kill_Len) == 0) {

		index = CefC_Ctrl_Len + CefC_Ctrl_Kill_Len;
		if ((memcmp (&msg[index], root_user_name, CefC_Ctrl_User_Len) == 0) ||
			(memcmp (&msg[index], hdl->launched_user_name, CefC_Ctrl_User_Len) == 0)) {
			cefnetd_running_f = 0;
			return (-1);
		} else {
			cef_log_write (CefC_Log_Warn, "Permission denied (cefnetdstop)\n");
		}
	} else if (
		memcmp (&msg[CefC_Ctrl_Len], CefC_Ctrl_StatusPit, CefC_Ctrl_StatusPit_Len) == 0) {
		res = cef_status_stats_output_pit (hdl);
		return (-1);
	} else if (
		memcmp (&msg[CefC_Ctrl_Len], CefC_Ctrl_StatusStat, CefC_Ctrl_StatusStat_Len) == 0) {
		memset (&cefstaus_msg, 0, sizeof(CefT_Cefstatus_Msg));
		memcpy (cefstaus_msg.msg, CefC_Ctrl_StatusStat, CefC_Ctrl_StatusStat_Len);
		index = CefC_Ctrl_Len + CefC_Ctrl_StatusStat_Len;
		/* output option */
		memcpy (&cefstaus_msg.msg[CefC_Ctrl_StatusStat_Len], &msg[index], sizeof (uint16_t));
		memcpy (&out_opt, &msg[index], sizeof (uint16_t));
		if ( out_opt & CefC_Ctrl_StatusOpt_Numofpit ) {
			index += sizeof (uint16_t);
			uint16_t	numofpit;
			memcpy( &numofpit, &msg[index], sizeof (uint16_t));
			memcpy (&cefstaus_msg.msg[CefC_Ctrl_StatusStat_Len + sizeof (uint16_t)], &numofpit, sizeof (uint16_t));
		}
		cefstaus_msg.resp_fd = fd;
		write (hdl->cefstatus_pipe_fd[0], &cefstaus_msg, sizeof(CefT_Cefstatus_Msg));
		memset (&cefstaus_msg, 0, sizeof(CefT_Cefstatus_Msg) );
		return (-1);
	} else if (
		memcmp (&msg[CefC_Ctrl_Len], CefC_Ctrl_Status, CefC_Ctrl_Status_Len) == 0) {
//		res = cef_status_stats_output (hdl, rspp);
		memset( &cefstaus_msg, 0, sizeof(CefT_Cefstatus_Msg) );
		memcpy( cefstaus_msg.msg, CefC_Ctrl_Status, CefC_Ctrl_Status_Len );
		cefstaus_msg.resp_fd = fd;
		write(hdl->cefstatus_pipe_fd[0], &cefstaus_msg, sizeof(CefT_Cefstatus_Msg));
		memset( &cefstaus_msg, 0, sizeof(CefT_Cefstatus_Msg) );
		return (-1);
	} else if (memcmp (&msg[CefC_Ctrl_Len], CefC_Ctrl_Route, CefC_Ctrl_Route_Len) == 0) {
		index = CefC_Ctrl_Len + CefC_Ctrl_Route_Len;
		if ((memcmp (&msg[index], root_user_name, CefC_Ctrl_User_Len) == 0) ||
			(memcmp (&msg[index], hdl->launched_user_name, CefC_Ctrl_User_Len) == 0)) {
			/* Own IP_addr check */
			unsigned char chk_msg[CefC_Max_Length] = {0};
			memcpy( chk_msg, &msg[index + CefC_Ctrl_User_Len],
						msg_size - (CefC_Ctrl_Len + CefC_Ctrl_Route_Len + CefC_Ctrl_User_Len) );
			res = cefnetd_route_msg_check( hdl, chk_msg, msg_size - (CefC_Ctrl_Len + CefC_Ctrl_Route_Len + CefC_Ctrl_User_Len) );
			if ( res < 0 ) {
				/* Error */
				return(0);
			}

			rc = cef_fib_route_msg_read (
				hdl->fib,
				&msg[index + CefC_Ctrl_User_Len],
				msg_size - (CefC_Ctrl_Len + CefC_Ctrl_Route_Len + CefC_Ctrl_User_Len),
//0.8.3c				CefC_Fib_Entry_Static, &change_f);
				CefC_Fib_Entry_Static, &change_f, NULL);		//0.8.3c
			cef_fib_faceid_cleanup (hdl->fib);
			if (rc > 0) {
				cef_face_update_listen_faces (
					hdl->inudpfds, hdl->inudpfaces, &hdl->inudpfdc,
					hdl->intcpfds, hdl->intcpfaces, &hdl->intcpfdc);
			}
			if (hdl->babel_use_f && change_f) {
				name_len = cef_fib_name_get_from_route_msg (
					&msg[index + CefC_Ctrl_User_Len],
					msg_size - (CefC_Ctrl_Len + CefC_Ctrl_Route_Len + CefC_Ctrl_User_Len),
					name);
				cefnetd_xroute_change_report (
					hdl, name, name_len, (change_f == 0x02) ? 0 : 1);
			}
		} else {
			cef_log_write (CefC_Log_Error, "Permission denied (cefroute)\n");
		}
	} else if (memcmp (&msg[CefC_Ctrl_Len], CefC_Ctrl_Babel, CefC_Ctrl_Babel_Len) == 0) {
		index = CefC_Ctrl_Len + CefC_Ctrl_Babel_Len;

		switch (msg[index]) {
		case 'R': {
			res = cefnetd_fib_info_get (hdl, rspp);
			break;
		}
		case 'A': {
			res = cefnetd_babel_process (hdl, &msg[index],
				msg_size - (CefC_Ctrl_Len + CefC_Ctrl_Babel_Len), *rspp);
			break;
		}
		case 'D': {
			res = cefnetd_babel_process (hdl, &msg[index],
				msg_size - (CefC_Ctrl_Len + CefC_Ctrl_Babel_Len), *rspp);
			break;
		}
		default: {
			break;
		}
		}
	}

	return (res);
}
/*--------------------------------------------------------------------------------------
	Handles the input message
----------------------------------------------------------------------------------------*/
static int
cefnetd_udp_input_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	int faceid									/* Face-ID that message arrived 		*/
) {
	int protocol;
	size_t recv_len;
	int peer_faceid;
	struct addrinfo sas;
	struct addrinfo *sas_p;
	socklen_t sas_len = (socklen_t) sizeof (struct addrinfo);
	unsigned char buff[CefC_Max_Length];

	sas_p = &sas;
	char user_id[512];	//0.8.3

	/* Receives the message(s) from the specified FD */
	recv_len
		= recvfrom (fd, buff, CefC_Max_Length, 0, (struct sockaddr*) &sas, &sas_len);

	// TBD: process for the special message

	/* Looks up the peer Face-ID 		*/
	protocol = cef_face_get_protocol_from_fd (fd);
	peer_faceid = cef_face_lookup_peer_faceid (sas_p/*&sas*/, sas_len, protocol, user_id);	//0.8.3
	if (peer_faceid < 0) {
		return (-1);
	}

#ifdef __APPLE__
	{
		CefT_Face*	face;
		int			ifindex;
		int			rc;

		struct sockaddr_storage ss;
		socklen_t sslen = sizeof(ss);

		if ((rc = getsockname(fd, (struct sockaddr *)&ss, &sslen)) < 0){
			return (-1);
		}
		if (((struct sockaddr *)&ss)->sa_family == AF_INET6) {
			face = cef_face_get_face_from_faceid (peer_faceid);
			if (face->ifindex == -1) {
				CefT_Hash_Handle* sock_tbl;
				CefT_Sock* sock;
				struct in6_pktinfo pinfo;
				char dst_addr[256];
				sock_tbl = cef_face_return_sock_table ();
				sock = (CefT_Sock*) cef_hash_tbl_item_get_from_index (*sock_tbl, face->index);

				{/* get ifindex */
					char	*ifname_p;
					char	name[128];
					int		result;
					result = getnameinfo ((struct sockaddr*) sas_p, sas_len,
											name, sizeof (name), 0, 0, NI_NUMERICHOST);
					if (result != 0) {
						cef_log_write (CefC_Log_Error, "%s (getnameinfo:%s)\n", __func__, gai_strerror(result));
						return (-1);
					}
					ifname_p = strchr(name, '%');
					if(ifname_p == NULL){
						cef_log_write (CefC_Log_Error, "%s IPv6 UDP recive error\n", __func__);
						return (-1);
					}
					ifindex = if_nametoindex(ifname_p+1);
					if (ifindex == 0) {
						cef_log_write (CefC_Log_Error, "%s if_nametoindex(%s) error: %s\n",
										__func__, ifname_p, strerror(errno));
						return (-1);
					 }
				}
				face->ifindex = ifindex;
				if(face->ifindex != -1){
					getnameinfo (sock->ai_addr, sock->ai_addrlen, dst_addr, sizeof(dst_addr),
									NULL, 0, NI_NUMERICHOST);
					if(strncmp(dst_addr, "fe80::", 6) == 0) { /* IPv6 linklocal address */
						pinfo.ipi6_addr = in6addr_any;
						pinfo.ipi6_ifindex = face->ifindex;
						if(setsockopt(sock->sock, IPPROTO_IPV6, IPV6_PKTINFO, &pinfo, sizeof(struct in6_pktinfo)) == -1){
							return(-1);
						}
					}
				}
			}
		}
	}
#endif

#if 0
{
	if ( hdl->bw_stat_hdl->stat_tbl_index_get ) {
		/* Check BW_STAT_I */
		int		bw_stat_idx;
		int		if_name_len;
		char	if_name[128];
		bw_stat_idx = cef_face_bw_stat_i_get( peer_faceid );
#ifdef	__INTEREST__
		fprintf (stderr, "[%s] peer_faceid:%d   bw_stat_idx:%d   Type:%d\n", __func__, peer_faceid, bw_stat_idx,
							cef_face_type_get(peer_faceid) );
#endif
		if ( bw_stat_idx != -1 ) {
			/* NOP */
		} else {
			if_name_len = cef_face_ip_route_get( user_id, if_name );
#ifdef	__INTEREST__
		fprintf (stderr, "[%s] if_name:%s\n", __func__, if_name );
#endif
			if ( if_name_len > 0 ) {
				bw_stat_idx = hdl->bw_stat_hdl->stat_tbl_index_get( if_name );
#ifdef	__INTEREST__
		fprintf (stderr, "[%s] bw_stat_idx:%d\n", __func__, bw_stat_idx );
#endif
				if ( bw_stat_idx >= 0 ) {
					cef_face_bw_stat_i_set( peer_faceid, bw_stat_idx );
				}
			}
		}
	}
}
#endif

	/* Handles the received CEFORE message 	*/
	cefnetd_input_message_process (hdl, faceid, peer_faceid, buff, (int) recv_len, user_id);

	return (1);
}

/*--------------------------------------------------------------------------------------
	Handles the input message from the TCP listen socket
----------------------------------------------------------------------------------------*/
static int
cefnetd_tcp_input_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	int faceid									/* Face-ID that message arrived 		*/
) {
	int recv_len;
	unsigned char buff[CefC_Max_Length];
	char	user_id[512];

	/* Receives the message(s) from the specified FD */
	recv_len = read (fd, buff, CefC_Max_Length);

	if (recv_len <= 0) {
		cef_log_write (CefC_Log_Warn, "Detected Face#%d (TCP) is down\n", faceid);
//		cef_fib_faceid_cleanup (hdl->fib, faceid);
//		cef_face_close (faceid);
		cef_face_close_for_down (faceid);
		cef_face_down (faceid);
		return (1);
	}
	// TBD: process for the special message

	unsigned char peer_node_id[16];
	char		addrstr[256];
	int id_len;
	id_len = cef_face_node_id_get (faceid, peer_node_id);
	if ( id_len == 4 ) {
		inet_ntop (AF_INET, peer_node_id, addrstr, sizeof (addrstr));
	} else if ( id_len == 16 ) {
		inet_ntop (AF_INET6, peer_node_id, addrstr, sizeof (addrstr));
	} else {
		addrstr[0] = 0x00;
	}

	strcpy( user_id, addrstr );
	/* Handles the received CEFORE message 	*/
	cefnetd_input_message_process (hdl, faceid, faceid, buff, recv_len, user_id);

	return (1);
}

/*--------------------------------------------------------------------------------------
	Handles the input message from csmgr
----------------------------------------------------------------------------------------*/
static int
cefnetd_csm_input_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	int faceid									/* Face-ID that message arrived 		*/
) {
#ifdef CefC_ContentStore
	int recv_len;
	unsigned char buff[CefC_Max_Length];

	recv_len = recv (fd, buff, CefC_Max_Length, 0);

	if (recv_len > 0) {
		cefnetd_input_message_from_csmgr_process (hdl, buff, recv_len);
	} else {
		cef_log_write (CefC_Log_Warn,
			"csmgr is down or connection refused, so mode moves to no cache\n");
		csmgr_sock_close (hdl->cs_stat);
//		hdl->cs_stat->cache_type = CefC_Cache_Type_None;
	}
#else
	cef_log_write (CefC_Log_Error, "Invalid input from csmgr\n");
	cefnetd_running_f = 0;
#endif // CefC_ContentStore

	return (1);
}

/*--------------------------------------------------------------------------------------
	Handles the input message from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_input_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	int faceid									/* Face-ID that message arrived 		*/
) {
#ifdef CefC_Ccore
	int recv_len;
	unsigned char buff[CcoreC_Max_Length];
	struct cefore_fixed_hdr* fixed_hdr;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t index = 0;
	uint16_t pkt_len;
	uint16_t msg_type;
	int res;

	recv_len = recv (fd, buff, CcoreC_Max_Length, 0);

	if (recv_len > 0) {
		while (index < recv_len) {
			fixed_hdr = (struct cefore_fixed_hdr*) &buff[index];

			if ((fixed_hdr->version != CcoreC_Cef_Version) ||
				(fixed_hdr->type != CcoreC_PT_CTRL)) {
				index++;
				continue;
			}
			pkt_len = ntohs (fixed_hdr->pkt_len);
			tlv_hdr = (struct ccore_tlv_hdr*) &buff[index + fixed_hdr->hdr_len];
			msg_type = ntohs (tlv_hdr->type) - 0x0F;

			if ((msg_type > CcoreC_Ope_Invalid) && (msg_type < CcoreC_Ope_Num)) {

				res = ccore_valid_msg_verify (&buff[index], pkt_len);

				if (res == 0) {
					(*cefnetd_ccr_operation_process[msg_type]) (
						hdl, fd, &buff[index], pkt_len
					);
				}
#ifdef CefC_Debug
				if (res != 0) {
					cef_dbg_write (CefC_Dbg_Fine,
						"Verify the received ctrl message is NG.\n");
				}
#endif // CefC_Debug
			}
			index += pkt_len;
		}

	} else {
		cef_log_write (CefC_Log_Warn,
			"ccored is down or connection refused, so attempt to reconnect\n");
		ccore_close_socket (hdl->rt_hdl);
		hdl->rt_hdl->sock = -1;
		hdl->rt_hdl->reconnect_time = hdl->nowtus + 3000000;
	}
#else
	cef_log_write (CefC_Log_Error, "Invalid input from ccored\n");
	cefnetd_running_f = 0;
#endif // CefC_Ccore

	return (1);
}
#ifdef CefC_Ccore
/*--------------------------------------------------------------------------------------
	Handles the invalid operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_invalid_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	/* NOP */
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the r-neighbor operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_r_neighbor_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	char info_buff[CcoreC_Max_Length];
	unsigned char resp_buff[CcoreC_Max_Length];
	int res;
	int header_footer_size;

	/* Obtains the neighbor information 	*/
	res = cef_face_neighbor_info_get (info_buff);
	if (res == 0) {
		strcpy (info_buff, "Neighbor is empty\n");
		res = strlen (info_buff);
	}

	/* Creates the Neighbor response 		*/
	{
		unsigned char	work[CcoreC_Max_Length];
		header_footer_size=ccore_frame_neighbor_res_create (work, (const char*)"", 0);
	}
	if ((header_footer_size + res) > CcoreC_Max_Length) {
		res = res - ((header_footer_size + res) - CcoreC_Max_Length);
	}
	{ /* Trim response data (end of data is a line feed code) */
		char* last_nl;
		info_buff[res] = '\0';
		last_nl = strrchr((const char*) info_buff, (int) '\n');
		if (last_nl != NULL){
			*(last_nl+1) = '\0';
			res = strlen((const char*) info_buff);
		}
	}
	res = ccore_frame_neighbor_res_create (resp_buff, info_buff, res);

	/* Sends the Neighbor response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, resp_buff, res);

	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the r-fib operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_r_fib_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	char info_buff[CcoreC_Max_Length] = {};
	unsigned char resp_buff[CcoreC_Max_Length];
	int res;
	struct cefore_fixed_hdr* cefore_hdr;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t type, length;
	uint16_t index;
	uint16_t name_len, name_index;
	int header_footer_size;

	/*-----------------------------------------------------------
		Parses the FIB retrieve request
	-------------------------------------------------------------*/

	cefore_hdr = (struct cefore_fixed_hdr*) &msg[0];
	index = cefore_hdr->hdr_len + CcoreC_S_TL;

	/* Obtains the length and offset of Name from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CefC_T_NAME) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (s-fib-add) has not NAME TLV\n");
		return (0);
	}
	name_index = index;
	name_len   = length;
	index += length;

	/*-----------------------------------------------------------
		Creates and send the FIB retrieve response
	-------------------------------------------------------------*/

	/* Obtains the matched FIB information 		*/
	res = cef_fib_info_get (
		&hdl->fib, info_buff, &msg[name_index], name_len, cefore_hdr->flag);
	if (res == 0) {
		strcpy (info_buff, "No entry has the specified prefix");
		res = strlen (info_buff);
	}

	/* Creates the FIB retrieve response 		*/
	{
		unsigned char	work[CcoreC_Max_Length];
		header_footer_size=ccore_frame_fib_res_create (work, (const char*)"", 0);
	}
	if ((header_footer_size + res) > CcoreC_Max_Length) {
		res = res - ((header_footer_size + res) - CcoreC_Max_Length);
	}
	{ /* Trim response data (end of data is a line feed code) */
		char* last_nl;
		info_buff[res] = '\0';
		last_nl = strrchr((const char*) info_buff, (int) '\n');
		if (last_nl != NULL){
			*(last_nl+1) = '\0';
			res = strlen((const char*) info_buff);
		}
	}
	res = ccore_frame_fib_res_create (resp_buff, info_buff, res);

	/* Sends the FIB retrieve response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, resp_buff, res);

	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the s-fib-add operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_s_fib_add_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length];
	char uri[CcoreC_Max_Length];
	struct cefore_fixed_hdr* cefore_hdr;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t name_len, name_index;
	uint16_t node_len, node_index;
	uint16_t uri_len;
	uint16_t index;
	uint16_t type, length;
	int res;
	int change_f;
	char	tmp_node[256] ={0};

	/*-----------------------------------------------------------
		Parses the FIB add request
	-------------------------------------------------------------*/
	cefore_hdr = (struct cefore_fixed_hdr*) &msg[0];
	index = cefore_hdr->hdr_len + CcoreC_S_TL;

	/* Obtains the length and offset of Name from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CefC_T_NAME) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (s-fib-add) has not NAME TLV\n");
		return (0);
	}
	name_index = index;
	name_len   = length;
	index += length;

	/* Obtains the length and offset of Node from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CcoreC_T_NODE) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (s-fib-add) has not NODE TLV\n");
		return (0);
	}
	node_index = index;
	node_len   = length;
	index += length;

	/*-----------------------------------------------------------
		Creates the FIB route message
	-------------------------------------------------------------*/
	index = 0;

	/* Set operation 		*/
	buff[index] = 0x01;
	index++;

	/* Set protocol 		*/
	buff[index] = cefore_hdr->flag;
	index++;

	/* Set URI		 		*/
	cef_frame_conversion_name_to_uri (&msg[name_index], name_len, uri);
	uri_len = (uint16_t) strlen (uri);
	memcpy (&buff[index], &uri_len, sizeof (uri_len));
	index += sizeof (uri_len);
	memcpy (&buff[index], uri, uri_len);
	index += uri_len;

	/* Set host		 		*/
	buff[index] = (uint8_t) node_len;
	index++;
	memcpy (&buff[index], &msg[node_index], node_len);
	index += node_len;

	memcpy (tmp_node, &msg[node_index], node_len);
	if ( cefnetd_ccr_s_fib_node_chack( hdl, tmp_node ) < 0 ) {
		cef_log_write (CefC_Log_Error,
			" Received message (s-fib-add) Next_Hop is Own Address.\n");
		res = ccore_frame_fib_add_res_create (buff, -1);
		ccore_send_msg (hdl->rt_hdl, buff, res);
		return(1);
	}

	/* Update FIB 			*/
//0.8.3c	res = cef_fib_route_msg_read (hdl->fib, buff, index, CefC_Fib_Entry_Ctrl, &change_f);
	res = cef_fib_route_msg_read (hdl->fib, buff, index, CefC_Fib_Entry_Ctrl, &change_f, NULL);		//0.8.3c
	if (res > 0) {
		cef_face_update_listen_faces (
			hdl->inudpfds, hdl->inudpfaces, &hdl->inudpfdc,
			hdl->intcpfds, hdl->intcpfaces, &hdl->intcpfdc);
	}
	if (hdl->babel_use_f && change_f) {
		cefnetd_xroute_change_report (
			hdl, &msg[name_index], name_len, (change_f == 0x02) ? 0 : 1);
	}

	/*-----------------------------------------------------------
		Creates and send the FIB add response
	-------------------------------------------------------------*/
	res = ccore_frame_fib_add_res_create (buff, res);

	/* Sends the FIB add response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, res);

	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the s-fib-del operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_s_fib_del_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length];
	char uri[CcoreC_Max_Length];
	struct cefore_fixed_hdr* cefore_hdr;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t name_len, name_index;
	uint16_t node_len, node_index;
	uint16_t uri_len;
	uint16_t index;
	uint16_t type, length;
	int res;
	int change_f;
	char	tmp_node[256] ={0};

	/*-----------------------------------------------------------
		Parses the FIB del request
	-------------------------------------------------------------*/
	cefore_hdr = (struct cefore_fixed_hdr*) &msg[0];
	index = cefore_hdr->hdr_len + CcoreC_S_TL;

	/* Obtains the length and offset of Name from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CefC_T_NAME) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (s-fib-del) has not NAME TLV\n");
		return (0);
	}
	name_index = index;
	name_len   = length;
	index += length;

	/* Obtains the length and offset of Node from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CcoreC_T_NODE) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (s-fib-del) has not NODE TLV\n");
		return (0);
	}
	node_index = index;
	node_len   = length;
	index += length;

	/*-----------------------------------------------------------
		Creates the FIB route message
	-------------------------------------------------------------*/
	index = 0;

	/* Set operation 		*/
	buff[index] = 0x02;
	index++;

	/* Set protocol 		*/
	buff[index] = cefore_hdr->flag;
	index++;

	/* Set URI		 		*/
	cef_frame_conversion_name_to_uri (&msg[name_index], name_len, uri);
	uri_len = (uint16_t) strlen (uri);
	memcpy (&buff[index], &uri_len, sizeof (uri_len));
	index += sizeof (uri_len);
	memcpy (&buff[index], uri, uri_len);
	index += uri_len;

	/* Set host		 		*/
	buff[index] = (uint8_t) node_len;
	index++;
	memcpy (&buff[index], &msg[node_index], node_len);
	index += node_len;

	memcpy (tmp_node, &msg[node_index], node_len);
	if ( cefnetd_ccr_s_fib_node_chack( hdl, tmp_node ) < 0 ) {
		cef_log_write (CefC_Log_Error,
			" Received message (s-fib-del) Next_Hop is Own Address.\n");
		res = ccore_frame_fib_add_res_create (buff, -1);
		ccore_send_msg (hdl->rt_hdl, buff, res);
		return(1);
	}

	/* Update FIB 			*/
	res = cef_fib_route_msg_read (hdl->fib, buff, index, CefC_Fib_Entry_Ctrl, &change_f, NULL);
	if (res > 0) {
		cef_face_update_listen_faces (
			hdl->inudpfds, hdl->inudpfaces, &hdl->inudpfdc,
			hdl->intcpfds, hdl->intcpfaces, &hdl->intcpfdc);
	}
	if (hdl->babel_use_f && change_f) {
		cefnetd_xroute_change_report (
			hdl, &msg[name_index], name_len, (change_f == 0x02) ? 0 : 1);
	}

	/*-----------------------------------------------------------
		Creates and send the FIB del response
	-------------------------------------------------------------*/
	res = ccore_frame_fib_del_res_create (buff, res);

	/* Sends the FIB add response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, res);

	return (1);
}

/*--------------------------------------------------------------------------------------
	Check Input NodeId the s-fib from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_s_fib_node_chack(
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	char* node
) {
	int		i_4;
	int		i_16;

	struct	in_addr		input_v4_addr;
	struct	in_addr		node_v4_addr;
	struct	in6_addr	input_v6_addr;
	struct	in6_addr	node_v6_addr;
	uint16_t	in_port = 0;
	long		port_l;
	char*		end_p;
	int		rc;
	char	v6_host[64] = {0};
	char	v4_host[64] = {0};

		if 	( node[0] == '[' ) {	//IPv6
			{
			int	diff1;
			int	diff2;
			char*	p1;
			char*	p2;
			p1 = strchr( &node[1], ']' );
			if ( p1 == NULL ) {
				/* Error */
				cef_log_write (CefC_Log_Error, "Failed cefroute command.(']' NotFound:%s)\n", node);
			}
			diff1 = p1 - &node[1];
			p2 = strchr( &node[1], '%' );
			if ( p2 == NULL ) {
				diff2 = diff1;
			} else {
				diff2 = p2 - &node[1];
			}
			strncpy( v6_host, &node[1], diff2 );
			diff1+=2;
			if ( node[diff1] == ':' ) {
				diff1++;
				port_l = strtol( &node[diff1], &end_p, 10 );
				if ( &node[diff1] == end_p ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", node);
					return(-1);
				}
				if ( (port_l > INT_MAX) || (port_l < INT_MIN) ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", node);
					return(-1);
				}
				if ( (port_l < 0) || (port_l > 65535) ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", node);
					return(-1);
				}
				in_port = (uint16_t)port_l;
			} else {
				in_port = hdl->port_num;
			}
			}
			rc = inet_pton( AF_INET6, v6_host, &input_v6_addr );
			if ( rc != 1 ) {
				/* Error */
				cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid address:%s)\n", node);
				return(-1);
			}
			for ( i_16 = 0; i_16 < hdl->nodeid16_num; i_16++ ) {
				rc = inet_pton( AF_INET6, hdl->nodeid16_c[i_16], &node_v6_addr );
				if ( memcmp( &input_v6_addr, &node_v6_addr, 16 ) == 0 ) {
					if ( in_port == hdl->port_num ) {
						/* Error */
						cef_log_write (CefC_Log_Error, "Failed cefroute command.(Own address:%s)\n", node);
						return(-1);
					}
				}
			}
		} else {					//IPv4
			{
			int	diff1;
			char*	p1;
			p1 = strchr( &node[0], ':' );
			if ( p1 == NULL ) {
				in_port = hdl->port_num;
				strcpy( v4_host, node );
			} else {
				diff1 = p1 - &node[0];
				strncpy( v4_host, &node[0], diff1 );
				diff1++;
				port_l = strtol( &node[diff1], &end_p, 10 );
				if ( &node[diff1] == end_p ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", node);
					return(-1);
				}
				if ( (port_l > INT_MAX) || (port_l < INT_MIN) ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", node);
					return(-1);
				}
				if ( (port_l < 0) || (port_l > 65535) ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", node);
					return(-1);
				}
				in_port = (uint16_t)atoi( &node[diff1] );
			}
			}
			rc = inet_pton( AF_INET, v4_host, &input_v4_addr );
			if ( rc != 1 ) {
				/* Error */
				cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid address:%s)\n", node);
				return(-1);
			}
			for ( i_4 = 0; i_4 < hdl->nodeid4_num; i_4++ ) {
				rc = inet_pton( AF_INET, hdl->nodeid4_c[i_4], &node_v4_addr );
				if ( memcmp( &input_v4_addr, &node_v4_addr, 4 ) == 0 ) {
					if ( in_port == hdl->port_num ) {
						/* Error */
						cef_log_write (CefC_Log_Error, "Failed cefroute command.(Own address:%s)\n", node);
						return(-1);
					}
				}
			}
		}
	return(0);

}


/*--------------------------------------------------------------------------------------
	Handles the r-cache-prefix operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_r_cache_prefix_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length] = {0};
	char uri[CcoreC_Max_Length*2];
	int res;
	uint8_t result = 0;
#ifdef CefC_ContentStore
	struct cefore_fixed_hdr* cefore_hdr;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t name_len, name_index;
	uint16_t index;
	uint16_t type;
	uint16_t length;
	uint64_t lifetime = 0;
#endif // CefC_ContentStore

	if ((hdl->cs_stat == NULL) || (hdl->cs_stat->cache_type == CefC_Cache_Type_None)) {
		cef_log_write (CefC_Log_Info, "Incoming ccore message, but CS is not used.\n");
		res = sprintf (uri, "cefnetd don't use CS\n");
		res = ccore_frame_cache_prefix_res_create (buff, uri, res, result);
		ccore_send_msg (hdl->rt_hdl, buff, res);
		return (0);
	}
#ifdef CefC_ContentStore
	/*-----------------------------------------------------------
		Parses the Cache Prefix Retrieve request
	-------------------------------------------------------------*/
	cefore_hdr = (struct cefore_fixed_hdr*) &msg[0];
	index = cefore_hdr->hdr_len + CcoreC_S_TL;

	/* Obtains the length and offset of Name from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CefC_T_NAME) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (r-cache-prefix) has not NAME TLV\n");
		return (0);
	}
	name_index = index;
	name_len   = length;
	index += length;

	/*-----------------------------------------------------------
		Creates and send the Cache Prefix Retrieve response
	-------------------------------------------------------------*/

	/* Obtains the lifetime of the specified Prefix 		*/
	res = cef_csmgr_con_lifetime_retrieve (
				hdl->cs_stat, (char*)&msg[name_index], name_len, &lifetime);
	if (res < 0) {
		/* case : error */
		sprintf (uri, "Failed to acquire the value inside Content Store.");
		result = 0;
	} else {
		cef_frame_conversion_name_to_uri (&msg[name_index], name_len, (char*)buff);
		if (lifetime) {
			/* usec -> sec */
			lifetime = lifetime / 1000000;
		}
		sprintf ((char*) uri, "%s lifetime "FMTU64"", buff, lifetime);
		result = 1;
	}
	res = (int) strlen ((char*) uri);

	/* Creates the Cache Prefix Retrieve response 		*/
	res = ccore_frame_cache_prefix_res_create (buff, uri, res, result);

	/* Sends the FIB del response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, res);
#endif // CefC_ContentStore
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the r-cache-capacity operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_r_cache_cap_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length] = {0};
	char capa[CcoreC_Max_Length] = {0};
	int res;
	uint8_t result = 0;
#ifdef CefC_ContentStore
	uint64_t cap;
#endif // CefC_ContentStore

	if ((hdl->cs_stat == NULL) || (hdl->cs_stat->cache_type == CefC_Cache_Type_None)) {
		cef_log_write (CefC_Log_Info, "Incoming ccore message, but CS is not used.\n");
		res = sprintf (capa, "cefnetd don't use CS\n");
		res = ccore_frame_cache_cap_retrieve_res_create (buff, capa, res, result);
		ccore_send_msg (hdl->rt_hdl, buff, res);
		return (0);
	}
#ifdef CefC_ContentStore
	/*-----------------------------------------------------------
		Creates and send the Cache Capacity Retrieve response
	-------------------------------------------------------------*/
	/* Obtains the capacity of Content Store		*/
	res = cef_csmgr_capacity_retrieve (hdl->cs_stat, &cap);
	if (res < 0) {
		/* case : error */
		// sprintf (capa, "Connection Failed\n");
		result = 0;
	} else {
		if (cap == 0) {
			sprintf (capa, "Unlimited\n");
		} else {
			sprintf (capa, ""FMTU64"\n", cap);
		}
		result = 1;
	}
	res = (int) strlen (capa);

	/* Creates the Cache Capacity Retrieve response 		*/
	res = ccore_frame_cache_cap_retrieve_res_create (buff, capa, res, result);

	/* Sends the Cache Capacity Retrieve response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, res);
#endif // CefC_ContentStore
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the s-cache-capacity operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_s_cache_cap_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length] = {0};
	int res;
#ifdef CefC_ContentStore
	uint64_t cap;
	struct cefore_fixed_hdr* cefore_hdr;
	uint16_t index;
	uint16_t length;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t cap_index;
#endif // CefC_ContentStore

	if ((hdl->cs_stat == NULL) || (hdl->cs_stat->cache_type == CefC_Cache_Type_None)) {
		cef_log_write (CefC_Log_Info, "Incoming ccore message, but CS is not used.\n");
		res = ccore_frame_cache_lifetime_res_create (buff, 0);
		ccore_send_msg (hdl->rt_hdl, buff, res);
		return (0);
	}
#ifdef CefC_ContentStore
	/*-----------------------------------------------------------
		Creates and send the Cache Capacity Update response
	-------------------------------------------------------------*/
	cefore_hdr = (struct cefore_fixed_hdr*) &msg[0];
	index = cefore_hdr->hdr_len + CcoreC_S_TL;
	/* Obtains the length and offset of capacity from the Cefore Message */
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);
	cap_index = index;
	index += length;
	cap = strtoull ((const char*)&msg[cap_index], NULL, 0);

	/* Update the capacity of Content Store		*/
	res = cef_csmgr_capacity_update (hdl->cs_stat, cap);
	if (res < 1) {
		/* case : error */
		res = 0;
	} else {
		res = 1;
	}
	/* Creates the Cache Capacity Update response 		*/
	res = ccore_frame_cache_cap_update_res_create (buff, res);

	/* Sends the Cache Capacity Update response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, res);
#endif // CefC_ContentStore
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the s-cache-lifetime operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_s_lifetime_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length];
	int res;
	uint8_t result = 0;
#ifdef CefC_ContentStore
	struct cefore_fixed_hdr* cefore_hdr;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t name_len, name_index;
	uint16_t index;
	uint16_t type, length;
	uint16_t life_len, life_index;
	char life_str[CcoreC_Max_Length];
	uint64_t life_val;
#endif // CefC_ContentStore

	if ((hdl->cs_stat == NULL) || (hdl->cs_stat->cache_type == CefC_Cache_Type_None)) {
		cef_log_write (CefC_Log_Info, "Incoming ccore message, but CS is not used.\n");
		// sprintf (err_msg, "Content store is not used.\n");
		res = ccore_frame_cache_lifetime_res_create (buff, result);
		ccore_send_msg (hdl->rt_hdl, buff, res);
		return (0);
	}
#ifdef CefC_ContentStore
	/*-----------------------------------------------------------
		Parses the Cache Lifetime request
	-------------------------------------------------------------*/
	cefore_hdr = (struct cefore_fixed_hdr*) &msg[0];
	index = cefore_hdr->hdr_len + CcoreC_S_TL;

	/* Obtains the length and offset of Name from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CefC_T_NAME) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (s-cache-lifetime) has not NAME TLV\n");
		return (0);
	}
	name_index = index;
	name_len   = length;
	index += length;

	/* Obtains the length and offset of lifetime from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CcoreC_T_NEWLIFETIME) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (s-cache-lifetime) has not T_NEWLIFETIME TLV\n");
		return (0);
	}
	life_index = index;
	life_len = length;
	index += length;

	/*-----------------------------------------------------------
		Creates and send the Cache Lifetime response
	-------------------------------------------------------------*/

	/* Update the lifetime of the specified content		*/
	memcpy (life_str, &msg[life_index], life_len);
	life_str[life_len] = 0;
	life_val = strtoull (life_str, NULL, 0);

	/* Obtains the lifetime of the specified Prefix 		*/
	res = cef_csmgr_con_lifetime_set (
				hdl->cs_stat, (char*)&msg[name_index], name_len, life_val);
	if (res < 0) {
		/* case : error */
		result = 0;
	} else {
		result = 1;
	}

	/* Creates the Cache Prefix Retrieve response 		*/
	res = ccore_frame_cache_lifetime_res_create (buff, result);

	/* Sends the FIB del response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, res);
#endif // CefC_ContentStore
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the s-write-setting operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_s_write_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length];
	int ret = 0;
	char src_ws[1024];
	char dst_ws[2048];
	FILE* src_fp = NULL;
	CefT_Hash_Handle* hash_handle = &hdl->fib;
	CefT_Fib_Entry* fib_entry;
	int i;
	int res;
	uint32_t index = 0;
	char uri[CefC_Max_Length];
	CefT_Fib_Face* faces;
	int fib_table_num;
	CefT_Face* face;
	char prot_str[3][16] = {"invalid", "tcp", "udp"};
	CefT_Sock* sock;
	CefT_Hash_Handle* sock_tbl;
	char node[NI_MAXHOST];

	/*-----------------------------------------------------------
		Creates backup file
	-------------------------------------------------------------*/
	/* Get directory */
	cef_client_config_dir_get (src_ws);
	if (mkdir (src_ws, 0777) != 0) {
		if (errno == ENOENT) {
			goto CCR_S_WRITE_POST;
		}
	}
	/* Create file name */
	sprintf (src_ws + strlen (src_ws), "/cefnetd.fib");
	sprintf (dst_ws , "%s.sav", src_ws);
	/* Create backup file */
	if (rename (src_ws, dst_ws) < 0) {
		cef_log_write (CefC_Log_Error,
			"<Fail> Write config file (%s)\n", strerror (errno));
		goto CCR_S_WRITE_POST;
	}

	/*-----------------------------------------------------------
		Creates and send the Write Memory response
	-------------------------------------------------------------*/
	/* Open file */
	src_fp = fopen (src_ws, "w");
	if (src_fp == NULL) {
		cef_log_write (CefC_Log_Error,
			"<Fail> Write config file (%s)\n", strerror (errno));
		goto CCR_S_WRITE_POST;
	}
	ret = 1;
	/* Get FIB entry num */
	fib_table_num = cef_hash_tbl_item_num_get (*hash_handle);
	if (fib_table_num == 0) {
		/* FIB entry is empty */
		goto CCR_S_WRITE_POST;
	}
	/* get socket table	*/
	sock_tbl = cef_face_return_sock_table ();
	/* Write to file */
	for (i = 0; i < fib_table_num; i++) {
		/* Get entry */
		fib_entry = (CefT_Fib_Entry*) cef_hash_tbl_elem_get (*hash_handle, &index);
		if (fib_entry == NULL) {
			break;
		}

		/* Get uri */
		res = cef_frame_conversion_name_to_uri (fib_entry->key, fib_entry->klen, uri);
		if (res < 0) {
			index++;
			continue;
		}
		/* Get Faces */
		faces = fib_entry->faces.next;
		/* Output TCP Faces */
		while (faces != NULL) {
			/* Check the bit is not dynamic bit only 	*/
			if (faces->type < CefC_Fib_Entry_Static) {
				faces = faces->next;
				continue;
			}

			/* Get Face info */
			face = cef_face_get_face_from_faceid (faces->faceid);
			/* Get sock */
			sock = (CefT_Sock*) cef_hash_tbl_item_get_from_index (
						*sock_tbl, face->index);
			if (sock == NULL) {
				faces = faces->next;
				continue;
			}
			/* Get Address */
			res = getnameinfo (sock->ai_addr, sock->ai_addrlen, node, sizeof(node),
							NULL, 0, NI_NUMERICHOST);
			if (res == 0) {
				/* Write information */
				fprintf (src_fp, "%s %s %s:%d\n"
					, uri, prot_str[sock->protocol], node, sock->port_num);
			}
			faces = faces->next;
		}
		index++;
	}
	ret = 1;

CCR_S_WRITE_POST:
	if (src_fp) {
		fclose (src_fp);
	}
	/* Creates the Write response 		*/
	ret = ccore_frame_write_memory_res_create (buff, ret);
	/* Sends the FIB del response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, ret);

	return (ret);
}
/*--------------------------------------------------------------------------------------
	Handles the r-status operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_r_status_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	// TODO
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the s-status operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_s_status_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	// TODO
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the r-cache-chunk operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_r_cache_chunk_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length] = {0};
	char info_buff[CcoreC_Max_Length] = {0};
	int res;
	uint8_t result = 0;
#ifdef CefC_ContentStore
	struct cefore_fixed_hdr* cefore_hdr;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t name_len, name_index;
	uint16_t range_len, range_index;
	uint16_t index;
	uint16_t type, length;
#endif // CefC_ContentStore
	int header_footer_size;

	if ((hdl->cs_stat == NULL) || (hdl->cs_stat->cache_type == CefC_Cache_Type_None)) {
		cef_log_write (CefC_Log_Info, "Incoming ccore message, but CS is not used.\n");
		res = sprintf (info_buff, "cefnetd don't use CS\n");
		res = ccore_frame_cache_chunk_res_create (buff, info_buff, res, result);
		ccore_send_msg (hdl->rt_hdl, buff, res);
		return (0);
	}
#ifdef CefC_ContentStore
	/*-----------------------------------------------------------
		Parses the Cache Chunk Retrieve request
	-------------------------------------------------------------*/
	cefore_hdr = (struct cefore_fixed_hdr*) &msg[0];
	index = cefore_hdr->hdr_len + CcoreC_S_TL;

	/* Obtains the length and offset of Name from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CefC_T_NAME) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (r-cache-chunk) has not NAME TLV\n");
		return (0);
	}
	name_index = index;
	name_len   = length;
	index += length;

	/* Obtains the length and offset of Chunk from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CcoreC_T_RANGE) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (r-cache-chunk) has not RANGE TLV\n");
		return (0);
	}
	range_index = index;
	range_len   = length;
	index += length;

	/*-----------------------------------------------------------
		Creates and send the Cache Chunk Retrieve response
	-------------------------------------------------------------*/

	/* Obtains the Cache Chunk of the specified Prefix 		*/
	res = cef_csmgr_con_chunk_retrieve (
				hdl->cs_stat, (char*)&msg[name_index], name_len,
				(char*)&msg[range_index], range_len, info_buff);
	if (res < 0) {
		/* case : error */
		if (info_buff[0] != 0x00) {
			result = 1;
		} else {
			sprintf (info_buff, "Failed to acquire the value inside Content Store.\n");
			result = 0;
		}
	} else {
		result = 1;
	}
	res = (int) strlen ((char*) info_buff);

	/* Creates the Cache Chunk Retrieve response 		*/
	{
		unsigned char	work[CcoreC_Max_Length];
		header_footer_size=ccore_frame_cache_chunk_res_create (work, (const char*)"", 0, result);
	}
	if ((header_footer_size + res) > CcoreC_Max_Length) {
		res = res - ((header_footer_size + res) - CcoreC_Max_Length);
	}
	{ /* Trim response data (end of data is a comma(',')) */
		char* last_comma;
		info_buff[res] = '\0';
		last_comma = strrchr((const char*) info_buff, (int) ',');
		if (last_comma != NULL){
			*(last_comma+1) = '\0';
			res = strlen((const char*) info_buff);
		}
	}

	res = ccore_frame_cache_chunk_res_create (buff, info_buff, res, result);

	/* Sends the Cache Chunk response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, res);
#endif // CefC_ContentStore
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the s-cache-del operation from ccored
----------------------------------------------------------------------------------------*/
static int
cefnetd_ccr_s_cache_del_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int fd, 									/* FD which is polled POLLIN			*/
	unsigned char* msg,
	int msg_len
) {
	unsigned char buff[CcoreC_Max_Length] = {0};
	int res;
	uint8_t result = 0;
#ifdef CefC_ContentStore
	struct cefore_fixed_hdr* cefore_hdr;
	struct ccore_tlv_hdr* tlv_hdr;
	uint16_t name_len, name_index;
	uint16_t range_len, range_index;
	uint16_t index;
	uint16_t type, length;
#endif // CefC_ContentStore

	if ((hdl->cs_stat == NULL) || (hdl->cs_stat->cache_type == CefC_Cache_Type_None)) {
		cef_log_write (CefC_Log_Info, "Incoming ccore message, but CS is not used.\n");
		res = ccore_frame_cache_del_res_create (buff, result);
		ccore_send_msg (hdl->rt_hdl, buff, res);
		return (0);
	}

#ifdef CefC_ContentStore
	/*-----------------------------------------------------------
		Parses the Cache Chunk Delete request
	-------------------------------------------------------------*/
	cefore_hdr = (struct cefore_fixed_hdr*) &msg[0];
	index = cefore_hdr->hdr_len + CcoreC_S_TL;

	/* Obtains the length and offset of Name from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CefC_T_NAME) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (r-cache-del) has not NAME TLV\n");
		return (0);
	}
	name_index = index;
	name_len   = length;
	index += length;

	/* Obtains the length and offset of Chunk from the Cefore Message		*/
	tlv_hdr = (struct ccore_tlv_hdr*) &msg[index];
	type   = ntohs (tlv_hdr->type);
	length = ntohs (tlv_hdr->length);
	index += sizeof (struct ccore_tlv_hdr);

	if (type != CcoreC_T_RANGE) {
		cef_log_write (CefC_Log_Warn,
			"Error: Received message (r-cache-del) has not RANGE TLV\n");
		return (0);
	}
	range_index = index;
	range_len   = length;
	index += length;

	/*-----------------------------------------------------------
		Creates and send the Cache Chunk Delete response
	-------------------------------------------------------------*/

	/* Obtains the Cache Chunk of the specified Prefix 		*/
	res = cef_csmgr_con_chunk_delete (
				hdl->cs_stat, (char*)&msg[name_index], name_len,
				(char*)&msg[range_index], range_len);
	if (res < 0) {
		/* case : error */
		result = 0;
	} else {
		result = 1;
	}

	/* Creates the Cache Chunk Retrieve response 		*/
	res = ccore_frame_cache_del_res_create (buff, result);

	/* Sends the Cache Chunk response to ccored 	*/
	ccore_send_msg (hdl->rt_hdl, buff, res);
#endif // CefC_ContentStore
	return (1);
}

#endif // CefC_Ccore
/*--------------------------------------------------------------------------------------
	Handles the received message(s)
----------------------------------------------------------------------------------------*/
static int										/* No care now							*/
cefnetd_input_message_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	int faceid, 								/* Face-ID where messages arrived at	*/
	int peer_faceid, 							/* Face-ID to reply to the origin of 	*/
												/* transmission of the message(s)		*/
	unsigned char* msg, 						/* the received message(s)				*/
	int msg_size,								/* size of received message(s)			*/
	char* user_id
) {
	CefT_Face* face;
	unsigned char* wp;
	uint16_t move_len;
	uint16_t fdv_payload_len;
	uint16_t fdv_header_len;
	int res;

#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Inputs CEFORE message(s) from Face#%d\n", peer_faceid);
	cef_dbg_buff_write (CefC_Dbg_Finest, msg, msg_size);
#endif // CefC_Debug

	/* Obtains the face structure corresponding to the peer Face-ID 	*/
	face = cef_face_get_face_from_faceid (peer_faceid);

	/* Handles the received message(s) 		*/
	while (msg_size > 0) {
		/* Calculates the size of the message which have not been yet handled 	*/
		if (msg_size > CefC_Max_Length - face->len) {
			move_len = CefC_Max_Length - face->len;
		} else {
			move_len = (uint16_t) msg_size;
		}
		msg_size -= move_len;

		/* Updates the receive buffer 		*/
		memcpy (face->rcv_buff + face->len, msg, move_len);
		face->len += move_len;
		msg += move_len;

		while (face->len > 0) {
			/* Seeks the top of the message */
			res = cefnetd_messege_head_seek (face, &fdv_payload_len, &fdv_header_len);
			if (res < 0) {
				break;
			}

			/* Calls the function corresponding to the type of the message 	*/
			if (face->rcv_buff[1] > CefC_PT_MAX) {
				cef_log_write (CefC_Log_Warn,
					"Detects the unknown PT_XXX=%d\n", face->rcv_buff[1]);
			} else {
				(*cefnetd_incoming_msg_process[face->rcv_buff[1]])
					(hdl, faceid, peer_faceid,
							face->rcv_buff, fdv_payload_len, fdv_header_len, user_id);
			}

			/* Updates the receive buffer 		*/
			move_len = fdv_payload_len + fdv_header_len;

			wp = face->rcv_buff + move_len;
			memmove (face->rcv_buff, wp, face->len - move_len);
			face->len -= move_len;
		}
	}

	return (1);
}
#ifdef CefC_ContentStore
/*--------------------------------------------------------------------------------------
	Handles the received message(s) from csmgr
----------------------------------------------------------------------------------------*/
static int										/* No care now							*/
cefnetd_input_message_from_csmgr_process (
	CefT_Netd_Handle* hdl,						/* cefnetd handle						*/
	unsigned char* msg, 						/* the received message(s)				*/
	int msg_size								/* size of received message(s)			*/
) {
	unsigned char* wp;
	uint16_t move_len;
	uint16_t fdv_payload_len;
	uint16_t fdv_header_len;
	int res;
	char	user_id[512];
	/* Handles the received message(s) 		*/
	while (msg_size > 0) {
		/* Calculates the size of the message which have not been yet handled 	*/
		if (msg_size > CefC_Max_Length - hdl->cs_stat->rcv_len) {
			move_len = CefC_Max_Length - hdl->cs_stat->rcv_len;
		} else {
			move_len = (uint16_t) msg_size;
		}
		msg_size -= move_len;

		/* Updates the receive buffer 		*/
		memcpy (hdl->cs_stat->rcv_buff + hdl->cs_stat->rcv_len, msg, move_len);
		hdl->cs_stat->rcv_len += move_len;
		msg += move_len;

		while (hdl->cs_stat->rcv_len > 0) {
			/* Seeks the top of the message */
			res = cefnetd_csmgr_messege_head_seek (
						hdl->cs_stat, &fdv_payload_len, &fdv_header_len);
			if (res < 0) {
				break;
			}

			/* Calls the function corresponding to the type of the message 	*/
			if (hdl->cs_stat->rcv_buff[1] > CefC_PT_MAX) {
				cef_log_write (CefC_Log_Warn,
					"Detects the unknown PT_XXX=%d from csmgr\n",
					hdl->cs_stat->rcv_buff[1]);
			} else {
				(*cefnetd_incoming_csmgr_msg_process[hdl->cs_stat->rcv_buff[1]])
					(hdl, 0, 0, hdl->cs_stat->rcv_buff, fdv_payload_len, fdv_header_len, user_id);
			}

			/* Updates the receive buffer 		*/
			move_len = fdv_payload_len + fdv_header_len;
			wp = hdl->cs_stat->rcv_buff + move_len;
			memmove( hdl->cs_stat->rcv_buff, wp, hdl->cs_stat->rcv_len - move_len);
			hdl->cs_stat->rcv_len -= move_len;
		}
	}

	return (1);
}
/*--------------------------------------------------------------------------------------
	Seeks the top of the frame from the receive buffer
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_csmgr_messege_head_seek (
	CefT_Cs_Stat* cs_stat,
	uint16_t* payload_len,
	uint16_t* header_len
) {
	uint16_t move_len;
	unsigned char* wp;
	unsigned char* ep;
	unsigned char buff[CefC_Max_Length];
	uint16_t index = 0;
	static uint16_t short_step = 0;

	struct cef_hdr* chp;
	uint16_t pkt_len;
	uint16_t hdr_len;

	while (cs_stat->rcv_len > 7) {
		chp = (struct cef_hdr*) &(cs_stat->rcv_buff[index]);

		pkt_len = ntohs (chp->pkt_len);
		hdr_len = chp->hdr_len;

		if (chp->version != CefC_Version) {
			wp = &cs_stat->rcv_buff[index];
			ep = cs_stat->rcv_buff + index + cs_stat->rcv_len;
			move_len = 0;

			while (wp < ep) {
				if (*wp != CefC_Version) {
					wp++;
				} else {
					move_len = ep - wp;
					memcpy (buff, wp, move_len);
					memcpy (cs_stat->rcv_buff, buff, move_len);
					cs_stat->rcv_len -= wp - cs_stat->rcv_buff;

					chp = (struct cef_hdr*) &(cs_stat->rcv_buff);
					pkt_len = ntohs (chp->pkt_len);
					hdr_len = chp->hdr_len;
					index = 0;

					if ((pkt_len == 0) || (hdr_len == 0)) {
						move_len = 0;
					}

					break;
				}
			}
			if (move_len == 0) {
				cs_stat->rcv_len = 0;
				return (-1);
			}
		}

		if (chp->type > CefC_PT_MAX) {
			cs_stat->rcv_len--;
			index++;
			continue;
		}

		/* Obtains values of Header Length and Payload Length 	*/
		*payload_len 	= pkt_len - hdr_len;
		*header_len 	= hdr_len;

		if (cs_stat->rcv_len < *payload_len + *header_len) {
			short_step++;
			if (short_step > 2) {
				short_step = 0;
				cs_stat->rcv_len--;
				index++;
				continue;
			}
			return (-1);
		}

		if (index > 0) {
			memmove (cs_stat->rcv_buff, cs_stat->rcv_buff + index, cs_stat->rcv_len);
		}
		short_step = 0;
		return (1);
	}

	return (-1);
}
#endif // CefC_ContentStore
/*--------------------------------------------------------------------------------------
	Seeks the top of the frame from the receive buffer
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_messege_head_seek (
	CefT_Face* face,						/* the face structure						*/
	uint16_t* payload_len,
	uint16_t* header_len
) {
	uint16_t move_len;
	unsigned char* wp;
	unsigned char* ep;
	unsigned char buff[CefC_Max_Length];
	uint16_t index = 0;
	static uint16_t short_step = 0;

	struct cef_hdr* chp;
	uint16_t pkt_len;
	uint16_t hdr_len;

	while (face->len > 7) {
		chp = (struct cef_hdr*) &(face->rcv_buff[index]);

		pkt_len = ntohs (chp->pkt_len);
		hdr_len = chp->hdr_len;

		if (chp->version != CefC_Version) {
			wp = &face->rcv_buff[index];
			ep = face->rcv_buff + index + face->len;
			move_len = 0;

			while (wp < ep) {
				if (*wp != CefC_Version) {
					wp++;
				} else {
					move_len = ep - wp;
					memcpy (buff, wp, move_len);
					memcpy (face->rcv_buff, buff, move_len);
					face->len -= wp - face->rcv_buff;

					chp = (struct cef_hdr*) &(face->rcv_buff);
					pkt_len = ntohs (chp->pkt_len);
					hdr_len = chp->hdr_len;
					index = 0;

					break;
				}
			}
			if (move_len == 0) {
				face->len = 0;
				return (-1);
			}
		}

		if (chp->type > CefC_PT_MAX) {
			face->len--;
			index++;
			continue;
		}

		/* Obtains values of Header Length and Payload Length 	*/
		*payload_len 	= pkt_len - hdr_len;
		*header_len 	= hdr_len;

		if (face->len < *payload_len + *header_len) {
			short_step++;
			if (short_step > 2) {
				short_step = 0;
				face->len--;
				index++;
				continue;
			}
			return (-1);
		}

		if (index > 0) {
			memmove (face->rcv_buff, face->rcv_buff + index, face->len);
		}
		short_step = 0;
		return (1);
	}

	return (-1);
}
/*--------------------------------------------------------------------------------------
	Handles the received Interest message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_interest_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {
	CefT_CcnMsg_MsgBdy pm = { 0 };
	CefT_CcnMsg_OptHdr poh = { 0 };

	int res;
	int pit_res;
	CefT_Pit_Entry* pe = NULL;
	CefT_Fib_Entry* fe = NULL;
	uint16_t name_len;
#ifdef CefC_ContentStore
	unsigned int dnfaces = 0;
	int cs_res = -1;
#endif // CefC_ContentStore
	CefT_Rx_Elem elem;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	int forward_interest_f = 0;
	int i;
	int tp_plugin_res = CefC_Pi_All_Permission;
	uint16_t* fip;

	unsigned char buff[CefC_Max_Length];	//0.8.3
	int		face_type;						//0.8.3
	int		bw_stat_idx = -1;
	int		if_name_len;
	char	if_name[128];
	uint32_t contents_hashv = 0;	/* cefore-0.10.0:Hash value of this contents (without chunk number) */


#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Process the Interest (%d bytes)\n", payload_len + header_len);
#endif // CefC_Debug

#ifdef	__INTEREST__
	fprintf (stderr, "[%s] IN   faceid:%d Local:%d  peer_faceid:%d Local:%d\n\n", __func__, faceid, cef_face_is_local_face(faceid),
							peer_faceid,  cef_face_is_local_face(peer_faceid) );
#endif

	if (payload_len + header_len > CefC_Max_Msg_Size) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Interest is too large\n");
#endif // CefC_Debug
		return (-1);
	}

	/* Checks the Validation 			*/
	res = cef_valid_msg_verify (msg, payload_len + header_len);
	if (res != 0) {
		return (-1);
	}

	//0.8.3
	if ( hdl->bw_stat_hdl->stat_tbl_index_get ) {
		face_type = cef_face_type_get(peer_faceid);
		if ( face_type == CefC_Face_Type_Local ) {
#ifdef	__INTEREST__
			fprintf( stderr, "[%s] peer_faceid:%d CefC_Face_Type_Local \n", __func__, peer_faceid );
#endif
			goto SKIP_BW_STAT_CHECK;
		} else if ( face_type == CefC_Face_Type_Udp ) {
			/* UDP */
			/* Check BW_STAT_I */
			bw_stat_idx = cef_face_bw_stat_i_get( peer_faceid );
#ifdef	__INTEREST__
			fprintf (stderr, "[%s] peer_faceid:%d   bw_stat_idx:%d   Type:%d\n", __func__, peer_faceid, bw_stat_idx,
								cef_face_type_get(peer_faceid) );
#endif
			if ( bw_stat_idx != -1 ) {
				/* NOP */
			} else {
#ifdef	__INTEREST__
				fprintf( stderr, "[%s] user_id=%s \n", __FUNCTION__, user_id );
#endif
				if_name_len = cef_face_ip_route_get( user_id, if_name );
#ifdef	__INTEREST__
				fprintf( stderr, "[%s] if_name:%s\n", __func__, if_name );
#endif
				if ( if_name_len > 0 ) {
					bw_stat_idx = hdl->bw_stat_hdl->stat_tbl_index_get( if_name );
#ifdef	__INTEREST__
					fprintf( stderr, "[%s] bw_stat_idx:%d\n", __func__, bw_stat_idx );
#endif
					if ( bw_stat_idx >= 0 ) {
						cef_face_bw_stat_i_set( peer_faceid, bw_stat_idx );
					}
				}
			}
			/* UDP END */
		} else if ( face_type == CefC_Face_Type_Tcp ) {
			/* TCP */
			/* Check BW_STAT_I */
			bw_stat_idx = cef_face_bw_stat_i_get( faceid );
#ifdef	__INTEREST__
			fprintf (stderr, "[%s] faceid:%d   bw_stat_idx:%d\n", __func__, faceid, bw_stat_idx );
#endif
			if ( bw_stat_idx != -1 ) {
				/* NOP */
			} else {
#ifdef __INTEREST__
				fprintf (stderr, "[%s] faceid:%d   peer_node_id:%s\n", __func__, faceid, user_id );
#endif
				if_name_len = cef_face_ip_route_get( user_id, if_name );
				if ( if_name_len > 0 ) {
					bw_stat_idx = hdl->bw_stat_hdl->stat_tbl_index_get( if_name );
#ifdef	__INTEREST__
					fprintf (stderr, "[%s] faceid:%d   bw_stat_idx:%d\n", __func__, faceid, bw_stat_idx );
#endif
					if ( bw_stat_idx >= 0 ) {
						cef_face_bw_stat_i_set( faceid, bw_stat_idx );
					}
				}
			}
		} else {
			return (-1);
		}
	}

	//0x06 is Enabled?
	if ( hdl->IR_enable[6] != 1 ) {
		goto SKIP_BW_STAT_CHECK;
	}

	if ( bw_stat_idx != -1 ) {
		if ( hdl->bw_stat_hdl->stat_get ) {
			/* Check BW_STAT */
			double	bw_stat;
			bw_stat_idx = cef_face_bw_stat_i_get( peer_faceid );
#ifdef	__INTEREST__
			fprintf (stderr, "[%s] bw_stat_idx:%d\n", __func__, bw_stat_idx );
#endif
			bw_stat = hdl->bw_stat_hdl->stat_get( bw_stat_idx );
#ifdef	__INTEREST__
			fprintf (stderr, "\t bw_stat:%f\n", bw_stat );
#endif
			if ( bw_stat > hdl->IR_Congesion ) {
				//Interest Return 0x06:Congestion
#ifdef	__INTEREST__
	fprintf (stderr, "\t Interest Return 0x06:Congestion\n" );
#endif
				res = cef_frame_interest_return_create( msg, payload_len + header_len, buff, CefC_IR_CONGESION );
				if ( res < 0 ) {
					return(-1);
				}
				//send Force
				cef_face_frame_send_forced (peer_faceid, buff, res);
				return(-1);
			}
		}
	}
SKIP_BW_STAT_CHECK:;

	/* Parses the received Interest 	*/
	res = cef_frame_message_parse (
					msg, payload_len, header_len, &poh, &pm, CefC_PT_INTEREST);
	if (res < 0) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Detects the invalid Interest\n");
#endif // CefC_Debug
		return (-1);
	}

	//0.8.3 HopLimit=0 IR 0x02
	if (pm.hoplimit < 1) {
		//Interest Return 0x02:HopLimit Exceeded
#ifdef	__INTEREST__
	fprintf (stderr, "\t Interest Return 0x02:HopLimit Exceeded SendForce\n" );
#endif
		if ( (hdl->IR_Option != 0) && (hdl->IR_enable[2] == 1) ) {	//202108
			res = cef_frame_interest_return_create( msg, payload_len + header_len, buff, CefC_IR_HOPLIMIT_EXCEEDED );
			if ( res < 0 ) {
				return(-1);
			}
			cef_face_frame_send_forced (peer_faceid, buff, res);
		}	//202108
		return(-1);
	}

#ifdef CefC_Debug
		cef_dbg_buff_write_name (CefC_Dbg_Finer,
									(unsigned char*)"Interest's Name [", strlen("Interest's Name ["),
									pm.name, pm.name_len,
									(unsigned char*)" ]\n", strlen(" ]\n"));
#endif // CefC_Debug

	/* Checks whether this interest is the command or not */
	res = cefnetd_incoming_command_process (hdl, faceid, peer_faceid, &pm);
	if (res > 0) {
		return (1);
	}

	if (poh.app_reg_f > 0) {
		cefnetd_input_app_reg_command (hdl, &pm, &poh, (uint16_t) peer_faceid);
		return (1);
	}

	//0.8.3	Symbolic/Osymbilic Check PIT
	if ( pm.InterestType == CefC_PIT_TYPE_Sym ) {
		res = cef_pit_symbolic_pit_check( hdl->pit, &pm, &poh );
		if ( res < 0 ) {
			return (-1);
		}
	}
	//0.8.3 Selective
	if ( pm.InterestType == CefC_PIT_TYPE_Sel ) {
		res = cefnetd_incoming_selective_interest_process (
			hdl, faceid, peer_faceid, msg, payload_len, header_len, &pm, &poh);
		if ( res < 0 ) {
			return(-1);
		}
		return (1);
	}

	if (pm.org.putverify_f) {
		if (pm.org.putverify_msgtype != CefC_CpvOp_FibRegMsg) {
			return (-1);
		}
		cefnetd_adv_route_process (hdl, &pm);
		return (1);
	}

	/* Searches a PIT entry matching this Interest 	*/
	pe = cef_pit_entry_lookup (hdl->pit, &pm, &poh, NULL, 0);

	if (pe == NULL) {
		return (-1);
	}

#ifdef CefC_ContentStore
	// TODO change process
	dnfaces = pe->dnfacenum;
#endif // CefC_ContentStore

	/* Updates the information of down face that this Interest arrived 	*/
	pit_res = cef_pit_entry_down_face_update (pe, peer_faceid, &pm, &poh, msg, hdl->IntrestRetrans);	//0.8.3
#ifdef CefC_Debug
#ifdef __T_VERSION__
	if (cef_dbg_loglv_finest) {
		fprintf (stderr, "*** Received Interest, lookup pit entry/update dnface ");
		if (pm.org.version_f == 1 && pm.org.version_len == 0) {
			fprintf(stderr,"> VerReq\n");
		} else {
			fprintf(stderr,"> %d, ver=%s(%d)\n", pm.org.version_f, pm.org.version_val, pm.org.version_len);
		}
		cef_pit_entry_print (hdl->pit);
	}
#endif
#endif

	//0.8.3 HopLimit==1
	if (pm.hoplimit == 1) {
		//Interest Return 0x02:HopLimit Exceeded
#ifdef	__INTEREST__
	fprintf (stderr, "\t Interest Return 0x02:HopLimit Exceeded HoldPIT\n" );
#endif
		if ( (hdl->IR_Option != 0) && (hdl->IR_enable[2] == 1) ) {	//202108
			res = cef_frame_interest_return_create( msg, payload_len + header_len, buff, CefC_IR_HOPLIMIT_EXCEEDED );
			if ( res < 0 ) {
				return(-1);
			}
			//Hold pe peer_faceid
			res = cef_pit_interest_return_set( pe, &pm, &poh, peer_faceid, CefC_IR_HOPLIMIT_EXCEEDED, res, buff );
			if ( res < 0 ) {
				/* */
			}
		}	//202108
	}

	/* Searches a FIB entry matching this Interest 		*/
	if (pm.chunk_num_f) {
		name_len = pm.name_len - (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
	} else {
		/* Symbolic Interest	*/
		name_len = pm.name_len;
	}

	if (pit_res != 0) {
		/* Searches a FIB entry matching this Interest 		*/
		fe = cef_fib_entry_search (hdl->fib, pm.name, name_len);

		/* Count of Received Interest */
		hdl->stat_recv_interest++;
		/* Count of Received Interest by type */
		hdl->stat_recv_interest_types[pm.InterestType]++;

		/* Obtains Face-ID(s) to forward the Interest */
		if (fe) {
			/* Count of Received Interest at FIB */
			fe->rx_int++;
			/* Count of Received Interest by type at FIB */
			fe->rx_int_types[pm.InterestType]++;
			face_num = cef_fib_forward_faceid_select (fe, peer_faceid, faceids);
		}

		if ( pm.payload_f ) {
			struct cef_hdr* msghdr;
			uint16_t pkt_len;
			uint16_t hdr_len;

			msghdr = (struct cef_hdr*) pm.payload;
			pkt_len = ntohs (msghdr->pkt_len);
			hdr_len = msghdr->hdr_len;

			cefnetd_incoming_piggyback_process (
				hdl, faceid, peer_faceid, pm.payload, pkt_len - hdr_len, hdr_len);

			tp_plugin_res = CefC_Pi_Interest_Send;
		}
	}

	// cefore-0.10.0
	{	CefT_Pit_Entry* tmpe = cef_pit_entry_search_without_chunk (hdl->pit, &pm, &poh);
		if ( tmpe ){
			contents_hashv = tmpe->hashv;	/* Hash value of this contents */
		} else {
			contents_hashv = 0;				/* Hash value of this contents */
		}
	}
	// cefore-0.10.0

#ifdef CefC_Mobility
	/*--------------------------------------------------------------------
		Mobility Plugin
	----------------------------------------------------------------------*/
	if ((hdl->plugin_hdl.mb)->interest) {

		/* Creates CefT_Rx_Elem 		*/
		memset (&elem, 0, sizeof (CefT_Rx_Elem));
		elem.type 				= CefC_Elem_Type_Interest;
		elem.hashv 				= contents_hashv;
		elem.in_faceid 			= (uint16_t) peer_faceid;
		elem.parsed_msg 		= &pm;
		memcpy (&(elem.msg[0]), msg, payload_len + header_len);
		elem.msg_len 			= payload_len + header_len;
		elem.out_faceid_num 	= face_num;

		for (i = 0 ; i < face_num ; i++) {
			elem.out_faceids[i] = faceids[i];
		}

		/* Callback 		*/
		tp_plugin_res = (*((hdl->plugin_hdl.mb))->interest)(
			hdl->plugin_hdl.mb, &elem
		);
	}
#endif // CefC_Mobility

	/*--------------------------------------------------------------------
		Transport Plugin
	----------------------------------------------------------------------*/
	if (poh.org.tp_variant > CefC_T_OPT_TP_NONE) {

		if ((hdl->plugin_hdl.tp)[poh.org.tp_variant].interest) {
			/* Creates CefT_Rx_Elem 		*/
			memset (&elem, 0, sizeof (CefT_Rx_Elem));
			elem.plugin_variant 	= poh.org.tp_variant;
			elem.type 				= CefC_Elem_Type_Interest;
			elem.hashv 				= contents_hashv;
			elem.in_faceid 			= (uint16_t) peer_faceid;
			elem.parsed_msg 		= &pm;
			memcpy (&(elem.msg[0]), msg, payload_len + header_len);
			elem.msg_len 			= payload_len + header_len;
			elem.out_faceid_num 	= face_num;

			for (i = 0 ; i < face_num ; i++) {
				elem.out_faceids[i] = faceids[i];
			}

			memcpy (elem.ophdr, poh.org.tp_val, poh.org.tp_len);
			elem.ophdr_len = poh.org.tp_len;

			/* Callback 		*/
			tp_plugin_res = (*(hdl->plugin_hdl.tp)[poh.org.tp_variant].interest)(
				&(hdl->plugin_hdl.tp[poh.org.tp_variant]), &elem
			);
		}
	}

#ifdef CefC_ContentStore
	/*--------------------------------------------------------------------
		Content Store
	----------------------------------------------------------------------*/
	if (tp_plugin_res & CefC_Pi_Object_Match) {

		if (pm.org.from_pub_f) {
			if (pm.chunk_num_f) {
				name_len = pm.name_len - (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
			} else {
				name_len = pm.name_len;
			}
			if (cef_hash_tbl_item_check_exact (hdl->app_reg, pm.name, name_len) < 0) {
				forward_interest_f = 1;
				goto FORWARD_INTEREST;
			}
		}

		/* Checks Reply 	*/
		if (!pm.org.longlife_f) {
			if (pit_res != 0) {
				pit_res = cef_csmgr_rep_f_check (pe, peer_faceid);
			}
			if ((pit_res == 0) || (dnfaces == pe->dnfacenum)) {
				forward_interest_f = 1;			//#909
				goto FORWARD_INTEREST;
			}
		}

		/* Checks Content Store */
		if (hdl->cs_stat->cache_type != CefC_Default_Cache_Type) {	//#938

			if (pm.org.version_f == 1 && pm.org.version_len == 0) {
				/* This interest is VerReq, so don't have to search the cache */
				forward_interest_f = 1;
				goto FORWARD_INTEREST;
			}
			/* Checks the temporary/local cache in cefnetd 		*/
			unsigned char* cob = NULL;
			cs_res = cef_csmgr_cache_lookup (hdl->cs_stat, peer_faceid, &pm, &poh, pe, &cob);

			if (cs_res < 0) {
#ifdef	CefC_Conpub
				if (hdl->cs_stat->cache_type != CefC_Cache_Type_ExConpub){
#endif	//CefC_Conpub
					/* Cache does not exist in the temporary cache in cefnetd, 		*/
					/* so inquiries to the csmgr 									*/
					cef_csmgr_excache_lookup (hdl->cs_stat, peer_faceid, &pm, &poh, pe);
#ifdef CefC_Debug
					cef_dbg_write (CefC_Dbg_Finer, "Forward the Interest to csmgr\n");
#endif // CefC_Debug
#ifdef	CefC_Conpub
				}
#endif	//CefC_Conpub
			} else {
#ifdef CefC_Debug
				cef_dbg_write (CefC_Dbg_Finer,
					"Return the Content Object from the buffer/local cache\n");
#endif // CefC_Debug
				cefnetd_cefcache_object_process (hdl, cob);
				return (-1);
			}
		}
	}

	if ((pit_res != 0) && (cs_res != 0)) {
		forward_interest_f = 1;
	}
FORWARD_INTEREST:
#else // CefC_ContentStore
	if (pit_res != 0) {
		forward_interest_f = 1;
	}
#endif // CefC_ContentStore

	/*--------------------------------------------------------------------
		Forwards the received Interest
	----------------------------------------------------------------------*/
	if (tp_plugin_res & CefC_Pi_Interest_Send) {
		if (pm.chunk_num_f) {
			name_len = pm.name_len - (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
		} else {
			name_len = pm.name_len;
		}
		fip = (uint16_t*) cef_hash_tbl_item_get_for_app (hdl->app_reg, pm.name, name_len);

		if (fip) {
			if (cef_face_check_active (*fip) > 0) {
#ifdef CefC_Debug
				cef_dbg_write (CefC_Dbg_Finer,
					"Forward the Interest to the local application(s)\n");
#endif // CefC_Debug
				cef_face_frame_send_forced (
					*fip, msg, (size_t) (payload_len + header_len));

				cef_pit_entry_up_face_update (pe, *fip, &pm, &poh);
			}
			return (1);
		}

		if ((forward_interest_f) && (face_num > 0)) {
#ifdef CefC_Debug
			cef_dbg_write (CefC_Dbg_Finer, "Forward the Interest to the next cefnetd(s)\n");
#endif // CefC_Debug
			cefnetd_interest_forward (
				hdl, faceids, face_num, peer_faceid, msg,
				payload_len, header_len, &pm, &poh, pe, fe
			);
			return (1);
		}

		//0.8.3
		else {
			if ((forward_interest_f) && (pit_res)) {
#ifdef	__PIT_CLEAN__
	fprintf( stderr, "[%s] IR:NO_ROUTE\n", __func__ );
#endif
				//Interest Return 0x01:NO Route
#ifdef	__INTEREST__
	fprintf (stderr, "\t Interest Return 0x01:NO Route HoldPIT\n" );
#endif
				if ( (hdl->IR_Option != 0) && (hdl->IR_enable[1] == 1) ) {	//202108
					res = cef_frame_interest_return_create( msg, payload_len + header_len, buff, CefC_IR_NO_ROUTE);
					if ( (hdl->cs_mode == 2) || (hdl->cs_mode == 3) ) {
						//Hold pe peer_faceid
						res = cef_pit_interest_return_set( pe, &pm, &poh, peer_faceid, CefC_IR_NO_ROUTE, res, buff );
						if ( res < 0 ) {
							/* */
						}
					} else {
						/* IR Send */
						cef_face_frame_send_forced (peer_faceid, buff, res);
					}
				}	//202108
				return(-1);
			}
			else {	//20210628
				//Interest Return 0x01:NO Route
#ifdef	__INTEREST__
	fprintf (stderr, "\t Interest Return 0x01:NO Route HoldPIT\n" );
#endif
				if ( (hdl->IR_Option != 0) && (hdl->IR_enable[1] == 1) ) {	//202108
					res = cef_frame_interest_return_create( msg, payload_len + header_len, buff, CefC_IR_NO_ROUTE);
					if ( (hdl->cs_mode == 2) || (hdl->cs_mode == 3) ) {
						//Hold pe peer_faceid
						res = cef_pit_interest_return_set( pe, &pm, &poh, peer_faceid, CefC_IR_NO_ROUTE, res, buff );
						if ( res < 0 ) {
							/* */
						}
					} else {
						/* IR Send */
						cef_face_frame_send_forced (peer_faceid, buff, res);
					}
				}	//202108
				return(-1);
			}	//20210628
		}

	}

	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the received Content Object message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_object_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {
	CefT_CcnMsg_MsgBdy pm = { 0 };
	CefT_CcnMsg_OptHdr poh = { 0 };
	CefT_Pit_Entry* pe = NULL;
	int loop_max = 2;						/* For App(0), Trans(1)						*/
	int pit_idx = 0;
	int res;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	CefT_Down_Faces* face;
	CefT_Rx_Elem elem;
	int i;
	int tp_plugin_res = CefC_Pi_All_Permission;
	uint16_t pkt_len = 0;	//0.8.3
	uint32_t contents_hashv = 0;	/* cefore-0.10.0:Hash value of this contents (without chunk number) */


#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Process the Content Object (%d bytes)\n", payload_len + header_len);
#endif // CefC_Debug

#ifdef	__SYMBOLIC__
	fprintf( stderr, "[%s] IN msg (%d bytes) \n", __func__, payload_len + header_len );
#endif

	if (payload_len + header_len > CefC_Max_Msg_Size) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Content Object is too large\n");
#endif // CefC_Debug

		return (-1);
	}

	pkt_len = payload_len + header_len;	//0.8.3

	/* Checks the Validation 			*/
	res = cef_valid_msg_verify (msg, payload_len + header_len);
	if (res != 0) {
#ifdef	__VALID_NG__
		fprintf( stderr, "[%s] Validation NG\n", __func__ );
#endif
		return (-1);
	}

	res = cef_frame_message_parse (
					msg, payload_len, header_len, &poh, &pm, CefC_PT_OBJECT);
	if (res < 0) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Detects the invalid Content Object\n");
#endif // CefC_Debug
		return (-1);
	}

#ifdef CefC_Debug
	cef_dbg_buff_write_name (CefC_Dbg_Finer,
								(unsigned char*)"Object's Name [", strlen("Object's Name ["),
								pm.name, pm.name_len,
								(unsigned char*)" ]\n", strlen(" ]\n"));
#endif // CefC_Debug

	/* Checks whether this Object is the command or not */
	res = cefnetd_incoming_command_process (hdl, faceid, peer_faceid, &pm);
	if (res > 0) {
		return (1);
	}
	hdl->stat_recv_frames++;

	stat_rcv_size_cnt++;
	stat_rcv_size_sum += (payload_len + header_len);
	if ((payload_len + header_len) < stat_rcv_size_min) {
		stat_rcv_size_min = payload_len + header_len;
	}
	if ((payload_len + header_len) > stat_rcv_size_max) {
		stat_rcv_size_max = payload_len + header_len;
	}

#ifdef CefC_ContentStore
	/*--------------------------------------------------------------------
		Content Store
	----------------------------------------------------------------------*/
	/* Stores Content Object to Content Store 		*/
	if ((pm.expiry > 0) && (hdl->cs_stat->cache_type != CefC_Default_Cache_Type)) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Finer, "Forward the Content Object to cache\n");
#endif // CefC_Debug
		cef_csmgr_excache_item_put (
			hdl->cs_stat, msg, payload_len + header_len, peer_faceid, &pm, &poh);
	}
#endif // CefC_ContentStore

	/* Searches a PIT entry matching this Object 	*/

	/* Initialize the loop counter */
	pit_idx = 0;

	for (; pit_idx < loop_max; pit_idx++) {
		if (pit_idx == 0) {
			pe = cef_pit_entry_search_with_chunk (hdl->app_pit, &pm, &poh);
			if ( pe == NULL )
				continue;
		} else {
			pe = cef_pit_entry_search_with_chunk (hdl->pit, &pm, &poh);
			/*JK*///20210824 Cob without chunk number
			if ( pm.chunk_num_f == 0 && pe ) {	//Cob without chunk number
				if ( pe->PitType == CefC_PIT_TYPE_Rgl ) {	//PIT_Type is Reg
					//NOP
				} else {	// PIT_Type not Reg(Sym)
					// Unmatch
					return (-1);
				}
			}
		}

		if (pe == NULL) {
			stat_nopit_frames++;

		} else {
#if defined (CefC_Debug) && defined (CefC_Conpub)
#ifdef __T_VERSION__
			if (cef_dbg_loglv_finest) {
				fprintf(stderr, "*** Received Object ");
				if (pm.org.version_f == 1 && pm.org.version_len == 0) {
					fprintf(stderr,"> VerReq(resp)\n");
				} else {
					fprintf (stderr, "> %d, ver=%s(%d)\n", pm.org.version_f, pm.org.version_val, pm.org.version_len);
				}
				cef_pit_entry_print (hdl->pit);
			}
#endif //__T_VERSION__
#endif
			//0.8.3
			if ( pe->COBHR_len > 0 ) {
				/* CobHash */
				SHA256_CTX		ctx;
				uint16_t		CobHash_index;
				uint16_t		CobHash_len;
				unsigned char 	hash[SHA256_DIGEST_LENGTH];

				CobHash_index = header_len;
				CobHash_len   = payload_len;
				SHA256_Init (&ctx);
				SHA256_Update (&ctx, &msg[CobHash_index], CobHash_len);
				SHA256_Final (hash, &ctx);
#ifdef	__RESTRICT__
{
	int hidx;
	char	hash_dbg[1024];

	printf ( "%s\n", __func__ );
	sprintf (hash_dbg, "PIT CobHash [");
	for (hidx = 0 ; hidx < 32 ; hidx++) {
		sprintf (hash_dbg, "%s %02X", hash_dbg, pe->COBHR_selector[hidx]);
	}
	sprintf (hash_dbg, "%s ]\n", hash_dbg);
	printf( "%s", hash_dbg );

	sprintf (hash_dbg, "OBJ CobHash [");
	for (hidx = 0 ; hidx < 32 ; hidx++) {
		sprintf (hash_dbg, "%s %02X", hash_dbg, pe->COBHR_selector[hidx]);
	}
	sprintf (hash_dbg, "%s ]\n", hash_dbg);
	printf( "%s", hash_dbg );
}
#endif

				if ( memcmp(pe->COBHR_selector, hash, 32) == 0 ) {
					/* OK */
				} else {
					/* NG */
					return (-1);
				}
			}
			if ( pe->KIDR_len > 0 ) {
				/* KeyIdRest */
				int keyid_len;
				unsigned char keyid_buff[32];
				keyid_len = cefnetd_keyid_get( msg, pkt_len, keyid_buff );
				if ( (keyid_len == 32) && (memcmp(pe->KIDR_selector, keyid_buff, 32) == 0) ) {
					/* OK */
				} else {
					/* NG */
					return (-1);
				}
			}

			face = &(pe->dnfaces);

			while (face->next) {
				face = face->next;
				faceids[face_num] = face->faceid;
				face_num++;
			}
		}

		// cefore-0.10.0
		{	CefT_Pit_Entry* tmpe = cef_pit_entry_search_without_chunk (hdl->pit, &pm, &poh);
			if ( tmpe ){
				contents_hashv = tmpe->hashv;	/* Hash value of this contents */
			} else {
				contents_hashv = 0;				/* Hash value of this contents */
			}
		}
		// cefore-0.10.0

#ifdef CefC_Mobility
		/*--------------------------------------------------------------------
			Mobility Plugin
		----------------------------------------------------------------------*/
		if ((hdl->plugin_hdl.mb)->cob) {

			/* Creates CefT_Rx_Elem 		*/
			pm.seqnum = poh.seqnum;
			memset (&elem, 0, sizeof (CefT_Rx_Elem));
			elem.type 				= CefC_Elem_Type_Object;
			elem.hashv 				= contents_hashv;
			elem.in_faceid 			= (uint16_t) peer_faceid;
			elem.parsed_msg 		= &pm;
			memcpy (&(elem.msg[0]), msg, payload_len + header_len);
			elem.msg_len 			= payload_len + header_len;
			elem.out_faceid_num 	= face_num;

			for (i = 0 ; i < face_num ; i++) {
				elem.out_faceids[i] = faceids[i];
			}

			/* Callback 		*/
			tp_plugin_res = (*((hdl->plugin_hdl.mb))->cob)(
				hdl->plugin_hdl.mb, &elem
			);
		}
#endif // CefC_Mobility

		/*--------------------------------------------------------------------
			Transport Plugin
		----------------------------------------------------------------------*/
		if (poh.org.tp_variant > CefC_T_OPT_TP_NONE) {
			if (hdl->plugin_hdl.tp[poh.org.tp_variant].cob) {

				/* Creates CefT_Rx_Elem 		*/
				memset (&elem, 0, sizeof (CefT_Rx_Elem));
				elem.plugin_variant 	= poh.org.tp_variant;
				elem.type 				= CefC_Elem_Type_Object;
				elem.hashv 				= contents_hashv;
				elem.in_faceid 			= (uint16_t) peer_faceid;
				elem.parsed_msg 		= &pm;
				memcpy (&(elem.msg[0]), msg, payload_len + header_len);
				elem.msg_len 			= payload_len + header_len;
				elem.out_faceid_num 	= face_num;

				for (i = 0 ; i < face_num ; i++) {
					elem.out_faceids[i] = faceids[i];
				}

				memcpy (elem.ophdr, poh.org.tp_val, poh.org.tp_len);
				elem.ophdr_len = poh.org.tp_len;

				/* Callback 		*/
				tp_plugin_res = (*(hdl->plugin_hdl.tp)[poh.org.tp_variant].cob)(
					&(hdl->plugin_hdl.tp[poh.org.tp_variant]), &elem
				);
			}
		}

		/*--------------------------------------------------------------------
			Forwards the Content Object
		----------------------------------------------------------------------*/
		if (tp_plugin_res & CefC_Pi_Object_Send) {
			if (face_num > 0) {
#ifdef CefC_Debug
				cef_dbg_write (CefC_Dbg_Finer, "Forward the Content Object to cefnetd(s)\n");
#endif // CefC_Debug
				cefnetd_object_forward (hdl, faceids, face_num, msg,
					payload_len, header_len, &pm, &poh, pe);

				if (pe->stole_f) {
#ifdef CefC_Debug
#ifdef __T_VERSION__
					if (cef_dbg_loglv_finest) {
						fprintf(stderr, "*** stole_f is ON\n");
					}
#endif //__T_VERSION__
#endif // CefC_Debug
					cef_pit_entry_free (hdl->pit, pe);
				}

				if(pit_idx == 0) {
					return (1);
				}
			}

#ifdef	__SYMBOLIC__
	fprintf( stderr, "\t CKP-010 \n" );
#endif
			if(pit_idx == 1) {
				CefT_Pit_Entry* tmpe = NULL;
				for (i = 0; i < face_num; i++)
					faceids[i] = 0;
				face_num = 0;
				/*JK*///20210824 Cob without chunk number
				if ( pm.chunk_num_f == 0 ) {	//Cob without chunk number
					return (1);
				}
				tmpe = cef_pit_entry_search_without_chunk (hdl->pit, &pm, &poh);
				if ( tmpe == NULL ) {
					/* NOP */
				} else {
					//Symbolic/Osyimbolic
					face = &(tmpe->dnfaces);

					while (face->next) {
						face = face->next;
						faceids[face_num] = face->faceid;
						face_num++;
					}
					if (face_num > 0) {
						if ( (tmpe->PitType == CefC_PIT_TYPE_Sym)
							&& ((tmpe->Last_chunk_num - hdl->SymbolicBack) <= pm.chunk_num) ) {
							cefnetd_object_forward (hdl, faceids, face_num, msg,
								payload_len, header_len, &pm, &poh, tmpe);
							if ( tmpe->Last_chunk_num < pm.chunk_num ) {
								tmpe->Last_chunk_num = pm.chunk_num;
							}
						}
					}
				}
			}
		}

		for (i = 0; i < face_num; i++){
			faceids[i] = 0;
		}
		face_num = 0;
		pe = NULL;
	}
#ifdef CefC_Debug
#ifdef __T_VERSION__
	if (cef_dbg_loglv_finest) {
		fprintf (stderr, "*** after Forward the Content Object to cefnetd(s)\n");
		cef_pit_entry_print (hdl->pit);
	}
#endif //__T_VERSION__
#endif // CefC_Debug
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the received InterestReturn
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_intreturn_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {
	//0.8.3
	CefT_CcnMsg_MsgBdy pm = { 0 };
	CefT_CcnMsg_OptHdr poh = { 0 };
	CefT_Pit_Entry* pe = NULL;
	int loop_max = 2;						/* For App(0), Trans(1)						*/
	int pit_idx = 0;
	int res;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	CefT_Down_Faces* face;
	int i;
	int tp_plugin_res = CefC_Pi_All_Permission;

	//202108
	if ( hdl->IR_Option == 0 ) {
		return( -1 );
	}

#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Process the Interest Return (%d bytes)\n", payload_len + header_len);
#endif // CefC_Debug

	if (payload_len + header_len > CefC_Max_Msg_Size) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Interest Return is too large\n");
#endif // CefC_Debug

		return (-1);
	}

	/* Checks the Validation 			*/
	res = cef_valid_msg_verify (msg, payload_len + header_len);
	if (res != 0) {
		return (-1);
	}

	res = cef_frame_message_parse (
					msg, payload_len, header_len, &poh, &pm, CefC_PT_INTRETURN);
	if (res < 0) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Detects the invalid Interest Return\n");
#endif // CefC_Debug
		return (-1);
	}

#ifdef CefC_Debug
	cef_dbg_buff_write_name (CefC_Dbg_Finer,
								(unsigned char*)"Interest Return's Name [", strlen("Interest Return's Name ["),
								pm.name, pm.name_len,
								(unsigned char*)" ]\n", strlen(" ]\n"));
#endif // CefC_Debug

	/* Searches a PIT entry matching this InterestReturn 	*/
	/* Initialize the loop counter */
	pit_idx = 0;

	for (; pit_idx < loop_max; pit_idx++) {
		if (pit_idx == 0) {
			pe = cef_pit_entry_search (hdl->app_pit, &pm, &poh, NULL, 0);
		} else {
			if (pit_idx == 1) {
				pe = cef_pit_entry_search (hdl->pit, &pm, &poh, NULL, 0);
			}
		}
		if (pe == NULL) {
			continue;
		}

		if (pe) {
			face = &(pe->dnfaces);

			while (face->next) {
				face = face->next;
				faceids[face_num] = face->faceid;
				face_num++;
			}
		} else {
			return (-1);
		}

		/*--------------------------------------------------------------------
			Forwards the Interest Return
		----------------------------------------------------------------------*/
		if (tp_plugin_res & CefC_Pi_Object_Send) {
			if (face_num > 0) {
#ifdef CefC_Debug
				cef_dbg_write (CefC_Dbg_Finer, "Forward the Interest Return cefnetd(s)\n");
#endif // CefC_Debug
				cefnetd_object_forward (hdl, faceids, face_num, msg,
					payload_len, header_len, &pm, &poh, pe);

				if (pe->stole_f) {
					cef_pit_entry_free (hdl->pit, pe);
				}

				if(pit_idx == 0) {
					return (1);
				}

			}
		}

		for (i = 0; i < face_num; i++)
			faceids[i] = 0;
		face_num = 0;
		pe = NULL;
	}

	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the received Piggyback message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_piggyback_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* pkt, 					/* received packet to handle				*/
	uint16_t msg_len, 						/* length of ccn message 					*/
	uint16_t header_len						/* length of fixed and option header 		*/
) {
	CefT_CcnMsg_MsgBdy pm = { 0 };
	CefT_CcnMsg_OptHdr poh = { 0 };
	CefT_Pit_Entry* pe;
	int res;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	CefT_Down_Faces* face;

#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Process the Piggyback (%d bytes)\n", msg_len + header_len);
#endif // CefC_Debug

	if (msg_len + header_len > CefC_Max_Msg_Size) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Finer, "Piggyback is too large\n");
#endif // CefC_Debug
		return (-1);
	}

	res = cef_frame_message_parse (
					pkt, msg_len, header_len, &poh, &pm, CefC_PT_OBJECT);
	if (res < 0) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Finer, "Detects the invalid Piggyback\n");
#endif // CefC_Debug
		return (-1);
	}
#ifdef CefC_Debug
	cef_dbg_buff_write_name (CefC_Dbg_Finer,
								(unsigned char*)"Piggyback Interest's Name [", strlen("Piggyback Interest's Name ["),
								pm.name, pm.name_len,
								(unsigned char*)" ]\n", strlen(" ]\n"));
#endif // CefC_Debug

#ifdef CefC_ContentStore
	/*--------------------------------------------------------------------
		Content Store
	----------------------------------------------------------------------*/
	/* Stores Content Object to Content Store 		*/
	if ((pm.expiry > 0) && (hdl->cs_stat->cache_type != CefC_Default_Cache_Type)) {
			cef_csmgr_excache_item_put (
				hdl->cs_stat, pkt, (msg_len + header_len), peer_faceid, &pm, &poh);
	}
#endif // CefC_ContentStore

	/* Searches a PIT entry matching this Object 	*/
	if (pm.chunk_num_f) {
		pm.name_len -= (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
	}

	pe = cef_pit_entry_search (hdl->pit, &pm, &poh, NULL, 0);

	if (pe) {
		face = &(pe->dnfaces);

		while (face->next) {
			face = face->next;

			if ((peer_faceid != face->faceid) &&
				(cef_face_is_local_face (face->faceid))) {
				faceids[face_num] = face->faceid;
				face_num++;
			}
		}
	}

	if (face_num > 0) {
		cefnetd_object_forward (
			hdl, faceids, face_num, pkt, msg_len, header_len, &pm, &poh, pe);
	}

	return (1);
}

/*--------------------------------------------------------------------------------------
	Handles the received Ccninfo Request
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_ccninforeq_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {
	CefT_CcnMsg_MsgBdy pm = { 0 };
	CefT_CcnMsg_OptHdr poh = { 0 };
	int res;
	int forward_req_f = 0;
	uint16_t new_header_len;
	uint16_t return_code = CefC_CtRc_NO_ERROR;
	CefT_Pit_Entry* pe;
	CefT_Fib_Entry* fe = NULL;
	uint16_t name_len;
	uint16_t pkt_len;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	struct timeval t;
	uint16_t* fip;
	unsigned int responder_mtu;
	// ccninfo-03
	CefT_Parsed_Ccninfo	*pci;
	CEF_FRAME_SKIPHOP_T	w_skiphop;
	unsigned char ccninfo_pit[1024];
	int			  ccninfo_pit_len;
	int			  detect_loop = 0;

	unsigned char peer_node_id[16];
	unsigned char node_id[16];

	unsigned char stamp_node_id[16] = {0};	/* Not Use */
	int id_len = -1;

	pkt_len = header_len + payload_len;

#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Process the Ccninfo Request (%d bytes)\n", payload_len + header_len);
#endif // CefC_Debug

#ifdef	DEB_CCNINFO
	printf( "[%s] Ccninfo Request (%d bytes)\n",
			"cefnetd_incoming_ccninforeq_process", payload_len + header_len );
#endif

	/* Check the message size 		*/
	if (payload_len + header_len > CefC_Max_Msg_Size) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Ccninfo Request is too large\n");
#endif // CefC_Debug

#ifdef	DEB_CCNINFO
		fprintf( stderr, "[%s] return(-1) Ccninfo Request is too large\n",
				"cefnetd_incoming_ccninforeq_process" );
#endif

		return (-1);
	}

	/* Clear information used in authentication & authorization */
	hdl->ccninfousr_id_len = 0;
	memset (hdl->ccninfousr_node_id, 0, sizeof(hdl->ccninfousr_node_id));
	if (hdl->ccninfo_rcvdpub_key_bi != NULL) {
		free (hdl->ccninfo_rcvdpub_key_bi);
		hdl->ccninfo_rcvdpub_key_bi = NULL;
	}
	hdl->ccninfo_rcvdpub_key_bi_len = 0;
	/* Parses the received  Ccninfo Request 	*/
	res = cef_frame_message_parse (
					msg, payload_len, header_len, &poh, &pm, CefC_PT_REQUEST);
	if (res < 0) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Detects the invalid Ccninfo Request\n");
#endif // CefC_Debug
		return (-1);
	}


	/* Parses the received Ccninfo ccninfo-03	*/
	if ( (pci = cef_frame_ccninfo_parse (msg)) == NULL ){
		return(-1);
	}
#ifdef DEB_CCNINFO
	cefnetd_dbg_cpi_print ( pci );
#endif

	/* ccninfo-03 Loop check */
	res = cefnetd_ccninfo_loop_check( hdl, pci );
	if ( res < 0 ) {
		/* Detect Loop */
		detect_loop = 1;
	} else {
		detect_loop = 0;
	}

	if (pci->putverify_f) {
		if (pci->putverify_msgtype != CefC_CpvOp_ContInfoMsg) {
			cef_frame_ccninfo_parsed_free (pci);
			return (-1);
		}
		res = cefnetd_continfo_process (
			hdl, faceid, peer_faceid, msg, payload_len, header_len, &pm, pci);
		cef_frame_ccninfo_parsed_free (pci);
		if (res < 0) {
			return (-1);
		}
		return (1);
	}

	/* Check HopLimit */
	if (pm.hoplimit < 1 || pm.hoplimit <= poh.skip_hop) {
		cef_frame_ccninfo_parsed_free (pci);
		return (-1);
	}

	/* Set information used in authentication & authorization */
	memcpy (hdl->ccninfousr_node_id, pci->node_id, pci->id_len);
	hdl->ccninfousr_id_len = pci->id_len;

#ifdef CefC_Debug
	cef_dbg_buff_write_name (CefC_Dbg_Finer,
								(unsigned char*)"Ccninfo Request's Name [", strlen("Ccninfo Request's Name ["),
								pm.name, pm.name_len,
								(unsigned char*)" ]\n", strlen(" ]\n"));
#endif // CefC_Debug

#ifdef DEB_CCNINFO
{
	int dbg_x;
	fprintf (stderr, "DEB_CCNINFO: Rcvd Ccnifno Request's Msg [ ");
	for (dbg_x = 0 ; dbg_x < payload_len+header_len ; dbg_x++) {
		fprintf (stderr, "%02x ", msg[dbg_x]);
	}
	fprintf (stderr, "](%d)\n", payload_len+header_len);
	fprintf(stderr, "poh.req_id=%d\n", poh.req_id);
	fprintf(stderr, "poh.skip_hop=%d\n", poh.skip_hop);
	fprintf(stderr, "poh.skip_hop_offset=%d\n", poh.skip_hop_offset);
	fprintf(stderr, "poh.ccninfo_flag=%d\n", poh.ccninfo_flag);
	fprintf(stderr, "poh.req_arrival_time=%u\n", poh.req_arrival_time);
	fprintf(stderr, "poh.node_id=");
	for(dbg_x = 0 ; dbg_x < poh.nodeid_len ; dbg_x++)
		fprintf(stderr, "%x", poh.nodeid_val[dbg_x]);
	fprintf(stderr, " (%d)\n", poh.nodeid_len);

}
#endif  //DEB_CCNINFO

	/* Adds the time stamp on this request 		*/
	id_len = cef_face_node_id_get (peer_faceid, peer_node_id);
	id_len = cefnetd_matched_node_id_get (hdl, peer_node_id, id_len, node_id, &responder_mtu);
	memcpy (stamp_node_id, node_id, id_len);
	new_header_len
		= header_len + CefC_S_TLF + hdl->My_Node_Name_TLV_len + CefC_S_ReqArrivalTime;

	if (poh.skip_hop == 0) {
		if (new_header_len <= CefC_Max_Header_Size) {
			/* NOP */
		} else {
			return_code = CefC_CtRc_NO_SPACE;
#ifdef DEB_CCNINFO
{
	fprintf(stderr, "[%s] NO_SPACE\n",
			"cefnetd_incoming_ccninforeq_process" );
}
#endif //DEB_CCNINFO
			goto SEND_REPLY;
		}
	}
	/* Checks the Validation 			*/
	res = cef_valid_msg_verify_forccninfo
						(msg, payload_len + header_len
					 	 , &(hdl->ccninfo_rcvdpub_key_bi_len)
						 , &(hdl->ccninfo_rcvdpub_key_bi));
	if (res != 0) {
		if (poh.skip_hop != 0) {
			cef_frame_ccninfo_parsed_free (pci);
			return (-1);
		} else {
			return_code = CefC_CtRc_INVALID_REQUEST;
			goto SEND_REPLY;
		}
	}
	/* Check Access Policy */
	if (hdl->ccninfo_access_policy == 2 /* Do not allow access */) {
		return_code = CefC_CtRc_ADMIN_PROHIB;
		goto SEND_REPLY;
	}
	if (hdl->ccninfo_full_discovery == 0 /* Not Allow ccninfo-03 */
		&& poh.ccninfo_flag & CefC_CtOp_FullDisCover) {
		return_code = CefC_CtRc_ADMIN_PROHIB;
		goto SEND_REPLY;
	}

	/* Check whether this cefnetd will be the responder 	*/
	if (poh.skip_hop > 0) {
		forward_req_f = 1;
	} else {
		if (hdl->ccninfo_access_policy == 0 /* No limit */) {
			/* Searches a App Reg Table entry matching this request 		*/
			if (pm.chunk_num_f) {
				name_len = pm.name_len - (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
			} else {
				name_len = pm.name_len;
			}
			fip = (uint16_t*) cef_hash_tbl_item_get_for_app (hdl->app_reg, pm.name, name_len);
#ifdef DEB_CCNINFO
{
	int ii;
	char outstr[4096];
	char xstr[32];

	fprintf(stderr, "DEB_CCNINFO: [%s] CALLed cef_hash_tbl_item_get(%d) fip=%p\n", __FUNCTION__, __LINE__, fip);
	fprintf(stderr, "========== name_len=%d / pm.name ==========\n", name_len);
	memset(outstr, 0, sizeof(outstr));
	for(ii=0; ii < name_len; ii++){
		if(isprint(pm.name[ii])){
			sprintf(xstr, ".%c", pm.name[ii]);
		} else {
			sprintf(xstr, "%02X", (unsigned char)pm.name[ii]);
		}
		strcat(outstr, xstr);
	}
	fprintf(stderr, "%s\n", outstr);
	fprintf(stderr, "===============================\n");
}
#endif //DEB_CCNINFO

			if (fip) {
				/* Check Validations ccninfo-05 */
				if ( poh.ccninfo_flag & CefC_CtOp_ReqValidation ) {
					if ( hdl->ccninfo_valid_type == CefC_T_ALG_INVALID ) {
						/* Invalid */
						return_code = CefC_CtRc_INVALID_REQUEST;
						goto SEND_REPLY;
					}
				}
				/* ccninfo-05 */
				cefnetd_FHR_Reply_process(hdl, peer_faceid, msg, payload_len, header_len
											, ((CefT_App_Reg*)fip)->name, ((CefT_App_Reg*)fip)->name_len, &poh);
				cef_frame_ccninfo_parsed_free (pci);
				return (0);
			} else {
				forward_req_f = 1;
			}
		}

#ifdef CefC_ContentStore
		if (hdl->cs_stat != NULL
			&& hdl->ccninfo_access_policy == 0 /* No limit */
			&& !(poh.ccninfo_flag & CefC_CtOp_Publisher)
			&& hdl->cs_stat->cache_type != CefC_Cache_Type_ExConpub
		   ) {
			/* Checks whether the specified contents is cached 	*/
			if (hdl->cs_stat->cache_type != CefC_Default_Cache_Type) {
				/* Query by Name without chunk number to check if content exists */
				if (pm.chunk_num_f) {
					name_len = pm.name_len - (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
				} else {
					name_len = pm.name_len;
				}
				/* Check content exists */
				res = cef_csmgr_excache_item_check_for_ccninfo (hdl->cs_stat, pm.name, name_len);
				if (res < 0) {
					forward_req_f = 1;
				} else {
					forward_req_f = 0;
				}
				if (forward_req_f != 1) {
					/* Query by Name to check if content(or chunk) exists */
					res = cefnetd_external_cache_seek (
						hdl, peer_faceid, msg, payload_len, header_len, &pm, &poh);

					if (res > 0) {
						cef_frame_ccninfo_parsed_free (pci);
						return (1);
					}
					forward_req_f = 1;
				}
			}
		} else {
			forward_req_f = 1;
		}
#endif // CefC_ContentStore
	}

	if ((forward_req_f == 1) && (pm.hoplimit == 1)) {
		forward_req_f = 0;
		return_code = CefC_CtRc_NO_INFO;
	}

	/* Detect Loop? */
	if ( detect_loop == 1 ) {
		goto SEND_REPLY;
	}

	if (forward_req_f) {
		/* Searches a FIB entry matching this request 		*/
		if (pm.chunk_num_f) {
			name_len = pm.name_len - (CefC_S_Type + CefC_S_Length + CefC_S_ChunkNum);
		} else {
			/* Symbolic Interest	*/
			name_len = pm.name_len;
		}

		/* Searches a FIB entry matching this request 	*/
		fe = cef_fib_entry_search (hdl->fib, pm.name, name_len);

		/* Obtains Face-ID(s) to forward the request 	*/
		if (fe) {

			/* Obtains the FaceID(s) to forward the request 	*/
			face_num = cef_fib_forward_faceid_select (fe, peer_faceid, faceids);

			/* Searches a PIT entry matching this request 	*/
			/* Create PIT for ccninfo ccninfo-03 */
			memset( ccninfo_pit, 0x00, 1024 );
			ccninfo_pit_len = cefnetd_ccninfo_pit_create( hdl, pci, ccninfo_pit, CCNINFO_REQ, 0 );
			pe = cef_pit_entry_search (hdl->pit, &pm, &poh, ccninfo_pit, ccninfo_pit_len);
			if ( pe != NULL ) {
				/* Alredy passed request */
				cef_frame_ccninfo_parsed_free (pci);
				return(-1);
			}

			pe = cef_pit_entry_lookup (hdl->pit, &pm, &poh, ccninfo_pit, ccninfo_pit_len);

			if (pe == NULL) {
				cef_frame_ccninfo_parsed_free (pci);
				return (-1);
			}

			/* Updates the information of down face that this request arrived 	*/
			res = cef_pit_entry_down_face_update (pe, peer_faceid, &pm, &poh, msg, CefC_IntRetrans_Type_SUP);

			/* Forwards the received Ccninfo Request */
			if (res != 0) {
				/* ccninfo-05 */
				if (poh.skip_hop > 0) {
					/* NOP */
				} else {
					gettimeofday (&t, NULL);
					pkt_len = cef_frame_ccninfo_req_add_stamp (
									msg, payload_len + header_len, hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len, t);
					header_len 	= msg[CefC_O_Fix_HeaderLength];
					payload_len = pkt_len - header_len;
				}
				/* ccninfo-05 */
				/* Updates the skip hop 					*/
				if (poh.skip_hop > 0) {
					poh.skip_hop--;
//					msg[poh.skip_hop_offset] = poh.skip_hop;
					w_skiphop.sh_4bit = poh.skip_hop;
					w_skiphop.fl_4bit = 0;
					memcpy(&msg[poh.skip_hop_offset], &w_skiphop, 1);
				}
				pm.hoplimit--;
				msg[CefC_O_Fix_HopLimit] = pm.hoplimit;
#ifdef DEB_CCNINFO
{
	int dbg_x;
	fprintf (stderr, "DEB_CCNINFO: Forward Ccninfo Request's Msg [ ");
	for (dbg_x = 0 ; dbg_x < payload_len+header_len ; dbg_x++) {
		fprintf (stderr, "%02x ", msg[dbg_x]);
	}
	fprintf (stderr, "](%d)\n", payload_len+header_len);
}
#endif //DEB_CCNINFO
				/* Forwards 		*/
				cefnetd_ccninforeq_forward (
					hdl, faceids, face_num, peer_faceid, msg,
					payload_len, header_len, &pm, &poh, pe, fe);
			}
			cef_frame_ccninfo_parsed_free (pci);
			return (1);
		} else {
			if (poh.skip_hop > 0) {
				cef_frame_ccninfo_parsed_free (pci);
				return (1);
			}
			return_code = CefC_CtRc_NO_ROUTE;
		}
	}

SEND_REPLY:;
#ifdef	DEB_CCNINFO
	printf( "[%s] SEND_REPLY\n",
			"cefnetd_incoming_ccninforeq_process" );
#endif
	/* ccninfo-05 */
	switch ( return_code ) {
		case	CefC_CtRc_NO_ERROR:
			if ( detect_loop == 1 ) {
				gettimeofday (&t, NULL);
				pkt_len = cef_frame_ccninfo_req_add_stamp (
								msg, payload_len + header_len, hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len, t);
				header_len 	= msg[CefC_O_Fix_HeaderLength];
				payload_len = pkt_len - header_len;
			}
			break;
		case	CefC_CtRc_NO_SPACE:
			break;
		default:
			gettimeofday (&t, NULL);
			pkt_len = cef_frame_ccninfo_req_add_stamp (
							msg, payload_len + header_len, hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len, t);
			header_len 	= msg[CefC_O_Fix_HeaderLength];
			payload_len = pkt_len - header_len;
			break;
	}

	if ( return_code != CefC_CtRc_NO_SPACE ) {
		if ( poh.ccninfo_flag & CefC_CtOp_ReqValidation ) {
			if ( hdl->ccninfo_valid_type == CefC_T_ALG_INVALID ) {
				return_code = CefC_CtRc_INVALID_REQUEST;
			} else {
				/* NOP */
			}
		} else {
			/* NOP */
		}
	}
	/* ccninfo-05 */
	/* ccninfo-03 */
	/* Loop? */
	if ( detect_loop == 1 ) {
		return_code |= CefC_CtRc_FATAL_ERROR;
	}
	/* ccninfo-03 */

	/* Remove the Valation-related TLV from the message */
	pkt_len = cef_valid_remove_valdsegs_fr_msg_forccninfo (msg, pkt_len);
	/* Set PacketType and Return Code 		*/
	msg[CefC_O_Fix_Type] 			= CefC_PT_REPLY;
	msg[CefC_O_Fix_Ccninfo_RetCode] 	= return_code;


	/*----------------------------------------------------------*/
	/* Validations	 											*/
	/*----------------------------------------------------------*/
	if ( poh.ccninfo_flag & CefC_CtOp_ReqValidation )
	{
		CefT_Ccninfo_TLVs tlvs;

		if (hdl->ccninfo_valid_type != CefC_T_ALG_INVALID) {
			tlvs.alg.valid_type = hdl->ccninfo_valid_type;
			pkt_len = cef_frame_ccninfo_vald_create_for_reply (msg, &tlvs);
		}
	}
	/* Check Ccninfo Relpy size										*/
	/*   NOTE: When oversize, create a reply message of NO_SPECE	*/
	pkt_len = cefnetd_ccninfo_check_relpy_size(hdl, msg, pkt_len, poh.ccninfo_flag);

	/* Returns a Ccninfo Reply with error return code 		*/
	cef_face_frame_send_forced (peer_faceid, msg, pkt_len);

	cef_frame_ccninfo_parsed_free (pci);

	return (1);
}
#ifdef CefC_ContentStore
/*--------------------------------------------------------------------------------------
	Seeks the csmgr and creates the ccninfo response
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_external_cache_seek (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	CefT_CcnMsg_MsgBdy* pm,				/* Structure to set parsed CEFORE message	*/
	CefT_CcnMsg_OptHdr* poh
) {
	unsigned char buff[CefC_Max_Length] = {0};
	int res=-1;
	uint16_t pld_len;
	uint16_t index;
	uint16_t name_len;
	uint16_t msg_len;
	uint16_t pkt_len;
	uint16_t pld_len_new;
	struct tlv_hdr pyld_tlv_hdr;
	struct fixed_hdr* fix_hdr;
	struct tlv_hdr* tlv_hp;
	struct tlv_hdr* name_tlv_hdr;
//	struct value32_tlv value32_tlv;	/* for T_DISC_REPLY */
	uint8_t rtn_cd;
	struct ccninfo_req_block req_blk;	/* for T_DISC_REPLY ccninfo-05 */

#ifdef	DEB_CCNINFO
	printf( "[%s] IN payload_len(%d) + header_len(%d) = %d\n",
			"cefnetd_external_cache_seek", payload_len, header_len, payload_len + header_len );
#endif

	/* Remove the Valation-related TLV from the message */
	payload_len = cef_valid_remove_valdsegs_fr_msg_forccninfo (msg, payload_len+header_len)
				  - header_len;
	if (hdl->cs_stat->cache_type != CefC_Cache_Type_Localcache) {
		if (pm->chunk_num_f) {
			res = cef_csmgr_excache_info_get (
				hdl->cs_stat, pm->name, pm->name_len, buff, 0/*ExactMatch*/);
		} else {
			res = cef_csmgr_excache_info_get (
				hdl->cs_stat, pm->name, pm->name_len, buff, 1/*PartialMatch*/);
		}
	}
#ifdef CefC_CefnetdCache
	else {
		res = cef_csmgr_locache_info_get (
			hdl->cs_stat, pm->name, pm->name_len, buff, 0/*ExactMatch*/);
	}
#endif //CefC_CefnetdCache
	if (res < 1) {
		return (0);
	}

#ifdef DEB_CCNINFO
{
	int dbg_x;
	fprintf (stderr, "res = %d [ ", res);
	for (dbg_x = 0 ; dbg_x < res ; dbg_x++) {
		fprintf (stderr, "%02x ", buff[dbg_x]);
	}
	fprintf (stderr, "]\n");
}
#endif //DEB_CCNINFO

	/* Check Validations */
	if ( poh->ccninfo_flag & CefC_CtOp_ReqValidation ) {
		if ( hdl->ccninfo_valid_type == CefC_T_ALG_INVALID ) {
			/* Invalid */
			rtn_cd = CefC_CtRc_INVALID_REQUEST;
			res = 0;
#ifdef DEB_CCNINFO
	printf( "\t000 rtn_cd = CefC_CtRc_INVALID_REQUEST\n");
#endif //DEB_CCNINFO
		} else {
			rtn_cd = CefC_CtRc_NO_ERROR;
#ifdef DEB_CCNINFO
	printf( "\t001 rtn_cd = CefC_CtRc_NO_ERROR\n");
#endif //DEB_CCNINFO
		}
	} else {
		rtn_cd = CefC_CtRc_NO_ERROR;
#ifdef DEB_CCNINFO
	printf( "\t002 rtn_cd = CefC_CtRc_NO_ERROR\n");
#endif //DEB_CCNINFO
	}


	/* Returns a Ccninfo Reply 			*/
	if (res) {
		if (poh->ccninfo_flag & CefC_CtOp_Cache) {
			pld_len = (uint16_t) res;
			index   = 0;

			while (index < pld_len) {
				index += CefC_S_TLF + sizeof (struct ccninfo_rep_block);

				name_tlv_hdr = (struct tlv_hdr*) &buff[index];
				name_len = ntohs (name_tlv_hdr->length);
				index += CefC_S_TLF;

				/* Sets the header of Reply Block 		*/
				index += name_len;
			}

			/* Sets type and length of T_DISC_REPLY 		*/
			pyld_tlv_hdr.type 	 = htons (CefC_T_DISC_REPLY);
			/* pld_len + R_ArrTime + NodeID */
//JK			pld_len_new = CefC_S_TLF + CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len + pld_len;
			pld_len_new = CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len + pld_len;
#ifdef DEB_CCNINFO
		printf( "\t010 pld_len_new = %d(%x)\n", pld_len_new, pld_len_new);
#endif //DEB_CCNINFO
			pyld_tlv_hdr.length  = htons (pld_len_new);
			memcpy (&msg[payload_len + header_len], &pyld_tlv_hdr, sizeof (struct tlv_hdr));
			index = payload_len + header_len;
#ifdef DEB_CCNINFO
		printf( "\t011 index = %d(%x)\n", index, index);
#endif //DEB_CCNINFO
			index += CefC_S_TLF;
#ifdef DEB_CCNINFO
		printf( "\t011-1 index = %d(%x)\n", index, index);
#endif //DEB_CCNINFO
			{	/* set 32bit-NTP time */
		    	struct timespec tv;
				uint32_t ntp32b;
				clock_gettime(CLOCK_REALTIME, &tv);
				ntp32b = ((tv.tv_sec + 32384) << 16) + ((tv.tv_nsec << 7) / 1953125);
				req_blk.req_arrival_time 	= htonl (ntp32b);
				memcpy (&msg[index], &req_blk, sizeof (struct ccninfo_req_block));
//JK				index += CefC_S_TLF + CefC_S_ReqArrivalTime;
				index += CefC_S_ReqArrivalTime;
#ifdef DEB_CCNINFO
		printf( "\t012 index = %d(%x)\n", index, index);
#endif //DEB_CCNINFO
			}
			/* NODE ID */
			memcpy ( &msg[index], hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len );
			index += hdl->My_Node_Name_TLV_len;
#ifdef DEB_CCNINFO
		printf( "\t013 index = %d(%x)\n", index, index);
#endif //DEB_CCNINFO
			payload_len += CefC_S_TLF + CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len;
#ifdef DEB_CCNINFO
		printf( "\t014 payload_len = %d(%x)\n", payload_len, payload_len);
#endif //DEB_CCNINFO
			memcpy (&msg[index], buff, pld_len);

			/* Sets ICN message length 	*/
			pkt_len = payload_len + header_len + pld_len;
//JK			pkt_len = payload_len + header_len + CefC_S_TLF + pld_len;
#ifdef DEB_CCNINFO
		printf( "\t015 pkt_len = %d(%x)\n", pkt_len, pkt_len);
#endif //DEB_CCNINFO
			msg_len = pkt_len - (header_len + CefC_S_TLF);
#ifdef DEB_CCNINFO
		printf( "\t016 msg_len = %d(%x)\n", msg_len, msg_len);
#endif //DEB_CCNINFO

			tlv_hp = (struct tlv_hdr*) &msg[header_len];
			tlv_hp->length = htons (msg_len);

			/* Updates PacketLength and HeaderLength 		*/
			fix_hdr = (struct fixed_hdr*) msg;
			fix_hdr->type 	  = CefC_PT_REPLY;
			fix_hdr->reserve1 = rtn_cd;
			fix_hdr->pkt_len  = htons (pkt_len);
		} else {
			pld_len = (uint16_t) 0;
			index   = 0;
			/* Sets type and length of T_DISC_REPLY 		*/
			pyld_tlv_hdr.type 	 = htons (CefC_T_DISC_REPLY);
			/* R_ArrTime + NodeID */
			pld_len_new = CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len;
			pyld_tlv_hdr.length  = htons (pld_len_new);
			memcpy (&msg[payload_len + header_len], &pyld_tlv_hdr, sizeof (struct tlv_hdr));
			index = payload_len + header_len;
			index += CefC_S_TLF;
			{	/* set 32bit-NTP time */
		    	struct timespec tv;
				uint32_t ntp32b;
				clock_gettime(CLOCK_REALTIME, &tv);
				ntp32b = ((tv.tv_sec + 32384) << 16) + ((tv.tv_nsec << 7) / 1953125);
				req_blk.req_arrival_time 	= htonl (ntp32b);
				memcpy (&msg[index], &req_blk, sizeof (struct ccninfo_req_block));
//JK				index += CefC_S_TLF + CefC_S_ReqArrivalTime;
				index += CefC_S_ReqArrivalTime;
			}
			/* NODE ID */
			memcpy ( &msg[index], hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len );
			index += hdl->My_Node_Name_TLV_len;
			payload_len += CefC_S_TLF + CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len;

			/* Sets ICN message length 	*/
//JK			pkt_len = payload_len + header_len + CefC_S_TLF;
			pkt_len = payload_len + header_len + pld_len;
//JK			pkt_len = payload_len + header_len + CefC_S_TLF + pld_len;
			msg_len = pkt_len - (header_len + CefC_S_TLF);

			tlv_hp = (struct tlv_hdr*) &msg[header_len];
			tlv_hp->length = htons (msg_len);
			/* ccninfo-05 */
			fix_hdr = (struct fixed_hdr*) msg;
			fix_hdr->type 	  = CefC_PT_REPLY;
			fix_hdr->reserve1 = rtn_cd;
			pkt_len  		  = fix_hdr->pkt_len;
		}


		/*----------------------------------------------------------*/
		/* Validations	 											*/
		/*----------------------------------------------------------*/
		if ( poh->ccninfo_flag & CefC_CtOp_ReqValidation )
		{
			CefT_Ccninfo_TLVs tlvs;

			if (hdl->ccninfo_valid_type != CefC_T_ALG_INVALID) {
				tlvs.alg.valid_type = hdl->ccninfo_valid_type;
				pkt_len = cef_frame_ccninfo_vald_create_for_reply (msg, &tlvs);
			}
		}

		/* Check Ccninfo Relpy size										*/
		/*   NOTE: When oversize, create a reply message of NO_SPECE	*/
		pkt_len = cefnetd_ccninfo_check_relpy_size(hdl, msg, pkt_len, poh->ccninfo_flag);
#ifdef DEB_CCNINFO
		printf( "\t020 pkt_len = %d(%x)\n", pkt_len, pkt_len);
#endif //DEB_CCNINFO

#ifdef CefC_Debug
			cef_dbg_write (CefC_Dbg_Finest, "Ccninfo response is follow:\n");
		cef_dbg_buff_write (CefC_Dbg_Finest, msg, pkt_len);
#endif // CefC_Debug
		cef_face_frame_send_forced (peer_faceid, msg, pkt_len);
#ifdef	DEB_CCNINFO
	printf( "[%s] Return(1)\n",
			"cefnetd_external_cache_seek" );
#endif

		return (1);
	}

#ifdef	DEB_CCNINFO
	printf( "[%s] Return(0)\n",
			"cefnetd_external_cache_seek" );
#endif
	return (0);
}
#endif // CefC_ContentStore
/*--------------------------------------------------------------------------------------
	Create and Send the FHR's ccninfo response
----------------------------------------------------------------------------------------*/
static void
cefnetd_FHR_Reply_process(
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	const unsigned char* name,				/* Report name								*/
	uint32_t name_len,						/* Report name length						*/
	CefT_CcnMsg_OptHdr* poh
){
	unsigned char rply_sub_block[CefC_Max_Length] = {0};
	uint16_t index = 0;
	uint16_t rec_index;
	struct ccninfo_rep_block rep_blk;
	struct tlv_hdr rply_tlv_hdr;
	struct tlv_hdr name_tlv_hdr;
	struct tlv_hdr pyld_tlv_hdr;
	struct tlv_hdr* tlv_hp;
	uint16_t msg_len;
	uint16_t pkt_len;
	struct fixed_hdr* fix_hdr;
	uint16_t pld_len_new;
	uint16_t pld_len;
	struct ccninfo_req_block req_blk;	/* for T_DISC_REPLY ccninfo-05 */

#ifdef	DEB_CCNINFO
	printf( "[%s] IN payload_len(%d) + header_len(%d) = %d\n",
			"cefnetd_FHR_Reply_process", payload_len, header_len, payload_len + header_len );
#endif

	/* Remove the Valation-related TLV from the message */
	payload_len = cef_valid_remove_valdsegs_fr_msg_forccninfo (msg, payload_len+header_len)
				  - header_len;

	if (!(poh->ccninfo_flag & CefC_CtOp_Cache)) {
		pld_len = 0;
		goto SKIP_REP_BLK;
	}
	name_tlv_hdr.type = htons (CefC_T_NAME);

	rec_index = index;
	index += CefC_S_TLF;
	//ccninfo-05
	rep_blk.cont_size 	= htonl (UINT32_MAX);
	rep_blk.cont_cnt 	= htonl (UINT32_MAX);
	rep_blk.rcv_int 	= htonl (UINT32_MAX);
	rep_blk.first_seq 	= htonl (UINT32_MAX);
	rep_blk.last_seq 	= htonl (UINT32_MAX);
	rep_blk.cache_time 	= htonl (UINT32_MAX);
	rep_blk.remain_time	= htonl (UINT32_MAX);

	memcpy (&rply_sub_block[index], &rep_blk, sizeof (struct ccninfo_rep_block));
	index += sizeof (struct ccninfo_rep_block);

	/* Name 				*/
	name_tlv_hdr.length = htons (name_len);
	memcpy (&rply_sub_block[index], &name_tlv_hdr, sizeof (struct tlv_hdr));
	memcpy (&rply_sub_block[index + CefC_S_TLF], name, name_len);
	index += CefC_S_TLF + name_len;

	/* Sets the header of Reply Block 		*/
	rply_tlv_hdr.type = htons (CefC_T_DISC_CONTENT_OWNER);
	rply_tlv_hdr.length = htons (index - (rec_index + CefC_S_TLF));
	memcpy (&rply_sub_block[rec_index], &rply_tlv_hdr, sizeof (struct tlv_hdr));
	pld_len = index;

SKIP_REP_BLK:;

	/* Sets type and length of T_DISC_REPLY 		*/
	pyld_tlv_hdr.type 	 = htons (CefC_T_DISC_REPLY);
	/* ccninfo-05 */
	/* pld_len + R_ArrTime + NodeID */
	pld_len_new = CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len + pld_len;
	pyld_tlv_hdr.length  = htons (pld_len_new);
	memcpy (&msg[payload_len + header_len], &pyld_tlv_hdr, sizeof (struct tlv_hdr));
	index = payload_len + header_len;
	index += CefC_S_TLF;
	{	/* set 32bit-NTP time */
    	struct timespec tv;
		uint32_t ntp32b;
		clock_gettime(CLOCK_REALTIME, &tv);
		ntp32b = ((tv.tv_sec + 32384) << 16) + ((tv.tv_nsec << 7) / 1953125);
		req_blk.req_arrival_time 	= htonl (ntp32b);
		memcpy (&msg[index], &req_blk, sizeof (struct ccninfo_req_block));
		index += CefC_S_ReqArrivalTime;
	}
	/* NODE ID */
	memcpy ( &msg[index], hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len );
	index += hdl->My_Node_Name_TLV_len;
	payload_len += CefC_S_TLF + CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len;

	if ( pld_len > 0 ) {
		memcpy (&msg[index], rply_sub_block, pld_len);
	}

	/* Sets ICN message length 	*/
	pkt_len = payload_len + header_len + pld_len;
	msg_len = pkt_len - (header_len + CefC_S_TLF);

	tlv_hp = (struct tlv_hdr*) &msg[header_len];
	tlv_hp->length = htons (msg_len);

	/* Updates PacketLength and HeaderLength 		*/
	fix_hdr = (struct fixed_hdr*) msg;
	fix_hdr->type 	  = CefC_PT_REPLY;
	fix_hdr->reserve1 = CefC_CtRc_NO_ERROR;
	fix_hdr->pkt_len  = htons (pkt_len);
#ifdef DEB_CCNINFO
{
	int dbg_x;
	fprintf (stderr, "DEB_CCNINFO: Sent Ccninfo Relpy's Msg [ ");
	for (dbg_x = 0 ; dbg_x < pkt_len ; dbg_x++) {
		fprintf (stderr, "%02x ", msg[dbg_x]);
	}
	fprintf (stderr, "](%d)\n", pkt_len);
}
#endif //DEB_CCNINFO

	/*----------------------------------------------------------*/
	/* Validations	 											*/
	/*----------------------------------------------------------*/
	if ( poh->ccninfo_flag & CefC_CtOp_ReqValidation )
	{
		CefT_Ccninfo_TLVs tlvs;

		if (hdl->ccninfo_valid_type != CefC_T_ALG_INVALID) {
			tlvs.alg.valid_type = hdl->ccninfo_valid_type;
			pkt_len = cef_frame_ccninfo_vald_create_for_reply (msg, &tlvs);
		}
	}

	/* Check Ccninfo Relpy size										*/
	/*   NOTE: When oversize, create a reply message of NO_SPECE	*/
	pkt_len = cefnetd_ccninfo_check_relpy_size(hdl, msg, pkt_len, poh->ccninfo_flag);

#ifdef DEB_CCNINFO
{
	int dbg_x;
	fprintf (stderr, "DEB_CCNINFO: Sent Ccninfo Relpy's Msg(2) [ ");
	for (dbg_x = 0 ; dbg_x < pkt_len ; dbg_x++) {
		fprintf (stderr, "%02x ", msg[dbg_x]);
	}
	fprintf (stderr, "](%d)\n", pkt_len);
}
#endif //DEB_CCNINFO
	cef_face_frame_send_forced (peer_faceid, msg, pkt_len);
}


/*--------------------------------------------------------------------------------------
	Handles the received Ccninfo Replay
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_ccninforep_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {

	CefT_CcnMsg_MsgBdy pm = { 0 };
	CefT_CcnMsg_OptHdr poh = { 0 };
	CefT_Pit_Entry* pe;
	int res, i;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	CefT_Down_Faces* face;
	// ccninfo-03
	CefT_Parsed_Ccninfo	*pci;
	unsigned char ccninfo_pit[1024];
	int			  ccninfo_pit_len;
#ifdef DEB_CCNINFO
{
	int dbg_x;
	fprintf (stderr, "DEB_CCNINFO: Rcvd Ccninfo Relpy's Msg (cefnetd_incoming_ccninforep_process())[ ");
	for (dbg_x = 0 ; dbg_x < payload_len+header_len ; dbg_x++) {
		fprintf (stderr, "%02x ", msg[dbg_x]);
	}
	fprintf (stderr, "](%d)\n", payload_len+header_len);
}
#endif //DEB_CCNINFO
#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Process the Ccnifno Response (%d bytes)\n", payload_len + header_len);
#endif // CefC_Debug

#ifdef	DEB_CCNINFO
	fprintf( stderr, "[%s] Ccninfo Response (%d bytes)\n",
			"cefnetd_incoming_ccninforep_process", payload_len + header_len );
#endif

	/* Check the message size 		*/
	if (payload_len + header_len > CefC_Max_Msg_Size) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Ccninfo Response is too large\n");
#endif // CefC_Debug
		return (-1);
	}
	/* Check Access Policy */
	if (hdl->ccninfo_access_policy == 2 /* Do not allow access */) {
#ifdef	DEB_CCNINFO
		fprintf( stderr, "\t(hdl->ccninfo_access_policy == 2 /* Do not allow access */) return(0)\n" );
#endif
		return (0);
	}

	/* Parses the received  Cefping Replay 	*/
	res = cef_frame_message_parse (
					msg, payload_len, header_len, &poh, &pm, CefC_PT_REPLY);
	if (res < 0) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Fine, "Detects the invalid Ccninfo Response\n");
#endif // CefC_Debug
#ifdef	DEB_CCNINFO
		fprintf( stderr, "\tDetects the invalid Ccninfo Response return(-1)\n" );
#endif
		return (-1);
	}

	/* Parses the received Ccninfo ccninfo-03	*/
	if ( (pci = cef_frame_ccninfo_parse (msg)) == NULL ){
#ifdef	DEB_CCNINFO
		fprintf( stderr, "\t(res < 0)cef_frame_ccninfo_parse() return(-1)\n" );
#endif
		return(-1);
	}
#ifdef DEB_CCNINFO
	cefnetd_dbg_cpi_print ( pci );
#endif

#ifdef CefC_Debug
	cef_dbg_buff_write_name (CefC_Dbg_Finer,
								(unsigned char*)"Ccninfo Response's Name [", strlen("Ccninfo Response's Name ["),
								pm.name, pm.name_len,
								(unsigned char*)" ]\n", strlen(" ]\n"));
#endif // CefC_Debug

	/* Create PIT for ccninfo ccninfo-03 */
	memset( ccninfo_pit, 0x00, 1024 );
	ccninfo_pit_len = cefnetd_ccninfo_pit_create( hdl, pci, ccninfo_pit, CCNINFO_REP, 1 );

	/* Searches a PIT entry matching this replay 	*/
	pe = cef_pit_entry_search (hdl->pit, &pm, &poh, ccninfo_pit, ccninfo_pit_len);
	if ( pe == NULL ) {
		memset( ccninfo_pit, 0x00, 1024 );
		ccninfo_pit_len = cefnetd_ccninfo_pit_create( hdl, pci, ccninfo_pit, CCNINFO_REP, 0 );
		pe = cef_pit_entry_search (hdl->pit, &pm, &poh, ccninfo_pit, ccninfo_pit_len);
	}

	if (pe) {
		face = &(pe->dnfaces);
		while (face->next) {
			face = face->next;
			faceids[face_num] = face->faceid;
			face_num++;
			cef_pit_entry_down_face_ver_remove (pe, face, &pm);
		}
	} else {
		cef_frame_ccninfo_parsed_free (pci);
		return (-1);
	}

	/* Forwards the Cefping Replay 					*/
	for (i = 0 ; i < face_num ; i++) {

		if (cef_face_check_active (faceids[i]) > 0) {
			cef_face_frame_send_forced (faceids[i], msg, payload_len + header_len);
		} else {
			cef_pit_down_faceid_remove (pe, faceids[i]);
		}
	}
	/* Not full discovery */
	if (pe->stole_f) {
		cef_pit_entry_free (hdl->pit, pe);
	}
	cef_frame_ccninfo_parsed_free (pci);

	return (1);
}
#ifdef CefC_ContentStore
/*--------------------------------------------------------------------------------------
	Handles the received Interest from csmgr
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_csmgr_interest_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {
	/* NOP */;
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the received Object from csmgr
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_csmgr_object_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {
	CefT_CcnMsg_MsgBdy pm = { 0 };
	CefT_CcnMsg_OptHdr poh = { 0 };
	CefT_Pit_Entry* pe;
	int loop_max = 2;						/* For App(0), Trans(1)						*/
	int i, j;
	int res;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	CefT_Down_Faces* face;
	uint16_t pkt_len = 0;	//0.8.3

#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Process the Content Object (%d bytes) from csmgr\n", payload_len + header_len);
	cef_dbg_buff_write (CefC_Dbg_Finest, msg, payload_len + header_len);
#endif // CefC_Debug

#ifdef	__SYMBOLIC__
	fprintf( stderr, "[%s] IN Object (%d bytes) from csmgr\n", __func__, payload_len + header_len );
#endif

	/*--------------------------------------------------------------------
		Parses the Object message
	----------------------------------------------------------------------*/
	if (payload_len + header_len > CefC_Max_Msg_Size) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Finer, "Content Object is too large\n");
#endif // CefC_Debug

		return (-1);
	}

	pkt_len = payload_len + header_len;	//0.8.3

	/* Checks the Validation 			*/
	res = cef_valid_msg_verify (msg, payload_len + header_len);
	if (res != 0) {
#ifdef	__VALID_NG__
		fprintf( stderr, "[%s] Validation NG\n", __func__ );
#endif
		return (-1);
	}

	res = cef_frame_message_parse (
					msg, payload_len, header_len, &poh, &pm, CefC_PT_OBJECT);
	if (res < 0) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Finer,
			"Detects the invalid Content Object from csmgr\n");
#endif // CefC_Debug

		return (-1);
	}
#ifdef	__SYMBOLIC__
	fprintf( stderr, "\t IN Object Chunk:%u \n", pm.chunk_num );
#endif
#ifdef CefC_Debug
	cef_dbg_buff_write_name (CefC_Dbg_Finer,
								(unsigned char*)"Object's Name [", strlen("Object's Name ["),
								pm.name, pm.name_len,
								(unsigned char*)" ]\n", strlen(" ]\n"));
#endif // CefC_Debug

	/*--------------------------------------------------------------------
		Inserts the Cob into temporary cache
	----------------------------------------------------------------------*/
#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer, "Insert the received Content Object to the buffer\n");
#endif // CefC_Debug
	cef_csmgr_cache_insert (hdl->cs_stat, msg, payload_len + header_len, &pm, &poh);

	/*--------------------------------------------------------------------
		Updates the statistics
	----------------------------------------------------------------------*/
	hdl->stat_recv_frames++;

	stat_rcv_size_cnt++;
	stat_rcv_size_sum += (payload_len + header_len);
	if ((payload_len + header_len) < stat_rcv_size_min) {
		stat_rcv_size_min = payload_len + header_len;
	}
	if ((payload_len + header_len) > stat_rcv_size_max) {
		stat_rcv_size_max = payload_len + header_len;
	}

	/*--------------------------------------------------------------------
		Searches a PIT entry matching this Object
	----------------------------------------------------------------------*/

	for (j = 0; j < loop_max; j++) {
		pe = NULL;
		if (j == 0) {
			pe = cef_pit_entry_search_with_chunk (hdl->app_pit, &pm, &poh);	//0.8.3
			if (pe == NULL)
				continue;

		} else {
			pe = cef_pit_entry_search_with_chunk (hdl->pit, &pm, &poh);	//0.8.3
			if ( pe != NULL ){
				/*JK*///20210824 Cob without chunk number
				if ( pm.chunk_num_f == 0 ) {	//Cob without chunk number
					if ( pe->PitType == CefC_PIT_TYPE_Rgl ) {	//PIT_Type is Reg
						//NOP
					} else {	//PIT_Type not Reg(Sym)
						//Miss
						return (-1);
					}
				}
			}
		}

		if (pe) {
			//0.8.3
			if ( pe->COBHR_len > 0 ) {
				/* CobHash */
				SHA256_CTX		ctx;
				uint16_t		CobHash_index;
				uint16_t		CobHash_len;
				unsigned char 	hash[SHA256_DIGEST_LENGTH];

				CobHash_index = header_len;
				CobHash_len   = payload_len;
				SHA256_Init (&ctx);
				SHA256_Update (&ctx, &msg[CobHash_index], CobHash_len);
				SHA256_Final (hash, &ctx);

#ifdef	__RESTRICT__
{
	int hidx;
	char	hash_dbg[1024];

	printf ( "%s\n", __func__ );
	sprintf (hash_dbg, "PIT CobHash [");
	for (hidx = 0 ; hidx < 32 ; hidx++) {
		sprintf (hash_dbg, "%s %02X", hash_dbg, pe->COBHR_selector[hidx]);
	}
	sprintf (hash_dbg, "%s ]\n", hash_dbg);
	printf( "%s", hash_dbg );

	sprintf (hash_dbg, "OBJ CobHash [");
	for (hidx = 0 ; hidx < 32 ; hidx++) {
		sprintf (hash_dbg, "%s %02X", hash_dbg, pe->COBHR_selector[hidx]);
	}
	sprintf (hash_dbg, "%s ]\n", hash_dbg);
	printf( "%s", hash_dbg );
}
#endif
				if ( memcmp(pe->COBHR_selector, hash, 32) == 0 ) {
					/* OK */
#ifdef	__RESTRICT__
					printf ( "%s, CobHash OK\n", __func__ );
#endif
				} else {
					/* NG */
					return (-1);
				}
			}
#ifdef	__RESTRICT__
			printf ( "%s, pe->KIDR_len:%d OK\n", __func__, pe->KIDR_len );
#endif
			if ( pe->KIDR_len > 0 ) {
				/* KeyIdRest */
				int keyid_len;
				unsigned char keyid_buff[32];
				keyid_len = cefnetd_keyid_get( msg, pkt_len, keyid_buff );
				if ( (keyid_len == 32) && (memcmp(pe->KIDR_selector, keyid_buff, 32) == 0) ) {
					/* OK */
#ifdef	__RESTRICT__
					printf ( "%s, KeiId OK\n", __func__ );
#endif
				} else {
					/* NG */
					return (-1);
				}
			}

			face = &(pe->dnfaces);
			while (face->next) {
				face = face->next;
				faceids[face_num] = face->faceid;
				face_num++;
			}
		}

		/*--------------------------------------------------------------------
			Forwards the Content Object
		----------------------------------------------------------------------*/
		if (face_num > 0) {
#ifdef CefC_Debug
			{
				int dbg_x;
				int len = 0;

				len = sprintf (cnd_dbg_msg, "Recorded PIT Faces:");

				for (dbg_x = 0 ; dbg_x < face_num ; dbg_x++) {
					len = len + sprintf (cnd_dbg_msg + len, " %d", faceids[dbg_x]);
				}
				cef_dbg_write (CefC_Dbg_Finer, "%s\n", cnd_dbg_msg);
			}
			cef_dbg_write (CefC_Dbg_Finer, "Forward the Content Object to cefnetd(s)\n");
#endif // CefC_Debug

			cefnetd_object_forward (hdl, faceids, face_num, msg,
				payload_len, header_len, &pm, &poh, pe);

			if (pe->stole_f) {
				cef_pit_entry_free (hdl->pit, pe);
			}

			if(j == 0) {
				return (1);
			}
		}

//0.8.3	S
		if(j == 1) {
			for (i = 0; i < face_num; i++)
				faceids[i] = 0;
			face_num = 0;
			pe = NULL;
			/*JK*///20210824 Cob without chunk number
			if ( pm.chunk_num_f == 0 ) {	//Cob without chunk number
				return (1);
			}
			pe = cef_pit_entry_search_without_chunk (hdl->pit, &pm, &poh);
			if ( pe == NULL ) {
				/* NOP */
			} else {
				//Symbolic/Osyimbolic
				face = &(pe->dnfaces);

				while (face->next) {
					face = face->next;
					faceids[face_num] = face->faceid;
					face_num++;
				}
				if (face_num > 0) {
					if ( pe->PitType == CefC_PIT_TYPE_Sym ) {
#ifdef	__SYMBOLIC__
fprintf( stderr, "\t pe->Last_chunk_num:%ld   pm.chunk_num:%u \n", pe->Last_chunk_num, pm.chunk_num );
#endif
						if ( (pe->Last_chunk_num - hdl->SymbolicBack) <= pm.chunk_num ) {
							cefnetd_object_forward (hdl, faceids, face_num, msg,
								payload_len, header_len, &pm, &poh, pe);
							if ( pe->Last_chunk_num < pm.chunk_num ) {
								pe->Last_chunk_num = pm.chunk_num;
							}
						}
					}
				}
			}
		}

//0.8.3	E

		for (i = 0; i < face_num; i++)
			faceids[i] = 0;
		face_num = 0;
	}


	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the received Ccninfo Request from csmgr
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_csmgr_ccninforeq_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {
	/* NOP */;
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the received Ping Request from csmgr
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_csmgr_pingreq_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	char*	user_id
) {
	/* NOP */;
	return (1);
}
#endif // CefC_ContentStore
/*--------------------------------------------------------------------------------------
	Reads the config file
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_config_read (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
) {
//	char 	ws[1024];
	char 	ws[2048];
	char 	ws_w[1024];
	FILE*	fp = NULL;
	char 	buff[1024];
	char 	pname[64];
	int 	res;
	double 	res_dbl;
	int		IR_enabled = 0;

	/* Obtains the directory path where the cefnetd's config file is located. */
	cef_client_config_dir_get (ws_w);

	if (mkdir (ws_w, 0777) != 0) {
		if (errno == ENOENT) {
			cef_log_write (CefC_Log_Error, "<Fail> cefnetd_config_read (mkdir)\n");
			return (-1);
		}
	}
	sprintf (ws, "%s/cefnetd.conf", ws_w);

	/* Opens the cefnetd's config file. */
	fp = fopen (ws, "r");
	if (fp == NULL) {
		cef_log_write (CefC_Log_Error, "<Fail> cefnetd_config_read (fopen)\n");
		return (-1);
	}

	/* Reads and records written values in the cefnetd's config file. */
	while (fgets (buff, 1023, fp) != NULL) {
		buff[1023] = 0;

		if (buff[0] == 0x23/* '#' */) {
			continue;
		}

		res = cefnetd_trim_line_string (buff, pname, ws);
		if (res < 0) {
			continue;
		}

		if (strcmp (pname, CefC_ParamName_PitSize) == 0) {
			res = atoi (ws);
			if (res < 1) {
				cef_log_write (CefC_Log_Error, "PIT_SIZE must be higher than 0.\n");
				return (-1);
			}
			if (res > 16777215) {	/* 16777216=2^24 */
				cef_log_write (CefC_Log_Error, "PIT_SIZE must be lower than 16777216.\n");
				return (-1);
			}
			hdl->pit_max_size = res;
		} else if (strcmp (pname, CefC_ParamName_FibSize) == 0) {
			res = atoi (ws);
			if (res < 1) {
				cef_log_write (CefC_Log_Error, "FIB_SIZE must be higher than 0.\n");
				return (-1);
			}
			if (res > 65535) {
				cef_log_write (CefC_Log_Error, "FIB_SIZE must be lower than 65536.\n");
				return (-1);
			}
			hdl->fib_max_size = (uint16_t) res;
		} else if (strcmp (pname, CefC_ParamName_Babel) == 0) {
			res = atoi (ws);
			if ((res != 0) && (res != 1)) {
				cef_log_write (CefC_Log_Error, "USE_CEFBABEL must be 0 or 1.\n");
				return (-1);
			}
			hdl->babel_use_f = res;
		} else if (strcmp (pname, CefC_ParamName_Babel_Route) == 0) {
			if (strcmp (ws, "udp") == 0) {
				hdl->babel_route = 0x02;
			} else if (strcmp (ws, "tcp") == 0) {
				hdl->babel_route = 0x01;
			} else if (strcmp (ws, "both") == 0) {
				hdl->babel_route = 0x03;
			} else {
				cef_log_write (CefC_Log_Error,
					"CEFBABEL_ROUTE must be tcp, udp or both\n");
				return (-1);
			}
		} else if (strcasecmp (pname, CefC_ParamName_Cs_Mode) == 0) {
			res = atoi (ws);
#ifdef CefC_NDEF_ContentStore
		if (res == CefC_Cache_Type_Excache){
			cef_log_write (CefC_Log_Error, "CS_MODE 2 is not supported.\n");
			return (-1);
		}
#endif  //CefC_CefnetdCache
#ifndef CefC_CefnetdCache
			if (res == CefC_Cache_Type_Localcache){
				cef_log_write (CefC_Log_Error, "CS_MODE 1 is not supported.\n");
				return (-1);
			}
#endif  //CefC_CefnetdCache
#ifndef CefC_Conpub
			if (res == CefC_Cache_Type_ExConpub){
				cef_log_write (CefC_Log_Error, "CS_MODE 3 is not supported.\n");
				return (-1);
			}
#endif  //CefC_Conpub
			if (!(CefC_Cache_Type_None <= res && res <= CefC_Cache_Type_ExConpub)){
				cef_log_write (CefC_Log_Error, "CS_MODE must be 0/1/2/3\n");
				return (-1);
			}
			hdl->cs_mode = res;
		}
		else if (strcasecmp (pname, CefC_ParamName_Forwarding_Strategy) == 0) {
			int		in_len;

			in_len = strlen(ws);
			if (in_len >= CefC_FwdStrPlg_Max_NameLen) {
				cef_log_write (CefC_Log_Error,
								"FORWARDING_STRATEGY must be less than %d characters.\n", CefC_FwdStrPlg_Max_NameLen);
				return (-1);
			}
			hdl->forwarding_strategy = calloc(in_len + 1, sizeof(char));
			strcpy(hdl->forwarding_strategy ,ws);
		}

		else if (strcasecmp (pname, CefC_ParamName_CcninfoAccessPolicy) == 0) {
			res = atoi (ws);
			if (!(res == 0 || res == 1 || res == 2)) {
				cef_log_write (CefC_Log_Error, "CCNINFO_ACCESS_POLICY must be 0, 1 or 2.\n");
				return (-1);
			}
			hdl->ccninfo_access_policy = res;
		}
		else if (strcasecmp (pname, CefC_ParamName_CcninfoFullDiscovery) == 0) {
			res = atoi (ws);
			if (!(res == 0 || res == 1 || res == 2)) {
				cef_log_write (CefC_Log_Error, "CCNINFO_FULL_DISCOVERY must be 0, 1 or 2.\n");
				return (-1);
			}
			hdl->ccninfo_full_discovery =  res;
		}
		else if (strcasecmp (pname, CefC_ParamName_CcninfoValidAlg) == 0) {
			if (!(strcmp(ws, "None") == 0 || strcmp(ws, "crc32") == 0 || strcmp(ws, "sha256") == 0)) {
				cef_log_write (CefC_Log_Error, "CCNINFO_VALID_ALG must be None, crc32 or sha256.\n");
				return (-1);
			}
			strcpy(hdl->ccninfo_valid_alg ,ws);
			hdl->ccninfo_valid_type = (uint16_t) cef_valid_type_get (hdl->ccninfo_valid_alg);
		}
		else if (strcasecmp (pname, CefC_ParamName_CcninfoSha256KeyPrfx) == 0) {
			strcpy(hdl->ccninfo_sha256_key_prfx ,ws);
		}
		else if (strcasecmp (pname, CefC_ParamName_CcninfoReplyTimeout) == 0) {
			res = atoi (ws);
			if (!(2 <= res && res <= 5)) {
				cef_log_write (CefC_Log_Error
				               , "CCNINFO_REPLY_TIMEOUT must be higher than or equal to 2 and lower than or equal to 5.\n");
				return (-1);
			}
			hdl->ccninfo_reply_timeout =  res;
		}

		else if (strcasecmp (pname, CefC_ParamName_Node_Name) == 0) {
			unsigned char	out_name[CefC_Max_Length];
			unsigned char	out_name_tlv[CefC_Max_Length];
			/* CHeck NodeName */
			res = cefnetd_nodename_check( ws, out_name );
			if ( res <= 0 ) {
				/* Error */
				cef_log_write (CefC_Log_Error, "NODE_NAME contains characters that cannot be used.\n");
				return( -1 );
			} else {
				hdl->My_Node_Name = malloc( sizeof(char) * res + 1 );
				strcpy( hdl->My_Node_Name, (char*)out_name );
				/* Convert Name TLV */
				strcpy( (char*)out_name, "ccnx:/" );
				strcat( (char*)out_name, hdl->My_Node_Name );
				res = cef_frame_conversion_uri_to_name ((char*)out_name, out_name_tlv);
				if ( res < 0 ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "NODE_NAME contains characters that cannot be used.\n");
					return( -1 );
				} else {
					struct tlv_hdr name_tlv_hdr;
					name_tlv_hdr.type = htons (CefC_T_NAME);
					name_tlv_hdr.length = htons (res);
					hdl->My_Node_Name_TLV = (unsigned char*)malloc( res+CefC_S_TLF );
					hdl->My_Node_Name_TLV_len = res + CefC_S_TLF;
					memcpy( &hdl->My_Node_Name_TLV[0], &name_tlv_hdr, sizeof(struct tlv_hdr) );
					memcpy( &hdl->My_Node_Name_TLV[CefC_S_TLF], out_name_tlv, res );
				}
			}
		}
		else if (strcasecmp (pname, CefC_ParamName_PitSize_App) == 0) {
			res = atoi(ws);
			if ( res < 1 ) {
				cef_log_write (CefC_Log_Error, "PIT_SIZE_APP must be higher than 0.\n");
				return (-1);
			}
			if (res >= CefC_PitAppSize_MAX) {
				cef_log_write (CefC_Log_Error, "PIT_SIZE_APP must be lower than 1025.\n");
				return (-1);
			}
			hdl->app_pit_max_size = res;
		}
		else if (strcasecmp (pname, CefC_ParamName_FibSize_App) == 0) {
			res = atoi(ws);
			if ( res < 1 ) {
				cef_log_write (CefC_Log_Error, "FIB_SIZE_APP must be higher than 0.\n");
				return (-1);
			}
			if (res >= CefC_FibAppSize_MAX) {
				cef_log_write (CefC_Log_Error, "FIB_SIZE_APP must be lower than 1024000.\n");
				return (-1);
			}
			hdl->app_fib_max_size = res;
		}
		else if (strcasecmp (pname, CefC_ParamName_SELECTIVE_MAX) == 0) {
			res = atoi(ws);
			if ( res < CefC_SELECTIVE_MIN ) {
				cef_log_write (CefC_Log_Error, "SELECTIVE_INTEREST_MAX_RANGE must be higher than 0.\n");
				return (-1);
			}
			if (res > CefC_SELECTIVE_MAX) {
				cef_log_write (CefC_Log_Error, "SELECTIVE_INTEREST_MAX_RANGE must be lower than 2049.\n");
				return (-1);
			}
			hdl->Selective_max_range = res;
		}

		else if ( strcasecmp (pname, CefC_ParamName_InterestRetrans) == 0 ) {
			if ( strcasecmp( ws, CefC_Default_InterestRetrans ) == 0 ) {
				hdl->IntrestRetrans = CefC_IntRetrans_Type_RFC;
			}
			else if ( strcasecmp( ws, "SUPPRESSIVE" ) == 0 ) {
				hdl->IntrestRetrans = CefC_IntRetrans_Type_SUP;
			}
			else {
				cef_log_write (CefC_Log_Error, "INTEREST_RETRANSMISSION must be RFC8569 or SUPPRESSIVE.\n");
				return (-1);
			}
		}
		else if ( strcasecmp (pname, CefC_ParamName_SelectiveForward) == 0 ) {
			res = atoi(ws);
			if ( (res != 0) && (res != 1) ) {
				cef_log_write (CefC_Log_Error, "SELECTIVE_FORWARDING must be 0 or 1.\n");
				return (-1);
			}
			hdl->Selective_fwd = res;
		}
		else if ( strcasecmp (pname, CefC_ParamName_SymbolicBackBuff) == 0 ) {
			res = atoi(ws);
			if ( res < 0 ) {
				cef_log_write (CefC_Log_Error, "SYMBOLIC_BACKBUFFER must be higher than 0.\n");
				return (-1);
			}
			hdl->SymbolicBack = res;
		}
		else if ( strcasecmp (pname, CefC_ParamName_BANDWIDTH_INTVAL) == 0 ) {
			res = atoi(ws);
			if ( res < 0 ) {
				cef_log_write (CefC_Log_Error, "BANDWIDTH_STAT_INTERVAL must be higher than 0.\n");
				return (-1);
			}
			hdl->BW_Stat_interval = res;
		}
		else if ( strcasecmp (pname, CefC_ParamName_SYMBOLIC_LIFETIME) == 0 ) {
			res = atoi(ws);
			if ( res < 0 ) {
				cef_log_write (CefC_Log_Error, "SYMBOLIC_INTEREST_MAX_LIFETIME must be higher than 0.\n");
				return (-1);
			}
			hdl->Symbolic_max_lifetime = res * 1000;
		}
		else if ( strcasecmp (pname, CefC_ParamName_REGULAR_LIFETIME) == 0 ) {
			res = atoi(ws);
			if ( res < 0 ) {
				cef_log_write (CefC_Log_Error, "REGULAR_INTEREST_MAX_LIFETIME must be higher than 0.\n");
				return (-1);
			}
			hdl->Regular_max_lifetime = res * 1000;
		}
		else if ( strcasecmp (pname, CefC_ParamName_IR_Congesion) == 0 ) {
			res_dbl = atof( ws );
			if ( res_dbl <= 0.0 ) {
				cef_log_write (CefC_Log_Error
				               , "INTEREST_RETURN_CONGESTION_THRESHOLD must be higher than or equal to 0.\n");
				return (-1);
			}
			hdl->IR_Congesion = res_dbl;
		}
		else if ( strcasecmp (pname, CefC_ParamName_BW_STAT_PLUGIN) == 0 ) {
			strcpy (hdl->bw_stat_pin_name, ws);
		}
		else if ( strcasecmp (pname, CefC_ParamName_CSMGR_ACCESS) == 0 ) {
			if ( strcasecmp( ws, "RW" ) == 0 ) {
				hdl->Ex_Cache_Access = CefC_Default_CSMGR_ACCESS_RW;
			}
			else if ( strcasecmp( ws, "RO" ) == 0 ) {
				hdl->Ex_Cache_Access = CefC_Default_CSMGR_ACCESS_RO;
			}
			else {
				cef_log_write (CefC_Log_Error, "CSMGR_ACCESS must RW or RO.\n");
				return (-1);
			}
		}
		else if ( strcasecmp (pname, CefC_ParamName_BUFFER_CACHE_TIME) == 0 ) {
			res = atoi(ws);
			if ( res < 0 ) {
				cef_log_write (CefC_Log_Error, "BUFFER_CACHE_TIME be higher than or equal to 0.\n");
				return (-1);
			}
			hdl->Buffer_Cache_Time = res * 1000;
		}
		//202108
#ifdef	CefC_INTEREST_RETURN
		else if ( strcasecmp (pname, CefC_ParamName_IR_Option) == 0 ) {
			res = atoi(ws);
			if ( (res == 0 ) || (res == 1) ){

			} else {
				cef_log_write (CefC_Log_Error, "ENABLE_INTEREST_RETURN must 0 or 1.\n");
				return (-1);
			}
			hdl->IR_Option = res;
		}
		else if ( strcasecmp (pname, CefC_ParamName_IR_Enabled) == 0 ) {
			IR_enabled = 1;
			{
				char*	wk_p;
				int		wk_i;
				int		wk_len;
				int		ll;
				wk_p = strtok( ws, "," );
				wk_len = strlen(wk_p);
				for ( ll = 0; ll < wk_len; ll++ ) {
					if ( isdigit(wk_p[ll]) != 0 ) {
						/* digit */
					} else {
						cef_log_write (CefC_Log_Error, "ENABLED_RETURN_CODE is not a number.\n");
						return (-1);
					}
				}
				wk_i = strtol(wk_p, NULL, 10);
				if ((wk_i < 1) || (wk_i > 9)) {
					cef_log_write (CefC_Log_Error, "ENABLED_RETURN_CODE must 1 to 9.\n");
					return (-1);
				}
				hdl->IR_enable[wk_i] = 1;
				while( wk_p != NULL ) {
					wk_p = strtok( NULL, "," );
					if ( wk_p != NULL ) {
						wk_len = strlen(wk_p);
						for ( ll = 0; ll < wk_len; ll++ ) {
							if ( isdigit(wk_p[ll]) != 0 ) {
								/* digit */
							} else {
								cef_log_write (CefC_Log_Error, "ENABLED_RETURN_CODE is not a number.\n");
								return (-1);
							}
						}
						wk_i = strtol(wk_p, NULL, 10);
						if ((wk_i < 1) || (wk_i > 9)) {
							cef_log_write (CefC_Log_Error, "ENABLED_RETURN_CODE must 1 to 9.\n");
							return (-1);
						}
						hdl->IR_enable[wk_i] = 1;
					}
				}
			}
		}
#endif	//CefC_INTREST_RETURN
#ifdef CefC_Debug
		else if (strcmp (pname, "CEF_DEBUG_LEVEL") == 0) {
			int log_lv = 0;
			log_lv = atoi (ws);
			if (log_lv == CefC_Dbg_Finest) {
				cef_dbg_loglv_finest = 1;
			}
		}
#endif // CefC_Debug
		else {
			/* NOP */;
		}
	}
	fclose (fp);

	//202108
	if ( hdl->IR_Option == 0 ) {
		strcpy( hdl->bw_stat_pin_name, "None" );
	} else {	//IR_Option==1
		if ( IR_enabled == 0 ) {
			//Set default
			hdl->IR_enable[1] = 1;
			hdl->IR_enable[2] = 1;
			hdl->IR_enable[6] = 1;
		}
	}

#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Fine, "PORT_NUM = %d\n", hdl->port_num);
	cef_dbg_write (CefC_Dbg_Fine, "PIT_SIZE = %d\n", hdl->pit_max_size);
	cef_dbg_write (CefC_Dbg_Fine, "FIB_SIZE = %d\n", hdl->fib_max_size);
	cef_dbg_write (CefC_Dbg_Fine, "PIT_SIZE_APP = %d\n", hdl->app_pit_max_size);
	cef_dbg_write (CefC_Dbg_Fine, "FIB_SIZE_APP = %d\n", hdl->app_fib_max_size);
	cef_dbg_write (CefC_Dbg_Fine, "FORWARDING_STRATEGY = %s\n", hdl->forwarding_strategy);
	cef_dbg_write (CefC_Dbg_Fine, "INTEREST_RETRANSMISSION = %s\n",
					(hdl->IntrestRetrans == CefC_IntRetrans_Type_RFC) ? "RFC8569" : "SUPPRESSIVE" );
	cef_dbg_write (CefC_Dbg_Fine, "SELECTIVE_FORWARDING    = %d\n", hdl->Selective_fwd);
	cef_dbg_write (CefC_Dbg_Fine, "SYMBOLIC_BACKBUFFER     = %d\n", hdl->SymbolicBack);
	cef_dbg_write (CefC_Dbg_Fine, "INTEREST_RETURN_CONGESTION_THRESHOLD = %f\n", hdl->IR_Congesion);
	cef_dbg_write (CefC_Dbg_Fine, "BANDWIDTH_STAT_INTERVAL = %d\n", hdl->BW_Stat_interval);
	cef_dbg_write (CefC_Dbg_Fine, "SYMBOLIC_INTEREST_MAX_LIFETIME = %d\n", hdl->Symbolic_max_lifetime);
	cef_dbg_write (CefC_Dbg_Fine, "REGULAR_INTEREST_MAX_LIFETIME = %d\n", hdl->Regular_max_lifetime);
	cef_dbg_write (CefC_Dbg_Fine, "CSMGR_ACCESS = %s\n",
					(hdl->Ex_Cache_Access == CefC_Default_CSMGR_ACCESS_RW) ? "RW" : "RO" );
	cef_dbg_write (CefC_Dbg_Fine, "BUFFER_CACHE_TIME    = %d\n", hdl->Buffer_Cache_Time);
	cef_dbg_write (CefC_Dbg_Fine, "BANDWIDTH_STAT_PLUGIN = %s\n", hdl->bw_stat_pin_name);
	//202108
	cef_dbg_write (CefC_Dbg_Fine, "ENABLE_INTEREST_RETURN = %d\n", hdl->IR_Option);
	cef_dbg_write (CefC_Dbg_Fine, "ENABLED_RETURN_CODE = %d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", hdl->IR_enable[0],
																						   hdl->IR_enable[1],
																						   hdl->IR_enable[2],
																						   hdl->IR_enable[3],
																						   hdl->IR_enable[4],
																						   hdl->IR_enable[5],
																						   hdl->IR_enable[6],
																						   hdl->IR_enable[7],
																						   hdl->IR_enable[8],
																						   hdl->IR_enable[9] );
	cef_dbg_write (CefC_Dbg_Fine, "SELECTIVE_INTEREST_MAX_RANGE = %d\n", hdl->Selective_max_range);	//20220311

	cef_dbg_write (CefC_Dbg_Fine, "CCNINFO_ACCESS_POLICY = %d\n"
								, hdl->ccninfo_access_policy);
	cef_dbg_write (CefC_Dbg_Fine, "CCNINFO_FULL_DISCOVERY = %d\n"
								, hdl->ccninfo_full_discovery);
	cef_dbg_write (CefC_Dbg_Fine, "CCNINFO_VALID_ALG = %s (type=%u)\n"
								, hdl->ccninfo_valid_alg, hdl->ccninfo_valid_type);
	cef_dbg_write (CefC_Dbg_Fine, "CCNINFO_SHA256_KEY_PRFX = %s\n"
								, hdl->ccninfo_sha256_key_prfx);
	cef_dbg_write (CefC_Dbg_Fine, "CCNINFO_REPLY_TIMEOUT = %d\n"
								, hdl->ccninfo_reply_timeout);

	if ( hdl->My_Node_Name != NULL ) {
		cef_dbg_write (CefC_Dbg_Fine, "NODE_NAME = %s\n", hdl->My_Node_Name );
	}

#endif // CefC_Debug
	return (1);
}

static int
cefnetd_trim_line_string (
	const char* p1, 							/* target string for trimming 			*/
	char* p2,									/* name string after trimming			*/
	char* p3									/* value string after trimming			*/
) {
	char ws[1024];
	char* wp = ws;
	char* rp = p2;
	int equal_f = -1;

	while (*p1) {
		if ((*p1 == 0x0D) || (*p1 == 0x0A)) {
			break;
		}

		if ((*p1 == 0x20) || (*p1 == 0x09)) {
			p1++;
			continue;
		} else {
			*wp = *p1;
		}

		p1++;
		wp++;
	}
	*wp = 0x00;
	wp = ws;

	while (*wp) {
		if (*wp == 0x3d /* '=' */) {
			if (equal_f > 0) {
				return (-1);
			}
			equal_f = 1;
			*rp = 0x00;
			rp = p3;
		} else {
			*rp = *wp;
			rp++;
		}
		wp++;
	}
	*rp = 0x00;

	return (equal_f);
}
/*--------------------------------------------------------------------------------------
	Creates the Command Filter(s)
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_command_filter_init (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
) {

	unsigned char buff[256];
	int len;
	uint8_t hash;

	/* Sets filter for the Link Request Command 		*/
	len = cef_frame_link_req_cmd_get (buff);
	if ((len > 0) && (len < CefC_Cmd_Len_Max)) {
		hash = hdl->cefrt_seed;
		CEFRTHASH8(buff, hash, len);
		hdl->cmd_filter[hash] = CefC_Cmd_Link_Req;
		hdl->cmd_len[hash] = len;
		memcpy (&hdl->cmd[hash][0], buff, len);
	}

	/* Sets filter for the Link Response Command 		*/
	len = cef_frame_link_res_cmd_get (buff);
	if ((len > 0) && (len < CefC_Cmd_Len_Max)) {
		hash = hdl->cefrt_seed;
		CEFRTHASH8(buff, hash, len);
		hdl->cmd_filter[hash] = CefC_Cmd_Link_Res;
		hdl->cmd_len[hash] = len;
		memcpy (&hdl->cmd[hash][0], buff, len);
	}

	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles a command message
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_command_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
) {

	uint8_t hash = hdl->cefrt_seed;

	CEFRTHASH8(pm->name, hash, pm->name_len);

	if ((hdl->cmd_filter[hash] < CefC_Cmd_Link_Req) ||
		(hdl->cmd_filter[hash] > CefC_Cmd_Link_Res)) {
		return (0);
	}

	if (hdl->cmd_len[hash] != pm->name_len) {
		return (0);
	} else {
		if (memcmp (&hdl->cmd[hash][0], pm->name, pm->name_len)) {
			return (0);
		}
	}

	/* Calls the function corresponding to the type of the command 	*/
	(*cefnetd_command_process[hdl->cmd_filter[hash]])(hdl, faceid, peer_faceid, pm);

	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the Invalid command
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_invalid_command_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
) {
	/* Ignores the invalid command */
	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the Link Request command
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_link_req_command_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
) {
	int msg_len;
	unsigned char buff[CefC_Max_Length];

	/* Creates and sends a Link Response message */
	memset (buff, 0, CefC_Max_Length);
	msg_len = cef_frame_object_link_msg_create (buff);

	if (msg_len > 0) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Finer,
			"Send a Object Link message to Face#%d\n", peer_faceid);
#endif // CefC_Debug
		cef_face_frame_send_forced (peer_faceid, buff, (size_t) msg_len);
	}

	return (1);
}
/*--------------------------------------------------------------------------------------
	Handles the Link Response command
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_link_res_command_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	CefT_CcnMsg_MsgBdy* pm					/* Structure to set parsed CEFORE message	*/
) {

#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Receive a Object Link message from Face#%d\n", peer_faceid);
#endif // CefC_Debug

	return (1);
}
/*--------------------------------------------------------------------------------------
	Creates listening socket(s)
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_faces_init (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
) {
	int res;
	int res_v4 = -1;
	int res_v6 = -1;

	/* Initialize the face module 		*/
	res = cef_face_init (hdl->node_type);
	if (res < 0) {
		cef_log_write (CefC_Log_Error, "Failed to init Face package.\n");
		return (-1);
	}

	/* Creates listening face 			*/
	res = cef_face_udp_listen_face_create (hdl->port_num, &res_v4, &res_v6);
	if (res < 0) {
		cef_log_write (CefC_Log_Error, "Failed to create the UDP listen socket.\n");
		return (-1);
	}
	/* Prepares file descriptors to listen 		*/
	if (res_v4 > 0) {
		hdl->inudpfaces[hdl->inudpfdc] = (uint16_t) res_v4;
		hdl->inudpfds[hdl->inudpfdc].fd = cef_face_get_fd_from_faceid ((uint16_t) res_v4);
		hdl->inudpfds[hdl->inudpfdc].events = POLLIN | POLLERR;
		hdl->inudpfdc++;
	}
	if (res_v6 > 0) {
		hdl->inudpfaces[hdl->inudpfdc] = (uint16_t) res_v6;
		hdl->inudpfds[hdl->inudpfdc].fd = cef_face_get_fd_from_faceid ((uint16_t) res_v6);
		hdl->inudpfds[hdl->inudpfdc].events = POLLIN | POLLERR;
		hdl->inudpfdc++;
	}

	res = cef_face_tcp_listen_face_create (hdl->port_num, &res_v4, &res_v6);
	if (res < 0) {
		cef_log_write (CefC_Log_Error, "Failed to create the TCP listen socket.\n");
		return (-1);
	}

	/* Creates the local face 			*/
	res = cef_face_local_face_create (hdl->sk_type);
	if (res < 0) {
		cef_log_write (CefC_Log_Error, "Failed to create the local listen socket.\n");
		return (-1);
	}

	/* Creates the local face for cefbabeld 	*/
	hdl->babel_sock = -1;
	hdl->babel_face = -1;

	if (hdl->babel_use_f) {
		res = cef_face_babel_face_create (hdl->sk_type);
		if (res < 0) {
			cef_log_write (CefC_Log_Error,
				"Failed to create the local listen socket for cefbabeld.\n");
			return (-1);
		}
		cef_log_write (CefC_Log_Info, "Initialization for cefbabeld ... OK\n");
	} else {
		cef_log_write (CefC_Log_Info, "Not use cefbabeld\n");
	}

	return (1);
}
/*--------------------------------------------------------------------------------------
	Closes faces
----------------------------------------------------------------------------------------*/
static int
cefnetd_faces_destroy (
	CefT_Netd_Handle* hdl						/* cefnetd handle						*/
) {
	/* Closes all faces 	*/
	cef_face_all_face_close ();
	return (1);
}
/*--------------------------------------------------------------------------------------
	Clean PIT entries
----------------------------------------------------------------------------------------*/
static void
cefnetd_pit_cleanup (
	CefT_Netd_Handle* hdl, 						/* cefnetd handle						*/
	uint64_t nowt								/* current time (usec) 					*/
) {
	CefT_Pit_Entry* pe;
	CefT_Down_Faces* face;
	int idx;
	CefT_Rx_Elem_Sig_DelPit sig_delpit;
	int clean_num = 0;
	int rec_index = 0;
	int end_index;
	int end_flag = 0;
	int pit_num = cef_lhash_tbl_item_num_get(hdl->pit);
	uint32_t elem_num, elem_index, eidx;

	if (nowt > hdl->pit_clean_t) {
		end_index = hdl->pit_clean_i;

#ifdef	__PIT_CLEAN__
	if (pit_num > 0) {
		fprintf(stderr, "[%s] pit_num=%d\n", __func__, pit_num);
	}
#endif

		while (clean_num < pit_num) {
			pe = (CefT_Pit_Entry*) cef_lhash_tbl_elem_get (hdl->pit, &(hdl->pit_clean_i), &elem_num);

			if (hdl->pit_clean_i == end_index) {
				end_flag++;
				if (end_flag > 1) {
					break;
				}
			}
			rec_index = hdl->pit_clean_i;
			hdl->pit_clean_i++;

			if (pe == NULL) {
				break;
			}
#ifdef	__PIT_CLEAN__
	fprintf( stderr, "[%s] elem_get rec_index=%d, elm_num=%d\n", __func__, rec_index, elem_num);
#endif

			for (eidx = 0, elem_index = 0; elem_index < elem_num; elem_index++) {
				pe = (CefT_Pit_Entry*)
						cef_lhash_tbl_item_get_from_index (hdl->pit, rec_index, eidx);

				if (pe != NULL) {
					clean_num++;
#ifdef	__PIT_CLEAN__
	fprintf( stderr, "[%s] cef_pit_clean(), elem_index=%d, eidx=%d\n", __func__, elem_index, eidx );
#endif
					cef_pit_clean (hdl->pit, pe);

					/* Indicates that a PIT entry was deleted to Transport  	*/
					if (hdl->plugin_hdl.tp[pe->tp_variant].pit) {

						/* Records PIT entries ware deleted  	*/
						face = &(pe->clean_dnfaces);
						idx = 0;

						while (face->next) {
							face = face->next;
							sig_delpit.faceids[idx] = face->faceid;
							idx++;
						}

						if (idx > 0) {

							sig_delpit.faceid_num = idx;
							sig_delpit.hashv = pe->hashv;

							(*(hdl->plugin_hdl.tp)[pe->tp_variant].pit)(
								&(hdl->plugin_hdl.tp[pe->tp_variant]), &sig_delpit);
						}
					}

					if (pe->dnfacenum < 1) {
#ifdef	__PIT_CLEAN__
	fprintf( stderr, "[%s] cef_pit_entry_free()\n", __func__ );
#endif

						cef_pit_entry_free (hdl->pit, pe);

					} else {
						eidx++;
					}
				}
			}
		}
		hdl->pit_clean_t = nowt + 1000000;
	}

	return;
}
/*--------------------------------------------------------------------------------------
	Clean FIB entries
----------------------------------------------------------------------------------------*/
static void
cefnetd_fib_cleanup (
	CefT_Netd_Handle* hdl, 						/* cefnetd handle						*/
	uint64_t nowt								/* current time (usec) 					*/
) {

	CefT_Fib_Entry* fe;
	int clean_num = 0;
	int end_index;
	int end_flag = 0;

	if (nowt > hdl->fib_clean_t) {

		end_index = hdl->fib_clean_i;

		while (clean_num < 16) {
			fe = (CefT_Fib_Entry*) cef_hash_tbl_elem_get (hdl->fib, &(hdl->fib_clean_i));

			if (hdl->fib_clean_i == end_index) {
				end_flag++;
				if (end_flag > 1) {
					break;
				}
			}
			hdl->fib_clean_i++;

			if (fe) {
				clean_num++;

			} else {
				break;
			}
		}
		hdl->fib_clean_t = nowt + 1000000;
	}

	return;
}
/*--------------------------------------------------------------------------------------
	Obtains my NodeID (IP Address)
----------------------------------------------------------------------------------------*/
static void
cefnetd_node_id_get (
	CefT_Netd_Handle* hdl						/* cefnetd handle						*/
) {
	struct ifaddrs *ifa_list;
	struct ifaddrs *ifa;
	int n;
	int mtu;


	n = getifaddrs (&ifa_list);
	if (n != 0) {
		return;
	}

	hdl->nodeid4_num 	= 0;
	hdl->nodeid16_num 	= 0;
	hdl->lo_mtu 		= 65536;

	for(ifa = ifa_list ; ifa != NULL ; ifa=ifa->ifa_next) {

		if (ifa->ifa_addr == NULL) {
			continue;
		}

		if (ifa->ifa_addr->sa_family == AF_INET) {
			if (ifa->ifa_flags & IFF_LOOPBACK) {
				continue;
			}
			hdl->nodeid4_num++;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			if (ifa->ifa_flags & IFF_LOOPBACK) {
				continue;
			}
			hdl->nodeid16_num++;
		} else {
			/* NOP */;
		}
	}
	hdl->nodeid4 =
		(unsigned char**) calloc (hdl->nodeid4_num, sizeof (unsigned char*));
	hdl->nodeid4_mtu =
		(unsigned int**) calloc (hdl->nodeid4_num, sizeof (unsigned int*));
	hdl->nodeid4_c =
		(char**) calloc (hdl->nodeid4_num, sizeof (char*));

	for (n = 0 ; n < hdl->nodeid4_num ; n++) {
		hdl->nodeid4[n] = (unsigned char*) calloc (4, 1);
		hdl->nodeid4_mtu[n] = (unsigned int*) calloc (sizeof (unsigned int), 1);
		hdl->nodeid4_c[n] = (char*) calloc (NI_MAXHOST, 1);
	}

	hdl->nodeid16 =
		(unsigned char**) calloc (hdl->nodeid16_num, sizeof (unsigned char*));
	hdl->nodeid16_mtu =
		(unsigned int**) calloc (hdl->nodeid16_num, sizeof (unsigned int*));
	hdl->nodeid16_c =
		(char**) calloc (hdl->nodeid16_num, sizeof (char*));
	for (n = 0 ; n < hdl->nodeid16_num ; n++) {
		hdl->nodeid16[n] = (unsigned char*) calloc (16, 1);
		hdl->nodeid16_mtu[n] = (unsigned int*) calloc (sizeof (unsigned int), 1);
		hdl->nodeid16_c[n] = (char*) calloc (NI_MAXHOST, 1);
	}

	hdl->nodeid4_num 	= 0;
	hdl->nodeid16_num 	= 0;

	for (ifa = ifa_list ; ifa != NULL ; ifa=ifa->ifa_next) {

		if (ifa->ifa_addr == NULL) {
			continue;
		}
		{ /* Get MTU */
			int fd;
			struct ifreq ifr;

			fd = socket(AF_INET, SOCK_DGRAM, 0);
			if(fd == -1) {
				return;
			}
			strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ-1);
			if (ioctl(fd, SIOCGIFMTU, &ifr) != 0) {
				return;
			}
			close(fd);
			mtu = ifr.ifr_mtu;
		}

		if (ifa->ifa_addr->sa_family == AF_INET) {
			if (ifa->ifa_flags & IFF_LOOPBACK) {
				if (hdl->lo_mtu > mtu){
					hdl->lo_mtu = mtu;
				}
				continue;
			}
			memcpy (&hdl->nodeid4[hdl->nodeid4_num][0],
				&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, 4);
			*(hdl->nodeid4_mtu[hdl->nodeid4_num]) = mtu;
			inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, hdl->nodeid4_c[hdl->nodeid4_num], NI_MAXHOST);
			hdl->nodeid4_num++;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			if (ifa->ifa_flags & IFF_LOOPBACK) {
				if (hdl->lo_mtu > mtu){
					hdl->lo_mtu = mtu;
				}
				continue;
			}
			memcpy (&hdl->nodeid16[hdl->nodeid16_num][0],
				&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr, 16);
			*(hdl->nodeid16_mtu[hdl->nodeid16_num]) = mtu;
			inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr, hdl->nodeid16_c[hdl->nodeid16_num], NI_MAXHOST);
			hdl->nodeid16_num++;
		} else {
			/* NOP */;
		}
	}
	freeifaddrs (ifa_list);

	if (hdl->nodeid4_num > 0) {
		memcpy (hdl->top_nodeid, hdl->nodeid4[0], 4);
		hdl->top_nodeid_len = 4;
		hdl->top_nodeid_mtu = *(hdl->nodeid4_mtu[0]);
	} else if (hdl->nodeid16_num > 0) {
		memcpy (hdl->top_nodeid, hdl->nodeid16[0], 16);
		hdl->top_nodeid_len = 16;
		hdl->top_nodeid_mtu = *(hdl->nodeid16_mtu[0]);
	} else {
		hdl->top_nodeid[0] = 0x7F;
		hdl->top_nodeid[1] = 0x00;
		hdl->top_nodeid[2] = 0x00;
		hdl->top_nodeid[3] = 0x01;
		hdl->top_nodeid_len = 4;
		hdl->top_nodeid_mtu = hdl->lo_mtu;
	}


	return;
}

/*--------------------------------------------------------------------------------------
	Obtains my NodeID (IP Address)
----------------------------------------------------------------------------------------*/
static int
cefnetd_matched_node_id_get (
	CefT_Netd_Handle* hdl,
	unsigned char* peer_node_id,
	int peer_node_id_len,
	unsigned char* node_id,
	unsigned int* responder_mtu
) {
	int i, n;
	int find_f = 0;

	if (peer_node_id_len == 16) {
		for (n = 15 ; n > 0 ; n--) {
			for (i = 0 ; i < hdl->nodeid16_num ; i++) {
				if (memcmp (&hdl->nodeid16[i][0], peer_node_id, n) == 0) {
					find_f = 1;
					break;
				}
			}
			if (find_f) {
				memcpy (node_id, &hdl->nodeid16[i][0], 16);
				*responder_mtu = *(hdl->nodeid16_mtu[i]);
				return (16);
			}
		}
	} else if (peer_node_id_len == 4) {
		for (n = 4 ; n > 0 ; n--) {
			for (i = 0 ; i < hdl->nodeid4_num ; i++) {
				if (memcmp (&hdl->nodeid4[i][0], peer_node_id, n) == 0) {
					find_f = 1;
					break;
				}
			}
			if (find_f) {
				memcpy (node_id, &hdl->nodeid4[i][0], 4);
				*responder_mtu = *(hdl->nodeid4_mtu[i]);
				return (4);
			}
		}
	}
	node_id[0] = 0x7F;
	node_id[1] = 0x00;
	node_id[2] = 0x00;
	node_id[3] = 0x01;

	return (4);
}

#ifdef CefC_ContentStore
/*--------------------------------------------------------------------------------------
	cefnetd cached Object process
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_cefcache_object_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	unsigned char* msg 						/* received message to handle				*/
){
	struct fixed_hdr* chp;
	uint16_t pkt_len;
	uint16_t hdr_len;
	uint16_t payload_len;
	uint16_t header_len;
	CefT_CcnMsg_MsgBdy pm = { 0 };
	CefT_CcnMsg_OptHdr poh = { 0 };
	CefT_Pit_Entry* pe;

	int loop_max = 2;						/* For App(0), Trans(1)						*/
	int i, j;
	int res;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	CefT_Down_Faces* face;
	chp = (struct fixed_hdr*) msg;
	pkt_len = ntohs (chp->pkt_len);
	hdr_len = chp->hdr_len;
	payload_len = pkt_len - hdr_len;
	header_len 	= hdr_len;

#ifdef	__SYMBOLIC__
	fprintf( stderr, "[%s] IN Msg (%d bytes) \n", __func__, payload_len + header_len );
#endif

#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Finer,
		"Process the Content Object (%d bytes) from cefnetd cache\n", payload_len + header_len);
	cef_dbg_buff_write (CefC_Dbg_Finest, msg, payload_len + header_len);
#endif // CefC_Debug
	/*--------------------------------------------------------------------
		Parses the Object message
	----------------------------------------------------------------------*/
	if (payload_len + header_len > CefC_Max_Msg_Size) {
#ifdef CefC_Debug
		cef_dbg_write (CefC_Dbg_Finer, "Content Object is too large\n");
#endif // CefC_Debug
		return (-1);
	}
	res = cef_frame_message_parse (
					msg, payload_len, header_len, &poh, &pm, CefC_PT_OBJECT);
	if (res < 0) {
		return (-1);
	}
	/*--------------------------------------------------------------------
		Searches a PIT entry matching this Object
	----------------------------------------------------------------------*/

	for (j = 0; j < loop_max; j++) {
		pe = NULL;
		if (j == 0) {
			pe = cef_pit_entry_search_with_chunk (hdl->app_pit, &pm, &poh);	//0.8.3
			if (pe == NULL)
				continue;

		} else {
			pe = cef_pit_entry_search_with_chunk (hdl->pit, &pm, &poh);	//0.8.3
			if ( pe != NULL ){
				/*JK*///20210824 Cob without chunk number
				if ( pm.chunk_num_f == 0 ) {	//Cob without chunk number
					if ( pe->PitType == CefC_PIT_TYPE_Rgl ) {	//PIT_Type is Reg
						//NOP
					} else {	//PIT_Type not Reg(Sym)
						//Miss
						return (-1);
					}
				}
			}
		}

		if (pe) {
			//0.8.3
			if ( pe->COBHR_len > 0 ) {
				/* CobHash */
				SHA256_CTX		ctx;
				uint16_t		CobHash_index;
				uint16_t		CobHash_len;
				unsigned char 	hash[SHA256_DIGEST_LENGTH];

				CobHash_index = header_len;
				CobHash_len   = payload_len;
				SHA256_Init (&ctx);
				SHA256_Update (&ctx, &msg[CobHash_index], CobHash_len);
				SHA256_Final (hash, &ctx);
				if ( memcmp(pe->COBHR_selector, hash, 32) == 0 ) {
					/* OK */
#ifdef	__RESTRICT__
printf( "%s CobHash OK\n", __func__ );
#endif
				} else {
					/* NG */
#ifdef	__RESTRICT__
printf( "%s CobHash NG\n", __func__ );
#endif
					return (-1);
				}
			}
#ifdef	__RESTRICT__
printf( "%s pe->KIDR_len:%d\n", __func__, pe->KIDR_len );
#endif
			if ( pe->KIDR_len > 0 ) {
				/* KeyIdRest */
				int keyid_len;
				unsigned char keyid_buff[32];
				keyid_len = cefnetd_keyid_get( msg, pkt_len, keyid_buff );
#ifdef	__RESTRICT__
				{
					printf( "%s\n", __func__ );
					int dbg_x;
					fprintf (stderr, "pe->KIDR_selector [ ");
					for (dbg_x = 0 ; dbg_x < 32 ; dbg_x++) {
						fprintf (stderr, "%02x ", pe->KIDR_selector[dbg_x]);
					}
					fprintf (stderr, "]\n");

					fprintf (stderr, "keyid_buff [ ");
					for (dbg_x = 0 ; dbg_x < 32 ; dbg_x++) {
						fprintf (stderr, "%02x ", keyid_buff[dbg_x]);
					}
					fprintf (stderr, "]\n");
				}
#endif
				if ( (keyid_len == 32) && (memcmp(pe->KIDR_selector, keyid_buff, 32) == 0) ) {
					/* OK */
#ifdef	__RESTRICT__
printf( "%s KeyIdRest OK\n", __func__ );
#endif
				} else {
					/* NG */
#ifdef	__RESTRICT__
printf( "%s KeyIdRest NG\n", __func__ );
#endif
					return (-1);
				}
			}

			face = &(pe->dnfaces);
			while (face->next) {
				face = face->next;
				faceids[face_num] = face->faceid;
				face_num++;
			}
		}

		/*--------------------------------------------------------------------
			Forwards the Content Object
		----------------------------------------------------------------------*/
		if (face_num > 0) {
#ifdef	__SYMBOLIC__
			fprintf( stderr, "\t face_num:%d\n", face_num );
#endif
#ifdef CefC_Debug
			{
				int dbg_x;
				int len = 0;

				len = sprintf (cnd_dbg_msg, "Recorded PIT Faces:");

				for (dbg_x = 0 ; dbg_x < face_num ; dbg_x++) {
					len = len + sprintf (cnd_dbg_msg + len, " %d", faceids[dbg_x]);
				}
				cef_dbg_write (CefC_Dbg_Finer, "%s\n", cnd_dbg_msg);
			}
			cef_dbg_write (CefC_Dbg_Finer, "Forward the Content Object to cefnetd(s)\n");
#endif // CefC_Debug

			cefnetd_object_forward (hdl, faceids, face_num, msg,
				payload_len, header_len, &pm, &poh, pe);

			if (pe->stole_f) {
				cef_pit_entry_free (hdl->pit, pe);
			}

			if(j == 0) {
				return (1);
			}
		}
#ifdef	__SYMBOLIC__
			fprintf( stderr, "\t j:%d\n", j );
#endif

		if(j == 1) {
			for (i = 0; i < face_num; i++)
				faceids[i] = 0;
			face_num = 0;
			pe = NULL;
			/*JK*///20210824 Cob without chunk number
			if ( pm.chunk_num_f == 0 ) {	//Cob without chunk number
				return (1);
			}
			pe = cef_pit_entry_search_without_chunk (hdl->pit, &pm, &poh);
			if ( pe == NULL ) {
				/* NOP */
#ifdef	__SYMBOLIC__
			fprintf( stderr, "\t pe == NULL\n" );
#endif
			} else {
#ifdef	__SYMBOLIC__
			fprintf( stderr, "\t pe != NULL\n" );
#endif
				//Symbolic/Osyimbolic
				face = &(pe->dnfaces);

				while (face->next) {
					face = face->next;
					faceids[face_num] = face->faceid;
					face_num++;
				}
				if (face_num > 0) {
					if ( pe->PitType == CefC_PIT_TYPE_Sym ) {
#ifdef	__SYMBOLIC__
						fprintf( stderr, "\t pe->Last_chunk_num:%ld   hdl->SymbolicBack:%d\n", pe->Last_chunk_num, hdl->SymbolicBack );
#endif
						if ( (pe->Last_chunk_num - hdl->SymbolicBack) <= pm.chunk_num ) {
							cefnetd_object_forward (hdl, faceids, face_num, msg,
								payload_len, header_len, &pm, &poh, pe);
							if ( pe->Last_chunk_num < pm.chunk_num ) {
								pe->Last_chunk_num = pm.chunk_num;
#ifdef	__SYMBOLIC__
						fprintf( stderr, "\t pe->Last_chunk_num:%ld\n", pe->Last_chunk_num );
#endif
							}
						}
					}
				}
			}
		}

		for (i = 0; i < face_num; i++){
			faceids[i] = 0;
		}
		face_num = 0;
	}


	return (1);
}
#endif //CefC_ContentStore


/* NodeName Check */
static int									/* 0:OK -1:Error */
cefnetd_nodename_check (
	const char* in_name,					/* input NodeName							*/
	unsigned char* ot_name					/* buffer to set After Check NodeName		*/
) {
	unsigned char chk_name[CefC_Max_Length];
	int		in_len;
	int		i;
	char*	ot_p;
	char*	chk_p;


	in_len = strlen(in_name);
	memset( chk_name, 0x00, CefC_Max_Length );

	memcpy( chk_name, in_name, in_len );

	ot_p = (char*)ot_name;
	chk_p = (char*)chk_name;

	if ( strncmp( (char*)chk_name, "http://", 7 ) == 0 ) {
		chk_p += 7;
		strcpy( ot_p, chk_p );
		return( strlen(ot_p) );
	}

	for ( i = 0; i < in_len; i++ ) {
		if ( *chk_p == '/' ) {
			chk_p++;
		} else {
			break;
		}
	}

	strcpy( ot_p, chk_p );

	return( strlen(ot_p) );
}

static	int
cefnetd_ccninfo_loop_check(
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	CefT_Parsed_Ccninfo* pci
) {

	CefT_Request_RptBlk*	rpt_p;		/* Report block */
	int					rpt_idx;
	unsigned char		chk_node_name[1024];

#ifdef	DEB_CCNINFO
	fprintf( stderr, "%s Entry\n", "cefnetd_ccninfo_loop_check()" );
	fprintf( stderr, "\t pci->rpt_blk_num=%d \n", pci->rpt_blk_num );
#endif

	if ( pci->rpt_blk_num == 0 ) {
		return( 0 );
	}

	rpt_p = pci->rpt_blk;
	for ( rpt_idx = 0; rpt_idx < pci->rpt_blk_num; rpt_idx++ ) {
		memset( chk_node_name, 0x00, 1024 );
		memcpy( chk_node_name, &rpt_p->node_id[0], rpt_p->id_len);
		if ( memcmp( chk_node_name, hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len ) == 0 ) {
			/* Detect Loop */
#ifdef	DEB_CCNINFO
			fprintf( stderr, "\t Detect Loop pci->rpt_blk_num=%d \n", pci->rpt_blk_num );
#endif
			return( -1 );
		}
		rpt_p = rpt_p->next;
	}

	return( 0 );
}

static int
cefnetd_ccninfo_pit_create(
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	CefT_Parsed_Ccninfo* pci,
	unsigned char* ccninfo_pit,
	int		req_or_rep,
	int		skip_option
) {


	int	wp = 0;
	unsigned char	tmp_buff[2048];
	CefT_Request_RptBlk*	rpt_p;		/* Report block */
	int					rpt_idx;
	int					find_my_node = 0;
	int					top_node = 0;
	int					is_skip = 0;

	memset( tmp_buff, 0x00, 2048 );


#ifdef	DEB_CCNINFO
	fprintf( stderr, "%s Entry\n", "cefnetd_ccninfo_pit_create()" );
#endif
	rpt_p = pci->rpt_blk;

	if ( pci->rpt_blk_num == 0 ) {
#ifdef	DEB_CCNINFO
		fprintf( stderr, "\t pci->rpt_blk_num=%d SKIP_CONCAT\n", pci->rpt_blk_num );
#endif
		goto SKIP_CONCAT;
	}

	if ( pci->id_len == rpt_p->id_len ) {
		if ( memcmp(&pci->node_id[0], &rpt_p->node_id[0], pci->id_len) == 0 ) {
			/* Req eq Top. NoSkip */
		} else {
			is_skip = 1;
		}
	} else {
		is_skip = 1;
	}

	find_my_node = 0;
	for ( rpt_idx = 0; rpt_idx < pci->rpt_blk_num; rpt_idx++ ) {
		if ( memcmp( &rpt_p->node_id[0], hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len ) == 0 ) {
			find_my_node = 1;
			if ( rpt_idx == 0 ) {
				top_node = 1;
			}
		}
		rpt_p = rpt_p->next;
	}

	if ( (req_or_rep == CCNINFO_REP) && (find_my_node != 1) ) {
#ifdef	DEB_CCNINFO
		fprintf( stderr, "\t Reply NotFound MyNode SKIP_CONCAT\n" );
#endif
		goto SKIP_CONCAT;
	}

	if ( req_or_rep == CCNINFO_REP ) {
		if ( pci->ret_code == 0x00 ) {
			/* Normal */
		} else if ( pci->ret_code >= 0x80 ) {
			if ( is_skip == 0 ) {
				/* Normal */
			} else {
				if ( top_node == 1 ) {
					/* Normal */
				} else {
					if ( skip_option == 1 ) {
						/* Special */
#ifdef	DEB_CCNINFO
						fprintf( stderr, "\t 0x80 Special-1 SKIP_CONCAT\n" );
#endif
						goto SKIP_CONCAT;
					}
				}
			}
		}
	}

	rpt_p = pci->rpt_blk;

#ifdef	DEB_CCNINFO
	fprintf( stderr, "\t 000 pci->rpt_blk_num=%d\n", pci->rpt_blk_num );
#endif
	for ( rpt_idx = 0; rpt_idx < pci->rpt_blk_num; rpt_idx++ ) {
#ifdef	DEB_CCNINFO
		fprintf( stderr, "\t 000-1 rpt_idx=%d\n", rpt_idx );
#endif
		if ( req_or_rep == CCNINFO_REP ) {
			/* Reply */
			if ( memcmp( &rpt_p->node_id[0], hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len ) == 0 ) {
#ifdef	DEB_CCNINFO
				fprintf( stderr, "\t 000-2 Break\n" );
#endif
				break;
			}
		}

		memcpy( &tmp_buff[wp], &rpt_p->node_id[0], rpt_p->id_len );
		wp += rpt_p->id_len;
		rpt_p = rpt_p->next;
	}

SKIP_CONCAT:;

#ifdef	DEB_CCNINFO
	fprintf( stderr, "\t 001 wp=%d\n", wp );
#endif
	/* Content Name */
	memcpy( &tmp_buff[wp], pci->disc_name, pci->disc_name_len );
	wp += pci->disc_name_len;
#ifdef	DEB_CCNINFO
	fprintf( stderr,"\t 002 wp=%d\n", wp );
#endif
#ifdef	DEB_CCNINFO
	{
		int	jj;
		int	tmp_len;
		tmp_len = wp;
		fprintf( stderr, "cefnetd_ccninfo_pit_create() wp=%d\n", wp );
		for (jj = 0 ; jj < tmp_len ; jj++) {
			if ( jj != 0 ) {
				if ( jj%32 == 0 ) {
				fprintf( stderr, "\n" );
				}
			}
			fprintf( stderr, "%02x ", tmp_buff[jj] );
		}
		fprintf( stderr, "\n" );
	}
#endif

	/* hash */
	MD5 ( (unsigned char*)tmp_buff, wp, ccninfo_pit );
#ifdef	DEB_CCNINFO
	fprintf( stderr, "\t 003\n" );
#endif
	/* RequestID */
	memcpy( &ccninfo_pit[MD5_DIGEST_LENGTH], &pci->req_id, sizeof(uint16_t) );
#ifdef	DEB_CCNINFO
	fprintf( stderr, "\t 004\n" );
#endif

#ifdef	DEB_CCNINFO
	{
		int	ii;
		int	tmp_len;
		tmp_len = MD5_DIGEST_LENGTH + sizeof(uint16_t);
		fprintf( stderr, "cefnetd_ccninfo_pit_create() pit_len=%d\n", tmp_len );
		for (ii = 0 ; ii < tmp_len ; ii++) {
			if ( ii != 0 ) {
				if ( ii%32 == 0 ) {
				fprintf( stderr, "\n" );
				}
			}
			fprintf( stderr, "%02x ", ccninfo_pit[ii] );
		}
		fprintf( stderr, "\n" );
	}
#endif

	return( MD5_DIGEST_LENGTH + sizeof(uint16_t) );
}


#ifdef DEB_CCNINFO
/*--------------------------------------------------------------------------------------
	for debug
----------------------------------------------------------------------------------------*/
static void
cefnetd_dbg_cpi_print (
	CefT_Parsed_Ccninfo* pci
) {
	int aaa, bbb;
	CefT_Request_RptBlk* rpt_p;
	CefT_Reply_SubBlk* rep_p;

	fprintf(stderr, "----- cef_frame_ccninfo_parse -----\n");
	fprintf(stderr, "PacketType                : 0x%02x\n", pci->pkt_type);
	fprintf(stderr, "ReturnCode                : 0x%02x\n", pci->ret_code);
	if (pci->putverify_f) {
		fprintf(stderr, "  --- option header ---\n");
		fprintf(stderr, "  MsgType                 : 0x%02x\n", pci->putverify_msgtype);
		fprintf(stderr, "  start seq               : %d\n", pci->putverify_sseq);
		fprintf(stderr, "  end seq                 : %d\n", pci->putverify_eseq);
	}
	fprintf(stderr, "  --- Request Block ---\n");
	fprintf(stderr, "  Request ID              : %u\n", pci->req_id);
	fprintf(stderr, "  SkipHopCount            : %u\n", pci->skip_hop);
	fprintf(stderr, "  Flags                   : 0x%02x V(%c) F(%c), O(%c), C(%c)\n", pci->ccninfo_flag,
						(pci->ccninfo_flag & CefC_CtOp_ReqValidation) ? 'o': 'x',
						(pci->ccninfo_flag & CefC_CtOp_FullDisCover) ? 'o': 'x',
						(pci->ccninfo_flag & CefC_CtOp_Publisher)    ? 'o': 'x',
						(pci->ccninfo_flag & CefC_CtOp_Cache)        ? 'o': 'x');
	fprintf(stderr, "  Request Arrival Time    : %u\n", pci->req_arrival_time);
	fprintf(stderr, "  Node Identifier         : ");
	for (aaa=0; aaa<pci->id_len; aaa++)
		fprintf(stderr, "%02x ", pci->node_id[aaa]);
	fprintf(stderr, "\n");
	fprintf(stderr, "  --- Report Block ---(%d)\n", pci->rpt_blk_num);
	rpt_p = pci->rpt_blk;
	for (bbb=0; bbb<pci->rpt_blk_num; bbb++) {
		fprintf(stderr, "  [%d]\n", bbb);
		fprintf(stderr, "    Request Arrival Time  : %u\n", rpt_p->req_arrival_time);
		fprintf(stderr, "    Node Identifier       : ");
		for (aaa=0; aaa<rpt_p->id_len; aaa++)
			fprintf(stderr, "%02x ", rpt_p->node_id[aaa]);
		fprintf(stderr, "\n");
		rpt_p = rpt_p->next;
	}
	fprintf(stderr, "  --- Discovery ---\n");
	fprintf(stderr, "  Name                    : ");
	for (aaa=0; aaa<pci->disc_name_len; aaa++)
		fprintf(stderr, "%02x ", pci->disc_name[aaa]);
	fprintf(stderr, "(%d)\n", pci->disc_name_len);

	if ( pci->reply_node_len > 0 ) {
	fprintf(stderr, "  --- Reply Node ---\n");
	fprintf(stderr, "    Request Arrival Time  : %u\n", pci->reply_req_arrival_time);
	fprintf(stderr, "    Node Identifier       : ");
	for (aaa=0; aaa<pci->reply_node_len; aaa++)
		fprintf(stderr, "%02x ", pci->reply_reply_node[aaa]);
	fprintf(stderr, "\n");
	}

	fprintf(stderr, "  --- Reply Block ---(%d)\n", pci->rep_blk_num);
	rep_p = pci->rep_blk;
	for (bbb=0; bbb<pci->rep_blk_num; bbb++) {
		fprintf(stderr, "  [%d]\n", bbb);
		fprintf(stderr, "    Content Type          : %s\n",
			(rep_p->rep_type == CefC_T_DISC_CONTENT) ? "T_DISC_CONTENT" : "T_DISC_CONTENT_OWNER");
		fprintf(stderr, "    Object Size           : %u\n", rep_p->obj_size);
		fprintf(stderr, "    Object Count          : %u\n", rep_p->obj_cnt);
		fprintf(stderr, "    # Received Interest   : %u\n", rep_p->rcv_interest_cnt);
		fprintf(stderr, "    First Seqnum          : %u\n", rep_p->first_seq);
		fprintf(stderr, "    Last Seqnum           : %u\n", rep_p->last_seq);
		fprintf(stderr, "    Elapsed Cache Time    : %u\n", rep_p->cache_time);
		fprintf(stderr, "    Remain Cache Lifetime : %u\n", rep_p->lifetime);
		fprintf(stderr, "    Name                  : ");
		for (aaa=0; aaa<rep_p->rep_name_len; aaa++)
			fprintf(stderr, "%02x ", rep_p->rep_name[aaa]);
		fprintf(stderr, "(%d)\n", rep_p->rep_name_len);
		if (rep_p->rep_range_len) {
			fprintf(stderr, "    Cache Chunk           : %s\n", rep_p->rep_range);
		}
		rep_p = rep_p->next;
	}
}
#endif

//0.8.3
/*--------------------------------------------------------------------------------------
	Handles the received Interest message has T_SELECTIVE
----------------------------------------------------------------------------------------*/
static int									/* Returns a negative value if it fails 	*/
cefnetd_incoming_selective_interest_process (
	CefT_Netd_Handle* hdl,					/* cefnetd handle							*/
	int faceid, 							/* Face-ID where messages arrived at		*/
	int peer_faceid, 						/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char* msg, 					/* received message to handle				*/
	uint16_t payload_len, 					/* Payload Length of this message			*/
	uint16_t header_len,					/* Header Length of this message			*/
	CefT_CcnMsg_MsgBdy* pm, 				/* Parsed CEFORE message					*/
	CefT_CcnMsg_OptHdr* poh				/* Parsed Option Header						*/
) {
	CefT_Pit_Entry* pe = NULL;
	CefT_Fib_Entry* fe = NULL;
	uint16_t faceids[CefC_Fib_UpFace_Max];
	uint16_t face_num = 0;
	unsigned char trg_name[CefC_Max_Length];
	struct value32_tlv value32_fld;
	uint16_t org_name_len;
	uint16_t trg_name_len;
	unsigned char 	org_name[CefC_Max_Length];
	int pit_res;
	int pit_res_first;

#if defined(CSMFILE)
	int	interest_to_csm	= 0;
#endif

	uint64_t		first_chunk;
	uint64_t		l;
	uint64_t		select_cob_num;

#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] IN\n", __func__ );
#endif

	//First Chunk Number to CefC_Select_Cob_Num : RegPIT & CS_MODE=2 Interest to Csmgrd
	if ( pm->org.req_chunk == 0 ) {
		/* Error 4 5 12 13 */
		return (-1);
	}

	//20220311
	if ( pm->org.req_chunk > hdl->Selective_max_range ) {
		return(-1);
	}


	first_chunk = (uint64_t)pm->org.first_chunk;
	if ( pm->org.last_chunk_f > 0 ) {
		/* ptn A  */
		if ( pm->org.first_chunk > pm->org.last_chunk ) {
			/* Error 3 */
			return (-1);
		}
		if ( (pm->org.last_chunk - pm->org.first_chunk) < pm->org.req_chunk ) {
			/* Error 7 */
			return (-1);
		}
		if ( pm->org.req_chunk > 0 ) {
			/* 1 2 */
			select_cob_num = (uint64_t)pm->org.req_chunk;
			goto SELECT_OK;
		}
		/* 6 8 */
		select_cob_num = (uint64_t)pm->org.req_chunk;
	} else {
		/* ptn B */
		/* 9 10 11 */
		select_cob_num = (uint64_t)pm->org.req_chunk;
	}
SELECT_OK:;

	org_name_len = pm->name_len;
	memcpy (org_name, pm->name, pm->name_len);
	memcpy (trg_name, pm->name, pm->name_len);

#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-010\n", __func__ );
#endif
	/* InterestType */
	pm->InterestType = CefC_PIT_TYPE_Rgl;
	for ( l = 0; l < select_cob_num; l++ ) {
		/* Max check */
		if ( first_chunk == UINT32_MAX ) {
			/* Warning */
#ifdef	__SELECTIVE__
			fprintf( stderr, "[%s] return(-1)\n", __func__ );
#endif
			return(-1);
		}

		/* set Chunk Number */
		trg_name_len = org_name_len;
		value32_fld.type   = htons (CefC_T_CHUNK);
		value32_fld.length = htons (CefC_S_ChunkNum);
		value32_fld.value  = htonl ((uint32_t)first_chunk);
		memcpy (&trg_name[trg_name_len], &value32_fld, sizeof (struct value32_tlv));
		trg_name_len += sizeof (struct value32_tlv);
		pm->name_len = trg_name_len;
		memcpy (pm->name, trg_name, trg_name_len);
		pm->chunk_num_f 	= 1;
		pm->chunk_num 	= (uint32_t)first_chunk;

		/* Searches a PIT entry matching this Interest 	*/
		pe = cef_pit_entry_lookup (hdl->pit, pm, poh, NULL, 0);
		if (pe == NULL) {
#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-020\n", __func__ );
#endif
			return (-1);
		}

#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-011\n", __func__ );
#endif
		/* Updates the information of down face that this Interest arrived 	*/
		pit_res = cef_pit_entry_down_face_update (pe, peer_faceid, pm, poh, msg, hdl->IntrestRetrans);
#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-012\n", __func__ );
#endif
		if ( l == 0 ) {
			pit_res_first = pit_res;
		}

#if defined(CSMFILE)
		if ( interest_to_csm != 0 ) {
			goto SKIP_INTEREST;
		}
#endif

#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-013\n", __func__ );
#endif

		if ( hdl->Selective_fwd == CefC_Selet_FWD_ON ) {
			goto SKIP_INTEREST;
		}

#ifdef CefC_ContentStore
		/*--------------------------------------------------------------------
			Content Store
		----------------------------------------------------------------------*/
		/* Checks Content Store */
		if (hdl->cs_stat->cache_type != CefC_Default_Cache_Type) {
			/* Checks the temporary/local cache in cefnetd 		*/
#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-014\n", __func__ );
#endif
			unsigned char* cob = NULL;
			int cs_res = -1;
			cs_res = cef_csmgr_cache_lookup (hdl->cs_stat, peer_faceid, pm, poh, pe, &cob);
			if (cs_res < 0) {
#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-015\n", __func__ );
#endif
#ifdef	CefC_Conpub
				if (hdl->cs_stat->cache_type != CefC_Cache_Type_ExConpub){
#endif	//CefC_Conpub
					/* Cache does not exist in the temporary cache in cefnetd, 		*/
					/* so inquiries to the csmgr 									*/
					cef_csmgr_excache_lookup (hdl->cs_stat, peer_faceid, pm, poh, pe);
#if defined(CSMFILE)
					interest_to_csm	= 1;
#endif

#ifdef	CefC_Conpub
				}
#endif	//CefC_Conpub
			} else {
#ifdef CefC_Debug
				cef_dbg_write (CefC_Dbg_Finer,
					"Return the Content Object from the buffer/local cache\n");
#endif // CefC_Debug
#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-016\n", __func__ );
#endif
				cefnetd_cefcache_object_process (hdl, cob);
			}
		}
#endif

		/* Update first_chunk */
#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-018\n", __func__ );
#endif

SKIP_INTEREST:;
		first_chunk++;
	}
#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] CKP-090\n", __func__ );
#endif

	pm->name_len = org_name_len;
	memcpy (pm->name, org_name, pm->name_len);

	/* Reset InterestType to the original type */
	pm->InterestType = CefC_PIT_TYPE_Sel;
	/* Count of Received Interest */
	hdl->stat_recv_interest++;
	/* Count of Received Interest by type */
	hdl->stat_recv_interest_types[pm->InterestType]++;
	fe = cef_fib_entry_search (hdl->fib, pm->name, pm->name_len);
	if (fe) {
		/* Count of Received Interest at FIB */
		fe->rx_int++;
		/* Count of Received Interest by type at FIB */
		fe->rx_int_types[pm->InterestType]++;
		fe = NULL;
	}

	//hdl->Selective_fwd==CefC_Selet_FWD_ON : NextHop
	if ( pit_res_first != 0 ) {
		if ( hdl->Selective_fwd == CefC_Selet_FWD_ON ) {
			/* Searches a FIB entry matching this Interest 		*/
			fe = cef_fib_entry_search (hdl->fib, pm->name, pm->name_len);
			/* Obtains Face-ID(s) to forward the Interest */
			if (fe) {
				face_num = cef_fib_forward_faceid_select (fe, peer_faceid, faceids);
			}
			if (face_num > 0) {
				cefnetd_interest_forward (
					hdl, faceids, face_num, peer_faceid, msg,
					payload_len, header_len, pm, poh, pe, fe
				);
			}
		}
	}
#ifdef	__SELECTIVE__
	fprintf( stderr, "[%s] OUT\n", __func__ );
#endif

	return (1);
}

#ifndef __PIN_NOT_USE__
static int
cefnetd_plugin_load (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
) {
	int (*func)(CefT_Plugin_Bw_Stat*);
	char func_name[256] = {0};

	/* Open library */
	hdl->mod_lib = dlopen (CefnetdC_Plugin_Library_Name, RTLD_LAZY);
	if (hdl->mod_lib == NULL) {
		cef_log_write (CefC_Log_Error, "%s\n", dlerror ());
		return (-1);
	}

	/* Load plugin */
	sprintf (func_name, "cefnetd_%s_plugin_load", hdl->bw_stat_pin_name);
#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Fine, "Plugin name = %s.\n", func_name);
#endif // CefC_Debug
	func = dlsym (hdl->mod_lib, (const char*)func_name);
	if (func == NULL) {
		cef_log_write (CefC_Log_Error, "%s\n", dlerror ());
		/* close library */
		dlclose (hdl->mod_lib);
		return (-1);
	}

	/* Load functions */
	if ((func) (hdl->bw_stat_hdl) != 0) {
		cef_log_write (CefC_Log_Error, "Initialize function is not set.\n");
		dlclose (hdl->mod_lib);
		return (-1);
	}

	return( 0 );
}
#endif

static int
cefnetd_keyid_get (
	const unsigned char* msg,
	int msg_len,
	unsigned char *keyid_buff
) {
	struct fixed_hdr* 	fixed_hp;
	struct tlv_hdr* 	tlv_ptr;

	uint16_t 	index;
	uint16_t 	pkt_len;
	uint16_t 	hdr_len;
	uint16_t 	val_len;
	uint16_t 	type, alg_type;
	uint16_t 	alg_offset = 0;
	uint16_t 	t_keyid_offset = 0;
	uint16_t 	t_keyid_len = 0;
	uint16_t 	t_keyid_type = 0;
#ifdef	__RESTRICT__
		printf( "\n\n%s IN\n", __func__ );
#endif

	/* Obtains header length and packet length 		*/
	fixed_hp = (struct fixed_hdr*) msg;
	pkt_len  = ntohs (fixed_hp->pkt_len);
	if (pkt_len != msg_len) {
#ifdef	__RESTRICT__
		printf( "\t (pkt_len:%d != msg_len:%d)\n", pkt_len, msg_len );
#endif
		return (-1);
	}
	hdr_len = fixed_hp->hdr_len;

	/* Obtains CCN message size 		*/
	tlv_ptr = (struct tlv_hdr*) &msg[hdr_len];
	val_len = ntohs (tlv_ptr->length);
	if (hdr_len + CefC_S_TLF + val_len == pkt_len) {
#ifdef	__RESTRICT__
		printf( "\t (hdr_len:%d + CefC_S_TLF:%d + val_len:%d == pkt_len:%d)\n", hdr_len, CefC_S_TLF, val_len, pkt_len);
#endif
		return (0);
	}
	index = hdr_len + CefC_S_TLF + val_len;

	/* Checks Validation Algorithm TLVs 	*/
	alg_offset = index;
#ifdef	__RESTRICT__
		printf( "\t alg_offset:%d index:%d\n", alg_offset, index );
#endif
	tlv_ptr = (struct tlv_hdr*) &msg[alg_offset];
	type = ntohs (tlv_ptr->type);
	if (type != CefC_T_VALIDATION_ALG) {
#ifdef	__RESTRICT__
		printf( "\t (type != CefC_T_VALIDATION_ALG)\n" );
#endif
		return (-1);
	}

	val_len = ntohs (tlv_ptr->length);
#ifdef	__RESTRICT__
		printf( "\t val_len:%d\n", val_len );
#endif
	if (index + CefC_S_TLF + val_len >= pkt_len) {
#ifdef	__RESTRICT__
		printf( "\t (index + CefC_S_TLF + val_len >= pkt_len)\n" );
#endif
		return (-1);
	}
	index += CefC_S_TLF;
#ifdef	__RESTRICT__
		printf( "\t alg_offset:%d index:%d\n", alg_offset, index );
#endif

	/* Checks Algorithm Type 		*/
	tlv_ptr = (struct tlv_hdr*) &msg[index];
	alg_type = ntohs (tlv_ptr->type);

	if ( alg_type != CefC_T_RSA_SHA256 ) {
		return (-1);
	}

	/* T_KEYID */
	t_keyid_offset = index + 4;
	tlv_ptr = (struct tlv_hdr*) &msg[t_keyid_offset];
	t_keyid_type = ntohs (tlv_ptr->type);
	t_keyid_len  = ntohs (tlv_ptr->length);
#ifdef	__RESTRICT__
		printf( "\t t_keyid_offset:%d t_keyid_type:%d t_keyid_len:%d\n", t_keyid_offset, t_keyid_type,t_keyid_len );
#endif

	if ( t_keyid_type != CefC_T_KEYID ) {
		return (-1);
	}
	if ( t_keyid_len != 32 ) {
		return (-1);
	}
	t_keyid_offset += CefC_S_TLF;
	memcpy( keyid_buff, &msg[t_keyid_offset], 32 );

	return ( t_keyid_len );

}


void *
cefnetd_cefstatus_thread (
	void *p
) {
	CefT_Netd_Handle*	cefned_hdl;
	int 				read_fd = -1;
	struct pollfd 		poll_fds[1];
	CefT_Cefstatus_Msg	in_msg;
	int					in_msg_len;
	char				msg[CefC_Max_Length];
	int					resp_fd = -1;
	int					res;
	unsigned char*		rsp_msg = NULL;
	struct pollfd		send_fds[1];
	uint16_t			output_opt = 0x0000;
	uint16_t			numofpit = 0;

	cefned_hdl = (CefT_Netd_Handle*)p;

	read_fd = cefned_hdl->cefstatus_pipe_fd[1];

	pthread_t self_thread = pthread_self();
	pthread_detach(self_thread);

	memset(&poll_fds, 0, sizeof(poll_fds));
	poll_fds[0].fd = read_fd;
	poll_fds[0].events = POLLIN | POLLERR;

#if 0
	uint64_t	dev_cnt = 0;
#endif
	while (1){
#if 0
	dev_cnt++;
	if ( (dev_cnt % 100000) == 0 ) {
		printf( "[%s]   dev_cnt:"FMTU64" \n", __func__, dev_cnt );
	}
#endif

	    poll(poll_fds, 1, 1000);
	    if (poll_fds[0].revents & POLLIN) {
			if((in_msg_len = read(read_fd, &in_msg, sizeof(CefT_Cefstatus_Msg))) < 1){
				continue;
			}

			memcpy( msg, (unsigned char*)in_msg.msg, 128 );
			resp_fd = in_msg.resp_fd;

			if ( strncmp( msg, CefC_Ctrl_Status, CefC_Ctrl_Status_Len) == 0) {
				rsp_msg = calloc(1, CefC_Max_Length*10);
				output_opt = 0x0000;
				if (strncmp (msg, CefC_Ctrl_StatusStat, CefC_Ctrl_StatusStat_Len) == 0) {
					/* output option */
					memcpy (&output_opt, &msg[CefC_Ctrl_StatusStat_Len], sizeof (uint16_t));
					if ( output_opt & CefC_Ctrl_StatusOpt_Numofpit ) {
						memcpy( &numofpit, &msg[CefC_Ctrl_StatusStat_Len + sizeof (uint16_t)], sizeof (uint16_t) );
					}
				}
				res = cef_status_stats_output (cefned_hdl, &rsp_msg, output_opt, numofpit);

				if (res > 0) {
					send_fds[0].fd = resp_fd;
					send_fds[0].events = POLLOUT | POLLERR;
					if (poll (send_fds, 1, 0) > 0) {
						if (send_fds[0].revents & (POLLERR | POLLNVAL | POLLHUP)) {
							cef_log_write (CefC_Log_Warn,
								"Failed to send to Local peer (%d)\n", send_fds[0].fd);
						} else {
							{
								int	fblocks;
								int rem_size;
								int counter;
								int fcntlfl;
								fcntlfl = fcntl (resp_fd, F_GETFL, 0);
								fcntl (resp_fd, F_SETFL, fcntlfl & ~O_NONBLOCK);
								fblocks = res / 65535;
								rem_size = res % 65535;
								for (counter=0; counter<fblocks; counter++){
									send (resp_fd, &rsp_msg[counter*65535], 65535, 0);
								}
								if (rem_size != 0){
									send (resp_fd, &rsp_msg[fblocks*65535], rem_size, 0);
								}
								fcntl (resp_fd, F_SETFL, fcntlfl | O_NONBLOCK);
							}
						}
					}
				}
				if ( rsp_msg != NULL ) {
					free( rsp_msg );
					rsp_msg = NULL;
				}
			} else {
				/* Erroe */
			}

			if ( rsp_msg != NULL ) {
				free( rsp_msg );
				rsp_msg = NULL;
			}
			memset( msg, 0, CefC_Max_Length );
			resp_fd = -1;
		}

	}

	pthread_exit (NULL);
	return 0;

}

static int
cefnetd_fwd_plugin_load (
	CefT_Netd_Handle* hdl					/* cefnetd handle							*/
) {
	int (*func)(CefT_Plugin_Fwd_Strtgy*);
	char func_name[CefC_FwdStrPlg_Max_NameLen+32] = {0};

	/* Open library */
	hdl->fwd_strtgy_lib = dlopen (CefnetdC_FwdPlugin_Library_Name, RTLD_LAZY);
	if (hdl->fwd_strtgy_lib == NULL) {
		cef_log_write (CefC_Log_Error, "%s\n", dlerror ());
		return (-1);
	}

	/* Load plugin */
	sprintf (func_name, "cefnetd_fwd_%s_plugin_load", hdl->forwarding_strategy);
#ifdef CefC_Debug
	cef_dbg_write (CefC_Dbg_Fine, "Fwd Plugin name = %s.\n", func_name);
#endif // CefC_Debug
	func = dlsym (hdl->fwd_strtgy_lib, (const char*)func_name);
	if (func == NULL) {
		cef_log_write (CefC_Log_Error, "%s\n", dlerror ());
		/* close library */
		dlclose (hdl->fwd_strtgy_lib);
		return (-1);
	}

	/* Load functions */
	if ((func) (hdl->fwd_strtgy_hdl) != 0) {
		cef_log_write (CefC_Log_Error, "Forwarding Strategy Plugin function is not set.\n");
		dlclose (hdl->fwd_strtgy_lib);
		return (-1);
	}

	if (hdl->fwd_strtgy_hdl->init == NULL) {
		cef_log_write (CefC_Log_Error, "Initialize Forwarding Strategy Plugin function is not set.\n");
		dlclose (hdl->fwd_strtgy_lib);
		return (-1);
	}

	if (hdl->fwd_strtgy_hdl->destroy == NULL) {
		cef_log_write (CefC_Log_Error, "Destory Forwarding Strategy Plugin function is not set.\n");
		dlclose (hdl->fwd_strtgy_lib);
		return (-1);
	}

	if (hdl->fwd_strtgy_hdl->fwd_int == NULL) {
		cef_log_write (CefC_Log_Error, "Forwarding Strategy Plugin function(fwd_int) is not set.\n");
		dlclose (hdl->fwd_strtgy_lib);
		return (-1);
	}

	if (hdl->fwd_strtgy_hdl->fwd_cob == NULL) {
		cef_log_write (CefC_Log_Error, "Forwarding Strategy Plugin function(fwd_cob) is not set.\n");
		dlclose (hdl->fwd_strtgy_lib);
		return (-1);
	}

	if (hdl->fwd_strtgy_hdl->fwd_ccninforeq == NULL) {
		cef_log_write (CefC_Log_Error, "Forwarding Strategy Plugin function(fwd_ccninforeq) is not set.\n");
		dlclose (hdl->fwd_strtgy_lib);
		return (-1);
	}

	if (hdl->fwd_strtgy_hdl->fwd_cefpingreq == NULL) {
		cef_log_write (CefC_Log_Error, "Forwarding Strategy Plugin function(fwd_cefpingreq) is not set.\n");
		dlclose (hdl->fwd_strtgy_lib);
		return (-1);
	}

	return( 0 );
}

static int
cefnetd_continfo_process (
	CefT_Netd_Handle*		hdl,			/* cefnetd handle							*/
	int						faceid,			/* Face-ID where messages arrived at		*/
	int						peer_faceid,	/* Face-ID to reply to the origin of 		*/
											/* transmission of the message(s)			*/
	unsigned char*			msg,			/* received message to handle				*/
	uint16_t				payload_len,	/* Payload Length of this message			*/
	uint16_t				header_len,		/* Header Length of this message			*/
	CefT_CcnMsg_MsgBdy*	pm,				/* Structure to set parsed CEFORE message	*/
	CefT_Parsed_Ccninfo*	pci				/* Structure to set parsed Ccninfo message	*/
) {
#ifdef CefC_ContentStore
	int						res;
	char*					info_buff;
	char					range[CefC_Max_Length];
	int						range_len;
#endif // CefC_ContentStore

	if ((hdl->cs_stat == NULL) ||
		(hdl->cs_stat->cache_type != CefC_Cache_Type_Excache)) {
		cef_log_write (CefC_Log_Warn, "Incoming ccore message, but CS is not Excache.\n");
		return (-1);
	}

#ifdef CefC_ContentStore
	/*-------------------------------------------------------------
		Creates and send the Contents Information Request/Response
	---------------------------------------------------------------*/

	/* Obtains the Contents Information of the specified Prefix */
	memset (range, 0, CefC_Max_Length);
	range_len = sprintf (range, "%u:%u", pci->putverify_sseq, pci->putverify_eseq);
	res = cef_csmgr_content_info_get (
				hdl->cs_stat, (char*)pm->name, pm->name_len,
				range, range_len, &info_buff);

	/* Returns a Ccninfo Reply 			*/
	if (res >= 0) {
		uint16_t					index = 0;
		struct tlv_hdr				tlv_hdr;
		uint16_t					pld_len_new;
		struct ccninfo_req_block	req_blk;
		struct tlv_hdr*				tlv_hp;
		struct fixed_hdr*			fix_hdr;
		char*						last_comma;
		uint16_t					pkt_len;
		uint16_t					msg_len;
		uint16_t					range_len_ns;
		uint16_t					total_rep_sub_length;
		struct ccninfo_rep_block	rep_blk;

		/* Sets the header size		*/
		index = payload_len + header_len;

		/* Sets type and length of T_DISC_REPLY 		*/
/*
		+---------------+---------------+---------------+---------------+
		|      Type (=T_DISC_REPLY)     |             Length            |
		+---------------+---------------+---------------+---------------+
		|                     Request Arrival Time                      |
		+---------------+---------------+---------------+---------------+
		/                        Node Identifier                        /
		+---------------+---------------+---------------+---------------+
*/
		/* Reply block TLV */
		tlv_hdr.type = htons (CefC_T_DISC_REPLY);
		/* R_ArrTime + NodeID */
		pld_len_new = CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len;
		/* the total length of Reply sub-block(s) */
		total_rep_sub_length = CefC_S_TLF + sizeof (struct ccninfo_rep_block)	/* T_DISC_CONTENT */
								+ CefC_S_TLF + pm->name_len						/* T_NAME */
								+ CefC_S_Length + res;							/* RespRange */
		tlv_hdr.length = htons (pld_len_new + total_rep_sub_length);
		memcpy (&msg[index], &tlv_hdr, sizeof (struct tlv_hdr));
		index += CefC_S_TLF;
		{	/* set 32bit-NTP time */
	    	struct timespec tv;
			uint32_t ntp32b;
			clock_gettime(CLOCK_REALTIME, &tv);
			ntp32b = ((tv.tv_sec + 32384) << 16) + ((tv.tv_nsec << 7) / 1953125);
			req_blk.req_arrival_time 	= htonl (ntp32b);
			memcpy (&msg[index], &req_blk, sizeof (struct ccninfo_req_block));
			index += CefC_S_ReqArrivalTime;
		}
		/* Node Identifier (is Name TLV format) */
		memcpy (&msg[index], hdl->My_Node_Name_TLV, hdl->My_Node_Name_TLV_len);
		index += hdl->My_Node_Name_TLV_len;
		payload_len += CefC_S_TLF + CefC_S_ReqArrivalTime + hdl->My_Node_Name_TLV_len;

		/* Cache Information (just adjust index) */
/*
		+---------------+---------------+---------------+---------------+
		|     Type (=T_DISC_CONTENT)    |             Length            |
		+---------------+---------------+---------------+---------------+
		|                          Object Size                          |
		+---------------+---------------+---------------+---------------+
		|                         Object Count                          |
		+---------------+---------------+---------------+---------------+
		|                      # Received Interest                      |
		+---------------+---------------+---------------+---------------+
		|                         First Seqnum                          |
		+---------------+---------------+---------------+---------------+
		|                          Last Seqnum                          |
		+---------------+---------------+---------------+---------------+
		|                       Elapsed Cache Time                      |
		+---------------+---------------+---------------+---------------+
		|                      Remain Cache Lifetime                    |
		+---------------+---------------+---------------+---------------+
		|            T_NAME             |             Length            |
		+---------------+---------------+---------------+---------------+
		/                       Name Segment TLVs                       /
		+---------------+---------------+---------------+---------------+
		|       RespRange Length        |                               /
		+---------------+---------------+---------------+---------------+
		|                        RespRange(string)                      /
		+---------------+---------------+---------------+---------------+
*/
		tlv_hdr.type = htons (CefC_T_DISC_CONTENT);
		/* exclude Type and Length */
		total_rep_sub_length = sizeof (struct ccninfo_rep_block);
		tlv_hdr.length = htons (total_rep_sub_length);
		memcpy (&msg[index], &tlv_hdr, sizeof (struct tlv_hdr));
		index += CefC_S_TLF;
		memset (&rep_blk, 0x00, sizeof (struct ccninfo_rep_block));
		memcpy (&msg[index], &rep_blk, sizeof (struct ccninfo_rep_block));
		index += sizeof (struct ccninfo_rep_block);

		/* Name */
		tlv_hdr.type = htons (CefC_T_NAME);
		tlv_hdr.length = htons (pm->name_len);
		memcpy (&msg[index], &tlv_hdr, sizeof (struct tlv_hdr));
		memcpy (&msg[index + CefC_S_TLF], pm->name, pm->name_len);
		index += CefC_S_TLF + pm->name_len;

		if (res > 0) {
			/* Trim response data (end of data is a comma(',')) */
			info_buff[res] = '\0';
			last_comma = strrchr((const char*) info_buff, (int) ',');
			if (last_comma != NULL){
				*(last_comma+1) = '\0';
				res = strlen((const char*) info_buff);
			}

			/* RespRange */
			range_len_ns = htons (res);
			memcpy (&msg[index], &range_len_ns, CefC_S_Length);
			index += CefC_S_Length;
			memcpy (&msg[index], info_buff, res);
			free (info_buff);
			index += res;
		} else {
			range_len_ns = 0x00;
			memcpy (&msg[index], &range_len_ns, CefC_S_Length);
			index += CefC_S_Length;
		}

		/* Sets ICN message length 	*/
		pkt_len = index;
		msg_len = pkt_len - (header_len + CefC_S_TLF);

		tlv_hp = (struct tlv_hdr*) &msg[header_len];
		tlv_hp->length = htons (msg_len);

		/* Updates PacketLength and HeaderLength 		*/
		fix_hdr = (struct fixed_hdr*) msg;
		fix_hdr->type 	  = CefC_PT_REPLY;
		fix_hdr->reserve1 = CefC_CtRc_NO_ERROR;
		fix_hdr->pkt_len  = htons (pkt_len);

#ifdef DEB_CCNINFO
{	CefT_Parsed_Ccninfo *snd_pci;
		if ( (snd_pci = cef_frame_ccninfo_parse (msg)) != NULL ){
			cefnetd_dbg_cpi_print (snd_pci);
			cef_frame_ccninfo_parsed_free (snd_pci);
		}
}
#endif
		cef_face_frame_send_forced (peer_faceid, msg, pkt_len);

		return (1);
	}
#endif // CefC_ContentStore

	return (1);
};

static void
cefnetd_adv_route_process (
	CefT_Netd_Handle*		hdl,			/* cefnetd handle							*/
	CefT_CcnMsg_MsgBdy*	pm				/* Structure to set parsed CEFORE message	*/
) {

	if (hdl->babel_use_f) {
		cefnetd_xroute_change_report (
			hdl, pm->name, pm->name_len, 1);
	}

	return;
}

static int
cefnetd_route_msg_check (
	CefT_Netd_Handle* hdl,				/* cefnetd handle							*/
	unsigned char* msg,					/* received message to handle				*/
	int msg_size							/* size of received message(s)				*/
) {
	uint8_t op;
	uint8_t prot;
	uint8_t host_len;
	char host[64] = {0};
	int index = 0;
	char uri[CefC_Max_Length];
	uint16_t uri_len;
	int		i_4;
	int		i_16;

	struct	in_addr		input_v4_addr;
	struct	in_addr		node_v4_addr;
	struct	in6_addr	input_v6_addr;
	struct	in6_addr	node_v6_addr;
	uint16_t	in_port = 0;
	long		port_l;
	char*		end_p;
	int		rc;
	char	v6_host[64] = {0};
	char	v4_host[64] = {0};

	/* get operation */
	if ((msg_size - index) < sizeof (op)) {
		return (0);
	}
	op = msg[index];
	index += sizeof (op);

	/* get protocol */
	if ((msg_size - index) < sizeof (prot)) {
		/* message is too short */
		return (-1);
	}
	prot = msg[index];
	index += sizeof (prot);

	/* get uri */
	memcpy (&uri_len, &msg[index], sizeof (uint16_t));
	index += sizeof (uint16_t);
	if (uri_len <= 0) {
		/* message is too short */
		return (0);
	}
	if ((msg_size - index) < uri_len) {
		/* message is too short */
		return (0);
	}
	memcpy (uri, &msg[index], uri_len);
	uri[uri_len] = 0x00;
	index += uri_len;

	while (index < msg_size) {
		/* get host */
		if ((msg_size - index) < sizeof (host_len)) {
			/* message is too short */
			return (-1);
		}
		host_len = msg[index];
		index += sizeof (host_len);
		if ((msg_size - index) < host_len) {
			/* message is too short */
			return (-1);
		}
		memcpy (host, &msg[index], host_len);
		host[host_len] = 0x00;

		if 	( host[0] == '[' ) {	//IPv6
			{
			int	diff1;
			int	diff2;
			char*	p1;
			char*	p2;
			p1 = strchr( &host[1], ']' );
			if ( p1 == NULL ) {
				/* Error */
				cef_log_write (CefC_Log_Error, "Failed cefroute command.(']' NotFound:%s)\n", host);
			}
			diff1 = p1 - &host[1];
			p2 = strchr( &host[1], '%' );
			if ( p2 == NULL ) {
				diff2 = diff1;
			} else {
				diff2 = p2 - &host[1];
			}
			strncpy( v6_host, &host[1], diff2 );
			diff1+=2;
			if ( host[diff1] == ':' ) {
				diff1++;
				port_l = strtol( &host[diff1], &end_p, 10 );
				if ( &host[diff1] == end_p ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", host);
					return(-1);
				}
				if ( (port_l > INT_MAX) || (port_l < INT_MIN) ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", host);
					return(-1);
				}
				if ( (port_l < 0) || (port_l > 65535) ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", host);
					return(-1);
				}
				in_port = (uint16_t)port_l;
			} else {
				in_port = hdl->port_num;
			}
			
			}
			rc = inet_pton( AF_INET6, v6_host, &input_v6_addr );
			if ( rc != 1 ) {
				/* Error */
				cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid address:%s)\n", host);
				return(-1);
			}
			for ( i_16 = 0; i_16 < hdl->nodeid16_num; i_16++ ) {
				rc = inet_pton( AF_INET6, hdl->nodeid16_c[i_16], &node_v6_addr );
				if ( memcmp( &input_v6_addr, &node_v6_addr, 16 ) == 0 ) {
					if ( in_port == hdl->port_num ) {
						/* Error */
						cef_log_write (CefC_Log_Error, "Failed cefroute command.(Own address:%s)\n", host);
						return(-1);
					}
				}
			}
		} else {					//IPv4
			{
			int	diff1;
			char*	p1;
			p1 = strchr( &host[0], ':' );
			if ( p1 == NULL ) {
				in_port = hdl->port_num;
				strcpy( v4_host, host );
			} else {
				diff1 = p1 - &host[0];
				strncpy( v4_host, &host[0], diff1 );
				diff1++;
				port_l = strtol( &host[diff1], &end_p, 10 );
				if ( &host[diff1] == end_p ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", host);
					return(-1);
				}
				if ( (port_l > INT_MAX) || (port_l < INT_MIN) ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", host);
					return(-1);
				}
				if ( (port_l < 0) || (port_l > 65535) ) {
					/* Error */
					cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid port:%s)\n", host);
					return(-1);
				}
				in_port = (uint16_t)port_l;
			}
			}
			rc = inet_pton( AF_INET, v4_host, &input_v4_addr );
			if ( rc != 1 ) {
				/* Error */
				cef_log_write (CefC_Log_Error, "Failed cefroute command.(Invalid address:%s)\n", host);
				return(-1);
			}
			for ( i_4 = 0; i_4 < hdl->nodeid4_num; i_4++ ) {
				rc = inet_pton( AF_INET, hdl->nodeid4_c[i_4], &node_v4_addr );
				if ( memcmp( &input_v4_addr, &node_v4_addr, 4 ) == 0 ) {
					if ( in_port == hdl->port_num ) {
						/* Error */
						cef_log_write (CefC_Log_Error, "Failed cefroute command.(Own address:%s)\n", host);
						return(-1);
					}
				}
			}
		}

		index += host_len;
	}

	return (0);

}

