#include <chrono>
#include <cstring>
#include <iostream>
#include <mpi.h>
#include "include/candidates.h"
#include "include/load_files.h"
#include "include/sched_single.h"
#include "include/sched_mpi.h"

using namespace std;
using namespace std::chrono;

int main(int argc, char** argv) {
  bool use_bloom = false;
  if (argc == 4) {
    if (strcmp(argv[3], "--use-bloom") == 0) {
      use_bloom = true;
    } else {
      cout << "Usage: " << argv[0]
           << " <dictionary> <word_list> [--use-bloom]" << endl;
      return 1;
    }
  } else if (argc != 3) {
    cout << "Usage: " << argv[0]
         << " <dictionary> <word_list> [--use-bloom]" << endl;
    return 1;
  }

  auto start = high_resolution_clock::now();  // Start timing

  // Load dictionary and word list
  char* filename_dict = argv[1];
  char* filename_word_list = argv[2];

  cout << "[*] Using dictionary: " << filename_dict << endl;
  cout << "[*] Using word list: " << filename_word_list << endl;
  cout << "[*] Bloom filter: " << (use_bloom ? "enabled" : "disabled") << endl;

  size_t dict_size, word_list_size;
  char* dict_text = LoadFile(filename_dict, &dict_size);
  char* word_list_text = LoadFile(filename_word_list, &word_list_size);

  int provided = 0, initialized = 0;
  MPI_Initialized(&initialized);
  if (!initialized) {
    MPI_Init_thread(nullptr, nullptr, MPI_THREAD_FUNNELED, &provided);
  }
  int world_size = 1, world_rank = 0;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  // Chose scheduling method
  Misspelled misspelled;
  if (world_size > 1) {
    misspelled =
        RunMpi(dict_text, dict_size, word_list_text, word_list_size, use_bloom);
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
  cout << "[*] Output: " << out_filename << endl;
  cout << "[*] Wall time: " << minutes << " mins " << seconds << " secs "
       << milliseconds << " ms"
       << "; " << duration << endl;

  MPI_Barrier(MPI_COMM_WORLD);
  int finalized = 0;
  MPI_Finalized(&finalized);
  if (!finalized) MPI_Finalize();

  return 0;
}
