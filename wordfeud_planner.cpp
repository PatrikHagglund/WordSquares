#include "trie.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <queue>

// Grid dimensions (15x15 for WordFeud)
#define GRID_SIZE 15

// Dictionary path - use the same as main WordSquares
#define DICTIONARY "WordFeud_ordlista.txt"

// Input grid file path
#define INPUT_GRID_FILE "planner_input.txt"

// Output file path
#define OUTPUT_FILE "planner_output.txt"

// Global variables
Trie g_word_trie;
std::ofstream output_file;
int g_deepest_reached = -1;
uint64_t g_combinations_tried = 0;
std::chrono::high_resolution_clock::time_point g_start_time;

// Helper function to output to both console and file
void output(const std::string& text) {
    std::cout << text;
    std::cout.flush();
    if (output_file.is_open()) {
        output_file << text;
        output_file.flush();
    }
}
std::vector<std::string> g_grid;
std::pair<int, int> g_starting_square = {-1, -1};

// Structure to represent a game state
struct GameState {
    std::vector<std::string> grid;
    std::vector<std::string> play_sequence;
    int moves_count = 0;
    
    GameState() : grid(GRID_SIZE, std::string(GRID_SIZE, ' ')) {}
    GameState(const std::vector<std::string>& g) : grid(g), moves_count(0) {}
};

// Load dictionary with all word lengths
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
        if (line.size() >= 1 && line.size() <= 15) { // 1-15 letter words for WordFeud (including long words)
            g_word_trie.add(line);
            num_words++;
        }
    }
    std::cout << "Loaded " << num_words << " words." << std::endl;
}

// Parse input grid from file
bool ParseInput(const char* filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open input file " << filename << std::endl;
        return false;
    }
    
    g_grid.resize(GRID_SIZE);
    std::string line;
    int row = 0;
    
    std::cout << "Parsing input grid..." << std::endl;
    
    while (std::getline(file, line) && row < GRID_SIZE) {
        if (line.empty()) continue;
        
        // Process line character by character to handle UTF-8 and special markers
        std::string processed_line;
        for (size_t i = 0; i < line.size() && processed_line.size() < GRID_SIZE; ++i) {
            unsigned char c = line[i];
            
            if (c == '*') {
                // Mark starting square with lowercase letter
                if (i + 1 < line.size()) {
                    char next_char = line[i + 1];
                    if (next_char >= 'A' && next_char <= 'Z') {
                        g_starting_square = {row, (int)processed_line.size()};
                        processed_line += next_char;
                        i++; // Skip the marked letter
                        std::cout << "Starting square found at (" << row << ", " << processed_line.size() - 1 << "): " << next_char << std::endl;
                    }
                }
            } else if (c >= 'a' && c <= 'z') {
                processed_line += (c - 'a' + 'A');
            } else if (c == 0xC3 && i + 1 < line.size()) {
                unsigned char next = line[i + 1];
                if (next == 0x85) { // Å
                    processed_line += 'Q';
                    ++i;
                } else if (next == 0x84) { // Ä
                    processed_line += 'W';
                    ++i;
                } else if (next == 0x96) { // Ö
                    processed_line += '[';
                    ++i;
                } else {
                    processed_line += c;
                }
            } else if (c >= 'A' && c <= 'Z') {
                processed_line += c;
            } else if (c == ' ' || c == '_' || c == '.') {
                processed_line += ' ';
            }
        }
        
        // Pad line to grid size
        while (processed_line.size() < GRID_SIZE) {
            processed_line += ' ';
        }
        
        g_grid[row] = processed_line;
        row++;
    }
    
    // Pad grid to full size
    while (row < GRID_SIZE) {
        g_grid[row] = std::string(GRID_SIZE, ' ');
        row++;
    }
    
    if (g_starting_square.first == -1) {
        std::cout << "No starting square marked - final word can be anywhere on grid" << std::endl;
    }
    
    return true;
}

// Extract all words from current grid state
std::vector<std::string> ExtractWords(const std::vector<std::string>& grid) {
    std::vector<std::string> words;
    
    // Extract horizontal words
    for (int row = 0; row < GRID_SIZE; ++row) {
        std::string current_word;
        for (int col = 0; col < GRID_SIZE; ++col) {
            char c = grid[row][col];
            if (c != ' ' && c != 0) {
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
            if (c != ' ' && c != 0) {
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

// Check if all letters form a single connected component using BFS
bool AreAllLettersConnected(const std::vector<std::string>& grid) {
    // First, find all letter positions
    std::vector<std::pair<int, int>> letter_positions;
    for (int row = 0; row < GRID_SIZE; ++row) {
        for (int col = 0; col < GRID_SIZE; ++col) {
            if (grid[row][col] != ' ' && grid[row][col] != 0) {
                letter_positions.push_back({row, col});
            }
        }
    }
    
    // If no letters or only one letter, it's connected
    if (letter_positions.size() <= 1) {
        return true;
    }
    
    // Use BFS to check if all letters are reachable from the first letter
    std::set<std::pair<int, int>> visited;
    std::queue<std::pair<int, int>> queue;
    
    // Start BFS from the first letter
    queue.push(letter_positions[0]);
    visited.insert(letter_positions[0]);
    
    // Directions for horizontal/vertical movement
    int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    
    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop();
        
        // Check all four directions
        for (int d = 0; d < 4; ++d) {
            int new_row = current.first + directions[d][0];
            int new_col = current.second + directions[d][1];
            
            // Check bounds
            if (new_row >= 0 && new_row < GRID_SIZE && new_col >= 0 && new_col < GRID_SIZE) {
                // Check if there's a letter at this position and we haven't visited it
                if (grid[new_row][new_col] != ' ' && grid[new_row][new_col] != 0) {
                    std::pair<int, int> neighbor = {new_row, new_col};
                    if (visited.find(neighbor) == visited.end()) {
                        visited.insert(neighbor);
                        queue.push(neighbor);
                    }
                }
            }
        }
    }
    
    // Check if all letters were visited
    return visited.size() == letter_positions.size();
}

// Check if all words in the grid are valid
bool AreAllWordsValid(const std::vector<std::string>& grid) {
    // First check if all letters are connected
    if (!AreAllLettersConnected(grid)) {
        return false;
    }
    
    std::vector<std::string> words = ExtractWords(grid);
    
    for (const std::string& word : words) {
        if (!g_word_trie.has(word)) {
            return false;
        }
    }
    
    return true;
}

// Check if the grid has only one word (optionally touching starting square)
bool IsTargetState(const std::vector<std::string>& grid) {
    std::vector<std::string> words = ExtractWords(grid);
    
    if (words.size() != 1) {
        return false;
    }
    
    // Check if the single word is valid length
    std::string word = words[0];
    if (word.length() < 2 || word.length() > 7) {
        return false;
    }
    
    // If no starting square is specified, any single word is valid
    if (g_starting_square.first == -1) {
        return true;
    }
    
    // Find the word's position and check if it contains the starting square
    int start_row = g_starting_square.first;
    int start_col = g_starting_square.second;
    
    // Check horizontal words containing starting square
    for (int row = 0; row < GRID_SIZE; ++row) {
        for (int col = 0; col <= GRID_SIZE - word.length(); ++col) {
            bool matches = true;
            for (size_t i = 0; i < word.length(); ++i) {
                if (grid[row][col + i] != word[i]) {
                    matches = false;
                    break;
                }
            }
            if (matches && row == start_row && start_col >= col && start_col < col + (int)word.length()) {
                return true;
            }
        }
    }
    
    // Check vertical words containing starting square
    for (int col = 0; col < GRID_SIZE; ++col) {
        for (int row = 0; row <= GRID_SIZE - (int)word.length(); ++row) {
            bool matches = true;
            for (size_t i = 0; i < word.length(); ++i) {
                if (grid[row + i][col] != word[i]) {
                    matches = false;
                    break;
                }
            }
            if (matches && col == start_col && start_row >= row && start_row < row + (int)word.length()) {
                return true;
            }
        }
    }
    
    return false;
}

// Generate all combinations of k elements from a vector
void GenerateCombinations(const std::vector<int>& positions, size_t k, size_t start, 
                         std::vector<int>& current, std::vector<std::vector<int>>& results) {
    if (current.size() == k) {
        results.push_back(current);
        return;
    }
    
    for (size_t i = start; i <= positions.size() - (k - current.size()); ++i) {
        current.push_back(positions[i]);
        GenerateCombinations(positions, k, i + 1, current, results);
        current.pop_back();
    }
}

// Get all possible removal combinations (1-7 letters from same row/column)
std::vector<std::vector<std::pair<int, int>>> GetRemovalCombinations(const std::vector<std::string>& grid) {
    std::vector<std::vector<std::pair<int, int>>> combinations;
    
    // Generate removal combinations for each row
    for (int row = 0; row < GRID_SIZE; ++row) {
        std::vector<int> positions;
        for (int col = 0; col < GRID_SIZE; ++col) {
            if (grid[row][col] != ' ' && (g_starting_square.first == -1 || std::make_pair(row, col) != g_starting_square)) {
                positions.push_back(col);
            }
        }
        
        // Generate all possible combinations of length 1-7
        for (size_t len = 1; len <= std::min(size_t(7), positions.size()); ++len) {
            std::vector<std::vector<int>> combos;
            std::vector<int> current;
            GenerateCombinations(positions, len, 0, current, combos);
            
            for (const auto& combo : combos) {
                std::vector<std::pair<int, int>> removal;
                for (int pos : combo) {
                    removal.push_back({row, pos});
                }
                combinations.push_back(removal);
            }
        }
    }
    
    // Generate removal combinations for each column
    for (int col = 0; col < GRID_SIZE; ++col) {
        std::vector<int> positions;
        for (int row = 0; row < GRID_SIZE; ++row) {
            if (grid[row][col] != ' ' && (g_starting_square.first == -1 || std::make_pair(row, col) != g_starting_square)) {
                positions.push_back(row);
            }
        }
        
        // Generate all possible combinations of length 1-7
        for (size_t len = 1; len <= std::min(size_t(7), positions.size()); ++len) {
            std::vector<std::vector<int>> combos;
            std::vector<int> current;
            GenerateCombinations(positions, len, 0, current, combos);
            
            for (const auto& combo : combos) {
                std::vector<std::pair<int, int>> removal;
                for (int pos : combo) {
                    removal.push_back({pos, col});
                }
                combinations.push_back(removal);
            }
        }
    }
    
    return combinations;
}

// Apply removal and return new grid state
std::vector<std::string> ApplyRemoval(const std::vector<std::string>& grid, const std::vector<std::pair<int, int>>& removal) {
    std::vector<std::string> new_grid = grid;
    
    for (const auto& pos : removal) {
        new_grid[pos.first][pos.second] = ' ';
    }
    
    return new_grid;
}

// Convert removal to human-readable move description
std::string DescribeMove(const std::vector<std::pair<int, int>>& removal, const std::vector<std::string>& original_grid) {
    if (removal.empty()) return "";
    
    std::string letters;
    for (const auto& pos : removal) {
        char c = original_grid[pos.first][pos.second];
        // Convert back to readable format
        if (c == 'Q') letters += "\u00c5";
        else if (c == 'W') letters += "\u00c4";
        else if (c == '[') letters += "\u00d6";
        else letters += c;
    }
    
    // Determine if horizontal or vertical removal
    bool horizontal = true;
    if (removal.size() > 1) {
        horizontal = (removal[0].first == removal[1].first);
    }
    
    char coord_letter = 'A' + removal[0].second;
    int coord_number = removal[0].first + 1;
    
    if (horizontal) {
        return "Remove \"" + letters + "\" from row " + std::to_string(coord_number) + 
               " starting at column " + coord_letter;
    } else {
        return "Remove \"" + letters + "\" from column " + coord_letter + 
               " starting at row " + std::to_string(coord_number);
    }
}

// Print grid state
void PrintGrid(const std::vector<std::string>& grid) {
    output("   A B C D E F G H I J K L M N O\n");
    for (int i = 0; i < GRID_SIZE; ++i) {
        std::string line = "";
        if (i + 1 < 10) line += " ";
        line += std::to_string(i + 1) + " ";
        
        for (int j = 0; j < GRID_SIZE; ++j) {
            char c = grid[i][j];
            if (c == ' ') {
                line += ". ";
            } else if (g_starting_square.first != -1 && i == g_starting_square.first && j == g_starting_square.second) {
                // Mark starting square
                if (c == 'Q') line += "*Å ";
                else if (c == 'W') line += "*Ä ";
                else if (c == '[') line += "*Ö ";
                else line += "*" + std::string(1, c) + " ";
            } else {
                // Convert back to readable format
                if (c == 'Q') line += "Å ";
                else if (c == 'W') line += "Ä ";
                else if (c == '[') line += "Ö ";
                else line += std::string(1, c) + " ";
            }
        }
        output(line + "\n");
    }
    output("\n");
}

// Recursive backtracking search for reverse play sequence
bool FindReverseSequence(GameState current_state, std::vector<GameState>& solution_path, int max_depth = 100) {
    // Check if we've reached a new deepest position
    if (current_state.moves_count > g_deepest_reached) {
        g_deepest_reached = current_state.moves_count;
        output("New deepest position reached: depth " + std::to_string(current_state.moves_count) + "\n");
        PrintGrid(current_state.grid);
        
        // Also show remaining words
        std::vector<std::string> remaining_words = ExtractWords(current_state.grid);
        output("Remaining words (" + std::to_string(remaining_words.size()) + "): ");
        for (const auto& word : remaining_words) {
            output("\"" + word + "\" ");
        }
        output("\n\n");
    }
    
    // Check if we've reached the target state
    if (IsTargetState(current_state.grid)) {
        solution_path.push_back(current_state);
        output("Found target state at depth " + std::to_string(current_state.moves_count) + "!\n");
        return true;
    }
    
    // Depth limit to prevent infinite recursion
    if (current_state.moves_count >= max_depth) {
        return false;
    }
    
    // Get all possible removals
    std::vector<std::vector<std::pair<int, int>>> removals = GetRemovalCombinations(current_state.grid);
    
    // Try each removal
    for (const auto& removal : removals) {
        g_combinations_tried++;
        
        // Print progress every 10,000 combinations
        if (g_combinations_tried % 10000000 == 0) {
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed_seconds = std::chrono::duration<double>(current_time - g_start_time).count();
            double combinations_per_second = g_combinations_tried / elapsed_seconds;
            output("Combinations tried: " + std::to_string(g_combinations_tried) + 
                   " (" + std::to_string((int)combinations_per_second) + " comb/sec)\n");
        }
        
        std::vector<std::string> new_grid = ApplyRemoval(current_state.grid, removal);
        
        // Check if the resulting grid has all valid words
        bool valid = AreAllWordsValid(new_grid);
        
        if (valid) {
            GameState new_state(new_grid);
            new_state.moves_count = current_state.moves_count + 1;
            new_state.play_sequence = current_state.play_sequence;
            new_state.play_sequence.push_back(DescribeMove(removal, current_state.grid));
            
            // Recurse
            if (FindReverseSequence(new_state, solution_path, max_depth)) {
                solution_path.insert(solution_path.begin(), current_state);
                return true;
            }
        }
    }
    
    return false;
}

int main(int argc, char* argv[]) {
    // Open output file
    output_file.open(OUTPUT_FILE);
    
    // Initialize timing
    g_start_time = std::chrono::high_resolution_clock::now();
    
    // Load dictionary
    LoadDictionary(DICTIONARY);
    
    // Parse input
    if (!ParseInput(INPUT_GRID_FILE)) {
        std::cerr << "Error: Could not load input grid file: " << INPUT_GRID_FILE << std::endl;
        std::cerr << "Make sure the file exists and contains a 15x15 grid with letters and spaces." << std::endl;
        std::cerr << "Optionally mark a starting square with '*' before the letter (e.g., *A)." << std::endl;
        return 1;
    }
    
    output("Initial grid:\n");
    PrintGrid(g_grid);
    
    // Validate initial grid
    std::vector<std::string> extracted_words = ExtractWords(g_grid);
    output("Extracted words from grid:\n");
    for (const std::string& word : extracted_words) {
        output("  \"" + word + "\" (len=" + std::to_string(word.length()) + ") - " + (g_word_trie.has(word) ? "VALID" : "INVALID"));
        output(" [");
        for (char c : word) output(std::to_string((int)(unsigned char)c) + " ");
        output("]\n");
    }
    
    if (!AreAllWordsValid(g_grid)) {
        std::cerr << "Error: Initial grid contains invalid words!" << std::endl;
        return 1;
    }
    
    output("Searching for reverse play sequence...\n");
    
    // Find reverse sequence
    GameState initial_state(g_grid);
    std::vector<GameState> solution_path;
    
    if (FindReverseSequence(initial_state, solution_path)) {
        output("\n=== WORDFEUD PLAYING PLAN ===\n");
        output("Found solution in " + std::to_string(solution_path.size() - 1) + " moves!\n");
        
        // Print the solution in reverse order (from simple to complex)
        for (int i = solution_path.size() - 1; i >= 0; --i) {
            output("\n--- Step " + std::to_string(solution_path.size() - i) + " ---\n");
            if (i > 0) {
                output("Play: " + solution_path[i - 1].play_sequence.back() + "\n");
            } else {
                output("Start with this configuration:\n");
            }
            PrintGrid(solution_path[i].grid);
        }
        
        output("=== PLAYING SEQUENCE (FORWARD) ===\n");
        for (size_t i = 0; i < solution_path[0].play_sequence.size(); ++i) {
            output("Move " + std::to_string(i + 1) + ": " + solution_path[0].play_sequence[i] + "\n");
        }
        
    } else {
        output("No valid reverse sequence found within the search depth limit.\n");
        output("Try a different starting configuration or increase the search depth.\n");
    }
    
    // Print final statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_seconds = std::chrono::duration<double>(end_time - g_start_time).count();
    double avg_combinations_per_second = g_combinations_tried / total_seconds;
    output("Done. Total combinations tried: " + std::to_string(g_combinations_tried) +
           " (avg " + std::to_string((int)avg_combinations_per_second) + " comb/sec)\n");
    
    // Close output file
    if (output_file.is_open()) {
        output_file.close();
    }
    
    return 0;
}