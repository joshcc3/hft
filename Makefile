FLAGS       := -Wall -mno-sse

igb-objs := lowlatencylab/client/containers.cpp.o \
           	    lowlatencylab/client/CoroutineMngr.cpp.o lowlatencylab/client/L2OB.cpp.o lowlatencylab/client/launch.cpp.o \
           	    lowlatencylab/client/mdmcclient.cpp.o lowlatencylab/client/Strat.cpp.o \
           	    cppkern/ucontext_.s.o

ccflags-y += $(FLAGS)

# Apply C flags to the cpp compiler and disable cpp features that can't be supported in the kernel module
cxx-selected-flags = $(shell echo $(KBUILD_CFLAGS) \
            | sed s/-D\"KBUILD.\"//g \
            | sed s/-Werror=strict-prototypes//g \
            | sed s/-Werror=implicit-function-declaration//g \
            | sed s/-Werror=implicit-int//g \
            | sed s/-Wdeclaration-after-statement//g \
            | sed s/-Wno-pointer-sign//g \
            | sed s/-Werror=incompatible-pointer-types//g \
            | sed s/-Werror=designated-init//g \
            | sed s/-Wreorder//g \
            | sed s/-std=gnu11//g )

cxxflags = $(FLAGS) \
            $(cxx-selected-flags) \
            -fno-builtin \
            -nostdlib \
            -fno-rtti \
            -fno-exceptions \
            -std=c++17

HOSTCXX := g++

cxx-prefix := " $(HOSTCXX) $(cxxflags) [M]  "

%.cpp.o: %.cpp
	@echo $(cxx-prefix)$@
	@$(HOSTCXX) $(cxxflags) -c $< -o $@
	@echo -n > $$(dirname $@)/.$$(basename $@).cmd

%.s.o: %.s
	as -g $< -o $@