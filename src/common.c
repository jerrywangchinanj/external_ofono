/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <glib.h>

#include <ofono/types.h>
#include <ofono/gprs-context.h>
#include "common.h"
#include "ofono.h"
#include "util.h"

#define PAUSE ","
#define WAIT ";"

static const int five_bar_rsrp_thresholds[] = {-140, -125, -115, -110, -102};
static const int default_rsrp_thresholds[] = {-128, -118, -108, -98};
static const int default_rssi_thresholds[] = {-113, -107, -101, -95};

struct error_entry {
	int error;
	const char *str;
};

/*
 * 0-127 from 24.011 Annex E2
 * 127-255 23.040 Section 9.2.3.22
 * Rest are from 27.005 Section 3.2.5
 */
struct error_entry cms_errors[] = {
	{ 1,	"Unassigned number" },
	{ 8,	"Operator determined barring" },
	{ 10,	"Call barred" },
	{ 21,	"Short message transfer rejected" },
	{ 27,	"Destination out of service" },
	{ 28,	"Unidentified subscriber" },
	{ 29,	"Facility rejected" },
	{ 30,	"Unknown subscriber" },
	{ 38,	"Network out of order" },
	{ 41,	"Temporary failure" },
	{ 42,	"Congestion" },
	{ 47,	"Resources unavailable" },
	{ 50,	"Requested facility not subscribed" },
	{ 69,	"Requested facility not implemented" },
	{ 81,	"Invalid short message transfer reference value" },
	{ 95,	"Invalid message, unspecified" },
	{ 96,	"Invalid mandatory information" },
	{ 97,	"Message type non existent or not implemented" },
	{ 98,	"Message not compatible with short message protocol state" },
	{ 99,	"Information element non-existent or not implemented" },
	{ 111,	"Protocol error, unspecified" },
	{ 127,	"Interworking error, unspecified" },
	{ 128,	"Telematic interworking not supported" },
	{ 129,	"Short message type 0 not supported" },
	{ 130,	"Cannot replace short message" },
	{ 143,	"Unspecified TP-PID error" },
	{ 144,	"Data code scheme not supported" },
	{ 145,	"Message class not supported" },
	{ 159,	"Unspecified TP-DCS error" },
	{ 160,	"Command cannot be actioned" },
	{ 161,	"Command unsupported" },
	{ 175,	"Unspecified TP-Command error" },
	{ 176,	"TPDU not supported" },
	{ 192,	"SC busy" },
	{ 193,	"No SC subscription" },
	{ 194,	"SC System failure" },
	{ 195,	"Invalid SME address" },
	{ 196,	"Destination SME barred" },
	{ 197,	"SM Rejected-Duplicate SM" },
	{ 198,	"TP-VPF not supported" },
	{ 199,	"TP-VP not supported" },
	{ 208,	"(U)SIM SMS Storage full" },
	{ 209,	"No SMS Storage capability in SIM" },
	{ 210,	"Error in MS" },
	{ 211,	"Memory capacity exceeded" },
	{ 212,	"SIM application toolkit busy" },
	{ 213,	"SIM data download error" },
	{ 255,	"Unspecified error cause" },
	{ 300,	"ME Failure" },
	{ 301,	"SMS service of ME reserved" },
	{ 302,	"Operation not allowed" },
	{ 303,	"Operation not supported" },
	{ 304,	"Invalid PDU mode parameter" },
	{ 305,	"Invalid Text mode parameter" },
	{ 310,	"(U)SIM not inserted" },
	{ 311,	"(U)SIM PIN required" },
	{ 312,	"PH-(U)SIM PIN required" },
	{ 313,	"(U)SIM failure" },
	{ 314,	"(U)SIM busy" },
	{ 315,	"(U)SIM wrong" },
	{ 316,	"(U)SIM PUK required" },
	{ 317,	"(U)SIM PIN2 required" },
	{ 318,	"(U)SIM PUK2 required" },
	{ 320,	"Memory failure" },
	{ 321,	"Invalid memory index" },
	{ 322,	"Memory full" },
	{ 330,	"SMSC address unknown" },
	{ 331,	"No network service" },
	{ 332,	"Network timeout" },
	{ 340,	"No +CNMA expected" },
	{ 500,	"Unknown error" },
};

/* 27.007, Section 9 */
struct error_entry cme_errors[] = {
	{ 0,	"Phone failure" },
	{ 1,	"No connection to phone" },
	{ 2,	"Phone adaptor link reserved" },
	{ 3,	"Operation not allowed" },
	{ 4,	"Operation not supported" },
	{ 5,	"PH_SIM PIN required" },
	{ 6,	"PH_FSIM PIN required" },
	{ 7,	"PH_FSIM PUK required" },
	{ 10,	"SIM not inserted" },
	{ 11,	"SIM PIN required" },
	{ 12,	"SIM PUK required" },
	{ 13,	"SIM failure" },
	{ 14,	"SIM busy" },
	{ 15,	"SIM wrong" },
	{ 16,	"Incorrect password" },
	{ 17,	"SIM PIN2 required" },
	{ 18,	"SIM PUK2 required" },
	{ 20,	"Memory full" },
	{ 21,	"Invalid index" },
	{ 22,	"Not found" },
	{ 23,	"Memory failure" },
	{ 24,	"Text string too long" },
	{ 25,	"Invalid characters in text string" },
	{ 26,	"Dial string too long" },
	{ 27,	"Invalid characters in dial string" },
	{ 30,	"No network service" },
	{ 31,	"Network timeout" },
	{ 32,	"Network not allowed, emergency calls only" },
	{ 40,	"Network personalization PIN required" },
	{ 41,	"Network personalization PUK required" },
	{ 42,	"Network subset personalization PIN required" },
	{ 43,	"Network subset personalization PUK required" },
	{ 44,	"Service provider personalization PIN required" },
	{ 45,	"Service provider personalization PUK required" },
	{ 46,	"Corporate personalization PIN required" },
	{ 47,	"Corporate personalization PUK required" },
	{ 48,	"PH-SIM PUK required" },
	{ 50,	"Incorrect parameters" },
	{ 100,	"Unknown error" },
	{ 103,	"Illegal MS" },
	{ 106,	"Illegal ME" },
	{ 107,	"GPRS services not allowed" },
	{ 111,	"PLMN not allowed" },
	{ 112,	"Location area not allowed" },
	{ 113,	"Roaming not allowed in this location area" },
	{ 126,	"Operation temporary not allowed" },
	{ 132,	"Service operation not supported" },
	{ 133,	"Requested service option not subscribed" },
	{ 134,	"Service option temporary out of order" },
	{ 148,	"Unspecified GPRS error" },
	{ 149,	"PDP authentication failure" },
	{ 150,	"Invalid mobile class" },
	{ 256,	"Operation temporarily not allowed" },
	{ 257,	"Call barred" },
	{ 258,	"Phone is busy" },
	{ 259,	"User abort" },
	{ 260,	"Invalid dial string" },
	{ 261,	"SS not executed" },
	{ 262,	"SIM Blocked" },
	{ 263,	"Invalid block" },
	{ 772,	"SIM powered down" },
};

/* 24.008 Annex H */
struct error_entry ceer_errors[] = {
	{ 1,	"Unassigned number" },
	{ 3,	"No route to destination" },
	{ 6,	"Channel unacceptable" },
	{ 8,	"Operator determined barring" },
	{ 16,	"Normal call clearing" },
	{ 17,	"User busy" },
	{ 18,	"No user responding" },
	{ 19,	"User alerting, no answer" },
	{ 21,	"Call rejected" },
	{ 22,	"Number changed" },
	{ 25,	"Pre-emption" },
	{ 26,	"Non-selected user clearing" },
	{ 27,	"Destination out of order" },
	{ 28,	"Invalid number format (incomplete number)" },
	{ 29,	"Facility rejected" },
	{ 30,	"Response to STATUS ENQUIRY" },
	{ 31,	"Normal, unspecified" },
	{ 34,	"No circuit/channel available" },
	{ 38,	"Network out of order" },
	{ 41,	"Temporary failure" },
	{ 42,	"Switching equipment congestion" },
	{ 43,	"Access information discarded" },
	{ 44,	"Requested circuit/channel not available" },
	{ 47,	"Resource unavailable (unspecified)" },
	{ 49,	"Quality of service unavailable" },
	{ 50,	"Requested facility not subscribed" },
	{ 55,	"Incoming calls barred within the CUG" },
	{ 57,	"Bearer capability not authorized" },
	{ 58,	"Bearer capability not presently available" },
	{ 63,	"Service or option not available, unspecified" },
	{ 65,	"Bearer service not implemented" },
	{ 68,	"ACM equal to or greater than ACMmax" },
	{ 69,	"Requested facility not implemented" },
	{ 70,	"Only restricted digital information bearer capability is available" },
	{ 79,	"Service or option not implemented, unspecified" },
	{ 81,	"Invalid transaction identifier value" },
	{ 87,	"User not member of CUG" },
	{ 88,	"Incompatible destination" },
	{ 91,	"Invalid transit network selection" },
	{ 95,	"Semantically incorrect message" },
	{ 96,	"Invalid mandatory information"},
	{ 97,	"Message type non-existent or not implemented" },
	{ 98,	"Message type not compatible with protocol state" },
	{ 99,	"Information element non-existent or not implemented" },
	{ 100,	"Conditional IE error" },
	{ 101,	"Message not compatible with protocol state" },
	{ 102,	"Recovery on timer expiry" },
	{ 111,	"Protocol error, unspecified" },
	{ 127,	"Interworking, unspecified" },
};

const char *abnormal_event[] = { "inside_modem",
				 "ef_file",
				 "profile",
				 "rlf",
				 "rach_access",
				 "oos",
				 "nas_timeout",
				 "sip_timeout",
				 "rrc_timeout",
				 "ecc_call_fail",
				 "rtp_rtcp",
				 "paging_decode",
				 "call_quality",
				 "pdcp",
				 "nas_reject",
				 "sip_reject",
				 "rrc_reject",
				 "ping_pong",
				 "call_control",
				 "xcap_fail",
				 "data_flow_interruption",
				 "sip_call_end_cause" };

const char *normal_event[] = { "limited_service_camp",
			       "redirect",
			       "handover",
			       "reselect",
			       "csfb",
			       "srvcc",
			       "ue_cap_info",
			       "camp_cell_info",
			       "sim_info" };

const char *reest_cause_str[] = { "RECFG_FAILURE",    "HO_FAILURE",
				  "T310 TIMEOUT",     "RACH_PROBLEM",
				  "MAX_RETRX RLC",    "IP_CHECK_FAILURE",
				  "SIB_READ_FAILURE", "SMC_FAILURE",
				  "CFG_L2_FAILURE",   "OTHER_FAILURE" };

const char *rach_fail_reason_str[] = { "RA_FAIL_CAUSE_NOMSG2",
				       "RA_FAIL_CAUSE_NOMSG4",
				       "RA_FAIL_CAUSE_NORARESOURCE" };

const char *oos_type_str[] = {
	"OOS_TYPE_S_CRIT_FAIL",	  "OOS_TYPE_RESYNC_FAIL",
	"OOS_TYPE_RESEL_FAIL",	  "OOS_TYPE_L1_ABN_IND",
	"OOS_TYPE_MORMAL_TO_OOS", "OOS_TYPE_OOS_DIRECTLY"
};

const char *nas_timer_id_1_str[] = { "EMM_T3402", "EMM_T3410", "EMM_T3411",
				     "EMM_T3412", "EMM_T3417", "EMM_T3421",
				     "EMM_T3430", "EMM_T3440" };
const char *nas_timer_id_2_str[] = { "ESM_T3480", "ESM_T3481", "ESM_T3482",
				     "ESM_T3492" };

const char *sip_srv_type_str[] = { "SRV_REGISTATION", "SRV_CALL",
				   "SRV_EMG_CALL",    "SRV_SMS",
				   "SRV_MPTY",	      "SRV_USSI" };

const char *sip_method_str[] = { "SIP_REGISTER",  "SIP_SUBSCRIBE", "SIP_INVITE",
				 "SIP_RE_INVITE", "SIP_PRACK",	   "SIP_UPDATE",
				 "SIP_MESSAGE",	  "SIP_REFER",	   "SIP_INFO" };

const char *rrc_timer_id_str[] = { "T300_EST_FAIL", "T301_REEST_FAIL",
				   "T304_HO_FAIL", "T310_RADIO_LINK_FAIL",
				   "T311_REEST_CELL_SELECT_FAIL" };

const char *ecall_fail_reason_str[] = { "other", "Lost covery",
					"Emergency Bearer not support by NW",
					"Emergency Bearer Establish failure" };

const char *rtp_rtcp_error_type_str[] = { "DL_RTP_TIMEOUT", "DL_RTCP_TIMEOUT",
					  "MV_UDP_SOCKET_ERROR" };

const char *nas_procedure_type1_str[] = { "ATTACH_REJ", "TAU_REJ", "SR_REJ",
					  "IDENTITY",	"SMC_REJ", "AUTH_REJ",
					  "MT_DETACH" };
const char *nas_procedure_type2_str[] = { "ESM_PDN_CONN_REJECT",
					  "ESM_BEARER_MT_DEACT" };

const char *xcap_mode_str[] = { "MODE_DISABLE", "MODE_ENABLE", "MODE_QUERY",
				"MODE_REGISTRATION", "MODE_ERASURE" };

const char *xcap_reason_str[] = {
	"CDIV_ALL",    "CDIV_CONDS",	  "CDIV_CFU",
	"CDIV_CFB",    "CDIV_CFNR",	  "CDIV_CFNR_TMR",
	"CDIV_CFNRC",  "CDIV_CFNI",	  "CB_ICB_ALL",
	"CB_ICB_BAIC", "CB_ICB_BICROAM",  "CB_ICB_ACR",
	"CB_OCB_AL",   "CB_OCB_BAOC",	  "CB_OCB_BOCROAM",
	"CB_OCB_BOIC", "CB_OCB_BOICEXHC", "CW",
	"OIP_CLIP",    "OIR_CLIR",	  "TIP_COLP",
	"TIR_COLR"
};

const char *xcap_error_str[] = { "NET_ERROR",	"HTTP_ERROR",	 "HTTP_TIMEOUT",
				 "GBA_ERROR",	"NO_DNS_RESULT", "DNS_TIMEOUT",
				 "NO_FUNCTION", "OTHER" };

const char *sip_call_end_reason_str[] = {
	"RTP_RTCP_TIMEOUT", "MEDIA_BEARER_LOSS",  "SIP_TIMEOUT_NO_ACK",
	"SIP_RESP_TIMEOUT", "CALL_SETUP_TIMEOUT", "REDIRECTION_FAILURE"
};

const char *limited_srv_cause_str[] = { "Reseved", "No suitable cell",
					"No SIM Insert", "No Cell" };

const char *abnormal_event_type_to_string(int type)
{
	if (type >= 1 &&
	    type <= sizeof(abnormal_event) / sizeof(abnormal_event[0])) {
		return abnormal_event[type - 1];
	}

	if (type >= 200 &&
	    type < 200 + sizeof(normal_event) / sizeof(normal_event[0])) {
		return normal_event[type - 200];
	}

	return "unexepected";
}

const char *reest_cause_to_string(unsigned int reest_cause)
{
	if (reest_cause >=
	    sizeof(reest_cause_str) / sizeof(reest_cause_str[0])) {
		ofono_debug("abnormal event:%d", reest_cause);
		return "unknow";
	}
	return reest_cause_str[reest_cause];
}

const char *rach_fail_reason_to_string(unsigned int rach_fail_reason)
{
	if (rach_fail_reason >=
	    sizeof(rach_fail_reason_str) / sizeof(rach_fail_reason_str[0])) {
		ofono_debug("abnormal event:%d", rach_fail_reason);
		return "unknow";
	}
	return rach_fail_reason_str[rach_fail_reason];
}

const char *oos_type_to_string(unsigned int oos_type)
{
	if (oos_type >= sizeof(oos_type_str) / sizeof(oos_type_str[0])) {
		ofono_debug("abnormal event:%d", oos_type);
		return "unknow";
	}
	return oos_type_str[oos_type];
}

const char *nas_timer_id_to_string(unsigned int timer_id)
{
	if (timer_id <
	    sizeof(nas_timer_id_1_str) / sizeof(nas_timer_id_1_str[0])) {
		return nas_timer_id_1_str[timer_id];
	}

	if (timer_id >= 100 &&
	    timer_id < 100 + sizeof(nas_timer_id_2_str) /
				       sizeof(nas_timer_id_2_str[0])) {
		return nas_timer_id_2_str[timer_id - 100];
	}

	ofono_debug("abnormal event:%d", timer_id);
	return "unknow";
}

const char *sip_srv_type_to_string(unsigned int srv_type)
{
	if (srv_type >=
	    sizeof(sip_srv_type_str) / sizeof(sip_srv_type_str[0])) {
		ofono_debug("abnormal event:%d", srv_type);
		return "unknow";
	}
	return sip_srv_type_str[srv_type];
}

const char *sip_method_to_string(unsigned int sip_method)
{
	if (sip_method >= sizeof(sip_method_str) / sizeof(sip_method_str[0])) {
		ofono_debug("abnormal event:%d", sip_method);
		return "unknow";
	}
	return sip_method_str[sip_method];
}

const char *rrc_timer_id_to_string(unsigned int timer_id)
{
	if (timer_id >=
	    sizeof(rrc_timer_id_str) / sizeof(rrc_timer_id_str[0])) {
		ofono_debug("abnormal event:%d", timer_id);
		return "unknow";
	}
	return rrc_timer_id_str[timer_id];
}

const char *ecall_fail_cause_to_string(unsigned int cause)
{
	if (cause >=
	    sizeof(ecall_fail_reason_str) / sizeof(ecall_fail_reason_str[0])) {
		ofono_debug("abnormal event:%d", cause);
		return "unknow";
	}
	return ecall_fail_reason_str[cause];
}

const char *rtp_rtcp_error_to_string(unsigned int error_type)
{
	if (error_type >= sizeof(rtp_rtcp_error_type_str) /
				  sizeof(rtp_rtcp_error_type_str[0])) {
		ofono_debug("abnormal event:%d", error_type);
		return "unknow";
	}
	return rtp_rtcp_error_type_str[error_type];
}

const char *nas_procedure_type_to_string(unsigned int procedure_type)
{
	if (procedure_type < sizeof(nas_procedure_type1_str) /
				     sizeof(nas_procedure_type1_str[0])) {
		return nas_procedure_type1_str[procedure_type];
	}

	if (procedure_type >= 100 &&
	    procedure_type <
		    100 + sizeof(nas_procedure_type2_str) /
				    sizeof(nas_procedure_type2_str[0])) {
		return nas_procedure_type2_str[procedure_type - 100];
	}

	ofono_debug("abnormal event:%d", procedure_type);
	return "unknow";
}

const char *xcap_mode_to_string(unsigned int mode)
{
	if (mode >= sizeof(xcap_mode_str) / sizeof(xcap_mode_str[0])) {
		ofono_debug("abnormal event:%d", mode);
		return "unknow";
	}
	return xcap_mode_str[mode];
}

const char *xcap_reason_to_string(unsigned int reason)
{
	if (reason >= sizeof(xcap_reason_str) / sizeof(xcap_reason_str[0])) {
		ofono_debug("abnormal event:%d", reason);
		return "unknow";
	}
	return xcap_reason_str[reason];
}

const char *xcap_error_to_string(unsigned int error_type)
{
	if (error_type >= sizeof(xcap_error_str) / sizeof(xcap_error_str[0])) {
		ofono_debug("abnormal event:%d", error_type);
		return "unknow";
	}
	return xcap_error_str[error_type];
}

const char *call_end_reason_to_string(unsigned int reason)
{
	if (reason >= sizeof(sip_call_end_reason_str) /
			      sizeof(sip_call_end_reason_str[0])) {
		ofono_debug("abnormal event:%d", reason);
		return "unknow";
	}
	return sip_call_end_reason_str[reason];
}

const char *limited_cause_to_string(unsigned int cause)
{
	if (cause >= sizeof(limited_srv_cause_str) /
			      sizeof(limited_srv_cause_str[0])) {
		ofono_debug("abnormal event:%d", cause);
		return "unknow";
	}
	return limited_srv_cause_str[cause];
}

void parse_post_dial_string(const char *str, char *target, char *postdial)
{
	char str_src[OFONO_MAX_PHONE_NUMBER_LENGTH];

	memset(str_src, 0, OFONO_MAX_PHONE_NUMBER_LENGTH);
	strcpy(str_src, str);

	gchar **number = g_strsplit(str_src, WAIT, -1);

	if (number[0] != NULL && strlen(number[0]) > 0) {
		gchar **post = g_strsplit(number[0], PAUSE, -1);

		if (post[0] != NULL && strlen(post[0]) > 0) {
			strcpy(target, post[0]);
		}
		if (post[1] != NULL && strlen(post[1]) > 0) {
			strcpy(postdial, post[1]);
		}
		g_strfreev(post);
	}

	g_strfreev(number);
}

gboolean valid_number_format(const char *number, int length)
{
	int len = strlen(number);
	int begin = 0;
	int i;

	if (!len)
		return FALSE;

	if (number[0] == '+')
		begin = 1;

	if (begin == len)
		return FALSE;

	if ((len - begin) > length)
		return FALSE;

	for (i = begin; i < len; i++) {
		if (number[i] >= '0' && number[i] <= '9')
			continue;

		if (number[i] == '*' || number[i] == '#'
			|| number[i] == ',' || number[i] == ';')
			continue;

		return FALSE;
	}

	return TRUE;
}

gboolean valid_actual_number_format(const char *number, int length)
{
	int len = strlen(number);
	int begin = 0;
	int i;

	if (!len)
		return FALSE;

	if (number[0] == '+')
		begin = 1;

	if (begin == len)
		return FALSE;

	if ((len - begin) > length)
		return FALSE;

	for (i = begin; i < len; i++) {
		if (number[i] >= '0' && number[i] <= '9')
			continue;

		if (number[i] == '*' || number[i] == '#')
			continue;

		return FALSE;
	}

	return TRUE;
}

/*
 * According to 3GPP TS 24.011 or 3GPP TS 31.102, some
 * addresses (or numbers), like Service Centre address,
 * Destination address, or EFADN (Abbreviated dialling numbers),
 * are up 20 digits.
 */
gboolean valid_phone_number_format(const char *number)
{
	return valid_number_format(number, 20);
}

gboolean valid_long_phone_number_format(const char *number)
{
	return valid_number_format(number, OFONO_MAX_PHONE_NUMBER_LENGTH);
}

gboolean valid_cdma_phone_number_format(const char *number)
{
	int len = strlen(number);
	int i;

	if (!len)
		return FALSE;

	if (len > OFONO_CDMA_MAX_PHONE_NUMBER_LENGTH)
		return FALSE;

	for (i = 0; i < len; i++) {
		if (number[i] >= '0' && number[i] <= '9')
			continue;

		if (number[i] == '*' || number[i] == '#'
			|| number[i] == ',' || number[i] == ';')
			continue;

		return FALSE;
	}

	return TRUE;
}

const char *telephony_error_to_str(const struct ofono_error *error)
{
	struct error_entry *e;
	int maxentries;
	int i;

	switch (error->type) {
	case OFONO_ERROR_TYPE_CME:
		e = cme_errors;
		maxentries = sizeof(cme_errors) / sizeof(struct error_entry);
		break;
	case OFONO_ERROR_TYPE_CMS:
		e = cms_errors;
		maxentries = sizeof(cms_errors) / sizeof(struct error_entry);
		break;
	case OFONO_ERROR_TYPE_CEER:
		e = ceer_errors;
		maxentries = sizeof(ceer_errors) / sizeof(struct error_entry);
		break;
	default:
		return "Unknown error type";
	}

	for (i = 0; i < maxentries; i++)
		if (e[i].error == error->error)
			return e[i].str;

	return "Unknown error";
}

int mmi_service_code_to_bearer_class(int code)
{
	int cls = 0;

	/*
	 * Teleservices according to 22.004
	 * 1 - Voice
	 * 2 - SMS
	 * 3,4,5 - Unallocated
	 * 6 - Fax
	 * 7 - All Data Async
	 * 8 - All Data Sync
	 * 12 - Voice Group
	 */

	switch (code) {
	/* 22.030: 1 to 6, 12 */
	case 10:
		cls = BEARER_CLASS_VOICE | BEARER_CLASS_FAX | BEARER_CLASS_SMS;
		break;
	/* 22.030: 1 */
	case 11:
		cls = BEARER_CLASS_VOICE;
		break;
	/* 22.030: 2-6 */
	case 12:
		cls = BEARER_CLASS_SMS | BEARER_CLASS_FAX;
		break;
	/* 22.030: 6 */
	case 13:
		cls = BEARER_CLASS_FAX;
		break;
	/* 22.030: 2 */
	case 16:
		cls = BEARER_CLASS_SMS;
		break;
	/* TODO: Voice Group Call & Broadcast VGCS & VBS */
	case 17:
	case 18:
		break;
	/* 22.030: 1, 3 to 6, 12 */
	case 19:
		cls = BEARER_CLASS_VOICE | BEARER_CLASS_FAX;
		break;
	/*
	 * 22.030: 7-11
	 * 22.004 only defines BS 7 (Data Sync) & BS 8 (Data Async)
	 * and PAD and Packet bearer services are deprecated.  Still,
	 * AT modems rely on these to differentiate between sending
	 * a 'All Sync' or 'All Data Sync' message types.  In theory
	 * both message types cover the same bearer services, but we
	 * must still send these for conformance reasons.
	 */
	case 20:
		cls = BEARER_CLASS_DATA_ASYNC | BEARER_CLASS_DATA_SYNC |
			BEARER_CLASS_PAD | BEARER_CLASS_PACKET;
		break;
	/* According to 22.030: All Async (7) */
	case 21:
		cls = BEARER_CLASS_DATA_ASYNC | BEARER_CLASS_PAD;
		break;
	/* According to 22.030: All Data Async (7)*/
	case 25:
		cls = BEARER_CLASS_DATA_ASYNC;
		break;
	/* According to 22.030: All Sync (8) */
	case 22:
		cls = BEARER_CLASS_DATA_SYNC | BEARER_CLASS_PACKET;
		break;
	/* According to 22.030: All Data Sync (8) */
	case 24:
		cls = BEARER_CLASS_DATA_SYNC;
		break;
	/* According to 22.030: Telephony & All Sync services (1, 8) */
	case 26:
		cls = BEARER_CLASS_VOICE | BEARER_CLASS_DATA_SYNC |
			BEARER_CLASS_PACKET;
		break;
	default:
		break;
	}

	return cls;
}

const char *phone_number_to_string(const struct ofono_phone_number *ph)
{
	static char buffer[OFONO_MAX_PHONE_NUMBER_LENGTH + 2];

	if (ph->type == 145 && (strlen(ph->number) > 0) &&
			ph->number[0] != '+') {
		buffer[0] = '+';
		strncpy(buffer + 1, ph->number, OFONO_MAX_PHONE_NUMBER_LENGTH);
		buffer[OFONO_MAX_PHONE_NUMBER_LENGTH + 1] = '\0';
	} else {
		strncpy(buffer, ph->number, OFONO_MAX_PHONE_NUMBER_LENGTH + 1);
		buffer[OFONO_MAX_PHONE_NUMBER_LENGTH + 1] = '\0';
	}

	return buffer;
}

void string_to_phone_number(const char *str, struct ofono_phone_number *ph, gboolean skip_plus)
{
	if (str[0] == '+') {
		if (skip_plus) {
			strcpy(ph->number, str + 1);
		} else {
			strcpy(ph->number, str);
		}
		ph->type = 145;	/* International */
	} else {
		strcpy(ph->number, str);
		ph->type = 129;	/* Local */
	}
}

const char *cdma_phone_number_to_string(
				const struct ofono_cdma_phone_number *ph)
{
	static char buffer[OFONO_CDMA_MAX_PHONE_NUMBER_LENGTH + 1];

	strncpy(buffer, ph->number, OFONO_CDMA_MAX_PHONE_NUMBER_LENGTH);
	buffer[OFONO_CDMA_MAX_PHONE_NUMBER_LENGTH] = '\0';

	return buffer;
}

void string_to_cdma_phone_number(const char *str,
					struct ofono_cdma_phone_number *ph)
{
	strcpy(ph->number, str);
}

gboolean valid_ussd_string(const char *str, gboolean call_in_progress)
{
	int len = strlen(str);

	if (!len)
		return FALSE;

	/*
	 * Return true if an MMI input string is to be sent as USSD.
	 *
	 * According to 3GPP TS 22.030, after checking the well-known
	 * supplementary service control, SIM control and manufacturer
	 * defined control codes, the terminal should check if the input
	 * should be sent as USSD according to the following rules:
	 *
	 * 1) Terminated by '#'
	 * 2) A short string of 1 or 2 digits
	 *
	 * As an exception, if a 2 digit string starts with a '1' and
	 * there are no calls in progress then this string is treated as
	 * a call setup request instead.
	 */

	if (str[len-1] == '#')
		return TRUE;

	if (!call_in_progress && len == 2 && str[0] == '1')
		return FALSE;

	if (len <= 2)
		return TRUE;

	return FALSE;
}

const char *ss_control_type_to_string(enum ss_control_type type)
{
	switch (type) {
	case SS_CONTROL_TYPE_ACTIVATION:
		return "activation";
	case SS_CONTROL_TYPE_REGISTRATION:
		return "registration";
	case SS_CONTROL_TYPE_QUERY:
		return "interrogation";
	case SS_CONTROL_TYPE_DEACTIVATION:
		return "deactivation";
	case SS_CONTROL_TYPE_ERASURE:
		return "erasure";
	}

	return NULL;
}

#define NEXT_FIELD(str, dest)			\
	do {					\
		dest = str;			\
						\
		str = strchrnul(str, '*');	\
		if (*str) {			\
			*str = '\0';		\
			str += 1;		\
		}				\
	} while (0)				\

/*
 * Note: The str will be modified, so in case of error you should
 * throw it away and start over
 */
gboolean parse_ss_control_string(char *str, int *ss_type,
					char **sc, char **sia,
					char **sib, char **sic,
					char **sid, char **dn)
{
	int len = strlen(str);
	int cur = 0;
	char *c;
	unsigned int i;
	gboolean ret = FALSE;

	/* Minimum is {*,#}SC# */
	if (len < 4)
		goto out;

	if (str[0] != '*' && str[0] != '#')
		goto out;

	cur = 1;

	if (str[1] != '*' && str[1] != '#' && (str[1] > '9' || str[1] < '0'))
		goto out;

	if (str[0] == '#' && str[1] == '*')
		goto out;

	if (str[1] == '#' || str[1] == '*')
		cur = 2;

	if (str[0] == '*' && str[1] == '*')
		*ss_type = SS_CONTROL_TYPE_REGISTRATION;
	else if (str[0] == '#' && str[1] == '#')
		*ss_type = SS_CONTROL_TYPE_ERASURE;
	else if (str[0] == '*' && str[1] == '#')
		*ss_type = SS_CONTROL_TYPE_QUERY;
	else if (str[0] == '*')
		*ss_type = SS_CONTROL_TYPE_ACTIVATION;
	else
		*ss_type = SS_CONTROL_TYPE_DEACTIVATION;

	/* Must have at least one other '#' */
	c = strrchr(str+cur, '#');

	if (c == NULL)
		goto out;

	*dn = c+1;
	*c = '\0';

	if (strlen(*dn) > 0 && !valid_phone_number_format(*dn))
		goto out;

	c = str+cur;

	NEXT_FIELD(c, *sc);

	/*
	 * According to 22.030 SC is 2 or 3 digits, there can be
	 * an optional digit 'n' if this is a call setup string,
	 * however 22.030 does not define any SC of length 3
	 * with an 'n' present
	 */
	if (strlen(*sc) < 2 || strlen(*sc) > 3)
		goto out;

	for (i = 0; i < strlen(*sc); i++)
		if (!g_ascii_isdigit((*sc)[i]))
			goto out;

	NEXT_FIELD(c, *sia);
	NEXT_FIELD(c, *sib);
	NEXT_FIELD(c, *sic);
	NEXT_FIELD(c, *sid);

	if (*c == '\0')
		ret = TRUE;

out:
	return ret;
}

static const char *bearer_class_lut[] = {
	"Voice",
	"Data",
	"Fax",
	"Sms",
	"DataSync",
	"DataAsync",
	"DataPad",
	"DataPacket"
};

const char *bearer_class_to_string(enum bearer_class cls)
{
	switch (cls) {
	case BEARER_CLASS_VOICE:
		return bearer_class_lut[0];
	case BEARER_CLASS_DATA:
		return bearer_class_lut[1];
	case BEARER_CLASS_FAX:
		return bearer_class_lut[2];
	case BEARER_CLASS_SMS:
		return bearer_class_lut[3];
	case BEARER_CLASS_DATA_SYNC:
		return bearer_class_lut[4];
	case BEARER_CLASS_DATA_ASYNC:
		return bearer_class_lut[5];
	case BEARER_CLASS_PACKET:
		return bearer_class_lut[6];
	case BEARER_CLASS_PAD:
		return bearer_class_lut[7];
	case BEARER_CLASS_DEFAULT:
	case BEARER_CLASS_SS_DEFAULT:
		break;
	};

	return NULL;
}

const char *registration_status_to_string(int status)
{
	switch (status) {
	case NETWORK_REGISTRATION_STATUS_NOT_REGISTERED:
		return "unregistered";
	case NETWORK_REGISTRATION_STATUS_REGISTERED:
		return "registered";
	case NETWORK_REGISTRATION_STATUS_SEARCHING:
		return "searching";
	case NETWORK_REGISTRATION_STATUS_DENIED:
		return "denied";
	case NETWORK_REGISTRATION_STATUS_UNKNOWN:
		return "unknown";
	case NETWORK_REGISTRATION_STATUS_ROAMING:
		return "roaming";
	case NETWORK_REGISTRATION_STATUS_REGISTERED_SMS_EUTRAN:
		return "registered";
	case NETWORK_REGISTRATION_STATUS_ROAMING_SMS_EUTRAN:
		return "roaming";
	case NETWORK_REGISTRATION_STATUS_REGISTED_EM:
		return "registered_em";
	case NETWORK_REGISTRATION_STATUS_NOT_REGISTERED_EM:
		return "unregistered_em";
	case NETWORK_REGISTRATION_STATUS_SEARCHING_EM:
		return "searching_em";
	case NETWORK_REGISTRATION_STATUS_DENIED_EM:
		return "denied_em";
	case NETWORK_REGISTRATION_STATUS_UNKNOWN_EM:
		return "unknown_em";
	}

	return "";
}

const char *registration_tech_to_string(int tech)
{
	switch (tech) {
	case ACCESS_TECHNOLOGY_GSM:
		return "gsm";
	case ACCESS_TECHNOLOGY_GSM_COMPACT:
		return "gsm";
	case ACCESS_TECHNOLOGY_UTRAN:
		return "umts";
	case ACCESS_TECHNOLOGY_GSM_EGPRS:
		return "edge";
	case ACCESS_TECHNOLOGY_UTRAN_HSDPA:
		return "hspa";
	case ACCESS_TECHNOLOGY_UTRAN_HSUPA:
		return "hspa";
	case ACCESS_TECHNOLOGY_UTRAN_HSDPA_HSUPA:
		return "hspa";
	case ACCESS_TECHNOLOGY_EUTRAN:
		return "lte";
	case ACCESS_TECHNOLOGY_NB_IOT_M1:
		return "lte-cat-m1";
	case ACCESS_TECHNOLOGY_NB_IOT_NB1:
		return "lte-cat-nb1";
	default:
		return "";
	}
}

int registration_tech_from_string(const char *tech)
{
	if (tech == NULL)
		return -1;

	if (strcmp(tech, "gsm") == 0)
		return ACCESS_TECHNOLOGY_GSM;
	else if (strcmp(tech, "edge") == 0)
		return ACCESS_TECHNOLOGY_GSM_EGPRS;
	else if (strcmp(tech, "umts") == 0)
		return ACCESS_TECHNOLOGY_UTRAN;
	else if (strcmp(tech, "hspa") == 0)
		return ACCESS_TECHNOLOGY_UTRAN;
	else if (strcmp(tech, "lte") == 0)
		return ACCESS_TECHNOLOGY_EUTRAN;

	return -1;
}

gboolean is_valid_apn(const char *apn)
{
	int i;
	int last_period = -1;

	if (apn == NULL)
		return FALSE;

	if (apn[0] == '.' || apn[0] == '\0')
		return FALSE;

	if (strlen(apn) > OFONO_GPRS_MAX_APN_LENGTH)
		return FALSE;

	for (i = 0; apn[i] != '\0'; i++) {
		if (g_ascii_isalnum(apn[i]))
			continue;

		if (apn[i] == '-')
			continue;

		if (apn[i] == '.' && (i - last_period) > 1) {
			last_period = i;
			continue;
		}

		return FALSE;
	}

	return TRUE;
}

const char *ofono_uuid_to_str(const struct ofono_uuid *uuid)
{
	static char buf[OFONO_SHA1_UUID_LEN * 2 + 1];

	return encode_hex_own_buf(uuid->uuid, OFONO_SHA1_UUID_LEN, 0, buf);
}

void ofono_call_init(struct ofono_call *call)
{
	memset(call, 0, sizeof(struct ofono_call));
	call->cnap_validity = CNAP_VALIDITY_NOT_AVAILABLE;
	call->clip_validity = CLIP_VALIDITY_NOT_AVAILABLE;
}

const char *call_status_to_string(enum call_status status)
{
	switch (status) {
	case CALL_STATUS_ACTIVE:
		return "active";
	case CALL_STATUS_HELD:
		return "held";
	case CALL_STATUS_DIALING:
		return "dialing";
	case CALL_STATUS_ALERTING:
		return "alerting";
	case CALL_STATUS_INCOMING:
		return "incoming";
	case CALL_STATUS_WAITING:
		return "waiting";
	case CALL_STATUS_DISCONNECTED:
		return "disconnected";
	}

	return "unknown";
}

const char *gprs_proto_to_string(enum ofono_gprs_proto proto)
{
	switch (proto) {
	case OFONO_GPRS_PROTO_IP:
		return "IP";
	case OFONO_GPRS_PROTO_IPV6:
		return "IPV6";
	case OFONO_GPRS_PROTO_IPV4V6:
		return "IPV4V6";
	};

	return NULL;
}

gboolean gprs_proto_from_string(const char *str, enum ofono_gprs_proto *proto)
{
	if (g_str_equal(str, "IP")) {
		*proto = OFONO_GPRS_PROTO_IP;
		return TRUE;
	} else if (g_str_equal(str, "IPV6")) {
		*proto = OFONO_GPRS_PROTO_IPV6;
		return TRUE;
	} else if (g_str_equal(str, "IPV4V6")) {
		*proto = OFONO_GPRS_PROTO_IPV4V6;
		return TRUE;
	}

	return FALSE;
}

const char *gprs_auth_method_to_string(enum ofono_gprs_auth_method auth)
{
	switch (auth) {
	case OFONO_GPRS_AUTH_METHOD_CHAP:
		return "chap";
	case OFONO_GPRS_AUTH_METHOD_PAP:
		return "pap";
	case OFONO_GPRS_AUTH_METHOD_NONE:
		return "none";
	};

	return NULL;
}

gboolean gprs_auth_method_from_string(const char *str,
					enum ofono_gprs_auth_method *auth)
{
	if (g_str_equal(str, "chap")) {
		*auth = OFONO_GPRS_AUTH_METHOD_CHAP;
		return TRUE;
	} else if (g_str_equal(str, "pap")) {
		*auth = OFONO_GPRS_AUTH_METHOD_PAP;
		return TRUE;
	} else if (g_str_equal(str, "none")) {
		*auth = OFONO_GPRS_AUTH_METHOD_NONE;
		return TRUE;
	}

	return FALSE;
}

int in_range_or_unavailable(int value, int range_min, int range_max) {
	if (value < range_min || value > range_max)
		return INT_MAX;

	return value;
}

int get_rssi_dbm_from_asu(int rssi_asu) {
	if (rssi_asu >= 0 && rssi_asu <= 31)
		return -113 + (2 * rssi_asu);

	return INT_MAX;
}

int convert_rssnr_unit_from_ten_db_to_db(int rssnr) {
	return (int) floor((float) rssnr / 10);
}

int get_signal_level_from_rsrp(int rsrp)
{
	const int *threshold = NULL;
	int length, level;
	const char *support_env;

	rsrp = in_range_or_unavailable(rsrp, -140, -43);
	if (rsrp == INT_MAX)
		return SIGNAL_STRENGTH_UNKNOWN;

	// Check for 5-level signal support
	support_env = getenv("OFONO_FIVE_SIGNAL_LEVEL_SUPPORT");
	if (support_env != NULL && strcmp(support_env, "1") == 0) {
		threshold = five_bar_rsrp_thresholds;
		length = sizeof(five_bar_rsrp_thresholds) / sizeof(int);
	} else {
		threshold = default_rsrp_thresholds;
		length = sizeof(default_rsrp_thresholds) / sizeof(int);
	}

	level = length;
	while (level > 0 && rsrp < threshold[level - 1]) level--;

	ofono_info("update signal level from rsrp, length = %d, rsrp = %d, level = %d",
				length, rsrp, level);

	return level;
}

int get_signal_level_from_rssi(int rssi)
{
	const int *threshold = NULL;
	int length, level;

	rssi = in_range_or_unavailable(rssi, -113, -51);
	if (rssi == INT_MAX)
		return SIGNAL_STRENGTH_UNKNOWN;

	threshold = default_rssi_thresholds;
	length = sizeof(default_rssi_thresholds) / sizeof(int);

	level = length;
	while (level > 0 && rssi < threshold[level - 1]) level--;

	ofono_info("update signal level from rssi, length = %d, rssi = %d, level = %d",
				length, rssi, level);

	return level;
}

gboolean is_gprs_context_type_support(const char *gc_type) {
	const char *gc_type_support;

	gc_type_support = getenv("OFONO_GPRS_CONTEXT_TYPE_SUPPORT");
	if (gc_type_support != NULL && strstr(gc_type_support, gc_type) != NULL)  {
		return TRUE;
	} else {
		ofono_debug("not support for gprs context %s type ! \n", gc_type);
		return FALSE;
	}
}

