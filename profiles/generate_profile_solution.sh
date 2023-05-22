#!/bin/bash

mkdir -p profiles/vulkan

python3 ~/.local/share/vulkan/registry/gen_profiles_solution.py \
	--registry ~/git/Vulkan-Headers/registry/vk.xml \
	--input . \
	--output-library-inc profiles/vulkan \
	--output-library-src profiles/vulkan \
	--validate
