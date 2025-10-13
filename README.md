# Project 2 Skeleton with A Sequential Solution

## Usage

### Make
 
#### Spellcheck

```
$ make spellcheck \
    && ./build/spellcheck files/dictionary.spec.txt files/words.spec.txt \
    && cat results/word_list_misspelled.txt

*] Using dictionary: files/dictionary.spec.txt
[*] Using word list: files/words.spec.txt
[*] Building dictionary with bk trees
        Dictionary size: 13
        Dict Chars size 14
        Word list size: 4
        Root len: 5
[*] Getting misspelled words
[*] Number of threads: 8
[*] Output: results/word_list_misspelled.txt
[*] Wall time: 0 mins 0 secs 2 ms; 2
stabX:
appel: appeal apple rappel
tim: Tim time vim
stap: sap snap stab step stop strap tap
```

#### Test

```
$ make test && ./build/test 

[==========] Running 21 tests from 5 test suites.
[----------] Global test environment set-up.
[----------] 1 test from BKTree
[ RUN      ] BKTree.GetCandidates
[       OK ] BKTree.GetCandidates (0 ms)
[----------] 1 test from BKTree (0 ms total)

[----------] 1 test from Candidates
[ RUN      ] Candidates.GetMisspelled
[       OK ] Candidates.GetMisspelled (1 ms)
[----------] 1 test from Candidates (1 ms total)

[----------] 1 test from FileIOTest
[ RUN      ] FileIOTest.SaveMisspelled
[       OK ] FileIOTest.SaveMisspelled (0 ms)
[----------] 1 test from FileIOTest (0 ms total)

[----------] 6 tests from Dictionary
[ RUN      ] Dictionary.ContainsExactWord
[       OK ] Dictionary.ContainsExactWord (0 ms)
[ RUN      ] Dictionary.NotContainExactWord
[       OK ] Dictionary.NotContainExactWord (0 ms)
[ RUN      ] Dictionary.ContainsFirstLetterLowercase
[       OK ] Dictionary.ContainsFirstLetterLowercase (0 ms)
[ RUN      ] Dictionary.NotContainsFirstLetterLowercase
[       OK ] Dictionary.NotContainsFirstLetterLowercase (0 ms)
[ RUN      ] Dictionary.ContainsAllCapitalAnyCapitalization
[       OK ] Dictionary.ContainsAllCapitalAnyCapitalization (0 ms)
[ RUN      ] Dictionary.NotContainsAllCapitalAnyCapitalization
[       OK ] Dictionary.NotContainsAllCapitalAnyCapitalization (0 ms)
[----------] 6 tests from Dictionary (0 ms total)

[----------] 12 tests from EditDistance
[ RUN      ] EditDistance.Empty
[       OK ] EditDistance.Empty (0 ms)
[ RUN      ] EditDistance.Zero
[       OK ] EditDistance.Zero (0 ms)
[ RUN      ] EditDistance.OneSubstitution
[       OK ] EditDistance.OneSubstitution (0 ms)
[ RUN      ] EditDistance.OneDeletion
[       OK ] EditDistance.OneDeletion (0 ms)
[ RUN      ] EditDistance.OneAddition
[       OK ] EditDistance.OneAddition (0 ms)
[ RUN      ] EditDistance.OneAdjacentSwap
[       OK ] EditDistance.OneAdjacentSwap (0 ms)
[ RUN      ] EditDistance.TwoSubstitution
[       OK ] EditDistance.TwoSubstitution (0 ms)
[ RUN      ] EditDistance.TwoDeletion
[       OK ] EditDistance.TwoDeletion (0 ms)
[ RUN      ] EditDistance.TwoAddition
[       OK ] EditDistance.TwoAddition (0 ms)
[ RUN      ] EditDistance.ThreeSubstitution
[       OK ] EditDistance.ThreeSubstitution (0 ms)
[ RUN      ] EditDistance.ThreeDeletion
[       OK ] EditDistance.ThreeDeletion (0 ms)
[ RUN      ] EditDistance.ThreeAddition
[       OK ] EditDistance.ThreeAddition (0 ms)
[----------] 12 tests from EditDistance (0 ms total)

[----------] Global test environment tear-down
[==========] 21 tests from 5 test suites ran. (1 ms total)
[  PASSED  ] 21 tests.
```

### CMake

#### Spellcheck

```
$ mkdir -p build && cd build && cmake .. && cmake --build . && ./spellcheck

Usage: ./build/spellcheck <dictionary> <word_list>
```

#### Test


```
$ mkdir -p build && cd build && cmake .. && cmake --build . && ./test

[==========] Running 21 tests from 5 test suites.
[----------] Global test environment set-up.
[----------] 1 test from BKTree
[ RUN      ] BKTree.GetCandidates
[       OK ] BKTree.GetCandidates (0 ms)
[----------] 1 test from BKTree (0 ms total)

[----------] 1 test from Candidates
[ RUN      ] Candidates.GetMisspelled
[       OK ] Candidates.GetMisspelled (3 ms)
[----------] 1 test from Candidates (3 ms total)

[----------] 1 test from FileIOTest
[ RUN      ] FileIOTest.SaveMisspelled
[       OK ] FileIOTest.SaveMisspelled (0 ms)
[----------] 1 test from FileIOTest (0 ms total)

[----------] 6 tests from Dictionary
[ RUN      ] Dictionary.ContainsExactWord
[       OK ] Dictionary.ContainsExactWord (0 ms)
[ RUN      ] Dictionary.NotContainExactWord
[       OK ] Dictionary.NotContainExactWord (0 ms)
[ RUN      ] Dictionary.ContainsFirstLetterLowercase
[       OK ] Dictionary.ContainsFirstLetterLowercase (0 ms)
[ RUN      ] Dictionary.NotContainsFirstLetterLowercase
[       OK ] Dictionary.NotContainsFirstLetterLowercase (0 ms)
[ RUN      ] Dictionary.ContainsAllCapitalAnyCapitalization
[       OK ] Dictionary.ContainsAllCapitalAnyCapitalization (0 ms)
[ RUN      ] Dictionary.NotContainsAllCapitalAnyCapitalization
[       OK ] Dictionary.NotContainsAllCapitalAnyCapitalization (0 ms)
[----------] 6 tests from Dictionary (0 ms total)

[----------] 12 tests from EditDistance
[ RUN      ] EditDistance.Empty
[       OK ] EditDistance.Empty (0 ms)
[ RUN      ] EditDistance.Zero
[       OK ] EditDistance.Zero (0 ms)
[ RUN      ] EditDistance.OneSubstitution
[       OK ] EditDistance.OneSubstitution (0 ms)
[ RUN      ] EditDistance.OneDeletion
[       OK ] EditDistance.OneDeletion (0 ms)
[ RUN      ] EditDistance.OneAddition
[       OK ] EditDistance.OneAddition (0 ms)
[ RUN      ] EditDistance.OneAdjacentSwap
[       OK ] EditDistance.OneAdjacentSwap (0 ms)
[ RUN      ] EditDistance.TwoSubstitution
[       OK ] EditDistance.TwoSubstitution (0 ms)
[ RUN      ] EditDistance.TwoDeletion
[       OK ] EditDistance.TwoDeletion (0 ms)
[ RUN      ] EditDistance.TwoAddition
[       OK ] EditDistance.TwoAddition (0 ms)
[ RUN      ] EditDistance.ThreeSubstitution
[       OK ] EditDistance.ThreeSubstitution (0 ms)
[ RUN      ] EditDistance.ThreeDeletion
[       OK ] EditDistance.ThreeDeletion (0 ms)
[ RUN      ] EditDistance.ThreeAddition
[       OK ] EditDistance.ThreeAddition (0 ms)
[----------] 12 tests from EditDistance (0 ms total)

[----------] Global test environment tear-down
[==========] 21 tests from 5 test suites ran. (4 ms total)
[  PASSED  ] 21 tests.
```