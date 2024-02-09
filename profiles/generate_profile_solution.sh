#!/bin/bash

mkdir -p profiles/vulkan/debug

if [ -z ${VULKAN_PROFILE_PREFIX} ]; then
	VULKAN_PROFILE_PREFIX="$HOME/.local"
	echo "Using VULKAN_PROFILE_PREFIX=$VULKAN_PROFILE_PREFIX."
fi

# Install Vulkan-Profiles to ~/.local prefix.

python3 "${VULKAN_PROFILE_PREFIX}/share/vulkan/registry/gen_profiles_solution.py" \
	--registry khronos/Vulkan-Headers/registry/vk.xml \
	--input . \
	--output-library-inc profiles/vulkan \
	--output-library-src profiles/vulkan \
	--output-doc profiles/PROFILES.md \
	--validate --debug
