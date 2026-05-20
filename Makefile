# ============================================================================
# ⚡ LightBase — Cross-Platform Makefile
# ============================================================================
# Supports: Debian/Ubuntu/Mint, Fedora/RHEL/AlmaLinux, Arch/Manjaro
#
# Usage:
#   make              — Build the C-Core and install Python deps
#   make build        — Build C-Core only
#   make run          — Start the bridge server
#   make test         — Run full test suite (75 tests)
#   make install      — System-wide install (appears in app menu)
#   make uninstall    — Remove system-wide install
#   make install-deps — Auto-detect distro and install system packages
#   make clean        — Remove all build artifacts, caches, temp files
#   make deps         — Install Python dependencies
#   make dist         — Build production bundle
# ============================================================================

.PHONY: all build run test clean deps dist install uninstall install-deps detect-distro

CORE_DIR     := core
BUILD_DIR    := $(CORE_DIR)/build_release
BRIDGE_DIR   := bridge
UI_DIR       := ui
WORKSPACE    := workspace
NPROC        := $(shell nproc 2>/dev/null || echo 4)

# Installation paths (FHS-compliant)
PREFIX       := /opt/lightbase
BINDIR       := /usr/local/bin
ICONDIR      := /usr/share/icons/hicolor
DESKTOPDIR   := /usr/share/applications

# ────────────────────────────────────────────────────────────────────────────
# Default target: build everything
# ────────────────────────────────────────────────────────────────────────────
all: deps build
	@echo ""
	@echo "⚡ LightBase build complete."
	@echo "   Run: make run       — Start the server"
	@echo "   Run: make test      — Run all tests"
	@echo "   Run: sudo make install  — Install system-wide"

# ────────────────────────────────────────────────────────────────────────────
# Detect Linux distribution
# ────────────────────────────────────────────────────────────────────────────
detect-distro:
	@if [ -f /etc/os-release ]; then \
		. /etc/os-release; \
		echo "🐧 Detected: $$NAME ($$ID family: $$ID_LIKE)"; \
	else \
		echo "⚠️  Could not detect distribution"; \
	fi

# ────────────────────────────────────────────────────────────────────────────
# Install system dependencies (auto-detects distro)
# ────────────────────────────────────────────────────────────────────────────
install-deps: detect-distro
	@echo "📦 Installing system dependencies..."
	@if [ -f /etc/os-release ]; then \
		. /etc/os-release; \
		case "$$ID" in \
			ubuntu|debian|linuxmint|pop|elementary|zorin|kali) \
				echo "  → Debian/Ubuntu family detected"; \
				sudo apt update && sudo apt install -y \
					cmake build-essential pkg-config \
					libssl-dev libsqlite3-dev libgit2-dev \
					python3 python3-pip python3-venv \
					python3-gi python3-gi-cairo gir1.2-gtk-3.0 gir1.2-webkit2-4.1 \
					curl xdg-utils ;; \
			fedora|rhel|centos|almalinux|rocky|ol) \
				echo "  → Red Hat/RPM family detected"; \
				sudo dnf install -y \
					cmake gcc gcc-c++ make pkg-config \
					openssl-devel sqlite-devel libgit2-devel \
					python3 python3-pip \
					python3-gobject gtk3 webkit2gtk4.1 \
					curl xdg-utils ;; \
			arch|manjaro|endeavouros|garuda) \
				echo "  → Arch family detected"; \
				sudo pacman -Syu --noconfirm --needed \
					cmake base-devel pkg-config \
					openssl sqlite libgit2 \
					python python-pip \
					python-gobject gtk3 webkit2gtk-4.1 \
					curl xdg-utils ;; \
			opensuse*|sles) \
				echo "  → openSUSE family detected"; \
				sudo zypper install -y \
					cmake gcc gcc-c++ make pkg-config \
					libopenssl-devel sqlite3-devel libgit2-devel \
					python3 python3-pip \
					curl xdg-utils ;; \
			*) \
				echo "  ⚠️  Unknown distro: $$ID"; \
				echo "  Please install manually: cmake, gcc, openssl-dev, sqlite3-dev, libgit2-dev, python3, pip"; \
				exit 1 ;; \
		esac; \
	else \
		echo "  ⚠️  Cannot detect distro. Install deps manually."; \
		exit 1; \
	fi
	@echo "✅ System dependencies installed."

# ────────────────────────────────────────────────────────────────────────────
# Build the C-Core via CMake
# ────────────────────────────────────────────────────────────────────────────
build:
	@echo "🔨 Building C-Core..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release .. 2>&1 | tail -1
	@cd $(BUILD_DIR) && make -j$(NPROC) 2>&1
	@echo "✅ C-Core built: $(BUILD_DIR)/libcore.so"

# ────────────────────────────────────────────────────────────────────────────
# Install Python dependencies
# ────────────────────────────────────────────────────────────────────────────
deps:
	@echo "📦 Installing Python dependencies..."
	@pip install -q -r requirements.txt 2>/dev/null || pip3 install -q -r requirements.txt 2>/dev/null || true
	@echo "✅ Python dependencies installed."

# ────────────────────────────────────────────────────────────────────────────
# Start the bridge server
# ────────────────────────────────────────────────────────────────────────────
run:
	@echo "🚀 Starting LightBase..."
	@cd $(BRIDGE_DIR) && python3 python_bridge.py

# ────────────────────────────────────────────────────────────────────────────
# Run full test suite
# ────────────────────────────────────────────────────────────────────────────
test:
	@echo "🧪 Running test suite..."
	@cd $(BRIDGE_DIR) && python3 python_bridge.py &
	@sleep 4
	@python3 test_all.py; EXIT_CODE=$$?; \
	 pkill -f python_bridge.py 2>/dev/null; \
	 exit $$EXIT_CODE

# ────────────────────────────────────────────────────────────────────────────
# System-wide install (requires sudo)
# Installs to /opt/lightbase with launcher in /usr/local/bin
# Registers desktop entry so it appears in app menus
# ────────────────────────────────────────────────────────────────────────────
install: build
	@echo "🚀 Installing LightBase system-wide..."

	@# 1. Create installation directory
	@sudo mkdir -p $(PREFIX)
	@sudo mkdir -p $(PREFIX)/core/build_release
	@sudo mkdir -p $(PREFIX)/bridge
	@sudo mkdir -p $(PREFIX)/ui
	@sudo mkdir -p $(PREFIX)/workspace/collections
	@sudo mkdir -p $(PREFIX)/workspace/environments
	@sudo mkdir -p $(PREFIX)/workspace/plugins
	@sudo mkdir -p $(PREFIX)/workspace/flows
	@sudo mkdir -p $(PREFIX)/workspace/history
	@sudo mkdir -p $(PREFIX)/workspace/monitors
	@sudo mkdir -p $(PREFIX)/workspace/reports
	@sudo mkdir -p $(PREFIX)/workspace/exports
	@sudo mkdir -p $(PREFIX)/workspace/docs/html
	@sudo mkdir -p $(PREFIX)/docs
	@sudo mkdir -p $(PREFIX)/assets/icons

	@# 2. Copy C-Core library
	@sudo cp $(BUILD_DIR)/libcore.so $(PREFIX)/core/build_release/
	@sudo cp $(BUILD_DIR)/lb-cli $(PREFIX)/core/build_release/ 2>/dev/null || true
	@sudo cp -r $(CORE_DIR)/include $(PREFIX)/core/

	@# 3. Copy bridge
	@sudo cp $(BRIDGE_DIR)/python_bridge.py $(PREFIX)/bridge/
	@sudo cp $(BRIDGE_DIR)/enterprise.py $(PREFIX)/bridge/

	@# 4. Copy UI
	@sudo cp -r $(UI_DIR)/* $(PREFIX)/ui/

	@# 5. Copy docs
	@sudo cp docs/user_guide.md $(PREFIX)/docs/ 2>/dev/null || true

	@# 6. Copy assets
	@sudo cp assets/logo.png $(PREFIX)/assets/
	@sudo cp -r assets/icons $(PREFIX)/assets/

	@# 7. Install launcher script
	@sudo cp assets/lightbase-launcher.sh $(BINDIR)/lightbase
	@sudo chmod +x $(BINDIR)/lightbase

	@# 8. Install icons to hicolor theme (all sizes)
	@for size in 16 32 48 64 128 256 512; do \
		sudo mkdir -p $(ICONDIR)/$${size}x$${size}/apps; \
		sudo cp assets/icons/lightbase_$${size}x$${size}.png \
			$(ICONDIR)/$${size}x$${size}/apps/lightbase.png; \
	done

	@# 9. Install .desktop entry
	@sudo cp assets/lightbase.desktop $(DESKTOPDIR)/lightbase.desktop
	@sudo chmod 644 $(DESKTOPDIR)/lightbase.desktop

	@# 10. Update icon cache and desktop database
	@sudo gtk-update-icon-cache $(ICONDIR) 2>/dev/null || true
	@sudo update-desktop-database $(DESKTOPDIR) 2>/dev/null || true

	@# 11. Set permissions (user-writable workspace)
	@sudo chmod -R 777 $(PREFIX)/workspace

	@echo ""
	@echo "✅ LightBase installed successfully!"
	@echo "   📍 Location:  $(PREFIX)"
	@echo "   🚀 Launcher:  $(BINDIR)/lightbase"
	@echo "   🖥️  App menu:  Search for 'LightBase Studio'"
	@echo ""
	@echo "   Run: lightbase"

# ────────────────────────────────────────────────────────────────────────────
# Uninstall
# ────────────────────────────────────────────────────────────────────────────
uninstall:
	@echo "🗑️  Uninstalling LightBase..."
	@sudo rm -rf $(PREFIX)
	@sudo rm -f $(BINDIR)/lightbase
	@sudo rm -f $(DESKTOPDIR)/lightbase.desktop
	@for size in 16 32 48 64 128 256 512; do \
		sudo rm -f $(ICONDIR)/$${size}x$${size}/apps/lightbase.png; \
	done
	@sudo gtk-update-icon-cache $(ICONDIR) 2>/dev/null || true
	@sudo update-desktop-database $(DESKTOPDIR) 2>/dev/null || true
	@echo "✅ LightBase uninstalled."

# ────────────────────────────────────────────────────────────────────────────
# Clean ALL build artifacts, caches, and temp files
# ────────────────────────────────────────────────────────────────────────────
clean:
	@echo "🧹 Cleaning..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(CORE_DIR)/__pycache__ $(BRIDGE_DIR)/__pycache__
	@rm -rf __pycache__ .pytest_cache .mypy_cache
	@find . -name '*.pyc' -delete 2>/dev/null || true
	@find . -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true
	@rm -f /tmp/lightbase.sock
	@rm -f *.log $(BRIDGE_DIR)/*.log $(CORE_DIR)/*.log
	@rm -f lightbase_telemetry.log
	@rm -rf $(WORKSPACE)/reports/*.xml $(WORKSPACE)/reports/*.html
	@rm -rf $(WORKSPACE)/exports/workspace_export.json
	@rm -rf $(WORKSPACE)/docs/html/*.html
	@echo "✅ Clean complete."

# ────────────────────────────────────────────────────────────────────────────
# Production distribution bundle
# ────────────────────────────────────────────────────────────────────────────
dist: build
	@echo "📦 Creating distribution bundle..."
	@mkdir -p dist/lib dist/bin dist/include
	@cp $(BUILD_DIR)/libcore.so dist/lib/
	@cp $(BUILD_DIR)/lb-cli dist/bin/ 2>/dev/null || true
	@cp $(CORE_DIR)/include/engine.h dist/include/
	@echo "✅ Distribution bundle created in dist/"
