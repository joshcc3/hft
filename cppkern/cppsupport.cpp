//
// Created by joshuacoutinho on 15/12/23.
//

/*
 * Kernel C++ support
 */

#include <cstddef>

void *operator new(size_t sz) throw ()
{
	return NULL;
}

void *operator new[](size_t sz) throw ()
{
	return NULL;
}

void operator delete(void *p)
{
}

void operator delete[](void *p)
{
}

void terminate()
{
}

extern "C" void __cxa_pure_virtual()
{
}