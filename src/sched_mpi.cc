#include "sched_mpi.h"
#include "sched_single.h"
#include <mpi.h>
#include <iostream>

Misspelled RunMpi(const char* dict_text, size_t dict_size,
                  const char* word_list_text, size_t word_list_size) {
  int rank = 0, size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (rank == 0) {
    std::cout << "[MPI] world_size=" << size << " (rank 0)" << std::endl;
    // TODO: Real MPI implementation
    return RunSingle(dict_text, dict_size, word_list_text, word_list_size);
  }

  // Other ranks do nothing
  return Misspelled{};
}
