#include "trie.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <regex>

#define GRID_SIZE 8
#define DICTIONARY "WordFeud_ordlista.txt"
#define OUTPUT_FILE "output.txt"

// Global dictionary
Trie g_word_trie;

// Structure to hold a solution and its metrics
struct Solution {
    std::vector<std::string> grid;
    int word_count = 0;
    int total_letter_count = 0;
    std::vector<std::string> words;
};

// Load dictionary
void LoadDictionary(const char* fname) {
    std::cout << "Loading WordFeud dictionary..." << std::endl;
    int num_words = 0;
    std::ifstream fin(fname);
    std::string line;
    
    while (std::getline(fin, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Handle UTF-8 Swedish characters and convert to uppercase
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
        if (line.size() >= 2 && line.size() <= 8) { // 2-8 letter words
            g_word_trie.add(line);
            num_words++;
        }
    }
    std::cout << "Loaded " << num_words << " words." << std::endl;
}

// Extract all words from a grid (horizontal and vertical)
std::vector<std::string> ExtractWords(const std::vector<std::string>& grid) {
    std::vector<std::string> words;
    
    // Extract horizontal words
    for (int row = 0; row < GRID_SIZE; ++row) {
        std::string current_word;
        for (int col = 0; col < GRID_SIZE; ++col) {
            char c = grid[row][col];
            if (c != ' ' && c != 0 && c != '.') {
                current_word += c;
            } else {
                if (current_word.length() >= 2) {
                    words.push_back(current_word);
                }
                current_word.clear();
            }
        }
        if (current_word.length() >= 2) {
            words.push_back(current_word);
        }
    }
    
    // Extract vertical words
    for (int col = 0; col < GRID_SIZE; ++col) {
        std::string current_word;
        for (int row = 0; row < GRID_SIZE; ++row) {
            char c = grid[row][col];
            if (c != ' ' && c != 0 && c != '.') {
                current_word += c;
            } else {
                if (current_word.length() >= 2) {
                    words.push_back(current_word);
                }
                current_word.clear();
            }
        }
        if (current_word.length() >= 2) {
            words.push_back(current_word);
        }
    }
    
    return words;
}

// Extract all substrings of a word that are valid dictionary words
std::vector<std::string> ExtractSubWords(const std::string& word) {
    std::vector<std::string> subwords;
    
    for (size_t start = 0; start < word.length(); ++start) {
        for (size_t len = 2; len <= word.length() - start; ++len) {
            std::string sub = word.substr(start, len);
            if (g_word_trie.has(sub)) {
                subwords.push_back(sub);
            }
        }
    }
    
    return subwords;
}

// Count all valid words in a solution (including subwords)
int CountAllWords(const Solution& solution) {
    std::set<std::string> unique_words;
    
    // Add all base words and their valid subwords
    for (const std::string& word : solution.words) {
        std::vector<std::string> subwords = ExtractSubWords(word);
        for (const std::string& subword : subwords) {
            unique_words.insert(subword);
        }
    }
    
    return unique_words.size();
}

// Parse solutions from output.txt
std::vector<Solution> ParseSolutions(const char* filename) {
    std::vector<Solution> solutions;
    std::ifstream file(filename);
    std::string line;
    
    std::cout << "Parsing solutions from " << filename << "..." << std::endl;
    
    while (std::getline(file, line)) {
        // Skip debug output lines that contain keywords
        if (line.find("combinations tried") != std::string::npos ||
            line.find("Combinations tried") != std::string::npos ||
            line.find("positions filled") != std::string::npos ||
            line.find("New closest attempt") != std::string::npos ||
            line.find("=== [") != std::string::npos) {
            continue;
        }
        
        // Remove timestamp prefix and check if this looks like a valid 8-letter word
        size_t space_pos = line.find(' ');
        if (space_pos != std::string::npos && space_pos < line.length() - 1) {
            std::string potential_word = line.substr(space_pos + 1);
            
            // Skip lines that are clearly not word grids
            if (potential_word.find("Loading") != std::string::npos ||
                potential_word.find("Loaded") != std::string::npos ||
                potential_word.find("Starting") != std::string::npos ||
                potential_word.find("attempt") != std::string::npos ||
                potential_word.find("filled") != std::string::npos ||
                potential_word.find("tried") != std::string::npos) {
                continue;
            }
            
            // Check if this line contains exactly 8 letters (no spaces or dots)
            bool is_8_letter_word = true;
            if (potential_word.length() >= 8) {
                for (size_t i = 0; i < 8; ++i) {
                    if (i >= potential_word.length() || 
                        (potential_word[i] == ' ' || potential_word[i] == '.' || potential_word[i] == 0)) {
                        is_8_letter_word = false;
                        break;
                    }
                }
                // Check if it's all letters (allowing UTF-8 Swedish chars)
                if (is_8_letter_word) {
                    for (size_t i = 0; i < std::min((size_t)8, potential_word.length()); ++i) {
                        unsigned char c = potential_word[i];
                        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                              (c == 0xC3) || (c >= 0x80 && c <= 0xFF))) {
                            is_8_letter_word = false;
                            break;
                        }
                    }
                }
            } else {
                is_8_letter_word = false;
            }
            
            if (is_8_letter_word) {
                // This might be the start of a complete 8x8 solution
                // Try to read the next 7 lines to see if we have a complete grid
                std::vector<std::string> potential_grid;
                
                // Convert the first line
                std::string processed_line;
                for (size_t j = 0; j < potential_word.size() && processed_line.length() < 8; ++j) {
                    unsigned char c = potential_word[j];
                    if (c >= 'a' && c <= 'z') {
                        processed_line += (c - 'a' + 'A');
                    } else if (c == 0xC3 && j + 1 < potential_word.size()) {
                        unsigned char next = potential_word[j + 1];
                        if (next == 0x85) { // Å
                            processed_line += 'Q';
                            ++j;
                        } else if (next == 0x84) { // Ä
                            processed_line += 'W';
                            ++j;
                        } else if (next == 0x96) { // Ö
                            processed_line += '[';
                            ++j;
                        } else {
                            processed_line += c;
                        }
                    } else if (c >= 'A' && c <= 'Z') {
                        processed_line += c;
                    }
                }
                
                if (processed_line.length() == 8) {
                    potential_grid.push_back(processed_line);
                    
                    // Try to read the next 7 lines
                    for (int i = 1; i < GRID_SIZE; ++i) {
                        if (std::getline(file, line)) {
                            size_t space_pos2 = line.find(' ');
                            if (space_pos2 != std::string::npos && space_pos2 < line.length() - 1) {
                                std::string grid_line = line.substr(space_pos2 + 1);
                                
                                // Convert Swedish characters
                                std::string processed_line2;
                                for (size_t j = 0; j < grid_line.size() && processed_line2.length() < 8; ++j) {
                                    unsigned char c = grid_line[j];
                                    if (c >= 'a' && c <= 'z') {
                                        processed_line2 += (c - 'a' + 'A');
                                    } else if (c == 0xC3 && j + 1 < grid_line.size()) {
                                        unsigned char next = grid_line[j + 1];
                                        if (next == 0x85) { // Å
                                            processed_line2 += 'Q';
                                            ++j;
                                        } else if (next == 0x84) { // Ä
                                            processed_line2 += 'W';
                                            ++j;
                                        } else if (next == 0x96) { // Ö
                                            processed_line2 += '[';
                                            ++j;
                                        } else {
                                            processed_line2 += c;
                                        }
                                    } else if (c >= 'A' && c <= 'Z') {
                                        processed_line2 += c;
                                    }
                                }
                                
                                if (processed_line2.length() == 8) {
                                    potential_grid.push_back(processed_line2);
                                } else {
                                    break; // Not a complete grid
                                }
                            } else {
                                break; // Not a valid line
                            }
                        } else {
                            break; // EOF
                        }
                    }
                    
                    // Check if we got a complete 8x8 grid
                    if (potential_grid.size() == GRID_SIZE) {
                        Solution solution;
                        solution.grid = potential_grid;
                        solution.words = ExtractWords(solution.grid);
                        solution.word_count = CountAllWords(solution);
                        solution.total_letter_count = 64; // Always 64 for complete 8x8 grid
                        
                        solutions.push_back(solution);
                    }
                }
            }
        }
    }
    
    std::cout << "Found " << solutions.size() << " complete 8x8 solutions." << std::endl;
    return solutions;
}

// Print a solution with its metrics
void PrintSolution(const Solution& solution, int rank) {
    std::cout << "=== RANK " << std::setw(2) << rank << " === (Word count: " << solution.word_count << ")" << std::endl;
    
    for (const auto& row : solution.grid) {
        std::cout << row << std::endl;
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // Load dictionary
    LoadDictionary(DICTIONARY);
    
    // Parse solutions
    std::vector<Solution> solutions = ParseSolutions(OUTPUT_FILE);
    
    if (solutions.empty()) {
        std::cout << "No complete solutions found in " << OUTPUT_FILE << std::endl;
        return 1;
    }
    
    // Sort solutions by word count (descending)
    std::sort(solutions.begin(), solutions.end(), [](const Solution& a, const Solution& b) {
        if (a.word_count != b.word_count) {
            return a.word_count > b.word_count; // Higher word count first
        }
        return a.total_letter_count > b.total_letter_count; // More letters as tiebreaker
    });
    
    // Print top 200 solutions
    std::cout << "\n=== TOP " << std::min(200, (int)solutions.size()) << " SOLUTIONS BY WORD COUNT ===" << std::endl;
    
    for (int i = 0; i < std::min(200, (int)solutions.size()); ++i) {
        PrintSolution(solutions[i], i + 1);
    }
    
    return 0;
}