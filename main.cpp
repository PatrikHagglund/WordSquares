#include "trie.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <set>
#include <chrono>

// Enable threading by default - comment out to disable
#define ENABLE_THREADING

// Enable frequency filtering - comment out to disable
//#define ENABLE_FREQ_FILTER

// Enable WordFeud compatibility pruning during search - comment out to disable
// This significantly improves performance by pruning branches that exceed WordFeud tile limits
#define ENABLE_WORDFEUD_PRUNING

#ifdef ENABLE_THREADING
#include <thread>
#include <mutex>
#include <atomic>
#endif

//Path to the dictionary file
//Recommended source: https://raw.githubusercontent.com/andrewchen3019/wordle/refs/heads/main/Collins%20Scrabble%20Words%20(2019).txt
#define DICTIONARY "WordFeud_ordlista.txt"
//Path to the word frequency file
//Recommended source: https://www.kaggle.com/datasets/wheelercode/dictionary-word-frequency
#define FREQ_FILTER "../../dictionaries/ngram_freq_dict.csv"
//Width of the word grid
#define SIZE_W 15
//Height of the word grid
#define SIZE_H 15
//Filter horizontal words to be in the top-N (or 0 for all words)
#define MIN_FREQ_W 0
//Filter vertical words to be in the top-N (or 0 for all words)
#define MIN_FREQ_H 0
//Only print solutions with all unique words (only for square grids)
#define UNIQUE false
//Diagonals must also be words (only for square grids)
#define DIAGONALS false

//WordFeud letter distribution (includes blanks as wildcards)
static const std::unordered_map<char, int> g_wordfeud_letters = {
  {'A', 9}, {'B', 2}, {'C', 1}, {'D', 5}, {'E', 8}, {'F', 2}, 
  {'G', 3}, {'H', 2}, {'I', 5}, {'J', 1}, {'K', 3}, {'L', 5}, 
  {'M', 3}, {'N', 6}, {'O', 6}, {'P', 2}, {'R', 8}, {'S', 8}, 
  {'T', 9}, {'U', 3}, {'V', 2}, {'X', 1}, {'Y', 1}, {'Z', 1},
  {'Q', 2}, {'W', 2}, {'[', 2} // Q=Å, W=Ä, [=Ö
};
static const int g_wordfeud_blanks = 2;

//Shape mask: true = valid position, false = empty/blocked position
//EDIT THIS MANUALLY to define your custom shape:
//true = letter goes here, false = empty space
static bool g_shape_mask[SIZE_H][SIZE_W] = {
  {true,true,true,true,true,true,true,true,true,true,true,true,true,true,true},
  {true,true,true,true,false,false,false,true,false,false,false,true,true,true,true},
  {true,true,false,false,false,false,false,true,false,false,false,false,false,true,true},
  {true,true,false,false,false,false,false,true,false,false,false,false,false,true,true},
  {true,false,false,false,false,false,false,true,false,false,false,false,false,false,true},
  {true,false,false,false,false,false,false,true,false,false,false,false,false,false,false},
  {true,false,false,false,false,false,false,true,false,false,false,false,false,false,true},
  {true,true,true,true,true,true,true,true,true,true,true,true,true,true,true},
  {true,false,false,false,false,false,false,true,false,false,false,false,false,false,true},
  {true,false,false,false,false,false,false,true,false,false,false,false,false,false,true},
  {true,false,false,false,false,false,false,true,false,false,false,false,false,false,true},
  {true,true,false,false,false,false,false,true,false,false,false,false,false,true,true},
  {true,true,false,false,false,false,false,true,false,false,false,false,false,true,true},
  {true,true,true,true,false,false,false,true,false,false,false,true,true,true,true},
  {true,true,true,true,true,true,true,true,true,true,true,true,true,true,true}
  
};

//Get total number of valid positions in the shape
int GetValidPositions();

//Check if a grid position is valid according to the shape mask
bool IsValidPosition(int pos);

//Get the next valid position after pos (or -1 if none)
int GetNextValidPosition(int pos);

//Check if position is at the start of a horizontal word segment
bool IsHorizontalWordStart(int pos);

//Check if position is at the end of a horizontal word segment
bool IsHorizontalWordEnd(int pos);

//Check if position is at the start of a vertical word segment
bool IsVerticalWordStart(int pos);

//Check if position is at the end of a vertical word segment
bool IsVerticalWordEnd(int pos);

//Get the horizontal word segment containing this position
std::string GetHorizontalSegment(int pos, char* words);

//Get the vertical word segment containing this position
std::string GetVerticalSegment(int pos, char* words);

//Get the expected horizontal word length for a given row
int GetHorizontalWordLength(int row);

//Get the expected vertical word length for a given column
int GetVerticalWordLength(int col);

static const int VTRIE_SIZE = (DIAGONALS ? SIZE_W + 2 : SIZE_W);
static const std::unordered_set<std::string> banned = {
  //Feel free to add words you don't want to see here
};

//Using global variables makes the recursive calls more compact
#ifdef ENABLE_FREQ_FILTER
std::unordered_map<std::string, uint32_t> g_freqs;
#endif
Trie g_trie_w;
Trie g_trie_h;
std::unordered_map<int, Trie> g_tries_by_length;

#ifdef ENABLE_THREADING
std::atomic<uint64_t> g_combinations_tried(0);
std::atomic<int> g_deepest_pos(-1);
std::atomic<int> g_deepest_unique_pos(-1);
std::mutex g_print_mutex;
#else
uint64_t g_combinations_tried = 0;
int g_deepest_pos = -1;
int g_deepest_unique_pos = -1;
#endif

//Timing variables for combinations per second calculation
auto g_start_time = std::chrono::high_resolution_clock::now();
auto g_last_report_time = std::chrono::high_resolution_clock::now();

//Dictionary should be list of words separated by newlines
void LoadDictionary(const char* fname, int length, Trie& trie, int min_freq) {
  std::cout << "Loading Dictionary " << fname << "..." << std::endl;
  int num_words = 0;
  std::ifstream fin(fname);
  std::string line;
  while (std::getline(fin, line)) {
    // Remove carriage return if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    // Handle UTF-8 Swedish characters FIRST, then check length
    std::string processed_line;
    for (size_t i = 0; i < line.size(); ++i) {
      unsigned char c = line[i];
      if (c >= 'a' && c <= 'z') {
        processed_line += (c - 'a' + 'A');
      } else if (c == 0xC3 && i + 1 < line.size()) {
        unsigned char next = line[i + 1];
        if (next == 0x85) { // Å
          processed_line += 'Q';
          ++i; // Skip next byte
        } else if (next == 0x84) { // Ä
          processed_line += 'W';
          ++i; // Skip next byte
        } else if (next == 0x96) { // Ö
          processed_line += '[';
          ++i; // Skip next byte
        } else {
          processed_line += c;
        }
      } else if (c >= 'A' && c <= 'Z') {
        processed_line += c;
      }
    }
    line = processed_line;
    // Now check the length after processing
    if (line.size() != length) { continue; }
#ifdef ENABLE_FREQ_FILTER
    if (g_freqs.size() > 0 && min_freq > 0) {
      const auto& freq = g_freqs.find(line);
      if (freq == g_freqs.end() || freq->second > min_freq) { continue; }
    }
#endif
    if (banned.count(line) != 0) { continue; }
    trie.add(line);
    num_words += 1;
  }
  std::cout << "Loaded " << num_words << " words." << std::endl;
}

//Check if current partial grid has all unique words
bool HasUniqueWords(char* words, int pos) {
  if (!UNIQUE || SIZE_H != SIZE_W) return true;
  
  std::unordered_set<std::string> used_words;
  
  //Check complete horizontal words
  for (int h = 0; h <= pos / SIZE_W; ++h) {
    int row_start = h * SIZE_W;
    int row_end = std::min(row_start + SIZE_W - 1, pos);
    if (row_end - row_start + 1 == SIZE_W) {
      std::string word(words + row_start, SIZE_W);
      if (used_words.count(word)) return false;
      used_words.insert(word);
    }
  }
  
  //Check complete vertical words
  for (int w = 0; w < SIZE_W; ++w) {
    int col_positions = 0;
    for (int h = 0; h < SIZE_H; ++h) {
      if (h * SIZE_W + w <= pos) col_positions++;
      else break;
    }
    if (col_positions == SIZE_H) {
      std::string word;
      for (int h = 0; h < SIZE_H; ++h) {
        word += words[h * SIZE_W + w];
      }
      if (used_words.count(word)) return false;
      used_words.insert(word);
    }
  }
  
  return true;
}

#ifdef ENABLE_FREQ_FILTER
//Frequency list is expecting a sorted 2-column CSV with header
//First column is the word, second column is the frequency
void LoadFreq(const char* fname) {
  std::cout << "Loading Frequency List " << fname << "..." << std::endl;
  int num_words = 0;
  std::ifstream fin(fname);
  std::string line;
  bool first = false;
  while (std::getline(fin, line)) {
    if (first) { first = false; continue; }
    std::string str = line.substr(0, line.find_first_of(','));
    for (auto& c : str) {
      if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
      else if ((unsigned char)c == 0xC5) c = 'Q'; // Å -> Q
      else if ((unsigned char)c == 0xC4) c = 'W'; // Ä -> W
      else if ((unsigned char)c == 0xD6) c = '['; // Ö -> [
    }
    g_freqs[str] = num_words;
    num_words += 1;
  }
  std::cout << "Loaded " << num_words << " words." << std::endl;
}
#endif

//Get total number of valid positions in the shape
int GetValidPositions() {
  int count = 0;
  for (int h = 0; h < SIZE_H; ++h) {
    for (int w = 0; w < SIZE_W; ++w) {
      if (g_shape_mask[h][w]) count++;
    }
  }
  return count;
}

//Check if a grid position is valid according to the shape mask
bool IsValidPosition(int pos) {
  if (pos < 0 || pos >= SIZE_H * SIZE_W) return false;
  int h = pos / SIZE_W;
  int w = pos % SIZE_W;
  return g_shape_mask[h][w];
}

//Get the next valid position after pos (or -1 if none)
int GetNextValidPosition(int pos) {
  for (int next_pos = pos + 1; next_pos < SIZE_H * SIZE_W; ++next_pos) {
    if (IsValidPosition(next_pos)) {
      return next_pos;
    }
  }
  return -1;
}

//Check if position is at the start of a horizontal word segment
bool IsHorizontalWordStart(int pos) {
  if (!IsValidPosition(pos)) return false;
  int h = pos / SIZE_W;
  int w = pos % SIZE_W;
  return (w == 0) || !IsValidPosition(h * SIZE_W + (w - 1));
}

//Check if position is at the end of a horizontal word segment
bool IsHorizontalWordEnd(int pos) {
  if (!IsValidPosition(pos)) return false;
  int h = pos / SIZE_W;
  int w = pos % SIZE_W;
  return (w == SIZE_W - 1) || !IsValidPosition(h * SIZE_W + (w + 1));
}

//Check if position is at the start of a vertical word segment
bool IsVerticalWordStart(int pos) {
  if (!IsValidPosition(pos)) return false;
  int h = pos / SIZE_W;
  int w = pos % SIZE_W;
  return (h == 0) || !IsValidPosition((h - 1) * SIZE_W + w);
}

//Check if position is at the end of a vertical word segment
bool IsVerticalWordEnd(int pos) {
  if (!IsValidPosition(pos)) return false;
  int h = pos / SIZE_W;
  int w = pos % SIZE_W;
  return (h == SIZE_H - 1) || !IsValidPosition((h + 1) * SIZE_W + w);
}

//Get the horizontal word segment containing this position
std::string GetHorizontalSegment(int pos, char* words) {
  if (!IsValidPosition(pos)) return "";
  
  int h = pos / SIZE_W;
  int w = pos % SIZE_W;
  
  //Find start of segment
  int start_w = w;
  while (start_w > 0 && IsValidPosition(h * SIZE_W + (start_w - 1))) {
    start_w--;
  }
  
  //Find end of segment
  int end_w = w;
  while (end_w < SIZE_W - 1 && IsValidPosition(h * SIZE_W + (end_w + 1))) {
    end_w++;
  }
  
  //Build segment string
  std::string segment;
  for (int i = start_w; i <= end_w; ++i) {
    char c = words[h * SIZE_W + i];
    if (c == 0) return ""; // Incomplete segment
    segment += c;
  }
  return segment;
}

//Get the vertical word segment containing this position
std::string GetVerticalSegment(int pos, char* words) {
  if (!IsValidPosition(pos)) return "";
  
  int h = pos / SIZE_W;
  int w = pos % SIZE_W;
  
  //Find start of segment
  int start_h = h;
  while (start_h > 0 && IsValidPosition((start_h - 1) * SIZE_W + w)) {
    start_h--;
  }
  
  //Find end of segment
  int end_h = h;
  while (end_h < SIZE_H - 1 && IsValidPosition((end_h + 1) * SIZE_W + w)) {
    end_h++;
  }
  
  //Build segment string
  std::string segment;
  for (int i = start_h; i <= end_h; ++i) {
    char c = words[i * SIZE_W + w];
    if (c == 0) return ""; // Incomplete segment
    segment += c;
  }
  return segment;
}

//Get the expected horizontal word length for a given row
int GetHorizontalWordLength(int row) {
  int count = 0;
  for (int w = 0; w < SIZE_W; ++w) {
    if (g_shape_mask[row][w]) count++;
  }
  return count;
}

//Get the expected vertical word length for a given column
int GetVerticalWordLength(int col) {
  int count = 0;
  for (int h = 0; h < SIZE_H; ++h) {
    if (g_shape_mask[h][col]) count++;
  }
  return count;
}

//Check if a completed grid can be played in WordFeud with available letters
bool CanPlayInWordFeud(char* words) {
  std::unordered_map<char, int> letter_count;
  
  //Count all letters used in the grid
  for (int pos = 0; pos < SIZE_H * SIZE_W; ++pos) {
    if (IsValidPosition(pos) && words[pos] != 0) {
      letter_count[words[pos]]++;
    }
  }
  
  //Check if we have enough of each letter
  int blanks_needed = 0;
  for (const auto& pair : letter_count) {
    char letter = pair.first;
    int needed = pair.second;
    
    auto wordfeud_it = g_wordfeud_letters.find(letter);
    if (wordfeud_it == g_wordfeud_letters.end()) {
      //Letter not in WordFeud (shouldn't happen with our mapping)
      blanks_needed += needed;
    } else {
      int available = wordfeud_it->second;
      if (needed > available) {
        blanks_needed += (needed - available);
      }
    }
  }
  
  return blanks_needed <= g_wordfeud_blanks;
}

//Check if a partial grid can potentially be played in WordFeud (for deepest position tracking)
bool CanPotentiallyPlayInWordFeud(char* words, int current_pos) {
  std::unordered_map<char, int> letter_count;
  
  //Count letters used so far (up to current_pos)
  for (int pos = 0; pos <= current_pos; ++pos) {
    if (IsValidPosition(pos) && words[pos] != 0) {
      letter_count[words[pos]]++;
    }
  }
  
  //Check if current usage already exceeds WordFeud limits
  int blanks_needed = 0;
  for (const auto& pair : letter_count) {
    char letter = pair.first;
    int needed = pair.second;
    
    auto wordfeud_it = g_wordfeud_letters.find(letter);
    if (wordfeud_it == g_wordfeud_letters.end()) {
      blanks_needed += needed;
    } else {
      int available = wordfeud_it->second;
      if (needed > available) {
        blanks_needed += (needed - available);
      }
    }
  }
  
  return blanks_needed <= g_wordfeud_blanks;
}

//Check if partial word segments at this position are valid (prefix check)
bool IsValidPartialSegments(int pos, char* words) {
  int h = pos / SIZE_W;
  int w = pos % SIZE_W;
  
  //Check horizontal word segments in this row
  //Find all word segments in the row and validate each one
  for (int start_w = 0; start_w < SIZE_W;) {
    if (!IsValidPosition(h * SIZE_W + start_w)) {
      start_w++;
      continue;
    }
    
    //Find the end of this segment
    int end_w = start_w;
    while (end_w < SIZE_W && IsValidPosition(h * SIZE_W + end_w)) {
      end_w++;
    }
    end_w--; // Back to last valid position
    
    //Validate all segments (including single letters)
    if (end_w >= start_w) {
      std::string h_word;
      bool h_complete = true;
      
      for (int col = start_w; col <= end_w; ++col) {
        int row_pos = h * SIZE_W + col;
        if (words[row_pos] == 0) {
          h_complete = false;
          break;
        }
        h_word += words[row_pos];
      }
      
      if (h_complete && h_word.length() >= 1) {
        if (!g_trie_w.has(h_word)) {
          return false;
        }
      } else if (!h_complete && h_word.length() > 0) {
        //Check if partial word is a valid prefix for words of the expected segment length
        int segment_length = end_w - start_w + 1;
        if (segment_length >= 1 && g_tries_by_length.count(segment_length)) {
          if (!g_tries_by_length[segment_length].hasPrefix(h_word)) {
            return false;
          }
        }
      }
    }
    
    start_w = end_w + 1;
  }
  
  //Check vertical word segments in this column
  //Find all word segments in the column and validate each one
  for (int start_h = 0; start_h < SIZE_H;) {
    if (!IsValidPosition(start_h * SIZE_W + w)) {
      start_h++;
      continue;
    }
    
    //Find the end of this segment
    int end_h = start_h;
    while (end_h < SIZE_H && IsValidPosition(end_h * SIZE_W + w)) {
      end_h++;
    }
    end_h--; // Back to last valid position
    
    //Validate all segments (including single letters)
    if (end_h >= start_h) {
      std::string v_word;
      bool v_complete = true;
      
      for (int row = start_h; row <= end_h; ++row) {
        int col_pos = row * SIZE_W + w;
        if (words[col_pos] == 0) {
          v_complete = false;
          break;
        }
        v_word += words[col_pos];
      }
      
      Trie* trie_v = (SIZE_W != SIZE_H) ? &g_trie_h : &g_trie_w;
      if (v_complete && v_word.length() >= 1) {
        if (!trie_v->has(v_word)) {
          return false;
        }
      } else if (!v_complete && v_word.length() > 0) {
        //Check if partial word is a valid prefix for words of the expected segment length
        int segment_length = end_h - start_h + 1;
        if (segment_length >= 1 && g_tries_by_length.count(segment_length)) {
          if (!g_tries_by_length[segment_length].hasPrefix(v_word)) {
            return false;
          }
        }
      }
    }
    
    start_h = end_h + 1;
  }
  
  return true;
}

//Validate all word segments in the completed grid
bool ValidateAllSegments(char* words) {
  //Check all horizontal segments (find separate word segments in each row)
  for (int h = 0; h < SIZE_H; ++h) {
    for (int start_w = 0; start_w < SIZE_W;) {
      if (!IsValidPosition(h * SIZE_W + start_w)) {
        start_w++;
        continue;
      }
      
      //Find the end of this segment
      int end_w = start_w;
      while (end_w < SIZE_W && IsValidPosition(h * SIZE_W + end_w)) {
        end_w++;
      }
      end_w--; // Back to last valid position
      
      //Validate all segments (including single letters)
      if (end_w >= start_w) {
        std::string word;
        for (int col = start_w; col <= end_w; ++col) {
          word += words[h * SIZE_W + col];
        }
        if (!g_trie_w.has(word)) {
          return false;
        }
      }
      
      start_w = end_w + 1;
    }
  }
  
  //Check all vertical segments (find separate word segments in each column)
  Trie* trie_v = (SIZE_W != SIZE_H) ? &g_trie_h : &g_trie_w;
  for (int w = 0; w < SIZE_W; ++w) {
    for (int start_h = 0; start_h < SIZE_H;) {
      if (!IsValidPosition(start_h * SIZE_W + w)) {
        start_h++;
        continue;
      }
      
      //Find the end of this segment
      int end_h = start_h;
      while (end_h < SIZE_H && IsValidPosition(end_h * SIZE_W + w)) {
        end_h++;
      }
      end_h--; // Back to last valid position
      
      //Validate all segments (including single letters)
      if (end_h >= start_h) {
        std::string word;
        for (int row = start_h; row <= end_h; ++row) {
          word += words[row * SIZE_W + w];
        }
        if (!trie_v->has(word)) {
          return false;
        }
      }
      
      start_h = end_h + 1;
    }
  }
  
  return true;
}

//Print a solution (thread-safe)
void PrintBox(char* words) {
  //Do a uniqueness check if requested
  if (UNIQUE && SIZE_H == SIZE_W) {
    for (int i = 0; i < SIZE_H; ++i) {
      int num_same = 0;
      for (int j = 0; j < SIZE_W; ++j) {
        if (words[i * SIZE_W + j] == words[j * SIZE_W + i]) {
          num_same += 1;
        }
      }
      if (num_same == SIZE_W) { return; }
    }
  }
  
  //Check WordFeud compatibility
  bool wordfeud_compatible = CanPlayInWordFeud(words);
  
  //Only print if WordFeud compatible
  if (wordfeud_compatible) {
    //Print result (with mutex protection in threaded mode)
#ifdef ENABLE_THREADING
    std::lock_guard<std::mutex> lock(g_print_mutex);
#endif
    std::cout << "*** SOLUTION FOUND (WordFeud compatible) ***" << std::endl;
    //Print the full grid
    for (int h = 0; h < SIZE_H; ++h) {
      for (int w = 0; w < SIZE_W; ++w) {
        if (g_shape_mask[h][w]) {
          char c = words[h * SIZE_W + w];
          // Reverse the mapping for output
          if (c == 'Q') std::cout << "\u00c5"; // Q -> \u00c5
          else if (c == 'W') std::cout << "\u00c4"; // W -> \u00c4
          else if (c == '[') std::cout << "\u00d6"; // [ -> \u00d6
          else std::cout << c;
        } else {
          std::cout << " "; // Empty space for blocked positions
        }
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;
  }
  //If not WordFeud compatible, print nothing
}

void BoxSearch(int pos, char* words) {
  //Skip invalid positions according to shape mask
  if (!IsValidPosition(pos)) {
    int next_pos = GetNextValidPosition(pos - 1);
    if (next_pos == -1) {
      //No more valid positions, we have a complete solution
      PrintBox(words);
      return;
    }
    BoxSearch(next_pos, words);
    return;
  }
  
  //Try all possible letters at this position
  for (char c = 'A'; c <= 'Z'; ++c) {
    words[pos] = c;
    
    //Check if current horizontal and vertical segments are valid so far
    if (IsValidPartialSegments(pos, words)) {
#ifdef ENABLE_WORDFEUD_PRUNING
      //Early pruning: skip if this partial grid already exceeds WordFeud tile limits
      if (!CanPotentiallyPlayInWordFeud(words, pos)) {
        continue; //Skip this letter and try the next one
      }
#endif
      //Track deepest position reached (only if WordFeud compatible)
#ifdef ENABLE_THREADING
      if (pos > g_deepest_pos && CanPotentiallyPlayInWordFeud(words, pos)) {
        g_deepest_pos = pos;
        std::lock_guard<std::mutex> lock(g_print_mutex);
        std::cout << "New deepest WordFeud-compatible position: " << pos << std::endl;
        std::cout << "Current grid state:" << std::endl;
        for (int h = 0; h < SIZE_H; ++h) {
          for (int w = 0; w < SIZE_W; ++w) {
            if (g_shape_mask[h][w]) {
              char ch = words[h * SIZE_W + w];
              if (ch == 0) std::cout << "_";
              else std::cout << ch;
            } else {
              std::cout << " ";
            }
          }
          std::cout << std::endl;
        }
        std::cout << std::endl;
      }
#else
      if (pos > g_deepest_pos && CanPotentiallyPlayInWordFeud(words, pos)) {
        g_deepest_pos = pos;
        std::cout << "New deepest WordFeud-compatible position: " << pos << std::endl;
        std::cout << "Current grid state:" << std::endl;
        for (int h = 0; h < SIZE_H; ++h) {
          for (int w = 0; w < SIZE_W; ++w) {
            if (g_shape_mask[h][w]) {
              char ch = words[h * SIZE_W + w];
              if (ch == 0) std::cout << "_";
              else std::cout << ch;
            } else {
              std::cout << " ";
            }
          }
          std::cout << std::endl;
        }
        std::cout << std::endl;
      }
#endif
      
      //Count combinations tried and show progress
#ifdef ENABLE_THREADING
      uint64_t combinations = ++g_combinations_tried;
      if (combinations % 1000000000 == 0) {
        std::lock_guard<std::mutex> lock(g_print_mutex);
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed_seconds = std::chrono::duration<double>(current_time - g_start_time).count();
        double combinations_per_second = combinations / elapsed_seconds;
        std::cout << "Combinations tried: " << combinations 
                  << " (" << std::fixed << std::setprecision(0) << combinations_per_second << " comb/sec)" << std::endl;
      }
#else
      ++g_combinations_tried;
      if (g_combinations_tried % 10000000 == 0) {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed_seconds = std::chrono::duration<double>(current_time - g_start_time).count();
        double combinations_per_second = g_combinations_tried / elapsed_seconds;
        std::cout << "Combinations tried: " << g_combinations_tried 
                  << " (" << std::fixed << std::setprecision(0) << combinations_per_second << " comb/sec)" << std::endl;
      }
#endif
      
      //Show progress (only for first position)
      if (pos == GetNextValidPosition(-1)) { 
#ifdef ENABLE_THREADING
        std::lock_guard<std::mutex> lock(g_print_mutex);
#endif
        std::cout << "=== [" << c << "] ===" << std::endl; 
      }
      
      //Check if we've reached the end
      int next_pos = GetNextValidPosition(pos);
      if (next_pos == -1) {
        //No more positions, validate complete solution
        if (ValidateAllSegments(words)) {
          PrintBox(words);
        }
      } else {
        //Continue to next position
        BoxSearch(next_pos, words);
      }
    }
  }
  
  //Clear the position when backtracking
  words[pos] = 0;
}

#ifdef ENABLE_THREADING
//Thread worker function for parallel search
void SearchWorker(char starting_letter, Trie* trie_h) {
  //Each thread has its own word grid
  char words[SIZE_H * SIZE_W] = { 0 };
  
  //Find the first valid position
  int first_pos = GetNextValidPosition(-1);
  if (first_pos == -1) return; // No valid positions
  
  //Set the first letter at the first valid position
  words[first_pos] = starting_letter;
  
#ifdef ENABLE_WORDFEUD_PRUNING
  //Check if even the first letter exceeds WordFeud limits (very unlikely but possible)
  if (!CanPotentiallyPlayInWordFeud(words, first_pos)) {
    return; //Skip this starting letter entirely
  }
#endif
  
  {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::cout << "=== [" << starting_letter << "] ===" << std::endl;
  }
  
  //Start the search from the next valid position after first_pos
  int next_pos = GetNextValidPosition(first_pos);
  if (next_pos != -1) {
    BoxSearch(next_pos, words);
  } else {
    //Only one position, validate and print if valid
    if (ValidateAllSegments(words)) {
      PrintBox(words);
    }
  }
}
#endif

int main(int argc, char* argv[]) {
#ifdef ENABLE_FREQ_FILTER
  //Load word frequency list
  LoadFreq(FREQ_FILTER);
#endif

  //Load words of all lengths needed for the shape
  std::set<int> needed_lengths;
  for (int h = 0; h < SIZE_H; ++h) {
    int segment_length = 0;
    for (int w = 0; w < SIZE_W; ++w) {
      if (g_shape_mask[h][w]) {
        segment_length++;
      } else {
        if (segment_length > 0) needed_lengths.insert(segment_length);
        segment_length = 0;
      }
    }
    if (segment_length > 0) needed_lengths.insert(segment_length);
  }
  
  for (int w = 0; w < SIZE_W; ++w) {
    int segment_length = 0;
    for (int h = 0; h < SIZE_H; ++h) {
      if (g_shape_mask[h][w]) {
        segment_length++;
      } else {
        if (segment_length > 0) needed_lengths.insert(segment_length);
        segment_length = 0;
      }
    }
    if (segment_length > 0) needed_lengths.insert(segment_length);
  }
  
  std::cout << "Loading words of lengths: ";
  for (int len : needed_lengths) {
    std::cout << len << " ";
  }
  std::cout << std::endl;
  
  //Load all needed word lengths into both general tries and length-specific tries
  for (int length : needed_lengths) {
    LoadDictionary(DICTIONARY, length, g_trie_w, MIN_FREQ_W);
    LoadDictionary(DICTIONARY, length, g_tries_by_length[length], MIN_FREQ_W);
  }
  
  //For non-square grids, also load words into the vertical trie
  if (SIZE_W != SIZE_H) {
    for (int length : needed_lengths) {
      LoadDictionary(DICTIONARY, length, g_trie_h, MIN_FREQ_H);
    }
  }
  
  Trie* trie_h = &g_trie_w;

#ifdef ENABLE_THREADING
  //Get available letters from the horizontal trie
  std::vector<char> available_letters;
  Trie::Iter iter = g_trie_w.iter();
  while (iter.next()) {
    available_letters.push_back(iter.getLetter());
  }
  
  //Determine number of threads (limit to hardware concurrency)
  const unsigned int num_threads = std::min(std::thread::hardware_concurrency(), 
                                           static_cast<unsigned int>(available_letters.size()));
  
  std::cout << "Starting parallel search with " << num_threads << " threads processing " 
            << available_letters.size() << " starting letters..." << std::endl;
  
  //Initialize timing for combinations per second tracking
  g_start_time = std::chrono::high_resolution_clock::now();
  g_last_report_time = g_start_time;
  
  //Work queue and synchronization
  std::atomic<size_t> work_index(0);
  
  //Worker function that processes multiple letters per thread
  auto worker = [&]() {
    size_t index;
    while ((index = work_index.fetch_add(1)) < available_letters.size()) {
      SearchWorker(available_letters[index], trie_h);
    }
  };
  
  //Create and launch threads
  std::vector<std::thread> threads;
  for (unsigned int i = 0; i < num_threads; ++i) {
    threads.emplace_back(worker);
  }
  
  //Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }
  
  auto end_time = std::chrono::high_resolution_clock::now();
  auto total_seconds = std::chrono::duration<double>(end_time - g_start_time).count();
  double avg_combinations_per_second = g_combinations_tried / total_seconds;
  std::cout << "Done. Total combinations tried: " << g_combinations_tried 
            << " (avg " << std::fixed << std::setprecision(0) << avg_combinations_per_second << " comb/sec)" << std::endl;
#else
  //Single-threaded search
  std::cout << "Starting single-threaded search..." << std::endl;
  
  //Initialize timing for combinations per second tracking
  g_start_time = std::chrono::high_resolution_clock::now();
  g_last_report_time = g_start_time;
  
  //Initialize word grid
  char words[SIZE_H * SIZE_W] = { 0 };
  
  //Start the search from the first valid position
  int first_pos = GetNextValidPosition(-1);
  if (first_pos != -1) {
    BoxSearch(first_pos, words);
  } else {
    std::cout << "No valid positions in shape mask!" << std::endl;
  }
  
  auto end_time = std::chrono::high_resolution_clock::now();
  auto total_seconds = std::chrono::duration<double>(end_time - g_start_time).count();
  double avg_combinations_per_second = g_combinations_tried / total_seconds;
  std::cout << "Done. Total combinations tried: " << g_combinations_tried 
            << " (avg " << std::fixed << std::setprecision(0) << avg_combinations_per_second << " comb/sec)" << std::endl;
#endif
  return 0;
}
