#include <mpi.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "include/candidates.h"
#include "include/load_files.h"
#include "include/mpi_utils.h"
#include "include/sched_mpi.h"
#include "include/sched_single.h"

using namespace std;
using namespace std::chrono;

namespace {
void ReportTimingMetric(const char* label, double local_time, int rank, int world_size) {
  double sum = 0.0;
  double min = 0.0;
  double max = 0.0;

  MPI_Reduce(&local_time, rank == 0 ? &sum : nullptr, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&local_time, rank == 0 ? &min : nullptr, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce(&local_time, rank == 0 ? &max : nullptr, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    double avg = sum / static_cast<double>(world_size);
    std::cout << "[MPI][timing] " << label << " avg=" << avg << "s min=" << min << "s max=" << max
              << "s" << std::endl;
  }
}
}  // namespace

int main(int argc, char** argv) {
  bool use_bloom = true;
  bool force_mpi = true;

  if (argc < 3) {
    cout << "Usage: " << argv[0] << " <dictionary> <word_list> [--no-bloom] [--no-mpi]" << endl;
    return 1;
  }

  for (int i = 3; i < argc; ++i) {
    if (strcmp(argv[i], "--no-bloom") == 0) {
      use_bloom = false;
    } else if (strcmp(argv[i], "--no-mpi") == 0) {
      force_mpi = false;
    } else {
      cout << "Usage: " << argv[0] << " <dictionary> <word_list> [--no-bloom] [--no-mpi]" << endl;
      return 1;
    }
  }

  auto start = high_resolution_clock::now();  // Start timing

  char* filename_dict = argv[1];
  char* filename_word_list = argv[2];

  int provided = 0, initialized = 0;
  MPI_Initialized(&initialized);
  if (!initialized) {
    MPI_Init_thread(nullptr, nullptr, MPI_THREAD_FUNNELED, &provided);
  }
  int world_size = 1, world_rank = 0;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  if (world_rank == 0) {
    cout << "[*] Using dictionary: " << filename_dict << endl;
    cout << "[*] Using word list: " << filename_word_list << endl;
    cout << "[*] Bloom filter: " << (use_bloom ? "enabled" : "disabled") << endl;
    cout << "[*] Force MPI: " << (force_mpi ? "enabled" : "disabled") << endl;
  }

  size_t dict_size = 0;
  size_t word_list_size = 0;
  char* dict_text = nullptr;
  char* word_list_text = nullptr;

  const bool use_mpi_backend = force_mpi && (world_size > 1);

  double dict_load_start = MPI_Wtime();
  if (use_mpi_backend) {
    dict_text = LoadDictionaryShard(filename_dict, world_rank, world_size, &dict_size);
  } else {
    dict_text = LoadFile(filename_dict, &dict_size);
  }
  double dict_load_time = MPI_Wtime() - dict_load_start;

  double word_list_load_start = MPI_Wtime();
  if (use_mpi_backend) {
    if (world_rank == 0) {
      word_list_text = LoadFile(filename_word_list, &word_list_size);
    }

    unsigned long long wl_bytes = static_cast<unsigned long long>(word_list_size);
    MPI_Bcast(&wl_bytes, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
    word_list_size = static_cast<size_t>(wl_bytes);

    if (word_list_size > 0) {
      if (world_rank != 0) {
        word_list_text = static_cast<char*>(malloc(word_list_size));
      }
      MPI_Bcast(word_list_text, static_cast<int>(word_list_size), MPI_CHAR, 0, MPI_COMM_WORLD);
    }
  } else {
    word_list_text = LoadFile(filename_word_list, &word_list_size);
  }
  double word_list_load_time = MPI_Wtime() - word_list_load_start;

  if (use_mpi_backend) {
    ReportTimingMetric("io_dict_load", dict_load_time, world_rank, world_size);
    ReportTimingMetric("io_word_list", word_list_load_time, world_rank, world_size);
  } else if (world_rank == 0) {
    std::cout << "[timing] io_dict_load " << dict_load_time << "s" << std::endl;
    std::cout << "[timing] io_word_list " << word_list_load_time << "s" << std::endl;
  }

  // Chose scheduling method
  Misspelled misspelled;
  if (use_mpi_backend) {
    misspelled = RunMpi(dict_text, dict_size, word_list_text, word_list_size, use_bloom);
  } else {
    misspelled = RunSingle(dict_text, dict_size, word_list_text, word_list_size);
  }

  const char* out_filename = "results/word_list_misspelled.txt";
  if (world_rank == 0) {
    FILE* outfile = fopen(out_filename, "w");
    SaveMisspelled(misspelled, outfile);
    fclose(outfile);
  }

  free(dict_text);
  free(word_list_text);

  // Show wall time
  auto end = high_resolution_clock::now();  // End timing
  auto duration = duration_cast<milliseconds>(end - start).count();
  // Convert duration to minutes, seconds, and milliseconds
  auto minutes = duration / 60000;
  auto seconds = (duration % 60000) / 1000;
  auto milliseconds = duration % 1000;
  if (world_rank == 0) {
    cout << "[*] Output: " << out_filename << endl;
    cout << "[*] Wall time: " << minutes << " mins " << seconds << " secs " << milliseconds << " ms"
         << "; " << duration << endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);
  int finalized = 0;
  MPI_Finalized(&finalized);
  if (!finalized)
    MPI_Finalize();

  return 0;
}
