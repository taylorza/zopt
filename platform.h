#ifndef PLATFORM_H__
#define PLATFORM_H__

#ifdef __SDCC
#define MYCC __sdcccall(1)
#else
#define MYCC
#endif

void* safe_alloc(size_t size, uint32_t line);

#endif //PLATFORM_H__