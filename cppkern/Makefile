FLAGS       := -Wall

OBJECTS := test.o \
            cpp_support.cpp.o

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
            | sed s/-std=gnu90//g )

cxxflags = $(FLAGS) \
            $(cxx-selected-flags) \
            -fno-builtin \
            -nostdlib \
            -fno-rtti \
            -fno-exceptions \
            -std=c++17



cxx-prefix := " $(HOSTCXX) [M]  "



%.cpp.o: %.cpp
    @echo $(cxx-prefix)$@
    @$(HOSTCXX) $(cxxflags) -c $< -o $@
    @echo -n > $$(dirname $@)/.$$(basename $@).cmd

.PHONY: clean
clean:
    @echo clean
    make -C M=$(KMOD_DIR) clean