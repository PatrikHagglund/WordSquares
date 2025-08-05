#!/usr/bin/env python3
"""
Script to check if output8x8.txt contains any 8x8 word square solutions 
with all unique words, and clean up "combinations tried" lines.
"""

import re
import sys

def extract_8x8_grids(text):
    """Extract all 8x8 word grids from the text."""
    grids = []
    lines = text.split('\n')
    
    i = 0
    while i < len(lines):
        # Look for 8 consecutive lines that are exactly 8 characters long and contain only letters
        if i + 7 < len(lines):
            potential_grid = []
            valid_grid = True
            
            for j in range(8):
                line = lines[i + j].strip()
                
                # Remove timestamp prefix if present (format: MMDD_HH:MM:SS )
                if re.match(r'^\d{4}_\d{2}:\d{2}:\d{2} ?', line):
                    line = line[14:]  # Remove timestamp and space
                
                if len(line) == 8 and line.isalpha():
                    potential_grid.append(line.upper())
                else:
                    valid_grid = False
                    break
            
            if valid_grid:
                grids.append(potential_grid)
                i += 8
            else:
                i += 1
        else:
            i += 1
    
    return grids

def has_all_unique_words(grid):
    """Check if all words (rows and columns) in the grid are unique."""
    words = set()
    
    # Add all rows
    for row in grid:
        if row in words:
            return False
        words.add(row)
    
    # Add all columns
    for col_idx in range(8):
        col_word = ''.join(grid[row_idx][col_idx] for row_idx in range(8))
        if col_word in words:
            return False
        words.add(col_word)
    
    return True

def count_unique_words(grid):
    """Count the number of unique words in a grid."""
    words = set()
    
    # Add all rows
    words.update(grid)
    
    # Add all columns
    for col_idx in range(8):
        col_word = ''.join(grid[row_idx][col_idx] for row_idx in range(8))
        words.add(col_word)
    
    return len(words)

def main():
    try:
        with open('output8x8.txt', 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except FileNotFoundError:
        print("Error: output8x8.txt not found")
        return 1
    
    # Remove "combinations tried" lines (with or without timestamp prefix)
    content = re.sub(r'^(\d{4}_\d{2}:\d{2}:\d{2} )?Combinations tried:.*$', '', content, flags=re.MULTILINE)
    
    # Extract all 8x8 grids
    grids = extract_8x8_grids(content)
    
    print(f"Found {len(grids)} complete 8x8 word grids")
    
    # Count unique words for each grid
    grid_scores = []
    for i, grid in enumerate(grids):
        unique_count = count_unique_words(grid)
        grid_scores.append((unique_count, i, grid))
    
    # Sort by unique word count (descending)
    grid_scores.sort(reverse=True)
    
    # Show statistics
    max_unique = grid_scores[0][0] if grid_scores else 0
    fully_unique = sum(1 for score, _, _ in grid_scores if score == 16)
    
    print(f"Maximum unique words found: {max_unique}/16")
    print(f"Solutions with all unique words: {fully_unique}")
    
    # Show top 10 solutions with most unique words
    print(f"\nTop 10 solutions with most unique words:")
    for rank, (unique_count, grid_idx, grid) in enumerate(grid_scores[:10]):
        print(f"\nRank {rank + 1}: {unique_count}/16 unique words (Grid {grid_idx + 1})")
        for row in grid:
            print(row)
        
        # Show all words used
        words = set()
        words.update(grid)  # rows
        for col_idx in range(8):
            col_word = ''.join(grid[row_idx][col_idx] for row_idx in range(8))
            words.add(col_word)
        
        duplicates = 16 - len(words)
        if duplicates > 0:
            print(f"Duplicate words: {duplicates}")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())