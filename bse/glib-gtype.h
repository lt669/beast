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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __G_TYPE_H__
#define __G_TYPE_H__

#include	<bse/glib-extra.h>	/* FIXME: <glib.h> */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Predefined Fundamentals And Type Flags
 */
typedef enum
{
  /* standard types, introduced by g_type_init() */
  G_TYPE_INVALID,
  G_TYPE_NONE,
  G_TYPE_INTERFACE,

  /* GLib type ids */
  G_TYPE_ENUM,
  G_TYPE_FLAGS,
  G_TYPE_PARAM,
  G_TYPE_OBJECT,

  /* reserved type ids, mail gtk-devel-list@redhat.com for reservations */
  G_TYPE_BSE_PROCEDURE,
  G_TYPE_GLE_GOBJECT,

  /* the following reserved ids should vanish soon */
  G_TYPE_GTK_OBJECT,
  G_TYPE_GTK_ARG,
  G_TYPE_BSE_OBJECT,
  G_TYPE_BSE_PARAM,

  G_TYPE_LAST_RESERVED_FUNDAMENTAL
} GTypeFundamentals;

/* Type Checking Macros
 */
#define G_TYPE_FUNDAMENTAL(type)                ((type) & 0xff)
#define G_TYPE_DERIVE_ID(ptype, branch_seqno)	(G_TYPE_FUNDAMENTAL (ptype) | ((branch_seqno) << 8))
#define G_TYPE_BRANCH_SEQNO(type)		((type) >> 8)
#define G_TYPE_FUNDAMENTAL_LAST	                ((GType) _g_type_fundamental_last)
#define	G_TYPE_IS_INTERFACE(type)		(G_TYPE_FUNDAMENTAL (type) == G_TYPE_INTERFACE)
#define	G_TYPE_IS_CLASSED(type)			(g_type_check_flags ((type), G_TYPE_FLAG_CLASSED))
#define	G_TYPE_IS_INSTANTIATABLE(type)		(g_type_check_flags ((type), G_TYPE_FLAG_INSTANTIATABLE))
#define	G_TYPE_IS_DERIVABLE(type)		(g_type_check_flags ((type), G_TYPE_FLAG_DERIVABLE))
#define	G_TYPE_IS_DEEP_DERIVABLE(type)		(g_type_check_flags ((type), G_TYPE_FLAG_DEEP_DERIVABLE))
#define	G_TYPE_IS_PARAM(type)			(G_TYPE_FUNDAMENTAL (type) == G_TYPE_PARAM)

/* Typedefs
 */
typedef guint32				GType;
typedef struct _GParam    		GParam;
typedef struct _GTypePlugin    		GTypePlugin;
typedef struct _GTypePluginVTable	GTypePluginVTable;
typedef struct _GTypeClass		GTypeClass;
typedef struct _GTypeInterface		GTypeInterface;
typedef struct _GTypeInstance		GTypeInstance;
typedef struct _GTypeInfo     		GTypeInfo;
typedef struct _GTypeFundamentalInfo	GTypeFundamentalInfo;
typedef struct _GInterfaceInfo		GInterfaceInfo;

/* Basic Type Structures
 */
struct _GTypeClass
{
  GType g_type;
};
struct _GTypeInstance
{
  GTypeClass *g_class;
};
struct _GTypeInterface
{
  GType g_type;		/* iface type */
  GType g_instance_type;
};

/* Casts, Checks And Convenience Macros For Structured Types
 */
#define	G_TYPE_CHECK_INSTANCE_CAST(instance, g_type, c_type)	(_G_TYPE_CIC ((instance), (g_type), c_type))
#define	G_TYPE_CHECK_CLASS_CAST(g_class, g_type, c_type)	(_G_TYPE_CCC ((g_class), (g_type), c_type))
#define	G_TYPE_CHECK_INSTANCE_TYPE(instance, g_type)		(_G_TYPE_CIT ((instance), (g_type)))
#define	G_TYPE_CHECK_CLASS_TYPE(g_class, g_type)		(_G_TYPE_CCT ((g_class), (g_type)))
#define	G_TYPE_INSTANCE_GET_CLASS(instance, c_type)		(_G_TYPE_IGC ((instance), c_type))
#define	G_TYPE_FROM_INSTANCE(instance)				(G_TYPE_FROM_CLASS (((GTypeInstance*) (instance))->g_class))
#define	G_TYPE_FROM_CLASS(g_class)				(((GTypeClass*) (g_class))->g_type)

/* --- prototypes --- */
void	 g_type_init			(void);
gchar*	 g_type_name			(GType			 type);
GQuark	 g_type_qname			(GType			 type);
GType	 g_type_from_name		(const gchar		*name);
GType	 g_type_parent			(GType			 type);
GType	 g_type_next_base		(GType			 type,
					 GType			 base_type);
gboolean g_type_is_a			(GType			 type,
					 GType			 is_a_type);
gboolean g_type_conforms_to		(GType			 type,
					 GType			 iface_type);
guint	 g_type_fundamental_branch_last	(GType			 type);
gpointer g_type_class_ref		(GType			 type);
gpointer g_type_class_peek		(GType			 type);
void	 g_type_class_unref		(gpointer		 g_class);
gpointer g_type_class_peek_parent	(gpointer		 g_class);
gpointer g_type_interface_peek		(gpointer		 instance_class,
					 GType			 iface_type);
/* g_free() the returned arrays */
GType*   g_type_children		(GType			 type,
					 guint                  *n_children);
GType*	 g_type_interfaces		(GType			 type,
					 guint			*n_interfaces);
/* per-type *static* data */
void	 g_type_set_qdata		(GType			 type,
					 GQuark			 quark,
					 gpointer		 data);
gpointer g_type_get_qdata		(GType			 type,
					 GQuark			 quark);
					  

/* --- type registration --- */
typedef void   (*GBaseInitFunc)		(gpointer	g_class);
typedef void   (*GBaseDestroyFunc)	(gpointer	g_class);
typedef void   (*GClassInitFunc)	(gpointer	g_class,
					 gpointer	class_data);
typedef void   (*GClassDestroyFunc)	(gpointer	g_class,
					 gpointer	class_data);
typedef void   (*GInstanceInitFunc)	(GTypeInstance *instance,
					 gpointer       g_class);
typedef void   (*GInterfaceInitFunc)	(gpointer	g_iface,
					 gpointer       iface_data);
typedef void   (*GInterfaceDestroyFunc)	(gpointer       g_iface,
					 gpointer       iface_data);
typedef gchar* (*GTypeParamCollector)	(GParam	       *param,
					 guint          n_bytes,
					 guint8        *bytes);
typedef void (*GTypePluginRef)		     (GTypePlugin    *plugin);
typedef void (*GTypePluginUnRef)	     (GTypePlugin    *plugin);
typedef void (*GTypePluginFillTypeInfo)      (GTypePlugin    *plugin,
					      GType           g_type,
					      GTypeInfo      *info);
typedef void (*GTypePluginFillInterfaceInfo) (GTypePlugin    *plugin,
					      GType           interface_type,
					      GType           instance_type,
					      GInterfaceInfo *info);
struct _GTypePlugin
{
  GTypePluginVTable	*vtable;
};
struct _GTypePluginVTable
{
  GTypePluginRef		plugin_ref;
  GTypePluginUnRef		plugin_unref;
  GTypePluginFillTypeInfo	complete_type_info;
  GTypePluginFillInterfaceInfo	complete_interface_info;
};
typedef enum
{
  G_TYPE_FLAG_CLASSED		= (1 << 0),
  G_TYPE_FLAG_INSTANTIATABLE	= (1 << 1),
  G_TYPE_FLAG_DERIVABLE		= (1 << 2),
  G_TYPE_FLAG_DEEP_DERIVABLE	= (1 << 3)
} GTypeFlags;
struct _GTypeInfo
{
  /* interface types, classed types, instantiated types */
  guint16		class_size;

  GBaseInitFunc		base_init;
  GBaseDestroyFunc	base_destroy;

  /* classed types, instantiated types */
  GClassInitFunc	class_init;
  GClassDestroyFunc	class_destroy;
  gconstpointer		class_data;

  /* instantiated types */
  guint16		instance_size;
  guint16		n_preallocs;
  GInstanceInitFunc	instance_init;
};
struct _GTypeFundamentalInfo
{
  GTypeFlags	      type_flags;
  guint               n_collect_bytes;
  GTypeParamCollector param_collector;
};
struct _GInterfaceInfo
{
  GInterfaceInitFunc	interface_init;
  GInterfaceDestroyFunc	interface_destroy;
  gpointer		interface_data;
};
GType g_type_register_static	   (GType           	        parent_type,
				    const gchar       	       *type_name,
				    const GTypeInfo 	       *info);
GType g_type_register_dynamic	   (GType           	        parent_type,
				    const gchar       	       *type_name,
				    GTypePlugin        	       *plugin);
GType g_type_register_fundamental  (GType           	        type_id,
				    const gchar       	       *type_name,
				    const GTypeFundamentalInfo *finfo,
				    const GTypeInfo 	       *info);
void  g_type_add_interface_static  (GType		        instance_type,
				    GType		        interface_type,
				    GInterfaceInfo	       *info);
void  g_type_add_interface_dynamic (GType		        instance_type,
				    GType		        interface_type,
				    GTypePlugin		       *plugin);


/* --- implementation details --- */
gboolean	g_type_class_is_a		(GTypeClass	*g_class,
						 GType		 is_a_type);
GTypeClass*	g_type_check_class_cast		(GTypeClass	*g_class,
						 GType		 is_a_type);
GTypeInstance*	g_type_check_instance_cast	(GTypeInstance	*instance,
						 GType		 iface_type);
gboolean	g_type_instance_conforms_to	(GTypeInstance	*instance,
						 GType		 iface_type);
gboolean	g_type_check_flags		(GType		 type,
						 GTypeFlags	 flags);
gboolean	g_type_is_dynamic		(GType		 type,
						 GTypeFlags	 flags);
GTypeInstance*	g_type_create_instance		(GType		 type);
void		g_type_free_instance		(GTypeInstance	*instance);

#ifndef G_DISABLE_CAST_CHECKS
#  define _G_TYPE_CIC(ip, gt, ct) \
    ((ct*) g_type_check_instance_cast ((GTypeInstance*) ip, gt))
#  define _G_TYPE_CCC(cp, gt, ct) \
    ((ct*) g_type_check_class_cast ((GTypeClass*) cp, gt))
# else /* G_DISABLE_CAST_CHECKS */
#  define _G_TYPE_CIC(ip, gt, ct)	((ct*) ip)
#  define _G_TYPE_CCC(cp, gt, ct)	((ct*) cp)
#endif /* G_DISABLE_CAST_CHECKS */
#define	_G_TYPE_IGC(ip, ct)		((ct*) (((GTypeInstance*) ip)->g_class))
#define _G_TYPE_CIT(ip, gt)		(g_type_instance_conforms_to ((GTypeInstance*) ip, gt))
#define _G_TYPE_CCT(cp, gt)		(g_type_class_is_a ((GTypeClass*) cp, gt))
extern GType    _g_type_fundamental_last;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __G_TYPE_H__ */
