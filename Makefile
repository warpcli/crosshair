CXX    = g++
HYPRLAND_CFLAGS = $(shell pkg-config --cflags hyprland)
PLUGIN_CXXFLAGS = $(HYPRLAND_CFLAGS) -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -std=c++2b -fPIC -fno-gnu-unique -shared

all: plugin

plugin: crosshair.so

crosshair.so: plugin/crosshair.cpp
	$(CXX) -o crosshair.so plugin/crosshair.cpp $(PLUGIN_CXXFLAGS)

clean:
	rm -f crosshair.so

.PHONY: all plugin clean
