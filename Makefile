.PHONY: run run-simple clean kill cmake pgo-instrument pgo-optimize pgo-train pgo pgo-run \
        podman-image podman-gcc podman-clang podman-pgo podman-run \
        remote-sync remote-podman-run remote-podman-shell remote-fetch-results \
        remote-tmux-run remote-tmux-attach remote-tmux-status \
        gcloud-start-remote gcloud-stop-remote gcloud-status-remote

# Allow overriding compiler, default to g++ -std=c++23
# Only set CXX if not already defined (including command line)
ifeq ($(origin CXX),default)
CXX = g++ -std=c++23
endif
CXXFLAGS ?= -O3 -flto=auto -march=native -mtune=native -static

# Current default target
remote-tmux-run:

run: wordsquares WordFeud_ordlista.txt
	stdbuf -o0 ./wordsquares 2>&1 | ts "%m%d_%H:%M:%S" | tee output.txt

run-simple: wordsquares WordFeud_ordlista.txt
	./wordsquares

WordFeud_ordlista.txt:
	printf '%s\n' A B C D E F G H I J K L M N O P Q R S T U V W X Y Z Å Ä Ö > WordFeud_ordlista.txt
	curl -s https://raw.githubusercontent.com/38DavidH/WordFeud-SAOL/refs/heads/main/WordFeud_ordlista.txt >> WordFeud_ordlista.txt

wordsquares: main.cpp trie.cpp trie.h
	$(CXX) $(CXXFLAGS) -o wordsquares main.cpp trie.cpp

cmake:
	env CXX="clang++ -std=c++23"  \
	cmake -S . -B build -G Ninja && cmake --build build -v
	
# Timeout for PGO training run (override: make PGO_TIMEOUT=120s pgo)
PGO_TIMEOUT ?= 20s

# Directory to store/read GCC PGO profiles
PGO_DIR ?= pgo-profile

pgo-instrument: main.cpp trie.cpp trie.h
	# Build instrumented binary as 'wordsquares' so profile filenames match during use
	$(CXX) $(CXXFLAGS) -fprofile-generate=$(PGO_DIR) -fprofile-update=atomic -DENABLE_PGO_FLUSH -o wordsquares main.cpp trie.cpp

pgo-optimize: main.cpp trie.cpp trie.h
	# Use same defines as instrumented build to match CFG with profiles
	$(CXX) $(CXXFLAGS) -fprofile-use=$(PGO_DIR) -fprofile-correction -DENABLE_PGO_FLUSH -o wordsquares main.cpp trie.cpp

# Run the instrumented binary with a graceful timeout to generate profiles
pgo-train: wordsquares
	mkdir -p $(PGO_DIR)
	rm -f $(PGO_DIR)/*.gcda 2>/dev/null || true
	timeout --signal=INT --kill-after=5s $(PGO_TIMEOUT) ./wordsquares || true

# Full PGO pipeline: instrument -> train (with timeout) -> optimize
pgo: pgo-instrument pgo-train pgo-optimize

pgo-run: pgo run

clean: kill
	$(RM) wordsquares output.txt wordsquares_instrumented wordsquares_optimized
	$(RM) -r build
	$(RM) *.gcda *.gcno

kill:
	killall wordsquares wordsquares_instrumented 2>/dev/null || true

# -------- Podman-based Fedora Rawhide builds --------

PODMAN_IMAGE ?= wordsquares-rawhide

podman-image: Containerfile.rawhide
	@if ! command -v podman >/dev/null 2>&1; then \
		echo "Podman not found, installing..."; \
		sudo apt update && sudo apt install -y podman; \
	fi
	podman build --pull=always -t $(PODMAN_IMAGE) -f Containerfile.rawhide .

podman-gcc: podman-image
	podman run --rm -v "$$(pwd)":/src:Z -w /src $(PODMAN_IMAGE) \
	  make CXX="g++ -std=c++26" wordsquares

podman-clang: podman-image
	podman run --rm -v "$$(pwd)":/src:Z -w /src $(PODMAN_IMAGE) \
	  make CXX="clang++ -std=c++26" wordsquares

podman-pgo: podman-image
	podman run --rm -v "$$(pwd)":/src:Z -w /src -e PGO_TIMEOUT=$(PGO_TIMEOUT) $(PODMAN_IMAGE) \
	  make CXX="g++ -std=c++26" pgo

podman-run: podman-pgo
	podman run -it --rm -v "$$(pwd)":/src:Z -w /src $(PODMAN_IMAGE) \
	  make run

# -------- Remote SSH execution on dev-2025-2 --------

REMOTE_HOST ?= dev-2025-2
GCLOUD_ZONE ?= europe-west4-c
REMOTE_WORKDIR ?= $(CURDIR)
TMUX_SESSION ?= wordsquares-run

# -------- Google Cloud VM management --------

# List available GCP VM instances
gcloud-list:
	gcloud compute instances list

# Start remote GCP VM instance
gcloud-start-remote:
	@echo "Starting GCP instance $(REMOTE_HOST)"
	gcloud compute instances start $(REMOTE_HOST) --zone=$(GCLOUD_ZONE)
	@echo "Waiting for SSH connectivity..."
	@for i in {1..30}; do \
		if ssh -o ConnectTimeout=5 -o BatchMode=yes $(REMOTE_HOST) true 2>/dev/null; then \
			echo "SSH connection established"; \
			break; \
		fi; \
		echo "Attempt $$i/30: waiting for SSH..."; \
		sleep 10; \
	done

# Stop remote GCP VM instance  
gcloud-stop-remote:
	@echo "Stopping GCP instance $(REMOTE_HOST)"
	gcloud compute instances stop $(REMOTE_HOST) --zone=$(GCLOUD_ZONE)

# Check status of remote GCP VM instance
gcloud-status-remote:
	gcloud compute instances describe $(REMOTE_HOST) --zone=$(GCLOUD_ZONE) --format="value(status)"

# Sync source code to remote host
remote-sync: gcloud-start-remote
	ssh $(REMOTE_HOST) "mkdir -p $(REMOTE_WORKDIR)"
	rsync -av --delete \
	  --exclude='.git*' --exclude='build/' --exclude='pgo-profile/' --exclude='*.gcda' --exclude='*.gcno' \
	  --exclude='wordsquares*' --exclude='output*.txt' \
	  ./ $(REMOTE_HOST):$(REMOTE_WORKDIR)/

# Build and run with podman on remote host
remote-podman-run: remote-sync
	ssh $(REMOTE_HOST) "cd $(REMOTE_WORKDIR) && make podman-run"

# Interactive shell in remote podman container
remote-podman-shell: remote-sync
	ssh -t $(REMOTE_HOST) "cd $(REMOTE_WORKDIR) && podman run -it --rm -v \$$(pwd):/src:Z -w /src $(PODMAN_IMAGE) bash"

# Copy results back from remote
remote-fetch-results:
	rsync -av $(REMOTE_HOST):$(REMOTE_WORKDIR)/output*.txt ./
	rsync -av $(REMOTE_HOST):$(REMOTE_WORKDIR)/wordsquares ./ 2>/dev/null || true

# -------- Long-running persistent execution with tmux --------

# Start long-running job in persistent tmux session on remote
remote-tmux-run: remote-sync
	ssh $(REMOTE_HOST) "cd $(REMOTE_WORKDIR) && \
	  if ! command -v tmux >/dev/null 2>&1; then \
	    echo 'tmux not found, installing...'; \
	    sudo apt update && sudo apt install -y tmux; \
	  fi && \
	  tmux kill-session -t $(TMUX_SESSION) 2>/dev/null || true && \
	  tmux new-session -d -s $(TMUX_SESSION) && \
	  if ! command -v make >/dev/null 2>&1; then \
	    echo 'make not found, installing...'; \
	    sudo apt update && sudo apt install -y make; \
	  fi && \
	  tmux send-keys -t $(TMUX_SESSION) 'make podman-run' Enter"
	@echo "Started wordsquares in tmux session '$(TMUX_SESSION)' on $(REMOTE_HOST)"
	@echo "To attach: make remote-tmux-attach"
	@echo "To check status: make remote-tmux-status"

# Attach to running tmux session on remote
remote-tmux-attach:
	ssh -t $(REMOTE_HOST) "tmux attach-session -t $(TMUX_SESSION)"

# Check status of tmux session without attaching
remote-tmux-status:
	@echo "Tmux session status on $(REMOTE_HOST):"
	ssh $(REMOTE_HOST) "tmux list-sessions | grep $(TMUX_SESSION) || echo 'Session $(TMUX_SESSION) not found'"
	@echo ""
	@echo "Recent output from session:"
	ssh $(REMOTE_HOST) "tmux capture-pane -t $(TMUX_SESSION) -p | tail -10 2>/dev/null || echo 'Cannot capture pane - session may not exist'"
