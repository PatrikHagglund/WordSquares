#include "trie.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <vector>

// Enable threading by default - comment out to disable
#define ENABLE_THREADING

// Enable frequency filtering - comment out to disable
#define ENABLE_FREQ_FILTER

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
#define SIZE_W 8
//Height of the word grid
#define SIZE_H 8
//Filter horizontal words to be in the top-N (or 0 for all words)
#define MIN_FREQ_W 0
//Filter vertical words to be in the top-N (or 0 for all words)
#define MIN_FREQ_H 0
//Only print solutions with all unique words (only for square grids)
#define UNIQUE false
//Diagonals must also be words (only for square grids)
#define DIAGONALS false

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
  //Print the grid (with mutex protection in threaded mode)
#ifdef ENABLE_THREADING
  std::lock_guard<std::mutex> lock(g_print_mutex);
#endif
  for (int h = 0; h < SIZE_H; ++h) {
    for (int w = 0; w < SIZE_W; ++w) {
      char c = words[h * SIZE_W + w];
      // Reverse the mapping for output
      if (c == 'Q') std::cout << "\u00c5"; // Q -> \u00c5
      else if (c == 'W') std::cout << "\u00c4"; // W -> \u00c4
      else if (c == '[') std::cout << "\u00d6"; // [ -> \u00d6
      else std::cout << c;
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
}

void BoxSearch(Trie* trie, Trie* vtries[VTRIE_SIZE], int pos, char* words) {
  //Reset when coming back to first letter
  const int v_ix = pos % SIZE_W;
#if DIAGONALS
  const int h_ix = pos / SIZE_W;
#endif
  //Check if this is the beginning of a row
  if (v_ix == 0) {
    //If the entire grid is filled, we're done, print the solution
    if (pos == SIZE_H * SIZE_W) {
      PrintBox(words);
      return;
    }
    //Reset the horizontal trie position to the beginning
    trie = &g_trie_w;
  }
  Trie::Iter iter = trie->iter();
  while (iter.next()) {
    //Try next letter if vertical trie fails
    if (!vtries[v_ix]->hasIx(iter.getIx())) { continue; }
    
    //Count combinations tried and show progress
#ifdef ENABLE_THREADING
    uint64_t combinations = ++g_combinations_tried;
    if (combinations % 1000000000 == 0) {
      std::lock_guard<std::mutex> lock(g_print_mutex);
      std::cout << "Combinations tried: " << combinations << std::endl;
    }
#else
    ++g_combinations_tried;
    if (g_combinations_tried % 1000000000 == 0) {
      std::cout << "Combinations tried: " << g_combinations_tried << std::endl;
    }
#endif
    
    //Track deepest position reached (closest to solution)
    if (UNIQUE) {
      //For UNIQUE mode, only track positions with all unique words
      if (HasUniqueWords(words, pos)) {
#ifdef ENABLE_THREADING
        int current_deepest_unique = g_deepest_unique_pos.load();
        if (pos > current_deepest_unique && g_deepest_unique_pos.compare_exchange_weak(current_deepest_unique, pos)) {
          std::lock_guard<std::mutex> lock(g_print_mutex);
          std::cout << "New deepest unique position: " << (pos + 1) << "/" << (SIZE_H * SIZE_W) << " positions filled (";
          std::cout << std::fixed << std::setprecision(1) << (100.0 * (pos + 1) / (SIZE_H * SIZE_W)) << "%)" << std::endl;
#else
        if (pos > g_deepest_unique_pos) {
          g_deepest_unique_pos = pos;
          std::cout << "New deepest unique position: " << (pos + 1) << "/" << (SIZE_H * SIZE_W) << " positions filled (";
          std::cout << std::fixed << std::setprecision(1) << (100.0 * (pos + 1) / (SIZE_H * SIZE_W)) << "%)" << std::endl;
#endif
          
          //Print the partial grid
          for (int h = 0; h < SIZE_H; ++h) {
            for (int w = 0; w < SIZE_W; ++w) {
              int grid_pos = h * SIZE_W + w;
              if (grid_pos <= pos) {
                char c = words[grid_pos];
                // Reverse the mapping for output
                if (c == 'Q') std::cout << "\u00c5"; // Q -> Å
                else if (c == 'W') std::cout << "\u00c4"; // W -> Ä
                else if (c == '[') std::cout << "\u00d6"; // [ -> Ö
                else std::cout << c;
              } else {
                std::cout << ".";
              }
            }
            std::cout << std::endl;
          }
          std::cout << std::endl;
        }
      }
    } else {
      //Original behavior for non-UNIQUE mode
#ifdef ENABLE_THREADING
      int current_deepest = g_deepest_pos.load();
      if (pos > current_deepest && g_deepest_pos.compare_exchange_weak(current_deepest, pos)) {
        std::lock_guard<std::mutex> lock(g_print_mutex);
        std::cout << "New closest attempt: " << (pos + 1) << "/" << (SIZE_H * SIZE_W) << " positions filled (";
        std::cout << std::fixed << std::setprecision(1) << (100.0 * (pos + 1) / (SIZE_H * SIZE_W)) << "%)" << std::endl;
#else
      if (pos > g_deepest_pos) {
        g_deepest_pos = pos;
        std::cout << "New closest attempt: " << (pos + 1) << "/" << (SIZE_H * SIZE_W) << " positions filled (";
        std::cout << std::fixed << std::setprecision(1) << (100.0 * (pos + 1) / (SIZE_H * SIZE_W)) << "%)" << std::endl;
#endif
        
        //Print the partial grid
        for (int h = 0; h < SIZE_H; ++h) {
          for (int w = 0; w < SIZE_W; ++w) {
            int grid_pos = h * SIZE_W + w;
            if (grid_pos <= pos) {
              char c = words[grid_pos];
              // Reverse the mapping for output
              if (c == 'Q') std::cout << "\u00c5"; // Q -> Å
              else if (c == 'W') std::cout << "\u00c4"; // W -> Ä
              else if (c == '[') std::cout << "\u00d6"; // [ -> Ö
              else std::cout << c;
            } else {
              std::cout << ".";
            }
          }
          std::cout << std::endl;
        }
        std::cout << std::endl;
      }
    }
    
    //Show progress bar (only for single-threaded first position)
    if (pos == 0) { 
#ifdef ENABLE_THREADING
      std::lock_guard<std::mutex> lock(g_print_mutex);
#endif
      std::cout << "=== [" << iter.getLetter() << "] ===" << std::endl; 
    }
#if DIAGONALS
    if (h_ix == v_ix) {
      if (!vtries[VTRIE_SIZE - 2]->hasIx(iter.getIx())) { continue; }
    }
    if (h_ix == SIZE_W - v_ix - 1) {
      if (!vtries[VTRIE_SIZE - 1]->hasIx(iter.getIx())) { continue; }
    }
#endif
    //Letter is valid, update the solution
    words[pos] = iter.getLetter();
    //Make a backup of the vertical trie position in the stack for backtracking
    Trie* backup_vtrie = vtries[v_ix];
    //Update the vertical trie position
    vtries[v_ix] = vtries[v_ix]->decend(iter.getIx());
#if DIAGONALS
    Trie* backup_dtrie1 = vtries[VTRIE_SIZE - 2];
    Trie* backup_dtrie2 = vtries[VTRIE_SIZE - 1];
    if (h_ix == v_ix) {
      vtries[VTRIE_SIZE - 2] = vtries[VTRIE_SIZE - 2]->decend(iter.getIx());
    }
    if (h_ix == SIZE_W - v_ix - 1) {
      vtries[VTRIE_SIZE - 1] = vtries[VTRIE_SIZE - 1]->decend(iter.getIx());
    }
#endif
    //Make the recursive call
    BoxSearch(iter.get(), vtries, pos + 1, words);
    //After returning, restore the vertical trie position from the stack
    vtries[v_ix] = backup_vtrie;
#if DIAGONALS
    vtries[VTRIE_SIZE - 2] = backup_dtrie1;
    vtries[VTRIE_SIZE - 1] = backup_dtrie2;
#endif
  }
}

#ifdef ENABLE_THREADING
//Thread worker function for parallel search
void SearchWorker(char starting_letter, Trie* trie_h) {
  //Each thread has its own word grid
  char words[SIZE_H * SIZE_W] = { 0 };
  
  //Initialize vertical tries for this thread
  Trie* vtries[VTRIE_SIZE];
  std::fill(vtries, vtries + VTRIE_SIZE, trie_h);
  
  //Set the first letter
  words[0] = starting_letter;
  
  //Find the starting trie node for this letter
  Trie* start_trie = g_trie_w.decend(starting_letter - 'A');
  if (!start_trie) return; // Invalid starting letter
  
  //Update vertical trie for first position
  vtries[0] = vtries[0]->decend(starting_letter - 'A');
  if (!vtries[0]) return; // Invalid starting letter for vertical
  
#if DIAGONALS
  //Update diagonal tries if needed
  vtries[VTRIE_SIZE - 2] = vtries[VTRIE_SIZE - 2]->decend(starting_letter - 'A');
  if (!vtries[VTRIE_SIZE - 2]) return;
  if (SIZE_W > 1) {
    vtries[VTRIE_SIZE - 1] = vtries[VTRIE_SIZE - 1]->decend(starting_letter - 'A');
    if (!vtries[VTRIE_SIZE - 1]) return;
  }
#endif
  
  {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::cout << "=== [" << starting_letter << "] ===" << std::endl;
  }
  
  //Start the search from position 1
  BoxSearch(start_trie, vtries, 1, words);
}
#endif

int main(int argc, char* argv[]) {
#ifdef ENABLE_FREQ_FILTER
  //Load word frequency list
  LoadFreq(FREQ_FILTER);
#endif

  //Load horizontal trie from dictionary
  LoadDictionary(DICTIONARY, SIZE_W, g_trie_w, MIN_FREQ_W);
  Trie* trie_h = &g_trie_w;
  if (SIZE_W != SIZE_H) {
    //Load vertical trie from dictionary (if needed)
    LoadDictionary(DICTIONARY, SIZE_H, g_trie_h, MIN_FREQ_H);
    trie_h = &g_trie_h;
  }

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
  
  std::cout << "Done. Total combinations tried: " << g_combinations_tried << std::endl;
#else
  //Single-threaded search
  std::cout << "Starting single-threaded search..." << std::endl;
  
  //Initialize word grid
  char words[SIZE_H * SIZE_W] = { 0 };
  
  //Initialize vertical tries
  Trie* vtries[VTRIE_SIZE];
  std::fill(vtries, vtries + VTRIE_SIZE, trie_h);
  
  //Start the search from position 0
  BoxSearch(&g_trie_w, vtries, 0, words);
  
  std::cout << "Done. Total combinations tried: " << g_combinations_tried << std::endl;
#endif
  return 0;
}
