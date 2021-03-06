// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#ifndef __BST_PATTERN_COLUMNS_H__
#define __BST_PATTERN_COLUMNS_H__

#include "bstutils.hh"

/* --- enums --- */
typedef enum /*< skip >*/
{
  BST_PATTERN_NONE                = 0,
  /* events */
  BST_PATTERN_REMOVE_EVENTS,
  BST_PATTERN_SET_NOTE,           /* #note */
  BST_PATTERN_SET_OCTAVE,         /* #octave */
  BST_PATTERN_NUMERIC_CHANGE,     /* -32 .. +32 */
  BST_PATTERN_SET_DIGIT,          /* 0 .. +32 */
  BST_PATTERN_MASK_ACTION         = 0x000000ff,
  /* base octave */
  BST_PATTERN_SET_BASE_OCTAVE     = 0x1 << 8,
  BST_PATTERN_CHANGE_BASE_OCTAVE  = 0x2 << 8,
  BST_PATTERN_MASK_CONTROLS       = 0x0000ff00,
  /* focus movement */
  BST_PATTERN_MOVE_LEFT           = 0x1 << 16,
  BST_PATTERN_MOVE_RIGHT          = 0x2 << 16,
  BST_PATTERN_MOVE_UP             = 0x3 << 16,
  BST_PATTERN_MOVE_DOWN           = 0x4 << 16,
  BST_PATTERN_PAGE_LEFT           = 0x5 << 16,
  BST_PATTERN_PAGE_RIGHT          = 0x6 << 16,
  BST_PATTERN_PAGE_UP             = 0x7 << 16,
  BST_PATTERN_PAGE_DOWN           = 0x8 << 16,
  BST_PATTERN_JUMP_LEFT           = 0x9 << 16,
  BST_PATTERN_JUMP_RIGHT          = 0xa << 16,
  BST_PATTERN_JUMP_TOP            = 0xb << 16,
  BST_PATTERN_JUMP_BOTTOM         = 0xc << 16,
  BST_PATTERN_MOVE_NEXT           = 0xd << 16,
  BST_PATTERN_SET_STEP_WIDTH      = 0xe << 16,
  BST_PATTERN_MASK_MOVEMENT       = 0x00ff0000,
} BstPatternFunction;
typedef enum /*< skip >*/
{
  BST_PATTERN_COLUMN_GC_TEXT0,
  BST_PATTERN_COLUMN_GC_TEXT1,
  BST_PATTERN_COLUMN_GC_VBAR,
  BST_PATTERN_COLUMN_GC_LAST
} BstPatternColumnCellGcType;
typedef enum {
  BST_PATTERN_LTYPE_SPACE,
  BST_PATTERN_LTYPE_NOTE,       /* plus #channel */
  BST_PATTERN_LTYPE_OFFSET,     /* plus #channel */
  BST_PATTERN_LTYPE_LENGTH,     /* plus #channel */
  BST_PATTERN_LTYPE_VELOCITY,   /* plus #channel */
  BST_PATTERN_LTYPE_FINE_TUNE,  /* plus #channel */
  BST_PATTERN_LTYPE_CONTROL,    /* plus #control */
  BST_PATTERN_LTYPE_BAR,
  BST_PATTERN_LTYPE_DBAR,
} BstPatternLType;
typedef enum {
  BST_PATTERN_LFLAG_DIGIT_1     = 0 << 0,
  BST_PATTERN_LFLAG_DIGIT_2     = 1 << 0,
  BST_PATTERN_LFLAG_DIGIT_3     = 2 << 0,
  BST_PATTERN_LFLAG_DIGIT_4     = 3 << 0,
  BST_PATTERN_LFLAG_DIGIT_MASK  = (3 << 0),
  BST_PATTERN_LFLAG_DEC         = 0 << 2,
  BST_PATTERN_LFLAG_HEX         = 1 << 2,
  BST_PATTERN_LFLAG_NUM_MASK    = (1 << 2),
  BST_PATTERN_LFLAG_SIGNED      = 1 << 5,
  BST_PATTERN_LFLAG_LFOLD       = 1 << 6,
  BST_PATTERN_LFLAG_RFOLD       = 1 << 7,
  BST_PATTERN_LFLAG_COL1        = 0 << 8,
  BST_PATTERN_LFLAG_COL2        = 1 << 8,
  BST_PATTERN_LFLAG_COL3        = 2 << 8,
  BST_PATTERN_LFLAG_COL4        = 3 << 8,
  BST_PATTERN_LFLAG_COL_MASK    = (3 << 8),
} BstPatternLFlags;

/* --- typedefs & structures --- */
typedef struct _BstPatternView        BstPatternView;
typedef struct _BstPatternColumn      BstPatternColumn;
typedef struct _BstPatternColumnClass BstPatternColumnClass;
struct _BstPatternColumn
{
  BstPatternColumnClass *klass;
  gint                   num;
  BstPatternLType        ltype;
  BstPatternLFlags       lflags;
  int                    n_focus_positions;
  /* fields private to BstPatternView */
  int                    x;
  int                    width;
  int                    focus_base;
};
struct _BstPatternColumnClass
{
  int                     n_focus_positions;
  guint                   instance_size;
  void                  (*init)                 (BstPatternColumn       *self);
  PangoFontDescription* (*create_font_desc)     (BstPatternColumn       *self);
  guint                 (*width_request)        (BstPatternColumn       *self,
                                                 BstPatternView         *pview,
                                                 GdkWindow              *drawable,
                                                 PangoLayout            *pango_layout,
                                                 guint                   duration);
  void                  (*draw_cell)            (BstPatternColumn       *self,
                                                 BstPatternView         *pview,
                                                 GdkWindow              *drawable,
                                                 PangoLayout            *pango_layout,
                                                 guint                   tick,
                                                 guint                   duration,
                                                 GdkRectangle           *cell_rect,
                                                 GdkRectangle           *expose_area,
                                                 GdkGC                  *gcs[BST_PATTERN_COLUMN_GC_LAST]);
  void                  (*get_focus_pos)        (BstPatternColumn       *self,
                                                 BstPatternView         *pview,
                                                 GdkWindow              *drawable,
                                                 PangoLayout            *pango_layout,
                                                 guint                   tick,
                                                 guint                   duration,
                                                 GdkRectangle           *cell_rect,
                                                 gint                    focus_pos,
                                                 gint                   *pos_x,
                                                 gint                   *pos_width);
  guint                   collision_group;
  gboolean              (*key_event)            (BstPatternColumn       *self,
                                                 BstPatternView         *pview,
                                                 GdkWindow              *drawable,
                                                 PangoLayout            *pango_layout,
                                                 guint                   tick,
                                                 guint                   duration,
                                                 GdkRectangle           *cell_rect,
                                                 gint                    focus_pos,
                                                 guint                   keyval,
                                                 GdkModifierType         modifier,
                                                 BstPatternFunction      action,
                                                 gdouble                 param,
                                                 BstPatternFunction     *movement);
  void                  (*finalize)             (BstPatternColumn       *self);
};

void              bst_pattern_column_layouter_popup (BstPatternView   *pview);
const gchar*      bst_pattern_layout_parse_column   (const gchar      *string,
                                                     BstPatternLType  *ltype,
                                                     gint             *num,
                                                     BstPatternLFlags *flags);
BstPatternColumn* bst_pattern_column_create         (BstPatternLType   ltype,
                                                     gint              num,
                                                     BstPatternLFlags  lflags);
gboolean          bst_pattern_column_has_notes      (BstPatternColumn *column);

// == Flags Enumeration Operators in C++ ==
#ifdef __cplusplus
constexpr BstPatternLFlags  operator&  (BstPatternLFlags  s1, BstPatternLFlags s2) { return BstPatternLFlags (s1 & (long long unsigned) s2); }
inline    BstPatternLFlags& operator&= (BstPatternLFlags &s1, BstPatternLFlags s2) { s1 = s1 & s2; return s1; }
constexpr BstPatternLFlags  operator|  (BstPatternLFlags  s1, BstPatternLFlags s2) { return BstPatternLFlags (s1 | (long long unsigned) s2); }
inline    BstPatternLFlags& operator|= (BstPatternLFlags &s1, BstPatternLFlags s2) { s1 = s1 | s2; return s1; }
constexpr BstPatternLFlags  operator~  (BstPatternLFlags  s1)                      { return BstPatternLFlags (~(long long unsigned) s1); }
constexpr BstPatternFunction  operator&  (BstPatternFunction  s1, BstPatternFunction s2) { return BstPatternFunction (s1 & (long long unsigned) s2); }
inline    BstPatternFunction& operator&= (BstPatternFunction &s1, BstPatternFunction s2) { s1 = s1 & s2; return s1; }
constexpr BstPatternFunction  operator|  (BstPatternFunction  s1, BstPatternFunction s2) { return BstPatternFunction (s1 | (long long unsigned) s2); }
inline    BstPatternFunction& operator|= (BstPatternFunction &s1, BstPatternFunction s2) { s1 = s1 | s2; return s1; }
constexpr BstPatternFunction  operator~  (BstPatternFunction  s1)                      { return BstPatternFunction (~(long long unsigned) s1); }
#endif // __cplusplus
#endif /* __BST_PATTERN_COLUMNS_H__ */
