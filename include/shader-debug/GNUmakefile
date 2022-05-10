INCLUDE_DIR := $(CURDIR)

VERT_SOURCES := $(wildcard $(M)/*.vert)
FRAG_SOURCES := $(wildcard $(M)/*.frag)
COMP_SOURCES := $(wildcard $(M)/*.comp)
TESC_SOURCES := $(wildcard $(M)/*.tesc)
TESE_SOURCES := $(wildcard $(M)/*.tese)
GEOM_SOURCES := $(wildcard $(M)/*.geom)
RGEN_SOURCES := $(wildcard $(M)/*.rgen)
RINT_SOURCES := $(wildcard $(M)/*.rint)
RAHIT_SOURCES := $(wildcard $(M)/*.rahit)
RCHIT_SOURCES := $(wildcard $(M)/*.rchit)
RMISS_SOURCES := $(wildcard $(M)/*.rmiss)
RCALL_SOURCES := $(wildcard $(M)/*.rcall)

SPV_OBJECTS := \
			   $(VERT_SOURCES:.vert=.spv) \
			   $(FRAG_SOURCES:.frag=.spv) \
			   $(COMP_SOURCES:.comp=.spv) \
			   $(TESC_SOURCES:.tesc=.spv) \
			   $(TESE_SOURCES:.tese=.spv) \
			   $(GEOM_SOURCES:.geom=.spv) \
			   $(RGEN_SOURCES:.rgen=.spv) \
			   $(RINT_SOURCES:.rint=.spv) \
			   $(RAHIT_SOURCES:.rahit=.spv) \
			   $(RCHIT_SOURCES:.rchit=.spv) \
			   $(RMISS_SOURCES:.rmiss=.spv) \
			   $(RCALL_SOURCES:.rcall=.spv)

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

%.spv: %.rgen
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1 --target-spv=spv1.4

%.spv: %.rint
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1 --target-spv=spv1.4

%.spv: %.rahit
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1 --target-spv=spv1.4

%.spv: %.rchit
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1 --target-spv=spv1.4

%.spv: %.rmiss
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1 --target-spv=spv1.4

%.spv: %.rcall
	glslc -o $@ $< -I$(INCLUDE_DIR) --target-env=vulkan1.1 --target-spv=spv1.4

all: $(SPV_OBJECTS)

clean:
	rm -f $(SPV_OBJECTS)

.PHONY: clean
