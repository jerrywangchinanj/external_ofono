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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "common.h"
#include "util.h"

#define LEN_MAX 128
#define TYPE_INTERNATIONAL 145

#define PHONEBOOK_FLAG_CACHED 0x1

static GSList *g_drivers = NULL;

enum phonebook_number_type {
	TEL_TYPE_HOME,
	TEL_TYPE_MOBILE,
	TEL_TYPE_FAX,
	TEL_TYPE_WORK,
	TEL_TYPE_OTHER,
};

struct ofono_phonebook {
	DBusMessage *pending;
	int storage_index; /* go through all supported storage */
	int flags;
	GString *vcards; /* entries with vcard 3.0 format */
	GSList *merge_list; /* cache the entries that may need a merge */
	const struct ofono_phonebook_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
	int fdn_flags;
	GTree *fdn_entries;	/* Container of fdn_entry structures */
};

struct phonebook_number {
	char *number;
	int type;
	enum phonebook_number_type category;
};

struct phonebook_person {
	GSList *number_list; /* one person may have more than one numbers */
	char *text;
	int hidden;
	char *group;
	char *email;
	char *sip_uri;
};

static const char *storage_support[] = { "SM", "ME", NULL };
static void export_phonebook(struct ofono_phonebook *pb);

/* according to RFC 2425, the output string may need folding */
static void vcard_printf(GString *str, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	int len_temp, line_number, i;
	unsigned int line_delimit = 75;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	line_number = strlen(buf) / line_delimit + 1;

	for (i = 0; i < line_number; i++) {
		len_temp = MIN(line_delimit, strlen(buf) - line_delimit * i);
		g_string_append_len(str,  buf + line_delimit * i, len_temp);
		if (i != line_number - 1)
			g_string_append(str, "\r\n ");
	}

	g_string_append(str, "\r\n");
}

/*
 * According to RFC 2426, we need escape following characters:
 * '\n', '\r', ';', ',', '\'.
 */
static void add_slash(char *dest, const char *src, int len_max, int len)
{
	int i, j;

	for (i = 0, j = 0; i < len && j < len_max; i++, j++) {
		switch (src[i]) {
		case '\n':
			dest[j++] = '\\';
			dest[j] = 'n';
			break;
		case '\r':
			dest[j++] = '\\';
			dest[j] = 'r';
			break;
		case '\\':
		case ';':
		case ',':
			dest[j++] = '\\';
			/* fall through */
		default:
			dest[j] = src[i];
			break;
		}
	}
	dest[j] = 0;
	return;
}

static void vcard_printf_begin(GString *vcards)
{
	vcard_printf(vcards, "BEGIN:VCARD");
	vcard_printf(vcards, "VERSION:3.0");
}

static void vcard_printf_text(GString *vcards, const char *text)
{
	char field[LEN_MAX];
	add_slash(field, text, LEN_MAX, strlen(text));
	vcard_printf(vcards, "FN:%s", field);
}

static void vcard_printf_number(GString *vcards, const char *number, int type,
					enum phonebook_number_type category)
{
	char *pref = "", *intl = "", *category_string = "";
	char buf[128];

	if (number == NULL || !strlen(number) || !type)
		return;

	switch (category) {
	case TEL_TYPE_HOME:
		category_string = "HOME,VOICE";
		break;
	case TEL_TYPE_MOBILE:
		category_string = "CELL,VOICE";
		break;
	case TEL_TYPE_FAX:
		category_string = "FAX";
		break;
	case TEL_TYPE_WORK:
		category_string = "WORK,VOICE";
		break;
	case TEL_TYPE_OTHER:
		category_string = "VOICE";
		break;
	}

	if ((type == TYPE_INTERNATIONAL) && (number[0] != '+'))
		intl = "+";

	snprintf(buf, sizeof(buf), "TEL;TYPE=%s%s:%s%s", pref,
			category_string, intl, number);
	vcard_printf(vcards, buf, number);
}

static void vcard_printf_group(GString *vcards,	const char *group)
{
	int len = 0;

	if (group)
		len = strlen(group);

	if (len) {
		char field[LEN_MAX];
		add_slash(field, group, LEN_MAX, len);
		vcard_printf(vcards, "CATEGORIES:%s", field);
	}
}

static void vcard_printf_email(GString *vcards, const char *email)
{
	int len = 0;

	if (email)
		len = strlen(email);

	if (len) {
		char field[LEN_MAX];
		add_slash(field, email, LEN_MAX, len);
		vcard_printf(vcards,
				"EMAIL;TYPE=INTERNET:%s", field);
	}
}

static void vcard_printf_sip_uri(GString *vcards, const char *sip_uri)
{
	int len = 0;

	if (sip_uri)
		len = strlen(sip_uri);

	if (len) {
		char field[LEN_MAX];
		add_slash(field, sip_uri, LEN_MAX, len);
		vcard_printf(vcards, "IMPP;TYPE=SIP:%s", field);
	}
}

static void vcard_printf_end(GString *vcards)
{
	vcard_printf(vcards, "END:VCARD");
	vcard_printf(vcards, "");
}

static void print_number(gpointer pointer, gpointer user_data)
{
	struct phonebook_number *pn = pointer;
	GString *vcards = user_data;
	vcard_printf_number(vcards, pn->number, pn->type, pn->category);
}

static void destroy_number(gpointer pointer)
{
	struct phonebook_number *pn = pointer;
	g_free(pn->number);
	g_free(pn);
}

static void print_merged_entry(gpointer pointer, gpointer user_data)
{
	struct phonebook_person *person = pointer;
	GString *vcards = user_data;
	vcard_printf_begin(vcards);
	vcard_printf_text(vcards, person->text);

	g_slist_foreach(person->number_list, print_number, vcards);

	vcard_printf_group(vcards, person->group);
	vcard_printf_email(vcards, person->email);
	vcard_printf_sip_uri(vcards, person->sip_uri);
	vcard_printf_end(vcards);
}

static void destroy_merged_entry(gpointer pointer)
{
	struct phonebook_person *person = pointer;
	g_free(person->text);
	g_free(person->group);
	g_free(person->email);
	g_free(person->sip_uri);

	g_slist_free_full(person->number_list, destroy_number);

	g_free(person);
}

static DBusMessage *generate_export_entries_reply(struct ofono_phonebook *pb,
							DBusMessage *msg)
{
	DBusMessage *reply;
	DBusMessageIter iter;

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, pb->vcards);

	return reply;
}

static gboolean need_merge(const char *text)
{
	int len;
	char c;

	if (text == NULL)
		return FALSE;

	len = strlen(text);

	if (len < 2)
		return FALSE;

	c = tolower(text[len-1]);

	if ((text[len-2] == '/') &&
			((c == 'w') || (c == 'h') || (c == 'm') || (c == 'o')))
		return TRUE;

	return FALSE;
}

static void merge_field_generic(char **str1, const char *str2)
{
	if ((*str1 == NULL) && (str2 != NULL) && (strlen(str2) != 0))
		*str1 = g_strdup(str2);
}

static void merge_field_number(GSList **l, const char *number, int type, char c)
{
	struct phonebook_number *pn = g_new0(struct phonebook_number, 1);
	enum phonebook_number_type category;

	pn->number = g_strdup(number);
	pn->type = type;
	switch (tolower(c)) {
	case 'w':
		category = TEL_TYPE_WORK;
		break;
	case 'h':
		category = TEL_TYPE_HOME;
		break;
	case 'm':
		category = TEL_TYPE_MOBILE;
		break;
	case 'f':
		category = TEL_TYPE_FAX;
		break;
	case 'o':
	default:
		category = TEL_TYPE_OTHER;
		break;
	}
	pn->category = category;
	*l = g_slist_append(*l, pn);
}

void ofono_phonebook_entry(struct ofono_phonebook *phonebook, int index,
				const char *number, int type,
				const char *text, int hidden,
				const char *group,
				const char *adnumber, int adtype,
				const char *secondtext, const char *email,
				const char *sip_uri, const char *tel_uri)
{
	/* There's really nothing to do */
	if ((number == NULL || number[0] == '\0') &&
			(text == NULL || text[0] == '\0'))
		return;

	/*
	 * We need to collect all the entries that belong to one person,
	 * so that only one vCard will be generated at last.
	 * Entries only differ with '/w', '/h', '/m', etc. in field text
	 * are deemed as entries of one person.
	 */
	if (need_merge(text)) {
		GSList *l;
		size_t len_text = strlen(text) - 2;
		struct phonebook_person *person;

		for (l = phonebook->merge_list; l; l = l->next) {
			person = l->data;
			if (!strncmp(text, person->text, len_text) &&
					(strlen(person->text) == len_text))
				break;
		}

		if (l == NULL) {
			person = g_new0(struct phonebook_person, 1);
			phonebook->merge_list =
				g_slist_prepend(phonebook->merge_list, person);
			person->text = g_strndup(text, len_text);
		}

		merge_field_number(&(person->number_list), number, type,
					text[len_text + 1]);
		merge_field_number(&(person->number_list), adnumber, adtype,
					text[len_text + 1]);

		merge_field_generic(&(person->group), group);
		merge_field_generic(&(person->email), email);
		merge_field_generic(&(person->sip_uri), sip_uri);

		return;
	}

	vcard_printf_begin(phonebook->vcards);

	if (text == NULL || text[0] == '\0')
		vcard_printf_text(phonebook->vcards, number);
	else
		vcard_printf_text(phonebook->vcards, text);

	vcard_printf_number(phonebook->vcards, number, type, TEL_TYPE_OTHER);
	vcard_printf_number(phonebook->vcards, adnumber, adtype,
				TEL_TYPE_OTHER);
	vcard_printf_group(phonebook->vcards, group);
	vcard_printf_email(phonebook->vcards, email);
	vcard_printf_sip_uri(phonebook->vcards, sip_uri);
	vcard_printf_end(phonebook->vcards);
}

static void export_phonebook_cb(const struct ofono_error *error, void *data)
{
	struct ofono_phonebook *phonebook = data;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		ofono_error("export_entries_one_storage_cb with %s failed",
				storage_support[phonebook->storage_index]);

	/* convert the collected entries that are already merged to vcard */
	phonebook->merge_list = g_slist_reverse(phonebook->merge_list);
	g_slist_foreach(phonebook->merge_list, print_merged_entry,
				phonebook->vcards);
	g_slist_free_full(phonebook->merge_list, destroy_merged_entry);
	phonebook->merge_list = NULL;

	phonebook->storage_index++;
	export_phonebook(phonebook);
	return;
}

static void export_phonebook(struct ofono_phonebook *phonebook)
{
	DBusMessage *reply;
	const char *pb = storage_support[phonebook->storage_index];

	if (pb) {
		phonebook->driver->export_entries(phonebook, pb,
						export_phonebook_cb, phonebook);
		return;
	}

	reply = generate_export_entries_reply(phonebook, phonebook->pending);
	if (reply == NULL) {
		dbus_message_unref(phonebook->pending);
		return;
	}

	__ofono_dbus_pending_reply(&phonebook->pending, reply);
	phonebook->flags |= PHONEBOOK_FLAG_CACHED;
}

static DBusMessage *import_entries(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_phonebook *phonebook = data;
	DBusMessage *reply;

	if (phonebook->pending) {
		reply = __ofono_error_busy(phonebook->pending);
		g_dbus_send_message(conn, reply);
		return NULL;
	}

	if (phonebook->flags & PHONEBOOK_FLAG_CACHED) {
		reply = generate_export_entries_reply(phonebook, msg);
		g_dbus_send_message(conn, reply);
		return NULL;
	}

	g_string_set_size(phonebook->vcards, 0);
	phonebook->storage_index = 0;

	phonebook->pending = dbus_message_ref(msg);
	export_phonebook(phonebook);

	return NULL;
}

static gboolean append_fdn_entry_struct_list(gpointer key, gpointer value, gpointer data)
{
	DBusMessageIter entry;
	DBusMessageIter *array = data;
	struct fdn_entry *fdn = value;
	int fdn_idx = GPOINTER_TO_INT(key);

	dbus_message_iter_open_container(array, DBUS_TYPE_STRUCT, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_INT32, &fdn_idx);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &fdn->name);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &fdn->number);
	dbus_message_iter_close_container(array, &entry);

	return FALSE;
}

static DBusMessage *generate_fdn_export_entries_reply(struct ofono_phonebook *pb,
							DBusMessage *msg)
{
	DBusMessage *reply;
	DBusMessageIter iter, array;

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					DBUS_STRUCT_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_INT32_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_STRUCT_END_CHAR_AS_STRING,
					&array);

	g_tree_foreach(pb->fdn_entries, append_fdn_entry_struct_list, &array);

	dbus_message_iter_close_container(&iter, &array);

	return reply;
}

static void export_fdn_entries_cb(const struct ofono_error *error, void *data)
{
	struct ofono_phonebook *phonebook = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error occurred during fdn entries export");
		reply = __ofono_error_failed(phonebook->pending);
		__ofono_dbus_pending_reply(&phonebook->pending, reply);
		return;
	}

	reply = generate_fdn_export_entries_reply(phonebook, phonebook->pending);
	if (reply == NULL) {
		dbus_message_unref(phonebook->pending);
		return;
	}

	__ofono_dbus_pending_reply(&phonebook->pending, reply);
	phonebook->fdn_flags |= PHONEBOOK_FLAG_CACHED;
}

static DBusMessage *import_fdn_entries(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_phonebook *phonebook = data;
	DBusMessage *reply;

	if (phonebook->driver->read_fdn_entries == NULL)
		return __ofono_error_not_implemented(msg);

	if (phonebook->pending) {
		reply = __ofono_error_busy(phonebook->pending);
		g_dbus_send_message(conn, reply);
		return NULL;
	}

	// already read
	if (phonebook->fdn_flags & PHONEBOOK_FLAG_CACHED) {
		reply = generate_fdn_export_entries_reply(phonebook, msg);
		g_dbus_send_message(conn, reply);
		return NULL;
	}

	phonebook->pending = dbus_message_ref(msg);
	phonebook->driver->read_fdn_entries(phonebook,
					export_fdn_entries_cb, phonebook);

	return NULL;
}

static void insert_fdn_entry_cb(const struct ofono_error *error, int record, void *data)
{
	struct ofono_phonebook *phonebook = data;
	char *new_name, *new_number, *pin2;
	struct fdn_entry *new_entry;
	DBusMessageIter iter;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error occurred during fdn entry insert");
		reply = __ofono_error_failed(phonebook->pending);
		__ofono_dbus_pending_reply(&phonebook->pending, reply);
		return;
	}

	//update phonebook->fdn_entries
	if (dbus_message_get_args(phonebook->pending, NULL,
			DBUS_TYPE_STRING, &new_name,
			DBUS_TYPE_STRING, &new_number,
			DBUS_TYPE_STRING, &pin2,
			DBUS_TYPE_INVALID) == FALSE) {
		reply = __ofono_error_invalid_format(phonebook->pending);
		__ofono_dbus_pending_reply(&phonebook->pending, reply);
		return;
	}

	new_entry = l_new(struct fdn_entry, 1);
	new_entry->name = l_strdup(new_name);
	new_entry->number = l_strdup(new_number);

	g_tree_insert(phonebook->fdn_entries, GINT_TO_POINTER(record), new_entry);

	reply = dbus_message_new_method_return(phonebook->pending);
	dbus_message_iter_init_append(reply, &iter);

	// returns the inserted fdnrecord number
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &record);

	__ofono_dbus_pending_reply(&phonebook->pending, reply);
}

static DBusMessage *insert_fdn_entry(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_phonebook *phonebook = data;
	char *new_name, *new_number, *pin2;
	DBusMessage *reply;

	if (phonebook->driver->insert_fdn_entry == NULL)
		return __ofono_error_not_implemented(msg);

	if (phonebook->pending) {
		reply = __ofono_error_busy(phonebook->pending);
		g_dbus_send_message(conn, reply);
		return NULL;
	}

	if (phonebook->fdn_flags ^ PHONEBOOK_FLAG_CACHED) {
		ofono_error("%s: read fdn file first ! \n", __func__);
		return __ofono_error_failed(msg);
	}

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &new_name,
				DBUS_TYPE_STRING, &new_number,
				DBUS_TYPE_STRING, &pin2,
				DBUS_TYPE_INVALID) == FALSE)
		return __ofono_error_invalid_args(msg);

	if ( !valid_phone_number_format(new_number) ||
		!__ofono_is_valid_sim_pin(pin2, OFONO_SIM_PASSWORD_SIM_PIN2))
		return __ofono_error_invalid_format(msg);

	phonebook->pending = dbus_message_ref(msg);
	phonebook->driver->insert_fdn_entry(phonebook, new_name, new_number,
					pin2, insert_fdn_entry_cb, phonebook);

	return NULL;
}

static void update_fdn_entry_cb(const struct ofono_error *error, int record, void *data)
{
	struct ofono_phonebook *phonebook = data;
	char *new_name, *new_number, *pin2;
	DBusMessage *reply;
	struct fdn_entry *entry;
	int fdn_idx;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error occurred during fdn entry update");
		reply = __ofono_error_failed(phonebook->pending);
		__ofono_dbus_pending_reply(&phonebook->pending, reply);
		return;
	}

	// update phonebook->fdn_entries
	if (dbus_message_get_args(phonebook->pending, NULL,
			DBUS_TYPE_STRING, &new_name,
			DBUS_TYPE_STRING, &new_number,
			DBUS_TYPE_STRING, &pin2,
			DBUS_TYPE_INT32, &fdn_idx,
			DBUS_TYPE_INVALID) == FALSE) {
		reply = __ofono_error_invalid_format(phonebook->pending);
		__ofono_dbus_pending_reply(&phonebook->pending, reply);
		return;
	}

	entry = g_tree_lookup(phonebook->fdn_entries, GINT_TO_POINTER(fdn_idx));
	if (entry) {
		/* If one already exists, delete it */
		if (entry->name)
			l_free(entry->name);
		entry->name = l_strdup(new_name);

		if (entry->number)
			l_free(entry->number);
		entry->number = l_strdup(new_number);
	}

	reply = dbus_message_new_method_return(phonebook->pending);

	__ofono_dbus_pending_reply(&phonebook->pending, reply);
}

static DBusMessage *update_fdn_entry(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_phonebook *phonebook = data;
	char *new_name, *new_number, *pin2;
	DBusMessage *reply;
	int fdn_idx;

	if (phonebook->driver->update_fdn_entry == NULL)
		return __ofono_error_not_implemented(msg);

	if (phonebook->pending) {
		reply = __ofono_error_busy(phonebook->pending);
		g_dbus_send_message(conn, reply);
		return NULL;
	}

	if (phonebook->fdn_flags ^ PHONEBOOK_FLAG_CACHED) {
		ofono_error("%s: read fdn file first ! \n", __func__);
		return __ofono_error_failed(msg);
	}

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &new_name,
				DBUS_TYPE_STRING, &new_number,
				DBUS_TYPE_STRING, &pin2,
				DBUS_TYPE_INT32, &fdn_idx,
				DBUS_TYPE_INVALID) == FALSE)
		return __ofono_error_invalid_args(msg);

	if ( !valid_phone_number_format(new_number) ||
		!__ofono_is_valid_sim_pin(pin2, OFONO_SIM_PASSWORD_SIM_PIN2))
		return __ofono_error_invalid_format(msg);

	phonebook->pending = dbus_message_ref(msg);
	phonebook->driver->update_fdn_entry(phonebook, fdn_idx,
					new_name, new_number, pin2,
					update_fdn_entry_cb, phonebook);

	return NULL;
}

static void delete_fdn_entry_cb(const struct ofono_error *error, int record, void *data)
{
	struct ofono_phonebook *phonebook = data;
	DBusMessage *reply;
	struct fdn_entry *entry;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error occurred during fdn entry delete");
		reply = __ofono_error_failed(phonebook->pending);
		__ofono_dbus_pending_reply(&phonebook->pending, reply);
		return;
	}

	// update phonebook->fdn_entries
	entry = g_tree_lookup(phonebook->fdn_entries, GINT_TO_POINTER(record));
	if (entry) {
		g_tree_remove(phonebook->fdn_entries, GINT_TO_POINTER(record));

		l_free(entry->name);
		l_free(entry->number);
		l_free(entry);
	}

	reply = dbus_message_new_method_return(phonebook->pending);

	__ofono_dbus_pending_reply(&phonebook->pending, reply);
}

static DBusMessage *delete_fdn_entry(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_phonebook *phonebook = data;
	DBusMessage *reply;
	char *pin2;
	int fdn_idx;

	if (phonebook->driver->delete_fdn_entry == NULL)
		return __ofono_error_not_implemented(msg);

	if (phonebook->pending) {
		reply = __ofono_error_busy(phonebook->pending);
		g_dbus_send_message(conn, reply);
		return NULL;
	}

	if (phonebook->fdn_flags ^ PHONEBOOK_FLAG_CACHED) {
		ofono_error("%s: read fdn file first ! \n", __func__);
		return __ofono_error_failed(msg);
	}

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &pin2,
				DBUS_TYPE_INT32, &fdn_idx,
				DBUS_TYPE_INVALID) == FALSE)
		return __ofono_error_invalid_args(msg);

	if (!__ofono_is_valid_sim_pin(pin2, OFONO_SIM_PASSWORD_SIM_PIN2))
		return __ofono_error_invalid_format(msg);

	phonebook->pending = dbus_message_ref(msg);
	phonebook->driver->delete_fdn_entry(phonebook, fdn_idx, pin2,
					delete_fdn_entry_cb, phonebook);

	return NULL;
}

static const GDBusMethodTable phonebook_methods[] = {
	{ GDBUS_ASYNC_METHOD("Import",
			NULL, GDBUS_ARGS({ "entries", "s" }),
			import_entries) },
	{ GDBUS_ASYNC_METHOD("ImportFdn",
			NULL, GDBUS_ARGS({ "entries", "a(iss)}" }),
			import_fdn_entries) },
	{ GDBUS_ASYNC_METHOD("InsertFdn",
			GDBUS_ARGS({ "name", "s" }, { "number", "s" },
			{ "pin2", "s" }),
			GDBUS_ARGS({ "fdn_idx", "i" }),
			insert_fdn_entry) },
	{ GDBUS_ASYNC_METHOD("UpdateFdn",
			GDBUS_ARGS({ "name", "s" }, { "number", "s" },
			{ "pin2", "s" }, { "fdn_idx", "i" }),
			NULL, update_fdn_entry) },
	{ GDBUS_ASYNC_METHOD("DeleteFdn",
			GDBUS_ARGS({ "pin2", "s" }, { "fdn_idx", "i" }),
			NULL, delete_fdn_entry) },
	{ }
};

static const GDBusSignalTable phonebook_signals[] = {
	{ }
};

int ofono_phonebook_driver_register(const struct ofono_phonebook_driver *d)
{
	/* Check for Phonebook interface support */
	if (!is_ofono_interface_supported(PHONEBOOK_INTERFACE)) {
		ofono_debug("%s : not support for phonebook! \n", __func__);
		return 0;
	}

	DBG("driver: %p, name: %s", d, d->name);

	if (d->probe == NULL)
		return -EINVAL;

	g_drivers = g_slist_prepend(g_drivers, (void *) d);

	return 0;
}

void ofono_phonebook_driver_unregister(const struct ofono_phonebook_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_drivers = g_slist_remove(g_drivers, (void *) d);
}

static void phonebook_unregister(struct ofono_atom *atom)
{
	struct ofono_phonebook *pb = __ofono_atom_get_data(atom);
	const char *path = __ofono_atom_get_path(pb->atom);
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(pb->atom);

	ofono_modem_remove_interface(modem, OFONO_PHONEBOOK_INTERFACE);
	g_dbus_unregister_interface(conn, path, OFONO_PHONEBOOK_INTERFACE);
}

static void phonebook_remove(struct ofono_atom *atom)
{
	struct ofono_phonebook *pb = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	if (pb == NULL)
		return;

	if (pb->pending != NULL) {
		DBusMessage *reply = __ofono_error_failed(pb->pending);
		__ofono_dbus_pending_reply(&pb->pending, reply);
	}

	if (pb->driver && pb->driver->remove)
		pb->driver->remove(pb);

	g_string_free(pb->vcards, TRUE);
	g_free(pb);
}

struct ofono_phonebook *ofono_phonebook_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver, void *data)
{
	struct ofono_phonebook *pb;
	GSList *l;

	/* Check for Phonebook interface support */
	if (!is_ofono_interface_supported(PHONEBOOK_INTERFACE)) {
		ofono_debug("%s : not support for phonebook! \n", __func__);
		return NULL;
	}

	if (driver == NULL)
		return NULL;

	if(__ofono_modem_find_atom(modem, OFONO_ATOM_TYPE_PHONEBOOK) != NULL) {
		//phonebook create when sim atom create,phonebook atom is removed in flush atom with sim atom together
		ofono_error("unexpected state:OFONO_ATOM_TYPE_PHONEBOOK atom exist");
		return NULL;
	}


	pb = g_try_new0(struct ofono_phonebook, 1);

	if (pb == NULL)
		return NULL;

	pb->vcards = g_string_new(NULL);
	pb->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_PHONEBOOK,
						phonebook_remove, pb);

	for (l = g_drivers; l; l = l->next) {
		const struct ofono_phonebook_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver))
			continue;

		if (drv->probe(pb, vendor, data) < 0)
			continue;

		pb->driver = drv;
		break;
	}

	return pb;
}

void ofono_phonebook_register(struct ofono_phonebook *pb)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(pb->atom);
	struct ofono_modem *modem = __ofono_atom_get_modem(pb->atom);

	if (!g_dbus_register_interface(conn, path, OFONO_PHONEBOOK_INTERFACE,
					phonebook_methods, phonebook_signals,
					NULL, pb, NULL)) {
		ofono_error("Could not create %s interface",
				OFONO_PHONEBOOK_INTERFACE);

		return;
	}

	ofono_modem_add_interface(modem, OFONO_PHONEBOOK_INTERFACE);

	__ofono_atom_register(pb->atom, phonebook_unregister);
}

void ofono_phonebook_remove(struct ofono_phonebook *pb)
{
	__ofono_atom_free(pb->atom);
}

void ofono_phonebook_set_data(struct ofono_phonebook *pb, void *data)
{
	pb->driver_data = data;
}

void *ofono_phonebook_get_data(struct ofono_phonebook *pb)
{
	return pb->driver_data;
}

void ofono_phonebook_set_fdn_data(struct ofono_phonebook *pb, void *data)
{
	pb->fdn_entries = data;
}
