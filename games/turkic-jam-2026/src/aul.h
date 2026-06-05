#ifndef TJ_AUL_H
#define TJ_AUL_H

/* Aul = the clan's permanent meta-progression. When an heir dies, part of the
 * run's resources is banked here and persists across runs (save). This is the
 * "мета" layer: heroes are temporary, the aul endures. */

typedef struct {
    int supplies, wisdom, glory, deaths;
    /* Last Tamga: a fallen heir leaves a mark on the road; the next heir can
     * walk over it to collect Wisdom/Glory. Persists across heirs. */
    int tamga_pending; /* 1 = a tamga is waiting to be collected */
    int tamga_cell;    /* loop cell index where the heir fell */
    int tamga_wisdom;  /* reward on collection */
    int tamga_glory;
} tj_aul_t;

extern tj_aul_t g_aul;

void tj_aul_load(void); /* read persisted aul into g_aul */
/* Apply death-keep percentages to a finished run and bank into the aul (persisted). */
void tj_aul_add_from_run(int run_supplies, int run_wisdom, int run_glory);
/* A heir fell on `cell` at `circle`: leave the Last Tamga (value from config, persisted). */
void tj_tamga_spawn(int cell, int circle);
void tj_tamga_clear(void); /* collected: clear the pending tamga (persisted) */

#endif /* TJ_AUL_H */
