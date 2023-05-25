#pragma once

void Atomic(void (*func)(void));
void Atomic1(void (*func)(void *), void *);
void Atomic2(void (*func)(void *, void *), void *, void *);
