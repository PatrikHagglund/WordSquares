# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

WordSquares is a C++ application that generates word grids where all rows and columns form valid words. It uses a Trie data structure for efficient word validation and backtracking search to find solutions.

## Build and Run Commands

This project uses standard C++ compilation without a build system:

```bash
# Compile the project
g++ -std=c++23 -O3 -flto=auto -march=native -mtune=native -o wordsquares main.cpp trie.cpp

# Run the solver
./wordsquares
```

## Architecture

### Core Components

- **main.cpp**: Entry point and main search algorithm (`BoxSearch`)
  - Contains configuration parameters at the top (dictionary paths, grid size, frequency filters)
  - Implements recursive backtracking to fill the word grid
  - Handles loading dictionaries and frequency data

- **trie.h/trie.cpp**: Trie data structure for efficient word lookup
  - `Trie` class with 26-way branching for A-Z letters
  - `Iter` inner class for traversing possible next letters
  - Key methods: `add()`, `has()`, `hasIx()`, `decend()`

### Key Configuration Parameters (main.cpp:8-25)

- `DICTIONARY`: Path to word list file (newline-separated words)
- `FREQ_FILTER`: Path to CSV frequency data (word,rank format)
- `SIZE_W`/`SIZE_H`: Grid dimensions (width/height)
- `MIN_FREQ_W`/`MIN_FREQ_H`: Frequency filtering thresholds
- `UNIQUE`: Require all unique words in solution
- `DIAGONALS`: Require diagonals to also be valid words

### Search Algorithm

The `BoxSearch` function uses:
- Horizontal trie for row validation
- Array of vertical tries for column validation
- Backtracking with trie state restoration
- Optional diagonal validation for square grids

### Data Requirements

The solver requires two external files:
1. Dictionary file: One word per line (recommended: Scrabble word list)
2. Frequency file: CSV with word,frequency_rank columns (recommended: NGram data)

Update the `DICTIONARY` and `FREQ_FILTER` paths in main.cpp before compiling.