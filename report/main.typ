#import "jacow.typ": jacow, jacow-table

#show: jacow.with(
  title: [
    COMP90025 Project 2: Distributed Spell Checkers with MPI
  ],
  authors: (
    (name: "Bojin Li", at: "sid")
  ),
  affiliations: (
    sid: "1636521"
  ),
)

#import "@preview/unify:0.7.1": unit, num, qty, numrange, qtyrange
#import "@preview/glossy:0.7.0": init-glossary
#import "@preview/physica:0.9.5": *
#import "@preview/lilaq:0.5.0" as lq
#import "@preview/tabut:1.0.2": tabut
#import "@preview/oxifmt:1.0.0": strfmt

= Introduction

Spell check is a fundamental task in text processing that involves matching words against a dictionary and suggesting corrections for misspellings. This project addresses the process of distributed spell checkers. Given a dictionary $D$ containing valid words and a word list $W$ of words to check, the spell checker identifies all words in $W$ that are not in $D$, and for each of these words, generate all valid candidate corrections at edit distance 1.

The edit operations involve tasks including character replacement, deletion, and insertion (transposition is not included). A word is considered matched to the dictionary if either the word itself appears in $D$, or the word with its first letter converted to lowercase appears in $D$. The output must list misspelled words ordered by the number of valid candidates (ascending), with ties broken by lexicographical order using `strcmp`.

The provided sequential implementation uses a Burkhard-Keller tree (BK-tree) @online-bk-tree-impl @online-bk-tree-intro built over the dictionary to search for matches within edit distance 1. While BK-trees are effective for metric space searches, they suffer from high traversal overhead when applied to large dictionaries. @inproceedings-bk-tree On the largest dataset evaluated in this report ($D$ size ≈626k words, $W$ size ≈658k words), the sequential BK-tree implementation required approximately 32 minutes to complete.

This report presents a parallel implementation using MPI that addresses the limitations of the sequential approach through hash-based dictionary sharding and owner-routed communication. The key design principles are:

- *Distributed dictionary storage*: The dictionary is partitioned across processes using consistent hashing based on normalized word keys, ensuring balanced load distribution.
- *Direct ownership routing*: Each word is owned by exactly one process, determined by a hash function. Membership queries and candidate validations are routed directly to the owning process.
- *Bloom filter optimization*: Each process maintains a Bloom filter over its dictionary shard. These filters are exchanged among all processes to enable local pre-filtering of remote candidates, dramatically reducing communication overhead.
- *Overlap communication and computation*: While waiting for responses to outbound requests, processes actively serve incoming requests using non-blocking probes, preventing deadlocks and maintaining high network utilization.

The parallel implementation achieves significant performance improvements over the sequential baseline implementation, with speedups of approximately 19× on 64 processes for the largest dataset when Bloom filtering is enabled.

= Methodology

This section describes the parallel technique implemented, the experimental methodology used to evaluate performance, and the metrics employed to assess the results.

== Parallel Technique

The parallel spell check system consists of five key stages, each designed to distribute tasks efficiently across MPI processes while minimizing communication overhead.

=== Dictionary Construction

The dictionary is read from a file and distributed across processes using a hash-sharding approach. Algorithm 1 shows the parallel dictionary construction procedure.

#figure(
  rect(width: 100%, inset: 1em, 
    align(left)[
      *Algorithm 1.* Parallel Dictionary Construction

      *Procedure* BuildDistributedDictionary($"filename"$, $p$, $"rank"$)
      
      1. Use MPI-IO to read disjoint line-aligned chunks of the dictionary file
      2. Parse local chunk into words
      3. For each word $w$:
         - Compute $"owner" = "GetOwnerRank"(w, p)$
         - If $"owner" = "rank"$: insert $w$ into local hash set
         - Else: add $w$ to exchange buffer for target owner
      4. Execute all-to-all string exchange to send words to their owners
      5. Insert received words into local hash set
      6. For each word $w$ in local hash set:
         - Compute normalized form $w'$ (first letter lowercase)
         - If $w' != w$: also insert $w'$ into local hash set

      *Function* GetOwnerRank($w$, $p$)
      
      1. Compute $w' =$ normalized form of $w$ (first letter lowercase)
      2. Compute $h = "hash"(w')$ using FNV-1a hash function @techreport-fnv
      3. Return $h mod p$
    ]
  ),
  kind: "pseudocode",
  supplement: [Pseudocode],
  caption: [Parallel dictionary construction with hash-based sharding]
)

The hash-based sharding ensures that each word is owned by exactly one process, while the normalization step ensures that both `Apple` and `apple` map to the same owner, satisfying the specification that requires matching with first-letter case-insensitivity.

=== Bloom Filter Setup

To reduce communication overhead when validating remote candidates, each process builds a Bloom filter over its dictionary shard. The Bloom filters from all processes are then gathered using `MPI_Allgather`, allowing each process to pre-filter remote candidates locally before sending network requests.

#figure(
  rect(width: 100%, inset: 1em,
    align(left)[
      *Algorithm 2.* Bloom Filter Setup

      *Procedure* SetupBloomFilters($"dict"$, $p$, $"rank"$)
      
      1. Create local Bloom filter $B_"rank"$ with $k=3$ hash functions
      2. For each word $w$ in local dictionary shard:
         - Insert $w$ into $B_"rank"$
      3. Serialize $B_"rank"$ into byte array
      4. Use MPI_Allgather to exchange Bloom filters:
         - Send local byte array
         - Receive byte arrays from all other processes
      5. Deserialize received byte arrays into Bloom filters $B_0, B_1, ..., B_(p-1)$
    ]
  ),
  kind: "pseudocode",
  supplement: [Pseudocode],
  caption: [Bloom filter construction and exchange]
)

The Bloom filter uses approximately $12 dotproduct p$ bits per word with 3 hash functions, providing a good balance between memory usage and false positive rate (which remains below 1% in our experiments). @article-bloom-filter

=== Membership Checking

For each word in the word list, the system determines whether it exists in the dictionary. This involves routing queries to the appropriate owner process.

#figure(
  rect(width: 100%, inset: 1em,
    align(left)[
      *Algorithm 3.* Parallel Membership Checking

      *Procedure* CheckMembership($W$, $p$, $"rank"$)
      
      1. Initialize result array $R$ and request buffers
      2. For $i = "rank", "rank"+p, "rank"+2p, ...$ while $i < |W|$:
         - Let $w = W[i]$ be the word at index $i$
         - Compute $"owner" = "GetOwnerRank"(w, p)$
         - If $"owner" = "rank"$:
           - Check if $w$ exists in local dictionary hash set
           - Store result in $R[i]$
         - Else:
           - Add $(i, w)$ to request buffer for process $"owner"$
      3. Execute string exchange to send membership requests
      4. For each received request $(i, w)$:
         - Check if $w$ exists in local dictionary hash set
         - Send response $(i, "exists")$ back to requester
      5. Receive all responses and update $R$
      6. Exchange results $R$ among all processes using `MPI_Allgather`
    ]
  ),
  kind: "pseudocode",
  supplement: [Pseudocode],
  caption: [Parallel membership checking with owner routing]
)

The cyclic distribution of words ($i = "hash"("word") mod p$) ensures balanced workload across processes. Each process is responsible for determining membership of words at indices congruent to its rank modulo $p$.

=== Candidate Generation and Validation

For each misspelled word owned by a process, candidates at edit distance 1 are generated and validated against the distributed dictionary.

#figure(
  rect(width: 100%, inset: 1em,
    align(left)[
      *Algorithm 4.* Parallel Candidate Generation and Validation

      *Procedure* GenerateAndValidateCandidates($"misspelled"$, $p$, $"rank"$)
      
      1. Initialize result list and request batches
      2. For each misspelled word $w$ owned by this process:
         - Generate all candidates $C_w$ at edit distance 1:
           - Deletion: remove each character
           - Replacement: replace each character with alphanumeric chars
           - Insertion: insert alphanumeric chars at each position
         - Partition $C_w$ into local and remote candidates:
           - For each candidate $c in C_w$:
             - Compute $"owner" = "GetOwnerRank"(c, p)$
             - If $"owner" = "rank"$: check locally and add to results if valid
             - Else if Bloom filter $B_{"owner"}$ indicates $c$ might exist:
               - Add $c$ to request batch for process $"owner"$
      3. While request batches are non-empty or responses pending:
         - Poll for incoming requests using `MPI_Iprobe`
         - Process any received validation requests immediately
         - Send next batch of candidate validation requests
         - Wait for and process responses
      4. Sort valid candidates for each misspelled word
      5. Return results
    ]
  ),
  kind: "pseudocode",
  supplement: [Pseudocode],
  caption: [Candidate generation with Bloom filtering and request-response protocol]
)

The request-response protocol with non-blocking progress is crucial for preventing deadlocks. While a process waits for responses to its outbound requests, it actively polls for and serves incoming requests from other processes.

== Experimental Setup

=== Platform

All experiments were conducted on the Spartan HPC cluster at the University of Melbourne, specifically on the cascade partition using sapphire nodes. The system uses OpenMPI with the standard compiler flags from the course environment. Jobs were launched using Python scripts with `srun`.

=== Test Data

The primary dataset consists of approximately 626,000 dictionary words and 658,000 words in the word list. Additional smaller datasets (10k-10k, etc.) were used to evaluate behavior at different scales. The dictionary and word list are both sorted in lexicographical order according to `strcmp`.

=== Performance Metrics

The following metrics were recorded for analysis:

- *Wall time* $T(p)$: The total execution time on $p$ processes, measured from program start to completion.
- *Speedup* $S(p) = T(1)/T(p)$: The ratio of execution time on 1 process to execution time on $p$ processes. Note that $T(1)$ refers to the 1-process MPI implementation, not the sequential BK-tree baseline.
- *Efficiency* $E(p) = S(p)/p$: The ratio of speedup to the number of processes, indicating how well the available processors are utilized.
- *Stage timing*: Average, minimum, and maximum execution time across all processes for each major stage:
  - Dictionary build time
  - Bloom filter setup time
  - Word list parsing time  
  - Membership lookup time
  - Candidate phase time
  - Result gathering time
  - Total MPI execution time
- *Bloom filter metrics*: 
  - Total candidates checked against Bloom filters
  - Percentage of candidates rejected by Bloom filters
  - Number of candidates forwarded to owners after Bloom filtering
  - Number of candidates confirmed to exist in dictionary
  - Percentage of forwarded candidates that were confirmed

=== Experimental Factors

The experiments systematically varied the following factors:

- *Number of processes*: $p in {1, 2, 4, 8, 16, 32, 64}$ for the primary dataset (626k-658k)
- *Bloom filter*: Enabled vs. disabled at selected process counts
- *Problem size*: Multiple dataset sizes to observe fixed overheads and identify scaling crossover points

=== Tools Used

- This report was prepared using the Typst @typst.

- Generative AI tools were utilized to assist in language refinement and stylistic polishing.


= Experiments

This section presents the experimental results obtained from running the parallel spell check implementation on the Spartan cluster. The results are organized to show how performance scales with the number of processes and how the Bloom filter optimization affects runtime.

== Performance on 626k Dictionary, 658k Word List

#show table.cell.where(y: 0): strong
#set table(
  stroke: (x, y) => if y == 0 {(bottom: 0.7pt + black)},
  align: (x, y) => (if x > 0 {center} else {left}),
)

#let data_bloom_on = csv("filtered_626k_658k.csv", row-type: dictionary).filter(x => x.at("bloom_enabled") == "1" and ("1","2","4","8","16","32","64").contains(x.at("parallel")))

// Table — Speedup and Efficiency Metrics

#let compute_speedup(data_array) = {
  let t1 = float(data_array.at(0).wall_time_ms_total)/1000
  data_array.map(r => {
    let tp = float(r.wall_time_ms_total)/1000
    let p = int(r.parallel)
    let speedup = t1 / tp
    let efficiency = speedup / p
    (
      p: r.parallel,
      tp: tp,
      speedup: speedup,
      efficiency: efficiency,
    )
  })
}

#let speedup_bloom_on = compute_speedup(data_bloom_on)

#figure(
  table(
    columns: 4,
    [*$p$*], [*$T(p)$ (s)*], [*$S(p)$*], [*$E(p)$*],
    ..speedup_bloom_on.map(row => (
      row.p,
      strfmt("{:.3}", row.tp),
      strfmt("{:.2}", row.speedup),
      strfmt("{:.3}", row.efficiency)
    )).flatten()
  )
) <table-speedup-efficiency>

@table-speedup-efficiency shows the speedup and efficiency metrics computed from the Bloom-enabled runs. The baseline $T(1)$ is taken from the 1-process MPI run. Speedup increases steadily with the number of processes, reaching approximately 19× on 64 processes. Efficiency decreases as expected, from 1.0 at $p=1$ to approximately 0.3 at $p=64$, indicating diminishing returns due to communication overhead and fixed costs.

// Table — Performance with Bloom Filter Enabled

#figure(
  align(
    center,
  )[#table(
      columns: (4.4%, 11%, 13%, 11%, 11%, 18%, 15%, 16%), 
      align: (auto, auto, auto, auto, auto, auto, auto, auto,),
      table.header(
        [$p$],
        rotate(-90deg, reflow: true)[`dict_build` s],
        rotate(-90deg, reflow: true)[`word_list_parse` s],
        rotate(-90deg, reflow: true)[`owner_lookup` s],
        rotate(-90deg, reflow: true)[`bloom_setup` s],
        rotate(-90deg, reflow: true)[`candidate_phase` s],
        rotate(-90deg, reflow: true)[`gather_results` s],
        rotate(-90deg, reflow: true)[`total_mpi` s],
      ),
      table.hline(),
      [1],  [0.339], [0.092], [0.270], [0.107], [#strong[19.170];], [0.008], [#strong[19.9893];],
      [2],  [0.224], [0.094], [0.234], [0.054], [#strong[8.110];],  [0.037], [#strong[8.755];],
      [4],  [0.109], [0.095], [0.153], [0.035], [#strong[3.996];],  [0.024], [#strong[4.415];],
      [8],  [0.085], [0.093], [0.145], [0.063], [#strong[1.898];],  [0.010], [#strong[2.296];],
      [16], [0.112], [0.093], [0.079], [0.138], [#strong[0.930];],  [0.005], [#strong[1.360];],
      [32], [0.179], [0.094], [0.065], [0.039], [#strong[0.503];],  [0.002], [#strong[0.885];],
      [64], [0.511], [0.094], [0.063], [0.078], [#strong[0.307];],  [0.001], [#strong[1.057];],
    )], kind: table,
    caption: [Performance with Bloom Filter Enabled]
) <table-pref-bloom-enabled>

@table-pref-bloom-enabled shows the wall time and stage timings for runs with Bloom filtering enabled. As the number of processes increases from 1 to 64, the total wall time decreases from approximately 20 seconds to just over 1 second, demonstrating significant parallel speedup.

// Table — Performance with Bloom Filter Disabled

#figure(
  align(
    center,
  )[#table(
      columns: (4.4%, 13.19%, 18.68%, 15.38%, 18.68%, 17.58%, 12.09%), 
      align: (auto, auto, auto, auto, auto, auto, auto,),
      table.header(
        [$p$],
        rotate(-90deg, reflow: true)[`dict_build` s],
        rotate(-90deg, reflow: true)[`word_list_parse` s],
        rotate(-90deg, reflow: true)[`owner_lookup` s],
        rotate(-90deg, reflow: true)[`candidate_phase` s],
        rotate(-90deg, reflow: true)[`gather_results` s],
        rotate(-90deg, reflow: true)[`total_mpi` s],
      ),
      table.hline(),
      [1], [0.336], [0.100], [0.289], [#strong[19.795];], [0.009], [#strong[20.531];],
      [4], [0.113], [0.095], [0.172], [#strong[8.596];], [0.025], [#strong[9.003];],
      [8], [0.056], [0.095], [0.114], [#strong[4.852];], [0.012], [#strong[5.131];],
      [16], [0.122], [0.094], [0.085], [#strong[2.587];], [0.006], [#strong[2.895];],
      [32], [0.160], [0.095], [0.065], [#strong[1.497];], [0.003], [#strong[1.822];],
      [64], [0.470], [0.095], [0.058], [#strong[0.928];], [0.001], [#strong[1.554];],
    )], kind: table,
    caption: [Performance with Bloom Filter Disabled]
) <table-pref-bloom-disable>

@table-pref-bloom-disable shows timing results with Bloom filtering disabled. Comparing with Table 1, the candidate phase time is significantly higher without Bloom filtering, particularly at higher process counts, where communication overhead dominates.


// Figure — Speedup vs $p$ (Bloom ON, 626k–658k)

#let data = lq.load-txt(
  read("filtered_626k_658k.csv"),
  delimiter: ",",
  header: true,
)
#let parallel = data.at("parallel")
#let wall_time = data.at("wall_time_ms_total")
#let T1 = wall_time.first()
#let speedup = wall_time.map(t => T1 / t)

#set figure.caption(position: top)
#figure(
  lq.diagram(
    width: 8cm, height: 6cm,
    lq.plot(parallel, speedup, mark: ".", label: [`626k–658k`]),
    lq.plot(parallel, parallel, stroke: (dash: "solid"), label: [Ideal $S(p)=p$]),
    legend: (position: bottom, dx: 1em),
    xlabel: [$p$ (processes)],
    ylabel: [$S(p)$],
    ylim: (0,22),
    title: [Speedup vs Processes]
  ),
  caption: [Speedup vs $p$ (Bloom ON, 626k–658k)],
) <diagram-speedup-vs-p>

@diagram-speedup-vs-p shows the speedup curve compared to the ideal linear speedup. The implementation achieves near-linear speedup up to $p=8$, after which the rate of improvement begins to decrease due to communication overhead and diminishing per-process workload. The curve demonstrates that the parallel implementation provides substantial performance benefits across all tested process counts.

// Figure — Efficiency vs $p$ (Bloom ON, 626k–658k)

#let data = lq.load-txt(
  read("filtered_626k_658k.csv"),
  delimiter: ",",
  header: true,
)
#let parallel = data.at("parallel")
#let wall_time = data.at("wall_time_ms_total")
#let T1 = wall_time.first()
#let efficiency = range(parallel.len()).map(i => T1 / (parallel.at(i) * wall_time.at(i)))

#figure(
  lq.diagram(
    width: 8cm, height: 6cm,
    lq.plot(parallel, efficiency, mark: ".", label: [Efficiency]),
    lq.plot(parallel, parallel.map(_ => 1), mark: none, stroke: (dash: "solid"), label: [Ideal $E(p)=1$]),
    legend: (position: right),
    xlabel: [$p$ (processes)],
    ylabel: [$E(p)$],
    title: [Efficiency vs Processes]
  ),
  caption: [Efficiency vs $p$ (Bloom ON, 626k–658k)]
) <diagram-efficiency-vs-p>
@diagram-efficiency-vs-p illustrates the efficiency metric, which quantifies how effectively the implementation utilizes available processes. Efficiency starts near 1.0 at low process counts and decreases steadily as $p$ increases. This pattern is expected in parallel applications due to increasing communication overhead and fixed sequential components. Despite the decreasing efficiency, the absolute execution time continues to improve, indicating that adding more processes remains beneficial up to $p=64$.

== Analysis of Stage Timings

// Figure — Stage breakdown (626k–658k)

#let data = lq.load-txt(
  read("filtered_626k_658k.csv"),
  delimiter: ",",
  header: true,
)
#let parallel = data.at("parallel")
#let dict_build = data.at("dict_build_avg_s")
#let bloom_setup = data.at("bloom_setup_avg_s")
#let word_list_parse = data.at("word_list_parse_avg_s")
#let owner_lookup = data.at("owner_lookup_avg_s")
#let candidate_phase = data.at("candidate_phase_avg_s")
#let gather_results = data.at("gather_results_avg_s")

#let vadd = (a, b) => a.zip(b).map(((x, y)) => x + y)

#let stacked-area(x, ys, ynames) = {
  let cumulative = ((0,) * x.len(),)
  for y in ys {
    cumulative.push(vadd(cumulative.last(), y))
  }
  range(ys.len()).map(i => lq.fill-between(
    x,
    cumulative.at(i),
    y2: cumulative.at(i + 1),
    label: ynames.at(i),
  ))
}

#figure(
  lq.diagram(
    width: 8cm, height: 8cm,
    xlabel: [$p$ (processes)],
    ylabel: [Stage time (s)],
    ylim: (0,2),
    legend: (position: top, dx:5em),
    title: [Stage breakdown],
    ..stacked-area(
      parallel,
      (dict_build, bloom_setup, word_list_parse, owner_lookup, candidate_phase, gather_results),
      (
        "dict_build",
        "bloom_setup",
        "word_list_parse",
        "owner_lookup",
        "candidate_phase",
        "gather_results",
      ),
    ),
  ),
  caption: [Figure — Stage breakdown (626k–658k)]
) <diagramn-stage-breakdown>

@diagramn-stage-breakdown shows how execution time is distributed across the major stages of the implementation. The candidate phase consistently dominates the execution time, particularly at lower process counts. As $p$ increases, the candidate phase time decreases significantly (from approximately 19s at $p=1$ to 0.3s at $p=64$ with Bloom enabled), indicating good parallelization of this compute-intensive stage.

The dictionary build time initially decreases as $p$ increases (from 0.34s at $p=1$ to 0.06s at $p=8$) as each process reads a smaller portion of the file. However, after $p=8$, the dictionary build time begins to increase again, reaching 0.47s at $p=64$. This non-monotonic behavior suggests I/O contention on the parallel file system when too many processes attempt concurrent file access.

The Bloom setup time remains relatively small and stable across all process counts (approximately 0.04-0.14s), indicating that the overhead of constructing and exchanging Bloom filters is acceptable. The membership lookup and result gathering stages contribute minimally to total execution time, remaining consistently small across all process counts.

== Bloom Filter Effectiveness

// Figure — Bloom rejection rate vs $p$, Bloom ON

#let data = lq.load-txt(
  read("filtered_626k_658k.csv"),
  delimiter: ",",
  header: true,
)

#let parallel = data.at("parallel")
#let rej = data.at("bloom_rejected_pct")
#let conf = data.at("bloom_confirmed_pct")

#figure(
  lq.diagram(
    width: 8cm, height: 6cm,
    lq.plot(parallel, rej, mark: ".", label: [Bloom rejection %]),
    lq.plot(parallel, conf, mark: "+", label: [Bloom confirmed %]),
    
    legend: (position: bottom, dx:5em),
    title: [Bloom filter rates vs Processes],
    xlabel: [$p$ (processes)],
    ylabel: [Rate (%) ],
    lq.rect(0, 50, width: 15, height: 50),
      
    lq.place(
      63%, 80% - 1.2em, 
      align: bottom,
      lq.diagram(
        width: 5cm, height: 2.5cm, 
        ylim: (50, 100),
        xlim: (0, 20),
        fill: white,
        lq.plot(parallel, rej, mark: "."),
        lq.plot(parallel, conf, mark: "+")
      )
    )
  ),
  caption: [Bloom rejection rate vs $p$, (626k–658k)]
) <diagram-bloom-rejection-rate-vs-p>

@diagram-bloom-rejection-rate-vs-p demonstrates the effectiveness of Bloom filtering across different process counts. The rejection rate (blue dots) shows the percentage of candidates that filtered out locally without network communication, while the confirmation rate (red crosses) shows the percentage of forwarded candidates that actually exist in the dictionary. The rejection rate exceeds 98% at all process counts and approaches 99.75% at higher $p$, while the confirmation rate remains consistently above 99%, indicating very low false positive rates.

// Figure — Candidate phase time vs $p$

#let parallel = (1,2,4,8,16,32,64)
#let bloom_on_candidate = (19.1703, 8.1102, 3.9962, 1.8988, 0.9303, 0.5033, 0.3078)
#let bloom_setup = (0.1078, 0.0543, 0.0357, 0.0633, 0.1387, 0.0394, 0.0782)
#let bloom_off_candidate = (19.7959, 15.5767, 8.5968, 4.8529, 2.5870, 1.4979, 0.9288)

#let idx = 6

#figure(
  for idx in range(1,7) {
    lq.diagram(
      width: 2cm,
      legend: (position: top, dy: -5.5em),
    
      lq.bar(
        (parallel.at(idx),0), 
        (bloom_off_candidate.at(idx),0),
        offset: -0.2, width: 0.4, 
      ),
      lq.bar(
        (parallel.at(idx),0), 
        (bloom_on_candidate.at(idx),0),
        offset: 0.2, width: 0.4, 
      ),
      lq.bar(
        (parallel.at(idx),0), 
        (bloom_setup.at(idx),0),
        offset: 0.2, width: 0.4,
      ),
      xlim: (parallel.at(idx)-0.5,parallel.at(idx)+0.5),
      title: str(parallel.at(idx))+" thread" 
    )
  },
  caption: [Candidate phase time (Bloom On vs Off)]
) <diagram-candidate-time-compare>

@diagram-candidate-time-compare compares the candidate phase execution time with Bloom filtering enabled (blue bars + green bars for setup) versus disabled (red bars) across all process counts. The figure clearly shows that Bloom filtering provides substantial time savings, especially at higher process counts. At $p=64$, the candidate phase with Bloom enabled (0.31s + 0.08s setup) is approximately 3× faster than without Bloom filtering (0.93s). The Bloom setup overhead (green bars) is minimal compared to the time saved.

===== *Table 4 — Bloom Filter Statistics*

#tabut(
  data_bloom_on,
  (
    (header: [$p$], func: r => r.parallel),
    (header: [Checked], func: r => strfmt("{:.2e}", float(r.bloom_checked))),
    (header: [Rejected %], func: r => strong(strfmt("{:.2}%", float(r.bloom_rejected_pct)))),
    (header: [Forwarded], func: r => strfmt("{:.2e}", float(r.bloom_forwarded))),
    (header: [Confirmed], func: r => strfmt("{:.2e}", float(r.bloom_confirmed))),
    (header: [Confirmed %], func: r => strong(strfmt("{:.2}%", float(r.bloom_confirmed_pct)))),
  ),
  align: (auto, right, right, right, right, right),
)

Table 4 demonstrates the effectiveness of Bloom filtering. The rejection rate (percentage of candidates filtered out locally) exceeds 98% at $p=2$ and approaches 99.75% at $p=64$. This means that out of millions of candidate checks, only a tiny fraction (fewer than 100,000) require actual network communication. Furthermore, the confirmation rate (percentage of forwarded candidates that actually exist in the dictionary) exceeds 99.9%, indicating a very low false positive rate.

= Discussion

This section analyzes the experimental results and discusses the effectiveness of the parallel implementation, identifies bottlenecks, and considers how performance would scale to larger systems.

== Candidate Phase Scaling

The candidate generation and validation phase was identified as the dominant component of execution time in the sequential implementation, and therefore the primary target for parallelization. The experimental results confirm that this phase scales well with the number of processes.

At $p=1$, the candidate phase requires approximately 19.2 seconds with Bloom filtering enabled. This time drops nearly proportionally with the number of processes: 8.1s at $p=2$, 4.0s at $p=4$, 1.9s at $p=8$, and 0.93s at $p=16$. The near-linear scaling in this range (speedup of approximately $p/log(p)$ for $p <= 16$) demonstrates that the hash-based sharding effectively distributes work evenly across processes, and the request-response protocol with non-blocking progress maintains high utilization of computational resources.

Beyond $p=16$, the scaling rate decreases. At $p=32$ and $p=64$, the candidate phase times are 0.50s and 0.31s, respectively. While still showing improvement, the reduced rate of decrease indicates that fixed costs (such as request batching overhead and synchronization) begin to constitute a larger fraction of total time as the per-process workload shrinks. Additionally, the probability of load imbalance increases with higher $p$, as some processes may finish their assigned portion of the word list earlier than others.

The comparison with Bloom filtering disabled is instructive. Without Bloom filters, the candidate phase at $p=64$ requires 0.93s instead of 0.31s, a 3× difference. This demonstrates that while parallelization provides the primary speedup, the Bloom filter optimization is essential for managing communication overhead at higher process counts.

== Dictionary Build Time Behavior

The dictionary construction stage exhibits interesting non-monotonic behavior. Initially, as $p$ increases from 1 to 8, the dictionary build time decreases from 0.34s to 0.06s. This is the expected behavior as each process reads a smaller portion of the dictionary file using MPI-IO, and the parallel file system can satisfy these independent reads efficiently.

However, beyond $p=8$, the dictionary build time begins to increase, reaching 0.18s at $p=32$ and 0.47s at $p=64$. This increase can be attributed to two factors:

First, MPI-IO contention becomes significant when many processes simultaneously access the same file. The parallel file system's metadata servers must handle a large number of concurrent open, seek, and read operations. When the per-process chunk size becomes small, the overhead of these operations relative to the actual data transfer becomes substantial.

Second, the all-to-all string exchange that distributes words to their owner processes incurs communication overhead that grows with $p$. While the total volume of data exchanged remains constant, the number of messages increases quadratically with $p$, leading to increased latency and contention in the MPI communication subsystem.

Despite this increase, the dictionary build time remains a small fraction of the total execution time even at $p=64$ (less than 1 second compared to total time of approximately 1.5-2.5 seconds). Therefore, while the dictionary build phase does not scale perfectly, it does not prevent overall speedup.

== Membership Checking Phase

The membership checking phase shows good scaling behavior, decreasing from 0.27s at $p=1$ to 0.07-0.15s at higher process counts. The time remains relatively stable across different values of $p$ beyond 16 processes, suggesting that the workload is well-balanced and the communication pattern is efficient.

The cyclic distribution strategy ($i = "rank" mod p$) ensures that each process is responsible for approximately $|W|/p$ membership checks. The hash-based owner routing ensures that queries are distributed uniformly across all processes, preventing any single process from becoming a hotspot.

== Bloom Filter Impact on Communication

The Bloom filter optimization has a dramatic impact on communication overhead, particularly at higher process counts. At $p=64$, the system generates approximately 36.9 million candidate checks. Without Bloom filtering, all candidates destined for remote processes would require network communication—a prohibitive amount of traffic.

With Bloom filtering enabled, 99.75% of these candidates (approximately 36.8 million) are filtered out locally, and only 93,897 candidates are actually sent over the network. Furthermore, of these forwarded candidates, 99.96% are confirmed to exist in the dictionary, indicating a false positive rate of less than 0.04%.

The cost of this optimization is the Bloom filter setup phase, which requires each process to build a filter over its dictionary shard and participate in an `MPI_Allgather` to exchange filters. This phase takes 0.04-0.14 seconds, which is minimal compared to the time saved in the candidate phase (0.93s vs 0.31s at $p=64$, a savings of 0.62s).

An interesting observation is that the Bloom rejection rate increases with $p$. At $p=2$, the rejection rate is 98.7%, while at $p=64$ it reaches 99.75%. This occurs because, as $p$ increases, each process owns a smaller fraction of the dictionary, making it less likely that a randomly generated candidate belongs to any particular process's shard. The Bloom filter efficiently captures this property, filtering out most candidates that do not belong to the target process's shard.

== Efficiency and Fixed Costs

The efficiency metric $E(p) = S(p)/p$ quantifies how effectively the implementation utilizes the available processes. Ideally, efficiency would remain at 1.0 for all $p$, indicating perfect speedup. In practice, efficiency decreases with increasing $p$ due to communication overhead, synchronization costs, and fixed sequential components.

At $p=2$, efficiency is approximately 0.9, indicating minimal overhead. By $p=8$, efficiency has decreased to approximately 0.63, and by $p=64$ it has dropped to approximately 0.30. This pattern is typical for parallel applications as they scale: the marginal benefit of each additional process decreases due to increasing communication-to-computation ratio and Amdahl's Law effects. @online-amdahl-law

The fixed costs that limit efficiency include:
- Word list parsing and broadcasting (performed by rank 0 only)
- Result gathering and serialization (all processes must wait for the slowest)
- Synchronization barriers between stages
- Request batching overhead in the candidate phase

These fixed costs become more significant as $p$ increases and the per-process workload decreases. However, the absolute execution time continues to improve up to $p=64$, indicating that the parallelization is beneficial despite decreasing efficiency.

== Comparison with Sequential BK-tree

The sequential BK-tree implementation provided as a baseline requires approximately 1,949 seconds (32 minutes) to process the 626k-658k dataset. In contrast, the parallel MPI implementation with $p=64$ and Bloom filtering enabled completes in approximately 1.1 seconds—a speedup of approximately 1,770×.

This dramatic improvement comes from two sources. First, the algorithmic change from tree traversal to explicit candidate enumeration with hash table lookups is more efficient for the specific case of edit distance 1. The BK-tree's generality for arbitrary edit distances comes at the cost of higher constant factors for the common case. Second, the parallelization distributes the still-substantial workload across multiple processes, yielding further speedup.

It is important to note that the speedup metrics reported in this paper (maximum 19× at $p=64$) use the 1-process MPI implementation as the baseline, not the sequential BK-tree. This provides a fair comparison of the parallel scaling behavior independent of the algorithmic improvement.

== Design Challenges and Solutions

Several design challenges were encountered during development, requiring careful solutions to ensure correctness and performance:

*Deadlock avoidance*: Early implementations that used blocking sends and receives in both directions could deadlock when two processes simultaneously attempted to send to each other. The solution was to implement a request-response protocol with explicit non-blocking progress: while waiting for responses, each process polls with `MPI_Iprobe` and immediately services any incoming requests. This ensures that forward progress is always possible.

*Message size limitations*: Some MPI implementations impose limits on message sizes (e.g., `INT_MAX` bytes). To handle potentially large result sets, the gathering phase uses a two-step protocol: each process first sends the size of its payload as a 64-bit integer, then sends the actual payload. The root process pre-allocates sufficient space and receives each payload individually.

*Load balance*: The cyclic distribution of word list items ensures that each process is assigned approximately the same number of words to check, preventing load imbalance. The hash-based dictionary sharding using normalized keys (first letter lowercase) ensures that dictionary lookups are distributed uniformly, even for adversarial inputs where many words differ only in first-letter capitalization.

*I/O contention*: The increase in dictionary build time at high $p$ indicates I/O contention. Two mitigations are used: (1) increasing the per-process chunk size estimate to reduce the number of seeks, and (2) pre-reserving hash table capacity based on estimated shard size to reduce rehashing overhead. A more sophisticated approach would be to use group-based I/O, where processes are organized into groups, one process per group reads a larger chunk and distributes it locally, reducing pressure on shared file system metadata.

== Scalability to Larger Systems

Projecting the performance of this implementation to larger systems requires considering multiple factors:

*Compute scalability*: The candidate phase, which dominates execution time, shows good scalability up to $p=64$ on the tested problem size. For larger problem sizes (dictionary and word list sizes of millions of words rather than hundreds of thousands), the per-process workload would remain sufficient to maintain efficiency at higher $p$. The theoretical speedup for this phase approaches $O(p/log(p))$ based on the hash-based distribution and request-response overhead.

*Communication scalability*: The Bloom filter optimization is critical for scalability. Without it, the candidate phase would require $O(n p)$ messages in the worst case, where $n$ is the number of candidates per process. With Bloom filtering, the message count is reduced by 2-3 orders of magnitude, making the communication overhead manageable. However, the `MPI_Allgather` operation used to exchange Bloom filters has complexity $O(p^2)$, which could become a bottleneck at very high $p$ (hundreds or thousands of processes). A hierarchical Bloom filter approach, where processes are organized into groups and filters are exchanged within groups first, could address this limitation.

*I/O scalability*: The dictionary build phase shows signs of I/O contention at $p=64$. On larger systems with more aggressive parallel file systems or with larger per-process chunk sizes (achievable with larger problem sizes), this effect may be less pronounced. However, for very high $p$ (thousands of processes), techniques such as group-based I/O or caching would likely be necessary to prevent I/O from becoming a bottleneck.

*Memory scalability*: Each process stores only its dictionary shard and Bloom filters from all processes. The per-process memory requirement is $O(D/p + p dot B)$, where $D$ is the dictionary size and $B$ is the Bloom filter size per process. For reasonable Bloom filter sizes (e.g., 12 bits per word), the memory requirement scales well with $p$ for fixed $D$. For weak scaling scenarios where both $D$ and $p$ increase proportionally, memory per process remains roughly constant.

In summary, the implementation demonstrates good scalability properties for the tested problem size and process counts. Projecting to larger systems, the candidate phase should continue to scale well, while the dictionary build and Bloom filter exchange phases may require optimization for very high process counts (hundreds to thousands). The Bloom filter optimization is essential for managing communication overhead and would remain critical at larger scales.

#bibliography("references.bib")
