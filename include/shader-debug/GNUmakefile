INCLUDE_DIR := $(CURDIR)

VERT_SOURCES := $(wildcard $(M)/*.vert)
FRAG_SOURCES := $(wildcard $(M)/*.frag)
COMP_SOURCES := $(wildcard $(M)/*.comp)
TESC_SOURCES := $(wildcard $(M)/*.tesc)
TESE_SOURCES := $(wildcard $(M)/*.tese)
GEOM_SOURCES := $(wildcard $(M)/*.geom)

SPV_OBJECTS := \
			   $(VERT_SOURCES:.vert=.spv) \
			   $(FRAG_SOURCES:.frag=.spv) \
			   $(COMP_SOURCES:.comp=.spv) \
			   $(TESC_SOURCES:.tesc=.spv) \
			   $(TESE_SOURCES:.tese=.spv) \
			   $(GEOM_SOURCES:.geom=.spv)

%.spv: %.vert
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1

%.spv: %.frag
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1 -DDEBUG_CHANNEL_HELPER_LANES

%.spv: %.comp
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1

%.spv: %.geom
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1

%.spv: %.tesc
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1

%.spv: %.tese
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1

all: $(SPV_OBJECTS)

clean:
	rm -f $(SPV_OBJECTS)

.PHONY: clean
