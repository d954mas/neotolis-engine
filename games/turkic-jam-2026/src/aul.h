#ifndef TJ_AUL_H
#define TJ_AUL_H

/* Aul = the clan's permanent meta-progression. When an heir dies, part of the
 * run's resources is banked here and persists across runs (save). This is the
 * "мета" layer: heroes are temporary, the aul endures. */

typedef struct {
    int supplies, wisdom, glory, deaths;
} tj_aul_t;

extern tj_aul_t g_aul;

void tj_aul_load(void); /* read persisted aul into g_aul */
/* Apply death-keep percentages to a finished run and bank into the aul (persisted). */
void tj_aul_add_from_run(int run_supplies, int run_wisdom, int run_glory);

#endif /* TJ_AUL_H */
