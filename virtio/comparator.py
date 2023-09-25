#!/usr/bin/env python3

import sys

files = sys.argv[1:]
print(len(files))

files = [open(f).readlines() for f in files]

for i in range(len(files)):
	lines = files[i]

	blocks = []

	for l in lines:
		if len(blocks) == 0 or " FS_" in l:
			blocks.append([])
		blocks[-1].append(l)

	files[i] = blocks

print(len(files))

for i in range(99999):
	blocks = [f[i] for f in files]

	longest = max(len(b) for b in blocks)

	for b in blocks:
		while len(b) < longest:
			b.append('')

	for j in range(longest):
		lines = [b[j] for b in blocks]
		s = ''
		for l in lines:
			s += l.rstrip().ljust(80)
		s = s.rstrip()
		print(s)