/*
 * e-mail-config-eee-summary.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_MAIL_CONFIG_EEE_SUMMARY_H
#define E_MAIL_CONFIG_EEE_SUMMARY_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EEE_SUMMARY \
	(e_mail_config_eee_summary_get_type ())
#define E_MAIL_CONFIG_EEE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EEE_SUMMARY, EMailConfigEeeSummary))
#define E_MAIL_CONFIG_EEE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EEE_SUMMARY, EMailConfigEeeSummaryClass))
#define E_IS_MAIL_CONFIG_EEE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EEE_SUMMARY))
#define E_IS_MAIL_CONFIG_EEE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EEE_SUMMARY))
#define E_MAIL_CONFIG_EEE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EEE_SUMMARY, EMailConfigEeeSummaryClass))

G_BEGIN_DECLS

typedef struct _EMailConfigEeeSummary EMailConfigEeeSummary;
typedef struct _EMailConfigEeeSummaryClass EMailConfigEeeSummaryClass;
typedef struct _EMailConfigEeeSummaryPrivate EMailConfigEeeSummaryPrivate;

struct _EMailConfigEeeSummary {
	EExtension parent;
	EMailConfigEeeSummaryPrivate *priv;
};

struct _EMailConfigEeeSummaryClass {
	EExtensionClass parent_class;
};

GType		e_mail_config_eee_summary_get_type
					(void) G_GNUC_CONST;
void		e_mail_config_eee_summary_type_register
					(GTypeModule *type_module);
gboolean	e_mail_config_eee_summary_get_applicable
					(EMailConfigEeeSummary *extension);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EEE_SUMMARY_H */

