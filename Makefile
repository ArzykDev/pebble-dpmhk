# Convenience targets for the MHD HK Pebble watchapp.
#
# In CI and inside the ubuntu distrobox, `pebble` is on PATH, so plain
# `make build` works. On the Fedora host the SDK only runs inside the box,
# so prefix every target with the distrobox wrapper:
#
#   make build PEBBLE="distrobox enter ubuntu -- pebble"
#
PEBBLE ?= pebble

.PHONY: build clean install-basalt install-emery logs

build:
	$(PEBBLE) build

# Run after editing messageKeys in package.json (regenerates message_keys.auto.*).
clean:
	$(PEBBLE) clean

install-basalt: build
	$(PEBBLE) install --emulator basalt

install-emery: build
	$(PEBBLE) install --emulator emery

logs:
	$(PEBBLE) logs
