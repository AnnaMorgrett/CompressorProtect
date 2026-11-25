#pragma clang optimize off
#include <stddef.h>
__attribute__((weak)) void *memcpy(void *d, const void *s, size_t n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;return d;}
__attribute__((weak)) void *memmove(void *d, const void *s, size_t n){unsigned char *a=d;const unsigned char *b=s;if(a<b){while(n--)*a++=*b++;}else{a+=n;b+=n;while(n--)*--a=*--b;}return d;}
__attribute__((weak)) void *memset(void *d, int c, size_t n){unsigned char *a=d;while(n--)*a++=(unsigned char)c;return d;}
__attribute__((weak)) int memcmp(const void *x, const void *y, size_t n){const unsigned char *a=x,*b=y;while(n--){if(*a!=*b)return *a-*b;a++;b++;}return 0;}
__attribute__((weak)) size_t strlen(const char *s){const char *p=s;while(*p)p++;return (size_t)(p-s);}
__attribute__((weak)) void *memchr(const void *s, int c, size_t n){const unsigned char *p=s;while(n--){if(*p==(unsigned char)c)return (void*)p;p++;}return 0;}
__attribute__((weak)) int strcmp(const char *a, const char *b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
__attribute__((weak)) char *strcpy(char *d, const char *s){char *r=d;while((*d++=*s++));return r;}
