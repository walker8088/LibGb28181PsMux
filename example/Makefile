TOOLCHAIN   := 

# target program
TARGET_PROG := PsMuxExample

# target library (static)
TARGET_LIB_STATIC  := 

# target library (dynamic)
TARGET_LIB_DYNAMIC := 

# for intermediate files
TEMP_DIR    := obj

# list of all source files
SOURCES     := PsMuxExample.cpp

# include and library 
INC_DIRS    := -I../libpsmux/inc

LIB_DIRS    := -L../lib

LD_LIBS     := -lpsmux -lfaac
 
# complie & link variable
ifeq ($(MAKECMDGOALS), debug)   
    CFLAGS := -Wall -O0 -g
else
    CFLAGS := -O2
endif   
ifneq ($(strip $(TARGET_LIB_DYNAMIC)),)
    CFLAGS += -fPIC -shared
endif
CFLAGS     += -DNXP_CHIPSET_ASC8850
CFLAGS     += $(INC_DIRS)
CXXFLAGS    = $(CFLAGS)
LDFLAGS    := 

# shell command
CC    := $(TOOLCHAIN)gcc
CXX   := $(TOOLCHAIN)g++
AR    := $(TOOLCHAIN)ar
RM    := rm -rf
MKDIR := mkdir -p
SED   := sed
MV    := mv

# init sources & objects & depends
source_short_name := $(notdir $(SOURCES))
sources_c   := $(filter %.c, $(SOURCES))
sources_cpp := $(filter %.cpp, $(SOURCES))
source_base_name := $(basename $(source_short_name))
objs        := $(addprefix $(TEMP_DIR)/, $(addsuffix .o, $(source_base_name)))
deps        := $(addprefix $(TEMP_DIR)/, $(addsuffix .d, $(source_base_name)))

# create intermediate file directory
$(shell $(MKDIR) $(TEMP_DIR))

# add vpath
vpath %.c $(sort $(dir $(sources_c)))
vpath %.cpp $(sort $(dir $(sources_cpp)))

# make-depend(depend-file,source-file,object-file,cc)
define make-depend
  $(RM) $1;                                     \
  $4 $(CFLAGS) -MM $2 |                         \
  $(SED) 's,\($(notdir $3)\): ,$3: ,' > $1;
endef

.PHONY: release debug clean show

all   := $(TARGET_PROG) $(TARGET_LIB_STATIC) $(TARGET_LIB_DYNAMIC)

release: $(all)

debug: $(all)

$(TARGET_PROG): $(objs)
ifeq "$(strip $(sources_cpp))" ""
	$(CC) $(LDFLAGS) $^ $(LIB_DIRS) $(LD_LIBS) -o $@
else
	$(CXX) $(LDFLAGS) $^ $(LIB_DIRS) $(LD_LIBS) -o $@
endif

$(TARGET_LIB_STATIC): $(objs)
	$(AR) -r $@ $^

$(TARGET_LIB_DYNAMIC): $(objs)
ifeq "$(strip $(sources_cpp))" ""
	$(CC) $(LDFLAGS) -fPIC -shared $^ $(LIB_DIRS) $(LD_LIBS) -o $@
else
	$(CXX) $(LDFLAGS)-fPIC -shared $^ $(LIB_DIRS) $(LD_LIBS) -o $@
endif

# generated depend files
# actually generated after object generatedd, beacasue it only used when next make)
ifneq "$(MAKECMDGOALS)" "clean"
include $(deps)
endif

# rules to generated objects file
$(TEMP_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

$(TEMP_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

# rules to generated depends file
$(TEMP_DIR)/%.d: %.c
	@$(call make-depend,$@,$<,$(patsubst %.d,%.o,$@),$(CC))

$(TEMP_DIR)/%.d: %.cpp
	@$(call make-depend,$@,$<,$(patsubst %.d,%.o,$@),$(CXX))

clean: 
	$(RM) $(TEMP_DIR)
	$(RM) $(TARGET_PROG) $(TARGET_LIB_STATIC) $(TARGET_LIB_DYNAMIC)

show:
	@echo $(objs)
