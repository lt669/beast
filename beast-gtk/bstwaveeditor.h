/* BEAST - Bedevilled Audio System
 * Copyright (C) 1998-1999, 2000-2002 Tim Janik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __BST_WAVE_EDITOR_H__
#define __BST_WAVE_EDITOR_H__

#include	"bstdefs.h"
#include	"bstqsampler.h"
#include	"bstplayback.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* --- Gtk+ type macros --- */
#define	BST_TYPE_WAVE_EDITOR		(bst_wave_editor_get_type ())
#define	BST_WAVE_EDITOR(object)	        (GTK_CHECK_CAST ((object), BST_TYPE_WAVE_EDITOR, BstWaveEditor))
#define	BST_WAVE_EDITOR_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), BST_TYPE_WAVE_EDITOR, BstWaveEditorClass))
#define	BST_IS_WAVE_EDITOR(object)	(GTK_CHECK_TYPE ((object), BST_TYPE_WAVE_EDITOR))
#define	BST_IS_WAVE_EDITOR_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), BST_TYPE_WAVE_EDITOR))
#define BST_WAVE_EDITOR_GET_CLASS(obj)  (GTK_CHECK_GET_CLASS ((obj), BST_TYPE_WAVE_EDITOR, BstWaveEditorClass))


/* --- structures & typedefs --- */
typedef	struct	_BstWaveEditor	BstWaveEditor;
typedef	struct	_BstWaveEditorClass	BstWaveEditorClass;
struct _BstWaveEditor
{
  GtkVBox    parent_object;
  GtkWidget *main_vbox;

  GtkListWrapper    *chunk_wrapper;
  GtkWidget         *tree;
  GtkWidget	    *qsampler_parent;
  GtkWidget	    *gmask_parent;
  GtkAdjustment     *zoom_adjustment;
  GtkAdjustment     *vscale_adjustment;
  guint		     draw_mode;

  /* preview (playback) */
  BstPlayBackHandle *phandle;
  guint		     playback_length;
  guint		     auto_scroll_mode;
  GtkWidget	    *preview_on;
  GtkWidget	    *preview_off;

  BswProxy	     wave;

  /* editable sample view */
  BswProxy	     esample;
  guint              esample_open : 1;
  guint              ignore_playpos : 1;
  GtkWidget	    *qsampler_hscroll;
  GtkWidget	    *qsampler_playpos;
  guint		     n_qsamplers;
  BstQSampler      **qsamplers;
};
struct _BstWaveEditorClass
{
  GtkVBoxClass parent_class;
};


/* --- prototypes --- */
GtkType		bst_wave_editor_get_type	(void);
GtkWidget*	bst_wave_editor_new		(BswProxy	 wave);
void		bst_wave_editor_set_wave	(BstWaveEditor	*self,
						 BswProxy	 wave);
void		bst_wave_editor_rebuild		(BstWaveEditor *wave_editor);
void		bst_wave_editor_set_esample	(BstWaveEditor	*self,
						 BswProxy	 editable_sample);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __BST_WAVE_EDITOR_H__ */
