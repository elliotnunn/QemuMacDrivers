#ifndef ATOMIC_H
#define ATOMIC_H
void Atomic(void (*func)(void));
void Atomic1(void (*func)(void *), void *);
void Atomic2(void (*func)(void *, void *), void *, void *);
#endif