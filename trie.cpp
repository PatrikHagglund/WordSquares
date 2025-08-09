#include "trie.h"
#include <cassert>
#include <iostream>

Trie::Trie() {
  std::fill(nodes, nodes + NUM_LETTERS, nullptr);
  is_word_end = false;
}

Trie::~Trie() {
  Iter i = iter();
  while (i.next()) { delete i.get(); }
}

void Trie::add(const std::string& str) {
  Trie* ptr = this;
  for (char c : str) {
    const int ix = c - 'A';
    if (ix < 0 || ix >= NUM_LETTERS) {
      std::cerr << "Invalid character '" << c << "' (code " << (int)c << ") in word: " << str << std::endl;
    }
    assert(ix >= 0 && ix < NUM_LETTERS);
    if (ptr->nodes[ix] == nullptr) {
      ptr->nodes[ix] = new Trie();
    }
    ptr = ptr->nodes[ix];
  }
  ptr->is_word_end = true;
}

bool Trie::has(const std::string& str) const {
  if (str.empty()) return false;
  
  const Trie* ptr = this;
  for (char c : str) {
    const int ix = c - 'A';
    if (ix < 0 || ix >= NUM_LETTERS) {
      return false; // Invalid character
    }
    if (ptr->nodes[ix] == nullptr) {
      return false;
    } else {
      ptr = ptr->nodes[ix];
    }
  }
  return ptr->is_word_end;
}

bool Trie::hasPrefix(const std::string& str) const {
  if (str.empty()) return true;
  
  const Trie* ptr = this;
  for (char c : str) {
    const int ix = c - 'A';
    if (ix < 0 || ix >= NUM_LETTERS) {
      return false; // Invalid character
    }
    if (ptr->nodes[ix] == nullptr) {
      return false;
    } else {
      ptr = ptr->nodes[ix];
    }
  }
  //For prefix check, we just need to reach the end of the string
  //We don't need to check if it's a complete word
  return true;
}
