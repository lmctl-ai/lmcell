#pragma once

/* Single-thread OpenMP compatibility surface used when PhysiCell is built
 * without libgomp/libomp.  #pragma omp directives are ignored by C++ when
 * OpenMP is disabled; these two queries cover the upstream calls. */
static inline int omp_get_thread_num(void) { return 0; }
static inline int omp_get_num_threads(void) { return 1; }
