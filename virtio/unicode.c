#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "unicode.h"

/*
TODO:
Preserve file extensions
Represent unknown chars with fewer than one-per-UTF8-byte "?"s
*/

void mr31name(unsigned char *roman, char *utf8) {
	unsigned char *src = (unsigned char *)utf8;
	unsigned char *dest = roman+1;
	int badchar = 0;

	while (*src!=0 && dest<roman+32) {
		if (src[0]==':') {
			*dest++ = '/';
			src += 1;
		} else if (src[0]<0x80) {
			*dest++ = src[0];
			src += 1;
		} else if (src[0]=='A' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0x80; // LATIN CAPITAL LETTER A + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x84) {
			*dest++ = 0x80; // LATIN CAPITAL LETTER A WITH DIAERESIS
			src += 2;
		} else if (src[0]=='A' && src[1]==0xcc && src[2]==0x8a) {
			*dest++ = 0x81; // LATIN CAPITAL LETTER A + COMBINING RING ABOVE
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x85) {
			*dest++ = 0x81; // LATIN CAPITAL LETTER A WITH RING ABOVE
			src += 2;
		} else if (src[0]=='C' && src[1]==0xcc && src[2]==0xa7) {
			*dest++ = 0x82; // LATIN CAPITAL LETTER C + COMBINING CEDILLA
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x87) {
			*dest++ = 0x82; // LATIN CAPITAL LETTER C WITH CEDILLA
			src += 2;
		} else if (src[0]=='E' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0x83; // LATIN CAPITAL LETTER E + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x89) {
			*dest++ = 0x83; // LATIN CAPITAL LETTER E WITH ACUTE
			src += 2;
		} else if (src[0]=='N' && src[1]==0xcc && src[2]==0x83) {
			*dest++ = 0x84; // LATIN CAPITAL LETTER N + COMBINING TILDE
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x91) {
			*dest++ = 0x84; // LATIN CAPITAL LETTER N WITH TILDE
			src += 2;
		} else if (src[0]=='O' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0x85; // LATIN CAPITAL LETTER O + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x96) {
			*dest++ = 0x85; // LATIN CAPITAL LETTER O WITH DIAERESIS
			src += 2;
		} else if (src[0]=='U' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0x86; // LATIN CAPITAL LETTER U + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x9c) {
			*dest++ = 0x86; // LATIN CAPITAL LETTER U WITH DIAERESIS
			src += 2;
		} else if (src[0]=='a' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0x87; // LATIN SMALL LETTER A + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa1) {
			*dest++ = 0x87; // LATIN SMALL LETTER A WITH ACUTE
			src += 2;
		} else if (src[0]=='a' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0x88; // LATIN SMALL LETTER A + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa0) {
			*dest++ = 0x88; // LATIN SMALL LETTER A WITH GRAVE
			src += 2;
		} else if (src[0]=='a' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0x89; // LATIN SMALL LETTER A + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa2) {
			*dest++ = 0x89; // LATIN SMALL LETTER A WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='a' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0x8a; // LATIN SMALL LETTER A + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa4) {
			*dest++ = 0x8a; // LATIN SMALL LETTER A WITH DIAERESIS
			src += 2;
		} else if (src[0]=='a' && src[1]==0xcc && src[2]==0x83) {
			*dest++ = 0x8b; // LATIN SMALL LETTER A + COMBINING TILDE
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa3) {
			*dest++ = 0x8b; // LATIN SMALL LETTER A WITH TILDE
			src += 2;
		} else if (src[0]=='a' && src[1]==0xcc && src[2]==0x8a) {
			*dest++ = 0x8c; // LATIN SMALL LETTER A + COMBINING RING ABOVE
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa5) {
			*dest++ = 0x8c; // LATIN SMALL LETTER A WITH RING ABOVE
			src += 2;
		} else if (src[0]=='c' && src[1]==0xcc && src[2]==0xa7) {
			*dest++ = 0x8d; // LATIN SMALL LETTER C + COMBINING CEDILLA
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa7) {
			*dest++ = 0x8d; // LATIN SMALL LETTER C WITH CEDILLA
			src += 2;
		} else if (src[0]=='e' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0x8e; // LATIN SMALL LETTER E + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa9) {
			*dest++ = 0x8e; // LATIN SMALL LETTER E WITH ACUTE
			src += 2;
		} else if (src[0]=='e' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0x8f; // LATIN SMALL LETTER E + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xa8) {
			*dest++ = 0x8f; // LATIN SMALL LETTER E WITH GRAVE
			src += 2;
		} else if (src[0]=='e' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0x90; // LATIN SMALL LETTER E + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xaa) {
			*dest++ = 0x90; // LATIN SMALL LETTER E WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='e' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0x91; // LATIN SMALL LETTER E + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xab) {
			*dest++ = 0x91; // LATIN SMALL LETTER E WITH DIAERESIS
			src += 2;
		} else if (src[0]=='i' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0x92; // LATIN SMALL LETTER I + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xad) {
			*dest++ = 0x92; // LATIN SMALL LETTER I WITH ACUTE
			src += 2;
		} else if (src[0]=='i' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0x93; // LATIN SMALL LETTER I + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xac) {
			*dest++ = 0x93; // LATIN SMALL LETTER I WITH GRAVE
			src += 2;
		} else if (src[0]=='i' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0x94; // LATIN SMALL LETTER I + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xae) {
			*dest++ = 0x94; // LATIN SMALL LETTER I WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='i' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0x95; // LATIN SMALL LETTER I + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xaf) {
			*dest++ = 0x95; // LATIN SMALL LETTER I WITH DIAERESIS
			src += 2;
		} else if (src[0]=='n' && src[1]==0xcc && src[2]==0x83) {
			*dest++ = 0x96; // LATIN SMALL LETTER N + COMBINING TILDE
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xb1) {
			*dest++ = 0x96; // LATIN SMALL LETTER N WITH TILDE
			src += 2;
		} else if (src[0]=='o' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0x97; // LATIN SMALL LETTER O + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xb3) {
			*dest++ = 0x97; // LATIN SMALL LETTER O WITH ACUTE
			src += 2;
		} else if (src[0]=='o' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0x98; // LATIN SMALL LETTER O + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xb2) {
			*dest++ = 0x98; // LATIN SMALL LETTER O WITH GRAVE
			src += 2;
		} else if (src[0]=='o' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0x99; // LATIN SMALL LETTER O + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xb4) {
			*dest++ = 0x99; // LATIN SMALL LETTER O WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='o' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0x9a; // LATIN SMALL LETTER O + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xb6) {
			*dest++ = 0x9a; // LATIN SMALL LETTER O WITH DIAERESIS
			src += 2;
		} else if (src[0]=='o' && src[1]==0xcc && src[2]==0x83) {
			*dest++ = 0x9b; // LATIN SMALL LETTER O + COMBINING TILDE
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xb5) {
			*dest++ = 0x9b; // LATIN SMALL LETTER O WITH TILDE
			src += 2;
		} else if (src[0]=='u' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0x9c; // LATIN SMALL LETTER U + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xba) {
			*dest++ = 0x9c; // LATIN SMALL LETTER U WITH ACUTE
			src += 2;
		} else if (src[0]=='u' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0x9d; // LATIN SMALL LETTER U + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xb9) {
			*dest++ = 0x9d; // LATIN SMALL LETTER U WITH GRAVE
			src += 2;
		} else if (src[0]=='u' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0x9e; // LATIN SMALL LETTER U + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xbb) {
			*dest++ = 0x9e; // LATIN SMALL LETTER U WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='u' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0x9f; // LATIN SMALL LETTER U + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xbc) {
			*dest++ = 0x9f; // LATIN SMALL LETTER U WITH DIAERESIS
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0xa0) {
			*dest++ = 0xa0; // DAGGER
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xb0) {
			*dest++ = 0xa1; // DEGREE SIGN
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xa2) {
			*dest++ = 0xa2; // CENT SIGN
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xa3) {
			*dest++ = 0xa3; // POUND SIGN
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xa7) {
			*dest++ = 0xa4; // SECTION SIGN
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0xa2) {
			*dest++ = 0xa5; // BULLET
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xb6) {
			*dest++ = 0xa6; // PILCROW SIGN
			src += 2;
		} else if (src[0]==0xc3 && src[1]==0x9f) {
			*dest++ = 0xa7; // LATIN SMALL LETTER SHARP S
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xae) {
			*dest++ = 0xa8; // REGISTERED SIGN
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xa9) {
			*dest++ = 0xa9; // COPYRIGHT SIGN
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x84 && src[2]==0xa2) {
			*dest++ = 0xaa; // TRADE MARK SIGN
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xb4) {
			*dest++ = 0xab; // ACUTE ACCENT
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xa8) {
			*dest++ = 0xac; // DIAERESIS
			src += 2;
		} else if (src[0]=='=' && src[1]==0xcc && src[2]==0xb8) {
			*dest++ = 0xad; // EQUALS SIGN + COMBINING LONG SOLIDUS OVERLAY
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x89 && src[2]==0xa0) {
			*dest++ = 0xad; // NOT EQUAL TO
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x86) {
			*dest++ = 0xae; // LATIN CAPITAL LETTER AE
			src += 2;
		} else if (src[0]==0xc3 && src[1]==0x98) {
			*dest++ = 0xaf; // LATIN CAPITAL LETTER O WITH STROKE
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x88 && src[2]==0x9e) {
			*dest++ = 0xb0; // INFINITY
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xb1) {
			*dest++ = 0xb1; // PLUS-MINUS SIGN
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x89 && src[2]==0xa4) {
			*dest++ = 0xb2; // LESS-THAN OR EQUAL TO
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x89 && src[2]==0xa5) {
			*dest++ = 0xb3; // GREATER-THAN OR EQUAL TO
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xa5) {
			*dest++ = 0xb4; // YEN SIGN
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xb5) {
			*dest++ = 0xb5; // MICRO SIGN
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x88 && src[2]==0x82) {
			*dest++ = 0xb6; // PARTIAL DIFFERENTIAL
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x88 && src[2]==0x91) {
			*dest++ = 0xb7; // N-ARY SUMMATION
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x88 && src[2]==0x8f) {
			*dest++ = 0xb8; // N-ARY PRODUCT
			src += 3;
		} else if (src[0]==0xcf && src[1]==0x80) {
			*dest++ = 0xb9; // GREEK SMALL LETTER PI
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x88 && src[2]==0xab) {
			*dest++ = 0xba; // INTEGRAL
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xaa) {
			*dest++ = 0xbb; // FEMININE ORDINAL INDICATOR
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xba) {
			*dest++ = 0xbc; // MASCULINE ORDINAL INDICATOR
			src += 2;
		} else if (src[0]==0xce && src[1]==0xa9) {
			*dest++ = 0xbd; // GREEK CAPITAL LETTER OMEGA
			src += 2;
		} else if (src[0]==0xc3 && src[1]==0xa6) {
			*dest++ = 0xbe; // LATIN SMALL LETTER AE
			src += 2;
		} else if (src[0]==0xc3 && src[1]==0xb8) {
			*dest++ = 0xbf; // LATIN SMALL LETTER O WITH STROKE
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xbf) {
			*dest++ = 0xc0; // INVERTED QUESTION MARK
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xa1) {
			*dest++ = 0xc1; // INVERTED EXCLAMATION MARK
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xac) {
			*dest++ = 0xc2; // NOT SIGN
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x88 && src[2]==0x9a) {
			*dest++ = 0xc3; // SQUARE ROOT
			src += 3;
		} else if (src[0]==0xc6 && src[1]==0x92) {
			*dest++ = 0xc4; // LATIN SMALL LETTER F WITH HOOK
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x89 && src[2]==0x88) {
			*dest++ = 0xc5; // ALMOST EQUAL TO
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x88 && src[2]==0x86) {
			*dest++ = 0xc6; // INCREMENT
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xab) {
			*dest++ = 0xc7; // LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xbb) {
			*dest++ = 0xc8; // RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0xa6) {
			*dest++ = 0xc9; // HORIZONTAL ELLIPSIS
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xa0) {
			*dest++ = 0xca; // NO-BREAK SPACE
			src += 2;
		} else if (src[0]=='A' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0xcb; // LATIN CAPITAL LETTER A + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x80) {
			*dest++ = 0xcb; // LATIN CAPITAL LETTER A WITH GRAVE
			src += 2;
		} else if (src[0]=='A' && src[1]==0xcc && src[2]==0x83) {
			*dest++ = 0xcc; // LATIN CAPITAL LETTER A + COMBINING TILDE
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x83) {
			*dest++ = 0xcc; // LATIN CAPITAL LETTER A WITH TILDE
			src += 2;
		} else if (src[0]=='O' && src[1]==0xcc && src[2]==0x83) {
			*dest++ = 0xcd; // LATIN CAPITAL LETTER O + COMBINING TILDE
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x95) {
			*dest++ = 0xcd; // LATIN CAPITAL LETTER O WITH TILDE
			src += 2;
		} else if (src[0]==0xc5 && src[1]==0x92) {
			*dest++ = 0xce; // LATIN CAPITAL LIGATURE OE
			src += 2;
		} else if (src[0]==0xc5 && src[1]==0x93) {
			*dest++ = 0xcf; // LATIN SMALL LIGATURE OE
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0x93) {
			*dest++ = 0xd0; // EN DASH
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0x94) {
			*dest++ = 0xd1; // EM DASH
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0x9c) {
			*dest++ = 0xd2; // LEFT DOUBLE QUOTATION MARK
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0x9d) {
			*dest++ = 0xd3; // RIGHT DOUBLE QUOTATION MARK
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0x98) {
			*dest++ = 0xd4; // LEFT SINGLE QUOTATION MARK
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0x99) {
			*dest++ = 0xd5; // RIGHT SINGLE QUOTATION MARK
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xb7) {
			*dest++ = 0xd6; // DIVISION SIGN
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x97 && src[2]==0x8a) {
			*dest++ = 0xd7; // LOZENGE
			src += 3;
		} else if (src[0]=='y' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0xd8; // LATIN SMALL LETTER Y + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0xbf) {
			*dest++ = 0xd8; // LATIN SMALL LETTER Y WITH DIAERESIS
			src += 2;
		} else if (src[0]=='Y' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0xd9; // LATIN CAPITAL LETTER Y + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc5 && src[1]==0xb8) {
			*dest++ = 0xd9; // LATIN CAPITAL LETTER Y WITH DIAERESIS
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x81 && src[2]==0x84) {
			*dest++ = 0xda; // FRACTION SLASH
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x82 && src[2]==0xac) {
			*dest++ = 0xdb; // EURO SIGN
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0xb9) {
			*dest++ = 0xdc; // SINGLE LEFT-POINTING ANGLE QUOTATION MARK
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0xba) {
			*dest++ = 0xdd; // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
			src += 3;
		} else if (src[0]==0xef && src[1]==0xac && src[2]==0x81) {
			*dest++ = 0xde; // LATIN SMALL LIGATURE FI
			src += 3;
		} else if (src[0]==0xef && src[1]==0xac && src[2]==0x82) {
			*dest++ = 0xdf; // LATIN SMALL LIGATURE FL
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0xa1) {
			*dest++ = 0xe0; // DOUBLE DAGGER
			src += 3;
		} else if (src[0]==0xc2 && src[1]==0xb7) {
			*dest++ = 0xe1; // MIDDLE DOT
			src += 2;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0x9a) {
			*dest++ = 0xe2; // SINGLE LOW-9 QUOTATION MARK
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0x9e) {
			*dest++ = 0xe3; // DOUBLE LOW-9 QUOTATION MARK
			src += 3;
		} else if (src[0]==0xe2 && src[1]==0x80 && src[2]==0xb0) {
			*dest++ = 0xe4; // PER MILLE SIGN
			src += 3;
		} else if (src[0]=='A' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0xe5; // LATIN CAPITAL LETTER A + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x82) {
			*dest++ = 0xe5; // LATIN CAPITAL LETTER A WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='E' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0xe6; // LATIN CAPITAL LETTER E + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x8a) {
			*dest++ = 0xe6; // LATIN CAPITAL LETTER E WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='A' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0xe7; // LATIN CAPITAL LETTER A + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x81) {
			*dest++ = 0xe7; // LATIN CAPITAL LETTER A WITH ACUTE
			src += 2;
		} else if (src[0]=='E' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0xe8; // LATIN CAPITAL LETTER E + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x8b) {
			*dest++ = 0xe8; // LATIN CAPITAL LETTER E WITH DIAERESIS
			src += 2;
		} else if (src[0]=='E' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0xe9; // LATIN CAPITAL LETTER E + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x88) {
			*dest++ = 0xe9; // LATIN CAPITAL LETTER E WITH GRAVE
			src += 2;
		} else if (src[0]=='I' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0xea; // LATIN CAPITAL LETTER I + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x8d) {
			*dest++ = 0xea; // LATIN CAPITAL LETTER I WITH ACUTE
			src += 2;
		} else if (src[0]=='I' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0xeb; // LATIN CAPITAL LETTER I + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x8e) {
			*dest++ = 0xeb; // LATIN CAPITAL LETTER I WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='I' && src[1]==0xcc && src[2]==0x88) {
			*dest++ = 0xec; // LATIN CAPITAL LETTER I + COMBINING DIAERESIS
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x8f) {
			*dest++ = 0xec; // LATIN CAPITAL LETTER I WITH DIAERESIS
			src += 2;
		} else if (src[0]=='I' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0xed; // LATIN CAPITAL LETTER I + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x8c) {
			*dest++ = 0xed; // LATIN CAPITAL LETTER I WITH GRAVE
			src += 2;
		} else if (src[0]=='O' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0xee; // LATIN CAPITAL LETTER O + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x93) {
			*dest++ = 0xee; // LATIN CAPITAL LETTER O WITH ACUTE
			src += 2;
		} else if (src[0]=='O' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0xef; // LATIN CAPITAL LETTER O + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x94) {
			*dest++ = 0xef; // LATIN CAPITAL LETTER O WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]==0xef && src[1]==0xa3 && src[2]==0xbf) {
			*dest++ = 0xf0; // U+F8FF
			src += 3;
		} else if (src[0]=='O' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0xf1; // LATIN CAPITAL LETTER O + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x92) {
			*dest++ = 0xf1; // LATIN CAPITAL LETTER O WITH GRAVE
			src += 2;
		} else if (src[0]=='U' && src[1]==0xcc && src[2]==0x81) {
			*dest++ = 0xf2; // LATIN CAPITAL LETTER U + COMBINING ACUTE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x9a) {
			*dest++ = 0xf2; // LATIN CAPITAL LETTER U WITH ACUTE
			src += 2;
		} else if (src[0]=='U' && src[1]==0xcc && src[2]==0x82) {
			*dest++ = 0xf3; // LATIN CAPITAL LETTER U + COMBINING CIRCUMFLEX ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x9b) {
			*dest++ = 0xf3; // LATIN CAPITAL LETTER U WITH CIRCUMFLEX
			src += 2;
		} else if (src[0]=='U' && src[1]==0xcc && src[2]==0x80) {
			*dest++ = 0xf4; // LATIN CAPITAL LETTER U + COMBINING GRAVE ACCENT
			src += 3;
		} else if (src[0]==0xc3 && src[1]==0x99) {
			*dest++ = 0xf4; // LATIN CAPITAL LETTER U WITH GRAVE
			src += 2;
		} else if (src[0]==0xc4 && src[1]==0xb1) {
			*dest++ = 0xf5; // LATIN SMALL LETTER DOTLESS I
			src += 2;
		} else if (src[0]==0xcb && src[1]==0x86) {
			*dest++ = 0xf6; // MODIFIER LETTER CIRCUMFLEX ACCENT
			src += 2;
		} else if (src[0]==0xcb && src[1]==0x9c) {
			*dest++ = 0xf7; // SMALL TILDE
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xaf) {
			*dest++ = 0xf8; // MACRON
			src += 2;
		} else if (src[0]==0xcb && src[1]==0x98) {
			*dest++ = 0xf9; // BREVE
			src += 2;
		} else if (src[0]==0xcb && src[1]==0x99) {
			*dest++ = 0xfa; // DOT ABOVE
			src += 2;
		} else if (src[0]==0xcb && src[1]==0x9a) {
			*dest++ = 0xfb; // RING ABOVE
			src += 2;
		} else if (src[0]==0xc2 && src[1]==0xb8) {
			*dest++ = 0xfc; // CEDILLA
			src += 2;
		} else if (src[0]==0xcb && src[1]==0x9d) {
			*dest++ = 0xfd; // DOUBLE ACUTE ACCENT
			src += 2;
		} else if (src[0]==0xcb && src[1]==0x9b) {
			*dest++ = 0xfe; // OGONEK
			src += 2;
		} else if (src[0]==0xcb && src[1]==0x87) {
			*dest++ = 0xff; // CARON
			src += 2;
		} else {
			*dest++ = '?';
			src += 1;
			badchar = 1;
		}
	}

	// Name overflows or has "?": append hash to distinguish
	// (cf MacOS appending the CNID to invalid names)
	if (*src!=0 || badchar) {
		uint16_t hash = 0;
		for (int i=0; utf8[i]!=0; i++) {
			hash = hash*31 + (unsigned char)utf8[i];
		}

		// Shorten name if needed to make room for hash
		if (dest > roman+32-5) dest = roman+32-5;

		char append[] = "#9999";
		sprintf(append+1, "%04X", hash);
		memcpy(dest, append, 5);
		dest += 5;
	}

	roman[0] = dest-roman-1; // length byte
}

// int main(int argc, char **argv) {
// 	unsigned char romish[32];
//
// 	for (int i=1; i<argc; i++) {
// 		mr31name(romish, argv[i]);
// 		printf("%.*s\n", romish[0], romish+1);
// 	}
//
// 	return 0;
// }
