# Convenience targets for the MHD HK Pebble watchapp.
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
