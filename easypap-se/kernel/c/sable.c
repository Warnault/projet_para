#include "easypap.h"
#include <omp.h>
#include <stdbool.h>

static long unsigned int *TABLE = NULL;
static long unsigned int *TABLE2 = NULL;

static volatile int changement;

static unsigned long int max_grains;

#define table(i, j) TABLE[(i)*DIM + (j)]
#define table2(i, j) TABLE2[(i)*DIM + (j)]

#define RGB(r, v, b) (((r) << 24 | (v) << 16 | (b) << 8) | 255)

void sable_init ()
{
  TABLE = calloc (DIM * DIM, sizeof (long unsigned int));
  TABLE2 = calloc (DIM * DIM, sizeof (long unsigned int));
}

void sable_finalize ()
{
  free (TABLE);
  free(TABLE2);
}


///////////////////////////// Production d'une image
void sable_refresh_img (){
  unsigned long int max = 0;
  for (int i = 1; i < DIM - 1; i++)
    for (int j = 1; j < DIM - 1; j++) {
      int g = table (i, j);
      int r, v, b;
      r = v = b = 0;
      if (g == 1)
        v = 255;
      else if (g == 2)
        b = 255;
      else if (g == 3)
        r = 255;
      else if (g == 4)
        r = v = b = 255;
      else if (g > 4)
        r = b = 255 - (240 * ((double)g) / (double)max_grains);

      cur_img (i, j) = RGB (r, v, b);
      if (g > max)
        max = g;
    }
  max_grains = max;
}

///////////////////////////// Configurations initiales

static void sable_draw_4partout (void);

void sable_draw (char *param)
{
  // Call function ${kernel}_draw_${param}, or default function (second
  // parameter) if symbol not found
  hooks_draw_helper (param, sable_draw_4partout);
}

void sable_draw_4partout (void)
{
  max_grains = 8;
  for (int i = 1; i < DIM - 1; i++)
    for (int j = 1; j < DIM - 1; j++)
      cur_img (i, j) = table (i, j) = 4;
}

void sable_draw_DIM (void)
{
  max_grains = DIM;
  for (int i = DIM / 4; i < DIM - 1; i += DIM / 4)
    for (int j = DIM / 4; j < DIM - 1; j += DIM / 4)
      cur_img (i, j) = table (i, j) = i * j / 4;
}

void sable_draw_alea (void)
{
  max_grains = 5000;
  for (int i = 0; i<DIM>> 3; i++) {
    int i = 1 + random () % (DIM - 2);
    int j = 1 + random () % (DIM - 2);
    int grains = 1000 + (random () % (4000));
    cur_img (i, j) = table (i, j) = grains;
  }
}

///////////////////////////// Version séquentielle simple (seq)

static inline void compute_new_state (int y, int x){
  if (table (y, x) >= 4) {
    unsigned long int div4 = table (y, x) / 4;
    table (y, x - 1) += div4;
    table (y, x + 1) += div4;
    table (y - 1, x) += div4;
    table (y + 1, x) += div4;
    table (y, x) %= 4;
    changement = 1;
  }
}

static void do_tile (int x, int y, int width, int height, int who){
  PRINT_DEBUG ('c', "tuile [%d-%d][%d-%d] traitée\n", x, x + width - 1, y, y + height - 1);
  monitoring_start_tile (who);
  for (int i = y; i < y + height; i++)
    for (int j = x; j < x + width; j++) {
          compute_new_state (i, j);
    }
  monitoring_end_tile (x, y, width, height, who);
}


// Renvoie le nombre d'itérations effectuées avant stabilisation, ou 0
unsigned sable_compute_seq (unsigned nb_iter){
  for (unsigned it = 1; it <= nb_iter; it++) {
    //#pragma omp critical
    changement = 0;
    // On traite toute l'image en un coup (oui, c'est une grosse tuile)
    do_tile (1, 1, DIM - 2, DIM - 2, 0);
    if (changement == 0)
      return it;
  }
  return 0;
}

///////////////////////////// Version séquentielle tuilée (tiled)

unsigned sable_compute_tiled(unsigned nb_iter){
  for (unsigned it = 1; it <= nb_iter; it++) {
    changement = 0;
    for (int y = 0; y < DIM; y += TILE_SIZE)
      for (int x = 0; x < DIM; x += TILE_SIZE)
        do_tile (x + (x == 0), y + (y == 0),
                 TILE_SIZE - ((x + TILE_SIZE == DIM) + (x == 0)),
                 TILE_SIZE - ((y + TILE_SIZE == DIM) + (y == 0)),
                 0 /* CPU id */);
    if (changement == 0)
      return it;
  }
  return 0;
}


///////////////////////////// Version 4 vagues

static inline int compute_new_state_prof (int y, int x){
  if (table (y, x) >= 4) {
    unsigned long int div4 = table (y, x) / 4;
    table (y, x - 1) += div4;
    table (y, x + 1) += div4;
    table (y - 1, x) += div4;
    table (y + 1, x) += div4;
    table (y, x) %= 4;
    return 1;
  }
  return 0;
}

static int do_tile_prof(int x, int y, int width, int height, int who){
  int c = 0;
  monitoring_start_tile(who);
  for (int i = y; i < y + height; i++)
    for (int j = x; j < x + width; j++){
      c += compute_new_state_prof(i, j);
    }
  monitoring_end_tile(x, y, width, height, who);
  return c;
}

unsigned sable_compute_FourWave(unsigned nb_iter){
  int change;
  for (unsigned it = 1; it <= nb_iter; it++) {
    change = 0;
    #pragma omp parallel
    {
    #pragma omp for schedule(runtime)
    for (int y = 0 ; y < DIM ; y += (TILE_SIZE)*2)
      for (int x = 0 ; x < DIM ; x += (TILE_SIZE)*2){
        int res1 = do_tile_prof(x + (x == 0), y + (y == 0), TILE_SIZE - ((x + TILE_SIZE == DIM) + (x == 0)), TILE_SIZE - ((y + TILE_SIZE == DIM) + (y == 0)), omp_get_thread_num());
        if(res1) change += res1;
      }
    #pragma omp for schedule(runtime)
    for (int y = TILE_SIZE ; y < DIM ; y += (TILE_SIZE)*2)
      for (int x = 0 ; x < DIM ; x += (TILE_SIZE)*2){
        int res2 = do_tile_prof(x + (x == 0), y + (y == 0), TILE_SIZE - ((x + TILE_SIZE == DIM) + (x == 0)), TILE_SIZE - ((y + TILE_SIZE == DIM) + (y == 0)), omp_get_thread_num());
        if(res2) change += res2;
      }
    #pragma omp for schedule(runtime)
    for (int y = TILE_SIZE ; y < DIM ; y += (TILE_SIZE)*2)
      for (int x = TILE_SIZE ; x < DIM ; x += (TILE_SIZE)*2){
        int res3 = do_tile_prof(x + (x == 0), y + (y == 0), TILE_SIZE - ((x + TILE_SIZE == DIM) + (x == 0)), TILE_SIZE - ((y + TILE_SIZE == DIM) + (y == 0)), omp_get_thread_num());
        if(res3) change += res3;
      }
    #pragma omp for schedule(runtime)
    for (int y = 0 ; y < DIM ; y += (TILE_SIZE)*2)
      for (int x = TILE_SIZE ; x < DIM ; x += (TILE_SIZE)*2){        
        int res4 = do_tile_prof(x + (x == 0), y + (y == 0), TILE_SIZE - ((x + TILE_SIZE == DIM) + (x == 0)), TILE_SIZE - ((y + TILE_SIZE == DIM) + (y == 0)), omp_get_thread_num());
        if(res4) change += res4;
      }
    }
    if(change == 0)
      return it;
  }
  return 0;
}

/////////////////Version synchrone ////////////////

void swap_table(){
  long unsigned int *tmp = TABLE;
  TABLE = TABLE2;
  TABLE2 = tmp;
}

static inline int compute_new_state_synch (int y, int x){
    long unsigned int sable = table(x, y);
    long unsigned int top = table(x, y+1);
    long unsigned int down = table(x, y-1);
    long unsigned int left = table(x-1, y);
    long unsigned int right = table(x+1, y);
    table2(x,y) =  sable%4 + top/4 + left/4 + right/4 + down/4;
    if(table2(x,y) != sable)
      return 1;
    return 0;
}

static int do_tile_synch(int x, int y, int width, int height, int who){
  int c = 0;
  monitoring_start_tile (who);
  for (int i = y; i < y + height; i++)
    for (int j = x; j < x + width; j++) {
            c += compute_new_state_synch(i, j);
    }
  monitoring_end_tile (x, y, width, height, who);
  return c;
}

unsigned sable_compute_synch (unsigned nb_iter){
    int change;
  for (unsigned it = 1; it <= nb_iter; it++) {
    change = 0;
    change  = do_tile_synch(1, 1, DIM - 2, DIM - 2, omp_get_thread_num());
    swap_table();
    if (change == 0)
      return it;
  }
  return 0;
}

unsigned sable_compute_tiled_synch(unsigned nb_iter){
  int change;
  for (unsigned it = 1; it <= nb_iter; it++) {
    change = 0;
    #pragma omp parallel for schedule(runtime)
    for (int y = 0; y < DIM; y += TILE_SIZE){
      for (int x = 0; x < DIM; x += TILE_SIZE){
        int res = do_tile_synch (x + (x == 0), y + (y == 0),
                 TILE_SIZE - ((x + TILE_SIZE == DIM) + (x == 0)),
                 TILE_SIZE - ((y + TILE_SIZE == DIM) + (y == 0)),
                 omp_get_thread_num());
        if(res)
          change += res;
      }
    }
    swap_table();
    if (change == 0)
      return it;
  }
  return 0;
}