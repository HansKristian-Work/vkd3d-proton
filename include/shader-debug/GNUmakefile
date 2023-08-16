INCLUDE_DIR := $(CURDIR)

VERT_SOURCES := $(wildcard $(M)/*.vert)
FRAG_SOURCES := $(wildcard $(M)/*.frag)
COMP_SOURCES := $(wildcard $(M)/*.comp)
TESC_SOURCES := $(wildcard $(M)/*.tesc)
TESE_SOURCES := $(wildcard $(M)/*.tese)
GEOM_SOURCES := $(wildcard $(M)/*.geom)
MESH_SOURCES := $(wildcard $(M)/*.mesh)
TASK_SOURCES := $(wildcard $(M)/*.task)
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
			   $(MESH_SOURCES:.mesh=.spv) \
			   $(TASK_SOURCES:.task=.spv) \
			   $(RGEN_SOURCES:.rgen=.spv) \
			   $(RINT_SOURCES:.rint=.spv) \
			   $(RAHIT_SOURCES:.rahit=.spv) \
			   $(RCHIT_SOURCES:.rchit=.spv) \
			   $(RMISS_SOURCES:.rmiss=.spv) \
			   $(RCALL_SOURCES:.rcall=.spv)

VULKAN_ENV := --target-env=vulkan1.1 --target-spv=spv1.4

%.spv: %.vert
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.frag
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) -DDEBUG_CHANNEL_HELPER_LANES $(GLSLC_FLAGS)

%.spv: %.comp
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.geom
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.tesc
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.tese
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.mesh
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.task
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.rgen
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.rint
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.rahit
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.rchit
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.rmiss
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

%.spv: %.rcall
	glslc -o $@ $< -I$(INCLUDE_DIR) $(VULKAN_ENV) $(GLSLC_FLAGS)

all: $(SPV_OBJECTS)

clean:
	rm -f $(SPV_OBJECTS)

.PHONY: clean
