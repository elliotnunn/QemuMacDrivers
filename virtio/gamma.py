#!/usr/bin/env python3

def srgb(n):
	if n <= 0.04045:
		return n / 12.92
	else:
		return ((r + 0.055) / 1.055) ** 2.4


for i in range(256):
	j = int(round(((i / 255) ** (1.8/2.4)) * 255))
	print(f'{j:#04x}, ', end='')
	if (i + 1) % 8 == 0:
		print('\n', end='')
