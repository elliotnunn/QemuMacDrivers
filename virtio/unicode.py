import unicodedata
import binascii

# unicodedata.normalize('NFC', '\u0061\u0301')

# There are no individual code points, other than these, that match Mac Roman

def mrcomposed(ordinal):
	return unicodedata.normalize('NFC', bytes([ordinal]).decode("mac_roman"))

def mrdecomposed(ordinal):
	return unicodedata.normalize('NFD', bytes([ordinal]).decode("mac_roman"))

def cpname(cpstring):
	try:
		return unicodedata.name(cpstring)
	except:
		return f"U+{ord(cpstring):04X}"

def charlit(n):
	if n >= 32 and n < 127:
		return "'" + chr(n) + "'"
	else:
		return f"{n:#02x}"

mr2uc = []

for mr in range(256):
	mr2uc.append({
		mrcomposed(mr),
		mrdecomposed(mr),
	})

for i in range(len(mr2uc)):
	mr2uc[i] = sorted(x.encode("utf-8") for x in mr2uc[i])

for mr, uc in enumerate(mr2uc):
	if mr < 128: continue # No need to convert ascii

	for variant in uc:
		explain = " + ".join(cpname(cp) for cp in variant.decode("utf-8"))

		cond = ' && '.join(f"src[{i}]=={charlit(ch)}" for i, ch in enumerate(variant))
		print(f"else if ({cond}) {{")

		print(f"\t*dest++ = {mr:#02x}; // {explain}")
		print(f"\tsrc += {len(variant)};")

		print("} ", end="")
