#ifndef MPI_CANDIDATES_H
#define MPI_CANDIDATES_H

#include <string>
#include <vector>

// Generate all candidates at edit distance = 1 from the given word
// Includes: replacement (one char changed), deletion (one char removed),
//           insertion (one char added)
// Does NOT include: transposition (swap adjacent chars)
std::vector<std::string> GenerateEditDistance1Candidates(const std::string& word);

#endif  // MPI_CANDIDATES_H
