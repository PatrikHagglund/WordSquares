.PHONY: run clean kill cmake

run: wordsquares
	stdbuf -o0 ./wordsquares 2>&1 | ts "%m%d_%H:%M:%S" | tee output.txt

wordsquares: main.cpp trie.cpp trie.h
	g++ -std=c++23 -O3 -flto=auto -march=native -mtune=native -o wordsquares main.cpp trie.cpp

cmake:
	env CXX=clang++ CXXFLAGS="-std=c++23 -O3 -flto=auto -march=native -mtune=native" \
	cmake -S . -B build -G Ninja && cmake --build build -v
	
clean: kill
	$(RM) wordsquares output.txt
	$(RM) -r build

kill:
	killall wordsquares 2>/dev/null || true
