/* BseAdder - BSE Adder
 * Copyright (C) 1999 Tim Janik
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "bseadder.h"

#include <bse/bsechunk.h>
#include <bse/bsehunkmixer.h>


/* --- parameters --- */
enum
{
  PARAM_0,
  PARAM_SUBTRACT,
};


/* --- prototypes --- */
static void	 bse_adder_init			(BseAdder	*adder);
static void	 bse_adder_class_init		(BseAdderClass	*class);
static void	 bse_adder_class_destroy	(BseAdderClass	*class);
static void      bse_adder_set_param            (BseAdder	*adder,
						 BseParam       *param);
static void      bse_adder_get_param            (BseAdder	*adder,
						 BseParam       *param);
static BseIcon*	 bse_adder_do_get_icon		(BseObject     	*object);
static void	 bse_adder_do_shutdown		(BseObject     	*object);
static void      bse_adder_prepare               (BseSource      *source,
						 BseIndex        index);
static BseChunk* bse_adder_calc_chunk            (BseSource      *source,
						 guint           ochannel_id);
static void      bse_adder_reset                 (BseSource      *source);


/* --- variables --- */
static BseType           type_id_adder = 0;
static gpointer          parent_class = NULL;
static const BseTypeInfo type_info_adder = {
  sizeof (BseAdderClass),
  
  (BseBaseInitFunc) NULL,
  (BseBaseDestroyFunc) NULL,
  (BseClassInitFunc) bse_adder_class_init,
  (BseClassDestroyFunc) bse_adder_class_destroy,
  NULL /* class_data */,
  
  sizeof (BseAdder),
  0 /* n_preallocs */,
  (BseObjectInitFunc) bse_adder_init,
};


/* --- functions --- */
static void
bse_adder_class_init (BseAdderClass *class)
{
#include "./icons/sub.c"
  BsePixdata sub_pix_data = { SUB_IMAGE_BYTES_PER_PIXEL | BSE_PIXDATA_1BYTE_RLE,
			      SUB_IMAGE_WIDTH, SUB_IMAGE_HEIGHT,
			      SUB_IMAGE_RLE_PIXEL_DATA, };
  BseObjectClass *object_class;
  BseSourceClass *source_class;
  guint ichannel_id, ochannel_id;
    
  parent_class = bse_type_class_peek (BSE_TYPE_SOURCE);
  object_class = BSE_OBJECT_CLASS (class);
  source_class = BSE_SOURCE_CLASS (class);
  
  object_class->set_param = (BseObjectSetParamFunc) bse_adder_set_param;
  object_class->get_param = (BseObjectGetParamFunc) bse_adder_get_param;
  object_class->get_icon = bse_adder_do_get_icon;
  object_class->shutdown = bse_adder_do_shutdown;
  
  source_class->prepare = bse_adder_prepare;
  source_class->calc_chunk = bse_adder_calc_chunk;
  source_class->reset = bse_adder_reset;

  class->sub_icon = bse_icon_from_pixdata (&sub_pix_data);
  
  bse_object_class_add_param (object_class, "Features",
			      PARAM_SUBTRACT,
			      bse_param_spec_bool ("subtract", "Subtract instead",
						   "Use subtraction to combine sample"
						   "values (instead of addition)",
						   TRUE,
						   BSE_PARAM_DEFAULT));

  ichannel_id = bse_source_class_add_ichannel (source_class, "mono_in1", "Mono Input 1", 1, 1);
  g_assert (ichannel_id == BSE_ADDER_ICHANNEL_MONO1);
  ichannel_id = bse_source_class_add_ichannel (source_class, "mono_in2", "Mono Input 2", 1, 1);
  g_assert (ichannel_id == BSE_ADDER_ICHANNEL_MONO2);
  ichannel_id = bse_source_class_add_ichannel (source_class, "mono_in3", "Mono Input 3", 1, 1);
  g_assert (ichannel_id == BSE_ADDER_ICHANNEL_MONO3);
  ichannel_id = bse_source_class_add_ichannel (source_class, "mono_in4", "Mono Input 4", 1, 1);
  g_assert (ichannel_id == BSE_ADDER_ICHANNEL_MONO4);
  ochannel_id = bse_source_class_add_ochannel (source_class, "Adder", "Mono Output", 1);
  g_assert (ochannel_id == BSE_ADDER_OCHANNEL_MONO);
}

static void
bse_adder_class_destroy (BseAdderClass *class)
{
  bse_icon_unref (class->sub_icon);
  class->sub_icon = NULL;
}

static void
bse_adder_init (BseAdder *adder)
{
  adder->mix_buffer = NULL;
  adder->subtract = FALSE;
}

static BseIcon*
bse_adder_do_get_icon (BseObject *object)
{
  BseAdder *adder = BSE_ADDER (object);

  if (adder->subtract)
    return BSE_ADDER_GET_CLASS (adder)->sub_icon;
  else /* chain parent class' handler */
    return BSE_OBJECT_CLASS (parent_class)->get_icon (object);
}

static void
bse_adder_do_shutdown (BseObject *object)
{
  BseAdder *adder;
  
  adder = BSE_ADDER (object);
  
  /* chain parent class' shutdown handler */
  BSE_OBJECT_CLASS (parent_class)->shutdown (object);
}

static void
bse_adder_set_param (BseAdder *adder,
		     BseParam *param)
{
  switch (param->pspec->any.param_id)
    {
    case PARAM_SUBTRACT:
      adder->subtract = param->value.v_bool;
      bse_object_notify_icon_changed (BSE_OBJECT (adder));
      break;
    default:
      g_warning ("%s(\"%s\"): invalid attempt to set parameter \"%s\" of type `%s'",
		 BSE_OBJECT_TYPE_NAME (adder),
		 BSE_OBJECT_NAME (adder),
		 param->pspec->any.name,
		 bse_type_name (param->pspec->type));
      break;
    }
}

static void
bse_adder_get_param (BseAdder *adder,
		     BseParam *param)
{
  switch (param->pspec->any.param_id)
    {
    case PARAM_SUBTRACT:
      param->value.v_bool = adder->subtract;
      break;
    default:
      g_warning ("%s(\"%s\"): invalid attempt to get parameter \"%s\" of type `%s'",
		 BSE_OBJECT_TYPE_NAME (adder),
		 BSE_OBJECT_NAME (adder),
		 param->pspec->any.name,
		 bse_type_name (param->pspec->type));
      break;
    }
}

static void
bse_adder_prepare (BseSource *source,
		  BseIndex   index)
{
  BseAdder *adder = BSE_ADDER (source);

  adder->mix_buffer = g_new (BseMixValue, BSE_TRACK_LENGTH);
  
  /* chain parent class' handler */
  BSE_SOURCE_CLASS (parent_class)->prepare (source, index);
}

static BseChunk*
bse_adder_calc_chunk (BseSource *source,
		     guint      ochannel_id)
{
  BseAdder *adder = BSE_ADDER (source);
  BseMixValue *mv, *bound;
  BseSampleValue *hunk;
  guint c;
  
  g_return_val_if_fail (ochannel_id == BSE_ADDER_OCHANNEL_MONO, NULL);

  if (source->n_inputs == 0)
    return bse_chunk_new_static_zero (1);
  else if (source->n_inputs == 1)
    return bse_source_ref_chunk (source->inputs[0].osource, source->inputs[0].ochannel_id, source->index);

  bound = adder->mix_buffer + BSE_TRACK_LENGTH;
  for (c = BSE_ADDER_ICHANNEL_MONO1; c <= BSE_ADDER_ICHANNEL_MONO4; c++)
    {
      BseSourceInput *input = bse_source_get_input (source, c);

      if (input)
	{
	  BseChunk *chunk = bse_source_ref_chunk (input->osource, input->ochannel_id, source->index);
	  BseSampleValue *s = bse_chunk_complete_hunk (chunk);

	  mv = adder->mix_buffer;
	  do
	    *(mv++) = *(s++);
	  while (mv < bound);

	  bse_chunk_unref (chunk);
	  break;
	}
    }

  if (adder->subtract)
    for (c = c + 1; c <= BSE_ADDER_ICHANNEL_MONO4; c++)
      {
	BseSourceInput *input = bse_source_get_input (source, c);
	
	if (input)
	  {
	    BseChunk *chunk = bse_source_ref_chunk (input->osource, input->ochannel_id, source->index);
	    BseSampleValue *s = bse_chunk_complete_hunk (chunk);
	    
	    mv = adder->mix_buffer;
	    do
	      *(mv++) -= *(s++);
	    while (mv < bound);
	    
	    bse_chunk_unref (chunk);
	  }
      }
  else
    for (c = c + 1; c <= BSE_ADDER_ICHANNEL_MONO4; c++)
      {
	BseSourceInput *input = bse_source_get_input (source, c);
	
	if (input)
	  {
	    BseChunk *chunk = bse_source_ref_chunk (input->osource, input->ochannel_id, source->index);
	    BseSampleValue *s = bse_chunk_complete_hunk (chunk);
	    
	    mv = adder->mix_buffer;
	    do
	      *(mv++) += *(s++);
	    while (mv < bound);
	    
	    bse_chunk_unref (chunk);
	  }
      }
    
  /* clip the mix buffer to output hunk */
  hunk = bse_hunk_alloc (1);
  bse_hunk_clip_mix_buffer (1, hunk, 1.0, adder->mix_buffer);

  return bse_chunk_new_orphan (1, hunk);
}

static void
bse_adder_reset (BseSource *source)
{
  BseAdder *adder = BSE_ADDER (source);
  
  g_free (adder->mix_buffer);
  adder->mix_buffer = NULL;

  /* chain parent class' handler */
  BSE_SOURCE_CLASS (parent_class)->reset (source);
}


/* --- Export to BSE --- */
#include "./icons/sum.c"
BSE_EXPORTS_BEGIN (BSE_PLUGIN_NAME);
BSE_EXPORT_OBJECTS = {
  { &type_id_adder, "BseAdder", "BseSource",
    "BseAdder is a channel adder to sum up incomiong signals",
    &type_info_adder,
    "/Source/Adder",
    { SUM_IMAGE_BYTES_PER_PIXEL | BSE_PIXDATA_1BYTE_RLE,
      SUM_IMAGE_WIDTH, SUM_IMAGE_HEIGHT,
      SUM_IMAGE_RLE_PIXEL_DATA, },
  },
  { NULL, },
};
BSE_EXPORTS_END;
