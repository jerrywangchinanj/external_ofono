/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2010  Nokia Corporation and/or its subsidiary(-ies).
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
#include <errno.h>
#include <stdint.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"
#include "common.h"
#include "storage.h"

#define SETTINGS_STORE "radiosetting"
#define SETTINGS_GROUP "Settings"
#define RADIO_SETTINGS_FLAG_CACHED 0x1

static GSList *g_drivers = NULL;

struct ofono_radio_settings {
	DBusMessage *pending;
	int flags;
	int mode;
	enum ofono_radio_band_gsm band_gsm;
	enum ofono_radio_band_umts band_umts;
	ofono_bool_t fast_dormancy;
	int pending_mode;
	enum ofono_radio_band_gsm pending_band_gsm;
	enum ofono_radio_band_umts pending_band_umts;
	ofono_bool_t fast_dormancy_pending;
	uint32_t available_rats;
	GKeyFile *settings;
	char *imsi;
	const struct ofono_radio_settings_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
	ofono_bool_t provisioned;
};

static void radio_band_set_callback_at_reg(const struct ofono_error *error, void *data);
static void radio_mode_set_callback_at_reg(const struct ofono_error *error, void *data);
static void radio_load_settings(struct ofono_radio_settings *rs);
static void radio_close_settings(struct ofono_radio_settings *rs);

static const char *radio_access_mode_to_string(int m)
{
	switch (m) {
	case OFONO_RADIO_ACCESS_MODE_GSM:
		return "gsm";
	case OFONO_RADIO_ACCESS_MODE_WCDMA_ONLY:
		return "umts";
	case OFONO_RADIO_ACCESS_MODE_LTE_ONLY:
		return "lte";
	case OFONO_RADIO_ACCESS_MODE_UMTS:
		return "umts,gsm";
	case OFONO_RADIO_ACCESS_MODE_LTE_WCDMA:
		return "lte,umts";
	case OFONO_RADIO_ACCESS_MODE_LTE_GSM_WCDMA:
		return "lte,umts,gsm";
	case OFONO_RADIO_ACCESS_MODE_ANY:
		return "any";
	}

	return "";
}

static gboolean radio_access_mode_from_string(const char *str,
					int *mode)

{
	if (g_str_equal(str, "gsm")) {
		*mode = OFONO_RADIO_ACCESS_MODE_GSM;
		return TRUE;
	} else if (g_str_equal(str, "umts")) {
		*mode = OFONO_RADIO_ACCESS_MODE_WCDMA_ONLY;
		return TRUE;
	} else if (g_str_equal(str, "lte")) {
		*mode = OFONO_RADIO_ACCESS_MODE_LTE_ONLY;
		return TRUE;
	} else if (g_str_equal(str, "umts,gsm")) {
		*mode = OFONO_RADIO_ACCESS_MODE_UMTS;
		return TRUE;
	} else if (g_str_equal(str, "lte,umts")) {
		*mode = OFONO_RADIO_ACCESS_MODE_LTE_WCDMA;
		return TRUE;
	} else if (g_str_equal(str, "lte,umts,gsm")) {
		*mode = OFONO_RADIO_ACCESS_MODE_LTE_GSM_WCDMA;
		return TRUE;
	} else if (g_str_equal(str, "any")) {
		*mode = OFONO_RADIO_ACCESS_MODE_ANY;
		return TRUE;
	}

	return FALSE;
}

static const char *radio_band_gsm_to_string(enum ofono_radio_band_gsm band)
{
	switch (band) {
	case OFONO_RADIO_BAND_GSM_ANY:
		return "any";
	case OFONO_RADIO_BAND_GSM_850:
		return "850";
	case OFONO_RADIO_BAND_GSM_900P:
		return "900P";
	case OFONO_RADIO_BAND_GSM_900E:
		return "900E";
	case OFONO_RADIO_BAND_GSM_1800:
		return "1800";
	case OFONO_RADIO_BAND_GSM_1900:
		return "1900";
	}

	return "";
}

static gboolean radio_band_gsm_from_string(const char *str,
					enum ofono_radio_band_gsm *band)
{
	if (g_str_equal(str, "any")) {
		*band = OFONO_RADIO_BAND_GSM_ANY;
		return TRUE;
	} else if (g_str_equal(str, "850")) {
		*band = OFONO_RADIO_BAND_GSM_850;
		return TRUE;
	} else if (g_str_equal(str, "900P")) {
		*band = OFONO_RADIO_BAND_GSM_900P;
		return TRUE;
	} else if (g_str_equal(str, "900E")) {
		*band = OFONO_RADIO_BAND_GSM_900E;
		return TRUE;
	} else if (g_str_equal(str, "1800")) {
		*band = OFONO_RADIO_BAND_GSM_1800;
		return TRUE;
	} else if (g_str_equal(str, "1900")) {
		*band = OFONO_RADIO_BAND_GSM_1900;
		return TRUE;
	}

	return FALSE;
}

static const char *radio_band_umts_to_string(enum ofono_radio_band_umts band)
{
	switch (band) {
	case OFONO_RADIO_BAND_UMTS_ANY:
		return "any";
	case OFONO_RADIO_BAND_UMTS_850:
		return "850";
	case OFONO_RADIO_BAND_UMTS_900:
		return "900";
	case OFONO_RADIO_BAND_UMTS_1700AWS:
		return "1700AWS";
	case OFONO_RADIO_BAND_UMTS_1900:
		return "1900";
	case OFONO_RADIO_BAND_UMTS_2100:
		return "2100";
	}

	return "";
}

static gboolean radio_band_umts_from_string(const char *str,
					enum ofono_radio_band_umts *band)
{
	if (g_str_equal(str, "any")) {
		*band = OFONO_RADIO_BAND_UMTS_ANY;
		return TRUE;
	} else if (g_str_equal(str, "850")) {
		*band = OFONO_RADIO_BAND_UMTS_850;
		return TRUE;
	} else if (g_str_equal(str, "900")) {
		*band = OFONO_RADIO_BAND_UMTS_900;
		return TRUE;
	} else if (g_str_equal(str, "1700AWS")) {
		*band = OFONO_RADIO_BAND_UMTS_1700AWS;
		return TRUE;
	} else if (g_str_equal(str, "1900")) {
		*band = OFONO_RADIO_BAND_UMTS_1900;
		return TRUE;
	} else if (g_str_equal(str, "2100")) {
		*band = OFONO_RADIO_BAND_UMTS_2100;
		return TRUE;
	}

	return FALSE;
}

static int radio_get_default_prefer_type(void) {
	const char *prefer_type_env;
	int prefer_type;

	prefer_type_env = getenv("OFONO_PREFER_NETWORK_TYPE");
	if (prefer_type_env == NULL
		|| !radio_access_mode_from_string(prefer_type_env, &prefer_type)) {
		prefer_type = OFONO_RADIO_ACCESS_MODE_LTE_WCDMA;
	}
	ofono_info("radio setting get default prefer type=%d", prefer_type);
	return prefer_type;
}

#ifndef CONFIG_SUPPORT_RADIO_GSM
/**
 * @brief Only when network_type contain gsm will be filter out gsm, and return value
 * will be TRUE, orelse do not change any network_type value and return FALSE.
 *
 * @param network_type
 * @return gboolean: if true, @param network_type filter out gsm, others false.
 */
static gboolean network_type_filter_out_gsm(int *network_type) {
	gboolean is_filter_out_gsm = FALSE;

	switch (*network_type) {
	case OFONO_RADIO_ACCESS_MODE_UMTS: /* "umts,gsm" -> "umts" */
		*network_type = OFONO_RADIO_ACCESS_MODE_WCDMA_ONLY;
		is_filter_out_gsm = TRUE;
		break;
	case OFONO_RADIO_ACCESS_MODE_ANY: /* "any" -> "lte,umts" */
	case OFONO_RADIO_ACCESS_MODE_GSM: /* "gsm" -> "lte,umts" */
	case OFONO_RADIO_ACCESS_MODE_LTE_GSM_WCDMA: /* "lte,umts,gsm" -> "lte,umts" */
		*network_type = OFONO_RADIO_ACCESS_MODE_LTE_WCDMA;
		is_filter_out_gsm = TRUE;
		break;
	}

	return is_filter_out_gsm;
}
#endif

static DBusMessage *radio_get_properties_reply(DBusMessage *msg,
						struct ofono_radio_settings *rs)
{
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;

	const char *mode = radio_access_mode_to_string(rs->mode);

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	ofono_dbus_dict_append(&dict, "TechnologyPreference",
					DBUS_TYPE_STRING, &mode);

	if (rs->driver->query_band) {
		const char *band = radio_band_gsm_to_string(rs->band_gsm);

		ofono_dbus_dict_append(&dict, "GsmBand",
					DBUS_TYPE_STRING, &band);

		band = radio_band_umts_to_string(rs->band_umts);

		ofono_dbus_dict_append(&dict, "UmtsBand",
					DBUS_TYPE_STRING, &band);
	}

	if (rs->driver->query_fast_dormancy) {
		dbus_bool_t value = rs->fast_dormancy;
		ofono_dbus_dict_append(&dict, "FastDormancy",
					DBUS_TYPE_BOOLEAN, &value);
	}

	if (rs->available_rats) {
		const char *rats[sizeof(uint32_t) * CHAR_BIT + 1];
		const char **dbus_rats = rats;
		int n = 0;
		unsigned int i;

		for (i = 0; i < sizeof(uint32_t) * CHAR_BIT; i++) {
			unsigned int tech = 1u << i;

			if (!(rs->available_rats & tech))
				continue;

			rats[n++] = radio_access_mode_to_string(tech);
		}

		rats[n] = NULL;

		ofono_dbus_dict_append_array(&dict, "AvailableTechnologies",
						DBUS_TYPE_STRING, &dbus_rats);
	}

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static void radio_set_fast_dormancy(struct ofono_radio_settings *rs,
					ofono_bool_t enable)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(rs->atom);
	dbus_bool_t value = enable;

	if (rs->fast_dormancy == enable)
		return;

	ofono_dbus_signal_property_changed(conn, path,
						OFONO_RADIO_SETTINGS_INTERFACE,
						"FastDormancy",
						DBUS_TYPE_BOOLEAN, &value);
	rs->fast_dormancy = enable;
}

static void radio_fast_dormancy_set_callback(const struct ofono_error *error,
						void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessage *reply;
	ofono_debug("%s, error_type: %d", __func__, (int)error->type);

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error setting fast dormancy");

		rs->fast_dormancy_pending = rs->fast_dormancy;

		reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);

		return;
	}

	reply = dbus_message_new_method_return(rs->pending);
	__ofono_dbus_pending_reply(&rs->pending, reply);

	radio_set_fast_dormancy(rs, rs->fast_dormancy_pending);
}

static void radio_set_band(struct ofono_radio_settings *rs)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;
	const char *str_band;

	path = __ofono_atom_get_path(rs->atom);

	if (rs->band_gsm != rs->pending_band_gsm) {
		rs->band_gsm = rs->pending_band_gsm;
		str_band = radio_band_gsm_to_string(rs->band_gsm);

		ofono_dbus_signal_property_changed(conn, path,
						OFONO_RADIO_SETTINGS_INTERFACE,
						"GsmBand", DBUS_TYPE_STRING,
						&str_band);

		if (rs->settings) {
			g_key_file_set_integer(rs->settings, SETTINGS_GROUP,
					"GsmBand", rs->band_gsm);
			storage_sync(SETTINGS_STORE, SETTINGS_STORE, rs->settings);
		}
	}

	if (rs->band_umts != rs->pending_band_umts) {
		rs->band_umts = rs->pending_band_umts;
		str_band = radio_band_umts_to_string(rs->band_umts);

		ofono_dbus_signal_property_changed(conn, path,
						OFONO_RADIO_SETTINGS_INTERFACE,
						"UmtsBand", DBUS_TYPE_STRING,
						&str_band);

		if (rs->settings) {
			g_key_file_set_integer(rs->settings, SETTINGS_GROUP,
					"UmtsBand", rs->band_umts);
			storage_sync(SETTINGS_STORE, SETTINGS_STORE, rs->settings);
		}
	}
}

static void radio_band_set_callback(const struct ofono_error *error,
					void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error setting radio frequency band");

		rs->pending_band_gsm = rs->band_gsm;
		rs->pending_band_umts = rs->band_umts;

		reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);

		return;
	}

	reply = dbus_message_new_method_return(rs->pending);
	__ofono_dbus_pending_reply(&rs->pending, reply);

	radio_set_band(rs);
}

static void radio_set_rat_mode(struct ofono_radio_settings *rs,
				int mode)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path;
	const char *str_mode;

	if (rs->mode == mode)
		return;

	rs->mode = mode;

	path = __ofono_atom_get_path(rs->atom);
	str_mode = radio_access_mode_to_string(rs->mode);

	ofono_dbus_signal_property_changed(conn, path,
						OFONO_RADIO_SETTINGS_INTERFACE,
						"TechnologyPreference",
						DBUS_TYPE_STRING, &str_mode);
}

static void radio_mode_set_callback(const struct ofono_error *error, void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error setting radio access mode");

		rs->pending_mode = rs->mode;

		reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);

		return;
	}

	if (rs->settings) {
		g_key_file_set_integer(rs->settings, SETTINGS_GROUP,
				"TechnologyPreference", rs->pending_mode);
		storage_sync(SETTINGS_STORE, SETTINGS_STORE, rs->settings);
	}

	reply = dbus_message_new_method_return(rs->pending);
	__ofono_dbus_pending_reply(&rs->pending, reply);

	radio_set_rat_mode(rs, rs->pending_mode);
}

static void radio_send_properties_reply(struct ofono_radio_settings *rs)
{
	DBusMessage *reply;

	rs->flags |= RADIO_SETTINGS_FLAG_CACHED;

	reply = radio_get_properties_reply(rs->pending, rs);
	__ofono_dbus_pending_reply(&rs->pending, reply);
}

static void radio_available_rats_query_callback(const struct ofono_error *error,
						unsigned int available_rats,
						void *data)
{
	struct ofono_radio_settings *rs = data;

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR)
		rs->available_rats = available_rats & 0x7;
	else
		DBG("Error while querying available rats");

	radio_send_properties_reply(rs);
}

static void radio_query_available_rats(struct ofono_radio_settings *rs)
{
	/* Modem technology is not supposed to change, so one query is enough */
	if (rs->driver->query_available_rats == NULL || rs->available_rats) {
		radio_send_properties_reply(rs);
		return;
	}

	rs->driver->query_available_rats(
				rs, radio_available_rats_query_callback, rs);
}

static void radio_fast_dormancy_query_callback(const struct ofono_error *error,
						ofono_bool_t enable, void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error during fast dormancy query");

		reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);

		return;
	}

	radio_set_fast_dormancy(rs, enable);
	radio_query_available_rats(rs);
}

static void radio_query_fast_dormancy(struct ofono_radio_settings *rs)
{
	if (rs->driver->query_fast_dormancy == NULL) {
		radio_query_available_rats(rs);
		return;
	}

	rs->driver->query_fast_dormancy(rs, radio_fast_dormancy_query_callback,
					rs);
}

static void radio_band_query_callback(const struct ofono_error *error,
					enum ofono_radio_band_gsm band_gsm,
					enum ofono_radio_band_umts band_umts,
					void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error during radio frequency band query");

		reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);

		return;
	}

	rs->pending_band_gsm = band_gsm;
	rs->pending_band_umts = band_umts;

	radio_set_band(rs);
	radio_query_fast_dormancy(rs);
}

static void radio_query_band(struct ofono_radio_settings *rs)
{
	if (rs->driver->query_band == NULL) {
		radio_query_fast_dormancy(rs);
		return;
	}

	rs->driver->query_band(rs, radio_band_query_callback, rs);
}

static void radio_rat_mode_query_callback(const struct ofono_error *error,
						int mode, void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		DBG("Error during radio access mode query");

		reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);

		return;
	}

	radio_set_rat_mode(rs, mode);
	radio_query_band(rs);
}

static DBusMessage *radio_get_properties(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_radio_settings *rs = data;

	if (rs->flags & RADIO_SETTINGS_FLAG_CACHED)
		return radio_get_properties_reply(msg, rs);

	if (rs->driver->query_rat_mode == NULL)
		return __ofono_error_not_implemented(msg);

	if (rs->pending)
		return __ofono_error_busy(msg);

	rs->pending = dbus_message_ref(msg);
	rs->driver->query_rat_mode(rs, radio_rat_mode_query_callback, rs);

	return NULL;
}

static DBusMessage *radio_set_property(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_radio_settings *rs = data;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *property;

	if (rs->pending)
		return __ofono_error_busy(msg);

	if (!dbus_message_iter_init(msg, &iter))
		return __ofono_error_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &property);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_recurse(&iter, &var);

	if (g_strcmp0(property, "TechnologyPreference") == 0) {
		const char *value;
		int mode;

		if (rs->driver->set_rat_mode == NULL)
			return __ofono_error_not_implemented(msg);

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &value);
		if (radio_access_mode_from_string(value, &mode) == FALSE)
			return __ofono_error_invalid_args(msg);

		if (rs->mode == mode)
			return dbus_message_new_method_return(msg);

		rs->pending = dbus_message_ref(msg);
		rs->pending_mode = mode;

		rs->driver->set_rat_mode(rs, mode, radio_mode_set_callback, rs);
		/* will be saved in radiosettng on success response*/
		return NULL;
	} else if (g_strcmp0(property, "GsmBand") == 0) {
		const char *value;
		enum ofono_radio_band_gsm band;

		if (rs->driver->set_band == NULL)
			return __ofono_error_not_implemented(msg);

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &value);
		if (radio_band_gsm_from_string(value, &band) == FALSE)
			return __ofono_error_invalid_args(msg);

		if (rs->band_gsm == band)
			return dbus_message_new_method_return(msg);

		rs->pending = dbus_message_ref(msg);
		rs->pending_band_gsm = band;

		rs->driver->set_band(rs, band, rs->band_umts,
					radio_band_set_callback, rs);
		/* will be saved in radiosettng on success response*/
		return NULL;
	} else if (g_strcmp0(property, "UmtsBand") == 0) {
		const char *value;
		enum ofono_radio_band_umts band;

		if (rs->driver->set_band == NULL)
			return __ofono_error_not_implemented(msg);

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &value);
		if (radio_band_umts_from_string(value, &band) == FALSE)
			return __ofono_error_invalid_args(msg);

		if (rs->band_umts == band)
			return dbus_message_new_method_return(msg);

		rs->pending = dbus_message_ref(msg);
		rs->pending_band_umts = band;

		rs->driver->set_band(rs, rs->band_gsm, band,
					radio_band_set_callback, rs);
		/* will be saved in radiosettng on success response*/
		return NULL;
	} else if (g_strcmp0(property, "FastDormancy") == 0) {
		dbus_bool_t value;
		int target;

		if (rs->driver->set_fast_dormancy == NULL)
			return __ofono_error_not_implemented(msg);

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &value);
		target = value;

		rs->pending = dbus_message_ref(msg);
		rs->fast_dormancy_pending = target;

		ofono_debug("Set fast_dormancy: %d", target);
		rs->driver->set_fast_dormancy(rs, target,
					radio_fast_dormancy_set_callback, rs);
		return NULL;
	}

	return __ofono_error_invalid_args(msg);
}

static const GDBusMethodTable radio_methods[] = {
	{ GDBUS_ASYNC_METHOD("GetProperties",
			NULL, GDBUS_ARGS({ "properties", "a{sv}" }),
			radio_get_properties) },
	{ GDBUS_ASYNC_METHOD("SetProperty",
			GDBUS_ARGS({ "property", "s" }, { "value", "v" }),
			NULL, radio_set_property) },
	{ }
};

static const GDBusSignalTable radio_signals[] = {
	{ GDBUS_SIGNAL("PropertyChanged",
			GDBUS_ARGS({ "name", "s" }, { "value", "v" })) },
	{ }
};

static void radio_state_change(int state, void *data)
{
	struct ofono_atom *atom = data;
	struct ofono_radio_settings *rs = __ofono_atom_get_data(atom);

	if (!rs->provisioned && state == RADIO_STATUS_ON) {
		rs->provisioned = TRUE;

		if (rs->driver == NULL)
			return;

		if (rs->driver->set_band != NULL)
			rs->driver->set_band(rs, rs->band_gsm, rs->band_umts,
						radio_band_set_callback_at_reg, rs);

		if (rs->driver->set_rat_mode != NULL) {
			rs->driver->set_rat_mode(rs, rs->mode,
						radio_mode_set_callback_at_reg, rs);
		}
	}
}

int ofono_radio_settings_driver_register(const struct ofono_radio_settings_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d == NULL || d->probe == NULL)
		return -EINVAL;

	g_drivers = g_slist_prepend(g_drivers, (void *) d);

	return 0;
}

void ofono_radio_settings_driver_unregister(const struct ofono_radio_settings_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d == NULL)
		return;

	g_drivers = g_slist_remove(g_drivers, (void *) d);
}

static void radio_settings_unregister(struct ofono_atom *atom)
{
	struct ofono_radio_settings *rs = __ofono_atom_get_data(atom);
	const char *path = __ofono_atom_get_path(rs->atom);
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(rs->atom);

	ofono_modem_remove_interface(modem, OFONO_RADIO_SETTINGS_INTERFACE);
	g_dbus_unregister_interface(conn, path, OFONO_RADIO_SETTINGS_INTERFACE);

	radio_close_settings(rs);
}

static void radio_settings_remove(struct ofono_atom *atom)
{
	struct ofono_radio_settings *rs = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	if (rs == NULL)
		return;

	if (rs->pending != NULL) {
		DBusMessage *reply = __ofono_error_failed(rs->pending);
		__ofono_dbus_pending_reply(&rs->pending, reply);
	}

	if (rs->driver && rs->driver->remove)
		rs->driver->remove(rs);

	g_free(rs);
}

struct ofono_radio_settings *ofono_radio_settings_create(struct ofono_modem *modem,
							unsigned int vendor,
							const char *driver,
							void *data)
{
	struct ofono_radio_settings *rs;
	GSList *l;

	if (driver == NULL)
		return NULL;

	rs = g_try_new0(struct ofono_radio_settings, 1);
	if (rs == NULL)
		return NULL;

	rs->provisioned = FALSE;

	rs->mode = radio_get_default_prefer_type();

	rs->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_RADIO_SETTINGS,
						radio_settings_remove, rs);

	__ofono_atom_add_radio_state_watch(rs->atom, radio_state_change);

	for (l = g_drivers; l; l = l->next) {
		const struct ofono_radio_settings_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver) != 0)
			continue;

		if (drv->probe(rs, vendor, data) < 0)
			continue;

		rs->driver = drv;
		break;
	}

	return rs;
}

static void ofono_radio_finish_register(struct ofono_radio_settings *rs)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(rs->atom);
	const char *path = __ofono_atom_get_path(rs->atom);

	if (!g_dbus_register_interface(conn, path,
					OFONO_RADIO_SETTINGS_INTERFACE,
					radio_methods, radio_signals,
					NULL, rs, NULL)) {
		ofono_error("Could not create %s interface",
				OFONO_RADIO_SETTINGS_INTERFACE);
		return;
	}

	ofono_modem_add_interface(modem, OFONO_RADIO_SETTINGS_INTERFACE);

	__ofono_atom_register(rs->atom, radio_settings_unregister);
}

static void radio_mode_set_callback_at_reg(const struct ofono_error *error,
						void *data)
{
	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		DBG("Error setting radio access mode register time");
}

static void radio_band_set_callback_at_reg(const struct ofono_error *error,
						void *data)
{
	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		DBG("Error setting radio access mode register time");
	/*
	 * Continue with atom register even if request fail at modem
	 * ofono_radio_finish_register called by radio_mode_set_callback_at_reg
	 */
}

static void radio_load_settings(struct ofono_radio_settings *rs)
{
	GError *error;

	rs->settings = storage_open(SETTINGS_STORE, SETTINGS_STORE);

	/*
	 * If no settings present or error; Set default.
	 * Default RAT mode: ANY (LTE > UMTS > GSM)
	 */
	if (rs->settings == NULL) {
		DBG("radiosetting storage open failed");
		rs->mode = OFONO_RADIO_ACCESS_MODE_LTE_GSM_WCDMA;
		rs->band_gsm = OFONO_RADIO_BAND_GSM_ANY;
		rs->band_umts = OFONO_RADIO_BAND_UMTS_ANY;
		return;
	}

	error = NULL;
	rs->band_gsm = g_key_file_get_integer(rs->settings, SETTINGS_GROUP,
					"GsmBand", &error);

	if (error || radio_band_gsm_to_string(rs->band_gsm) == NULL) {
		rs->band_gsm = OFONO_RADIO_BAND_GSM_ANY;
		g_key_file_set_integer(rs->settings, SETTINGS_GROUP,
						"GsmBand", rs->band_gsm);
	}

	if (error) {
		g_error_free(error);
		error = NULL;
	}

	rs->pending_band_gsm = rs->band_gsm;

	rs->band_umts = g_key_file_get_integer(rs->settings, SETTINGS_GROUP,
					"UmtsBand", &error);

	if (error || radio_band_umts_to_string(rs->band_umts) == NULL) {
		rs->band_umts = OFONO_RADIO_BAND_UMTS_ANY;
		g_key_file_set_integer(rs->settings, SETTINGS_GROUP,
						"UmtsBand", rs->band_umts);
	}

	if (error) {
		g_error_free(error);
		error = NULL;
	}

	rs->pending_band_umts = rs->band_umts;

	rs->mode = g_key_file_get_integer(rs->settings, SETTINGS_GROUP,
					"TechnologyPreference", &error);

	if (error || radio_access_mode_to_string(rs->mode) == NULL) {
		rs->mode = radio_get_default_prefer_type();
		g_key_file_set_integer(rs->settings, SETTINGS_GROUP,
					"TechnologyPreference", rs->mode);
	}

	#ifndef CONFIG_SUPPORT_RADIO_GSM
	if (network_type_filter_out_gsm(&rs->mode)) {
		ofono_info("TechnologyPreference: %d: filter out gsm", rs->mode);
		g_key_file_set_integer(rs->settings, SETTINGS_GROUP,
					"TechnologyPreference", rs->mode);
	}
	#endif

	if (error) {
		g_error_free(error);
		error = NULL;
	}

	DBG("TechnologyPreference: %d", rs->mode);
	DBG("GsmBand: %d", rs->band_gsm);
	DBG("UmtsBand: %d", rs->band_umts);
}

static void radio_close_settings(struct ofono_radio_settings *rs)
{
	if (rs->settings) {
		storage_close(SETTINGS_STORE, SETTINGS_STORE, rs->settings, TRUE);
		rs->settings = NULL;
	}
}

void ofono_radio_settings_register(struct ofono_radio_settings *rs)
{
	radio_load_settings(rs);

	ofono_radio_finish_register(rs);
}

void ofono_radio_settings_remove(struct ofono_radio_settings *rs)
{
	__ofono_atom_free(rs->atom);
}

void ofono_radio_settings_set_data(struct ofono_radio_settings *rs,
					void *data)
{
	rs->driver_data = data;
}

void *ofono_radio_settings_get_data(struct ofono_radio_settings *rs)
{
	return rs->driver_data;
}

struct ofono_modem *ofono_radio_settings_get_modem(
					struct ofono_radio_settings *rs)
{
	return __ofono_atom_get_modem(rs->atom);
}
