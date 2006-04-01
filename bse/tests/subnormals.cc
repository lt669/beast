/* BSE - Bedevilled Sound Engine
 * Copyright (C) 2006 Tim Janik
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
#include <bse/bse.h>
#include <bse/bseieee754.h>

#if 0
inline float    test1 (float v) { return v;     }
inline float    test2 (float v) { return bse_float_zap_denormal (v); }
inline float    test3 (float v) { BSE_FLOAT_FLUSH_with_cond (v); return v; }
inline float    test4 (float v) { BSE_FLOAT_FLUSH_with_if (v); return v; }
inline float    test5 (float v) { BSE_FLOAT_FLUSH_with_threshold (v); return v; }
#else
extern float    test1 (float v);
extern float    test2 (float v);
extern float    test3 (float v);
extern float    test4 (float v);
extern float    test5 (float v);
#endif

int
main (int   argc,
      char *argv[])
{
  birnet_init (&argc, &argv, NULL);

  const float max_sub = BSE_FLOAT_MAX_SUBNORMAL;

  float n = 10 * 1000000;
  float sum;
  GTimer *timer = g_timer_new();
  volatile double volatile_accu = 0;

  int j;
  const int blen = 4096;
  volatile float buffer[blen];

  sum = j = 0;
  memset ((void*) buffer, 0, sizeof (buffer));
  g_timer_start (timer);
  for (float i = 0; i <= n; i += 1)
    {
      float v = max_sub * i / n;
      buffer[j++] = test1 (v);
      buffer[j++] = test1 (-v);
      j %= blen;
    }
  volatile_accu += sum;
  g_timer_stop (timer);
  float test1_time = g_timer_elapsed (timer, NULL);

  sum = j = 0;
  memset ((void*) buffer, 0, sizeof (buffer));
  g_timer_start (timer);
  for (float i = 0; i <= n; i += 1)
    {
      float v = max_sub * i / n;
      buffer[j++] = test2 (v);
      buffer[j++] = test2 (-v);
      j %= blen;
    }
  volatile_accu += sum;
  g_timer_stop (timer);
  float test2_time = g_timer_elapsed (timer, NULL);

  sum = j = 0;
  memset ((void*) buffer, 0, sizeof (buffer));
  g_timer_start (timer);
  for (float i = 0; i <= n; i += 1)
    {
      float v = max_sub * i / n;
      buffer[j++] = test3 (v);
      buffer[j++] = test3 (-v);
      j %= blen;
    }
  volatile_accu += sum;
  g_timer_stop (timer);
  float test3_time = g_timer_elapsed (timer, NULL);

  sum = j = 0;
  memset ((void*) buffer, 0, sizeof (buffer));
  g_timer_start (timer);
  for (float i = 0; i <= n; i += 1)
    {
      float v = max_sub * i / n;
      buffer[j++] = test4 (v);
      buffer[j++] = test4 (-v);
      j %= blen;
    }
  volatile_accu += sum;
  g_timer_stop (timer);
  float test4_time = g_timer_elapsed (timer, NULL);

  sum = j = 0;
  memset ((void*) buffer, 0, sizeof (buffer));
  g_timer_start (timer);
  for (float i = 0; i <= n; i += 1)
    {
      float v = max_sub * i / n;
      buffer[j++] = test5 (v);
      buffer[j++] = test5 (-v);
      j %= blen;
    }
  volatile_accu += sum;
  g_timer_stop (timer);
  float test5_time = g_timer_elapsed (timer, NULL);

  g_print ("subnormal cancellation times: keep=%fs zap=%fs inlined-cond=%fs if-cond=%fs arithmetic=%f\n",
           test1_time, test2_time, test3_time, test4_time, test5_time);

  return 0;
}
