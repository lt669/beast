/* GObject - GLib Type, Object, Parameter and Signal Library
 * Copyright (C) 1998, 1999, 2000 Tim Janik and Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __G_ENUMS_H__
#define __G_ENUMS_H__

#include	<bse/glib-gtype.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* --- type macros --- */
#define G_TYPE_IS_ENUM(type)	(G_TYPE_FUNDAMENTAL (type) == G_TYPE_ENUM)
#define G_ENUM_TYPE(class)	(G_TYPE_FROM_CLASS (class))
#define G_ENUM_NAME(class)	(g_type_name (G_ENUM_TYPE (class)))
#define G_ENUM_CLASS(class)	(G_TYPE_CHECK_CLASS_CAST ((class), G_TYPE_ENUM, GEnumClass))
#define G_IS_ENUM_CLASS(class)	(G_TYPE_CHECK_CLASS_TYPE ((class), G_TYPE_ENUM))
#define G_TYPE_IS_FLAGS(type)	(G_TYPE_FUNDAMENTAL (type) == G_TYPE_FLAGS)
#define G_FLAGS_TYPE(class)	(G_TYPE_FROM_CLASS (class))
#define G_FLAGS_NAME(class)	(g_type_name (G_FLAGS_TYPE (class)))
#define G_FLAGS_CLASS(class)	(G_TYPE_CHECK_CLASS_CAST ((class), G_TYPE_FLAGS, GFlagsClass))
#define G_IS_FLAGS_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), G_TYPE_FLAGS))


/* --- enum/flag values & classes --- */
typedef struct _GEnumClass  GEnumClass;
typedef struct _GFlagsClass GFlagsClass;
typedef struct _GEnumValue  GEnumValue;
typedef struct _GFlagsValue GFlagsValue;
struct	_GEnumClass
{
  GTypeClass  g_class;
  
  gint	      minimum;
  gint	      maximum;
  guint	      n_values;
  GEnumValue *values;
};
struct	_GFlagsClass
{
  GTypeClass   g_class;
  
  guint	       mask;
  guint	       n_values;
  GFlagsValue *values;
};
struct _GEnumValue
{
  gint	 value;
  gchar *value_name;
  gchar *value_nick;
};
struct _GFlagsValue
{
  guint	 value;
  gchar *value_name;
  gchar *value_nick;
};


/* --- prototypes --- */
void		g_enums_init			(void);
GEnumValue*	g_enum_get_value		(GEnumClass	*enum_class,
						 gint		 value);
GEnumValue*	g_enum_get_value_by_name	(GEnumClass	*enum_class,
						 const gchar	*name);
GEnumValue*	g_enum_get_value_by_nick	(GEnumClass	*enum_class,
						 const gchar	*nick);
GFlagsValue*	g_flags_get_first_value		(GFlagsClass	*flags_class,
						 guint		 value);
GFlagsValue*	g_flags_get_value_by_name	(GFlagsClass	*flags_class,
						 const gchar	*name);
GFlagsValue*	g_flags_get_value_by_nick	(GFlagsClass	*flags_class,
						 const gchar	*nick);


/* --- registration functions --- */
/* const_static_values is a NULL terminated array of enum/flags
 * values that is taken over!
 */
GType	g_enum_register_static	   (const gchar	      *name,
				    const GEnumValue  *const_static_values);
GType	g_flags_register_static	   (const gchar	      *name,
				    const GFlagsValue *const_static_values);
/* functions to complete the type information
 * for enums/flags implemented by plugins
 */
void	g_enum_complete_type_info  (GType	       g_enum_type,
				    GTypeInfo	      *info,
				    const GEnumValue  *const_values);
void	g_flags_complete_type_info (GType	       g_flags_type,
				    GTypeInfo	      *info,
				    const GFlagsValue *const_values);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __G_ENUMS_H__ */
