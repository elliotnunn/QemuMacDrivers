#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "unicode.h"

/*
TODO:
Preserve file extensions
Represent unknown chars with fewer than one-per-UTF8-byte "?"s
*/

static int toMacRoman(char **utf8);

void mr31name(unsigned char *roman, const char *utf8) {
	char *this = utf8;
	int badchar = 0, incomplete = 1;

	roman[0] = 0;
	while (roman[0] < 31) {
		int ch = toMacRoman(&this); // increments this
		if (ch < 0) {
			roman[++(roman[0])] = '?';
			badchar = 1;
		} else if (ch == ':') {
			roman[++(roman[0])] = '/';
		} else if (ch == 0) {
			incomplete = 0;
			break;
		} else {
			roman[++(roman[0])] = ch;
		}
	}

	// Name overflows or has "?": append hash to distinguish
	// (cf MacOS appending the CNID to invalid names)
	if (incomplete || badchar) {
		uint16_t hash = 0;
		for (int i=0; utf8[i]!=0; i++) {
			hash = hash*31 + (unsigned char)utf8[i];
		}

		// Shorten name if needed to make room for hash
		if (roman[0] > 26) roman[0] = 26;

		char append[6];
		sprintf(append, "#%04x", hash);
		memcpy(roman+1+roman[0], append, 5);
		roman[0] += 5;
	}
}

// Simpler rules and shorter limit for volume names
void mr27name(unsigned char *roman, const char *utf8) {
	const char *this = utf8;

	roman[0] = 0;
	while (roman[0] < 27) {
		int ch = toMacRoman(&this); // increments this
		if (ch < 0) {
			roman[++(roman[0])] = '?';
		} else if (ch == ':') {
			roman[++(roman[0])] = '/';
		} else if (ch == 0) {
			break;
		} else {
			roman[++(roman[0])] = ch;
		}
	}
}

static int toMacRoman(char **utf8) {
	int nbytes, roman;

	if ((*utf8)[0]<0x80) {
		// ASCII
		nbytes = 1;
		roman = (unsigned char)(*utf8)[0];
	} else if ((*utf8)[0]=='A' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN CAPITAL LETTER A + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0x80;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x84) {
		// LATIN CAPITAL LETTER A WITH DIAERESIS
		nbytes = 2;
		roman = 0x80;
	} else if ((*utf8)[0]=='A' && (*utf8)[1]==0xcc && (*utf8)[2]==0x8a) {
		// LATIN CAPITAL LETTER A + COMBINING RING ABOVE
		nbytes = 3;
		roman = 0x81;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x85) {
		// LATIN CAPITAL LETTER A WITH RING ABOVE
		nbytes = 2;
		roman = 0x81;
	} else if ((*utf8)[0]=='C' && (*utf8)[1]==0xcc && (*utf8)[2]==0xa7) {
		// LATIN CAPITAL LETTER C + COMBINING CEDILLA
		nbytes = 3;
		roman = 0x82;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x87) {
		// LATIN CAPITAL LETTER C WITH CEDILLA
		nbytes = 2;
		roman = 0x82;
	} else if ((*utf8)[0]=='E' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN CAPITAL LETTER E + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0x83;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x89) {
		// LATIN CAPITAL LETTER E WITH ACUTE
		nbytes = 2;
		roman = 0x83;
	} else if ((*utf8)[0]=='N' && (*utf8)[1]==0xcc && (*utf8)[2]==0x83) {
		// LATIN CAPITAL LETTER N + COMBINING TILDE
		nbytes = 3;
		roman = 0x84;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x91) {
		// LATIN CAPITAL LETTER N WITH TILDE
		nbytes = 2;
		roman = 0x84;
	} else if ((*utf8)[0]=='O' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN CAPITAL LETTER O + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0x85;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x96) {
		// LATIN CAPITAL LETTER O WITH DIAERESIS
		nbytes = 2;
		roman = 0x85;
	} else if ((*utf8)[0]=='U' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN CAPITAL LETTER U + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0x86;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x9c) {
		// LATIN CAPITAL LETTER U WITH DIAERESIS
		nbytes = 2;
		roman = 0x86;
	} else if ((*utf8)[0]=='a' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN SMALL LETTER A + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0x87;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa1) {
		// LATIN SMALL LETTER A WITH ACUTE
		nbytes = 2;
		roman = 0x87;
	} else if ((*utf8)[0]=='a' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN SMALL LETTER A + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0x88;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa0) {
		// LATIN SMALL LETTER A WITH GRAVE
		nbytes = 2;
		roman = 0x88;
	} else if ((*utf8)[0]=='a' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN SMALL LETTER A + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0x89;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa2) {
		// LATIN SMALL LETTER A WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0x89;
	} else if ((*utf8)[0]=='a' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN SMALL LETTER A + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0x8a;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa4) {
		// LATIN SMALL LETTER A WITH DIAERESIS
		nbytes = 2;
		roman = 0x8a;
	} else if ((*utf8)[0]=='a' && (*utf8)[1]==0xcc && (*utf8)[2]==0x83) {
		// LATIN SMALL LETTER A + COMBINING TILDE
		nbytes = 3;
		roman = 0x8b;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa3) {
		// LATIN SMALL LETTER A WITH TILDE
		nbytes = 2;
		roman = 0x8b;
	} else if ((*utf8)[0]=='a' && (*utf8)[1]==0xcc && (*utf8)[2]==0x8a) {
		// LATIN SMALL LETTER A + COMBINING RING ABOVE
		nbytes = 3;
		roman = 0x8c;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa5) {
		// LATIN SMALL LETTER A WITH RING ABOVE
		nbytes = 2;
		roman = 0x8c;
	} else if ((*utf8)[0]=='c' && (*utf8)[1]==0xcc && (*utf8)[2]==0xa7) {
		// LATIN SMALL LETTER C + COMBINING CEDILLA
		nbytes = 3;
		roman = 0x8d;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa7) {
		// LATIN SMALL LETTER C WITH CEDILLA
		nbytes = 2;
		roman = 0x8d;
	} else if ((*utf8)[0]=='e' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN SMALL LETTER E + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0x8e;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa9) {
		// LATIN SMALL LETTER E WITH ACUTE
		nbytes = 2;
		roman = 0x8e;
	} else if ((*utf8)[0]=='e' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN SMALL LETTER E + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0x8f;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa8) {
		// LATIN SMALL LETTER E WITH GRAVE
		nbytes = 2;
		roman = 0x8f;
	} else if ((*utf8)[0]=='e' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN SMALL LETTER E + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0x90;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xaa) {
		// LATIN SMALL LETTER E WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0x90;
	} else if ((*utf8)[0]=='e' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN SMALL LETTER E + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0x91;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xab) {
		// LATIN SMALL LETTER E WITH DIAERESIS
		nbytes = 2;
		roman = 0x91;
	} else if ((*utf8)[0]=='i' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN SMALL LETTER I + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0x92;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xad) {
		// LATIN SMALL LETTER I WITH ACUTE
		nbytes = 2;
		roman = 0x92;
	} else if ((*utf8)[0]=='i' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN SMALL LETTER I + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0x93;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xac) {
		// LATIN SMALL LETTER I WITH GRAVE
		nbytes = 2;
		roman = 0x93;
	} else if ((*utf8)[0]=='i' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN SMALL LETTER I + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0x94;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xae) {
		// LATIN SMALL LETTER I WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0x94;
	} else if ((*utf8)[0]=='i' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN SMALL LETTER I + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0x95;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xaf) {
		// LATIN SMALL LETTER I WITH DIAERESIS
		nbytes = 2;
		roman = 0x95;
	} else if ((*utf8)[0]=='n' && (*utf8)[1]==0xcc && (*utf8)[2]==0x83) {
		// LATIN SMALL LETTER N + COMBINING TILDE
		nbytes = 3;
		roman = 0x96;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb1) {
		// LATIN SMALL LETTER N WITH TILDE
		nbytes = 2;
		roman = 0x96;
	} else if ((*utf8)[0]=='o' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN SMALL LETTER O + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0x97;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb3) {
		// LATIN SMALL LETTER O WITH ACUTE
		nbytes = 2;
		roman = 0x97;
	} else if ((*utf8)[0]=='o' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN SMALL LETTER O + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0x98;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb2) {
		// LATIN SMALL LETTER O WITH GRAVE
		nbytes = 2;
		roman = 0x98;
	} else if ((*utf8)[0]=='o' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN SMALL LETTER O + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0x99;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb4) {
		// LATIN SMALL LETTER O WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0x99;
	} else if ((*utf8)[0]=='o' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN SMALL LETTER O + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0x9a;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb6) {
		// LATIN SMALL LETTER O WITH DIAERESIS
		nbytes = 2;
		roman = 0x9a;
	} else if ((*utf8)[0]=='o' && (*utf8)[1]==0xcc && (*utf8)[2]==0x83) {
		// LATIN SMALL LETTER O + COMBINING TILDE
		nbytes = 3;
		roman = 0x9b;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb5) {
		// LATIN SMALL LETTER O WITH TILDE
		nbytes = 2;
		roman = 0x9b;
	} else if ((*utf8)[0]=='u' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN SMALL LETTER U + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0x9c;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xba) {
		// LATIN SMALL LETTER U WITH ACUTE
		nbytes = 2;
		roman = 0x9c;
	} else if ((*utf8)[0]=='u' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN SMALL LETTER U + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0x9d;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb9) {
		// LATIN SMALL LETTER U WITH GRAVE
		nbytes = 2;
		roman = 0x9d;
	} else if ((*utf8)[0]=='u' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN SMALL LETTER U + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0x9e;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xbb) {
		// LATIN SMALL LETTER U WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0x9e;
	} else if ((*utf8)[0]=='u' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN SMALL LETTER U + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0x9f;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xbc) {
		// LATIN SMALL LETTER U WITH DIAERESIS
		nbytes = 2;
		roman = 0x9f;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0xa0) {
		// DAGGER
		nbytes = 3;
		roman = 0xa0;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xb0) {
		// DEGREE SIGN
		nbytes = 2;
		roman = 0xa1;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xa2) {
		// CENT SIGN
		nbytes = 2;
		roman = 0xa2;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xa3) {
		// POUND SIGN
		nbytes = 2;
		roman = 0xa3;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xa7) {
		// SECTION SIGN
		nbytes = 2;
		roman = 0xa4;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0xa2) {
		// BULLET
		nbytes = 3;
		roman = 0xa5;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xb6) {
		// PILCROW SIGN
		nbytes = 2;
		roman = 0xa6;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x9f) {
		// LATIN SMALL LETTER SHARP S
		nbytes = 2;
		roman = 0xa7;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xae) {
		// REGISTERED SIGN
		nbytes = 2;
		roman = 0xa8;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xa9) {
		// COPYRIGHT SIGN
		nbytes = 2;
		roman = 0xa9;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x84 && (*utf8)[2]==0xa2) {
		// TRADE MARK SIGN
		nbytes = 3;
		roman = 0xaa;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xb4) {
		// ACUTE ACCENT
		nbytes = 2;
		roman = 0xab;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xa8) {
		// DIAERESIS
		nbytes = 2;
		roman = 0xac;
	} else if ((*utf8)[0]=='=' && (*utf8)[1]==0xcc && (*utf8)[2]==0xb8) {
		// EQUALS SIGN + COMBINING LONG SOLIDUS OVERLAY
		nbytes = 3;
		roman = 0xad;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x89 && (*utf8)[2]==0xa0) {
		// NOT EQUAL TO
		nbytes = 3;
		roman = 0xad;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x86) {
		// LATIN CAPITAL LETTER AE
		nbytes = 2;
		roman = 0xae;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x98) {
		// LATIN CAPITAL LETTER O WITH STROKE
		nbytes = 2;
		roman = 0xaf;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x88 && (*utf8)[2]==0x9e) {
		// INFINITY
		nbytes = 3;
		roman = 0xb0;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xb1) {
		// PLUS-MINUS SIGN
		nbytes = 2;
		roman = 0xb1;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x89 && (*utf8)[2]==0xa4) {
		// LESS-THAN OR EQUAL TO
		nbytes = 3;
		roman = 0xb2;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x89 && (*utf8)[2]==0xa5) {
		// GREATER-THAN OR EQUAL TO
		nbytes = 3;
		roman = 0xb3;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xa5) {
		// YEN SIGN
		nbytes = 2;
		roman = 0xb4;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xb5) {
		// MICRO SIGN
		nbytes = 2;
		roman = 0xb5;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x88 && (*utf8)[2]==0x82) {
		// PARTIAL DIFFERENTIAL
		nbytes = 3;
		roman = 0xb6;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x88 && (*utf8)[2]==0x91) {
		// N-ARY SUMMATION
		nbytes = 3;
		roman = 0xb7;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x88 && (*utf8)[2]==0x8f) {
		// N-ARY PRODUCT
		nbytes = 3;
		roman = 0xb8;
	} else if ((*utf8)[0]==0xcf && (*utf8)[1]==0x80) {
		// GREEK SMALL LETTER PI
		nbytes = 2;
		roman = 0xb9;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x88 && (*utf8)[2]==0xab) {
		// INTEGRAL
		nbytes = 3;
		roman = 0xba;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xaa) {
		// FEMININE ORDINAL INDICATOR
		nbytes = 2;
		roman = 0xbb;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xba) {
		// MASCULINE ORDINAL INDICATOR
		nbytes = 2;
		roman = 0xbc;
	} else if ((*utf8)[0]==0xce && (*utf8)[1]==0xa9) {
		// GREEK CAPITAL LETTER OMEGA
		nbytes = 2;
		roman = 0xbd;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xa6) {
		// LATIN SMALL LETTER AE
		nbytes = 2;
		roman = 0xbe;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb8) {
		// LATIN SMALL LETTER O WITH STROKE
		nbytes = 2;
		roman = 0xbf;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xbf) {
		// INVERTED QUESTION MARK
		nbytes = 2;
		roman = 0xc0;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xa1) {
		// INVERTED EXCLAMATION MARK
		nbytes = 2;
		roman = 0xc1;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xac) {
		// NOT SIGN
		nbytes = 2;
		roman = 0xc2;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x88 && (*utf8)[2]==0x9a) {
		// SQUARE ROOT
		nbytes = 3;
		roman = 0xc3;
	} else if ((*utf8)[0]==0xc6 && (*utf8)[1]==0x92) {
		// LATIN SMALL LETTER F WITH HOOK
		nbytes = 2;
		roman = 0xc4;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x89 && (*utf8)[2]==0x88) {
		// ALMOST EQUAL TO
		nbytes = 3;
		roman = 0xc5;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x88 && (*utf8)[2]==0x86) {
		// INCREMENT
		nbytes = 3;
		roman = 0xc6;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xab) {
		// LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
		nbytes = 2;
		roman = 0xc7;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xbb) {
		// RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
		nbytes = 2;
		roman = 0xc8;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0xa6) {
		// HORIZONTAL ELLIPSIS
		nbytes = 3;
		roman = 0xc9;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xa0) {
		// NO-BREAK SPACE
		nbytes = 2;
		roman = 0xca;
	} else if ((*utf8)[0]=='A' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN CAPITAL LETTER A + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0xcb;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x80) {
		// LATIN CAPITAL LETTER A WITH GRAVE
		nbytes = 2;
		roman = 0xcb;
	} else if ((*utf8)[0]=='A' && (*utf8)[1]==0xcc && (*utf8)[2]==0x83) {
		// LATIN CAPITAL LETTER A + COMBINING TILDE
		nbytes = 3;
		roman = 0xcc;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x83) {
		// LATIN CAPITAL LETTER A WITH TILDE
		nbytes = 2;
		roman = 0xcc;
	} else if ((*utf8)[0]=='O' && (*utf8)[1]==0xcc && (*utf8)[2]==0x83) {
		// LATIN CAPITAL LETTER O + COMBINING TILDE
		nbytes = 3;
		roman = 0xcd;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x95) {
		// LATIN CAPITAL LETTER O WITH TILDE
		nbytes = 2;
		roman = 0xcd;
	} else if ((*utf8)[0]==0xc5 && (*utf8)[1]==0x92) {
		// LATIN CAPITAL LIGATURE OE
		nbytes = 2;
		roman = 0xce;
	} else if ((*utf8)[0]==0xc5 && (*utf8)[1]==0x93) {
		// LATIN SMALL LIGATURE OE
		nbytes = 2;
		roman = 0xcf;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0x93) {
		// EN DASH
		nbytes = 3;
		roman = 0xd0;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0x94) {
		// EM DASH
		nbytes = 3;
		roman = 0xd1;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0x9c) {
		// LEFT DOUBLE QUOTATION MARK
		nbytes = 3;
		roman = 0xd2;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0x9d) {
		// RIGHT DOUBLE QUOTATION MARK
		nbytes = 3;
		roman = 0xd3;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0x98) {
		// LEFT SINGLE QUOTATION MARK
		nbytes = 3;
		roman = 0xd4;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0x99) {
		// RIGHT SINGLE QUOTATION MARK
		nbytes = 3;
		roman = 0xd5;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xb7) {
		// DIVISION SIGN
		nbytes = 2;
		roman = 0xd6;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x97 && (*utf8)[2]==0x8a) {
		// LOZENGE
		nbytes = 3;
		roman = 0xd7;
	} else if ((*utf8)[0]=='y' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN SMALL LETTER Y + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0xd8;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0xbf) {
		// LATIN SMALL LETTER Y WITH DIAERESIS
		nbytes = 2;
		roman = 0xd8;
	} else if ((*utf8)[0]=='Y' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN CAPITAL LETTER Y + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0xd9;
	} else if ((*utf8)[0]==0xc5 && (*utf8)[1]==0xb8) {
		// LATIN CAPITAL LETTER Y WITH DIAERESIS
		nbytes = 2;
		roman = 0xd9;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x81 && (*utf8)[2]==0x84) {
		// FRACTION SLASH
		nbytes = 3;
		roman = 0xda;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x82 && (*utf8)[2]==0xac) {
		// EURO SIGN
		nbytes = 3;
		roman = 0xdb;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0xb9) {
		// SINGLE LEFT-POINTING ANGLE QUOTATION MARK
		nbytes = 3;
		roman = 0xdc;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0xba) {
		// SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
		nbytes = 3;
		roman = 0xdd;
	} else if ((*utf8)[0]==0xef && (*utf8)[1]==0xac && (*utf8)[2]==0x81) {
		// LATIN SMALL LIGATURE FI
		nbytes = 3;
		roman = 0xde;
	} else if ((*utf8)[0]==0xef && (*utf8)[1]==0xac && (*utf8)[2]==0x82) {
		// LATIN SMALL LIGATURE FL
		nbytes = 3;
		roman = 0xdf;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0xa1) {
		// DOUBLE DAGGER
		nbytes = 3;
		roman = 0xe0;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xb7) {
		// MIDDLE DOT
		nbytes = 2;
		roman = 0xe1;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0x9a) {
		// SINGLE LOW-9 QUOTATION MARK
		nbytes = 3;
		roman = 0xe2;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0x9e) {
		// DOUBLE LOW-9 QUOTATION MARK
		nbytes = 3;
		roman = 0xe3;
	} else if ((*utf8)[0]==0xe2 && (*utf8)[1]==0x80 && (*utf8)[2]==0xb0) {
		// PER MILLE SIGN
		nbytes = 3;
		roman = 0xe4;
	} else if ((*utf8)[0]=='A' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN CAPITAL LETTER A + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0xe5;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x82) {
		// LATIN CAPITAL LETTER A WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0xe5;
	} else if ((*utf8)[0]=='E' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN CAPITAL LETTER E + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0xe6;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x8a) {
		// LATIN CAPITAL LETTER E WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0xe6;
	} else if ((*utf8)[0]=='A' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN CAPITAL LETTER A + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0xe7;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x81) {
		// LATIN CAPITAL LETTER A WITH ACUTE
		nbytes = 2;
		roman = 0xe7;
	} else if ((*utf8)[0]=='E' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN CAPITAL LETTER E + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0xe8;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x8b) {
		// LATIN CAPITAL LETTER E WITH DIAERESIS
		nbytes = 2;
		roman = 0xe8;
	} else if ((*utf8)[0]=='E' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN CAPITAL LETTER E + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0xe9;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x88) {
		// LATIN CAPITAL LETTER E WITH GRAVE
		nbytes = 2;
		roman = 0xe9;
	} else if ((*utf8)[0]=='I' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN CAPITAL LETTER I + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0xea;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x8d) {
		// LATIN CAPITAL LETTER I WITH ACUTE
		nbytes = 2;
		roman = 0xea;
	} else if ((*utf8)[0]=='I' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN CAPITAL LETTER I + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0xeb;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x8e) {
		// LATIN CAPITAL LETTER I WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0xeb;
	} else if ((*utf8)[0]=='I' && (*utf8)[1]==0xcc && (*utf8)[2]==0x88) {
		// LATIN CAPITAL LETTER I + COMBINING DIAERESIS
		nbytes = 3;
		roman = 0xec;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x8f) {
		// LATIN CAPITAL LETTER I WITH DIAERESIS
		nbytes = 2;
		roman = 0xec;
	} else if ((*utf8)[0]=='I' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN CAPITAL LETTER I + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0xed;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x8c) {
		// LATIN CAPITAL LETTER I WITH GRAVE
		nbytes = 2;
		roman = 0xed;
	} else if ((*utf8)[0]=='O' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN CAPITAL LETTER O + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0xee;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x93) {
		// LATIN CAPITAL LETTER O WITH ACUTE
		nbytes = 2;
		roman = 0xee;
	} else if ((*utf8)[0]=='O' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN CAPITAL LETTER O + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0xef;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x94) {
		// LATIN CAPITAL LETTER O WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0xef;
	} else if ((*utf8)[0]==0xef && (*utf8)[1]==0xa3 && (*utf8)[2]==0xbf) {
		// U+F8FF
		nbytes = 3;
		roman = 0xf0;
	} else if ((*utf8)[0]=='O' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN CAPITAL LETTER O + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0xf1;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x92) {
		// LATIN CAPITAL LETTER O WITH GRAVE
		nbytes = 2;
		roman = 0xf1;
	} else if ((*utf8)[0]=='U' && (*utf8)[1]==0xcc && (*utf8)[2]==0x81) {
		// LATIN CAPITAL LETTER U + COMBINING ACUTE ACCENT
		nbytes = 3;
		roman = 0xf2;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x9a) {
		// LATIN CAPITAL LETTER U WITH ACUTE
		nbytes = 2;
		roman = 0xf2;
	} else if ((*utf8)[0]=='U' && (*utf8)[1]==0xcc && (*utf8)[2]==0x82) {
		// LATIN CAPITAL LETTER U + COMBINING CIRCUMFLEX ACCENT
		nbytes = 3;
		roman = 0xf3;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x9b) {
		// LATIN CAPITAL LETTER U WITH CIRCUMFLEX
		nbytes = 2;
		roman = 0xf3;
	} else if ((*utf8)[0]=='U' && (*utf8)[1]==0xcc && (*utf8)[2]==0x80) {
		// LATIN CAPITAL LETTER U + COMBINING GRAVE ACCENT
		nbytes = 3;
		roman = 0xf4;
	} else if ((*utf8)[0]==0xc3 && (*utf8)[1]==0x99) {
		// LATIN CAPITAL LETTER U WITH GRAVE
		nbytes = 2;
		roman = 0xf4;
	} else if ((*utf8)[0]==0xc4 && (*utf8)[1]==0xb1) {
		// LATIN SMALL LETTER DOTLESS I
		nbytes = 2;
		roman = 0xf5;
	} else if ((*utf8)[0]==0xcb && (*utf8)[1]==0x86) {
		// MODIFIER LETTER CIRCUMFLEX ACCENT
		nbytes = 2;
		roman = 0xf6;
	} else if ((*utf8)[0]==0xcb && (*utf8)[1]==0x9c) {
		// SMALL TILDE
		nbytes = 2;
		roman = 0xf7;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xaf) {
		// MACRON
		nbytes = 2;
		roman = 0xf8;
	} else if ((*utf8)[0]==0xcb && (*utf8)[1]==0x98) {
		// BREVE
		nbytes = 2;
		roman = 0xf9;
	} else if ((*utf8)[0]==0xcb && (*utf8)[1]==0x99) {
		// DOT ABOVE
		nbytes = 2;
		roman = 0xfa;
	} else if ((*utf8)[0]==0xcb && (*utf8)[1]==0x9a) {
		// RING ABOVE
		nbytes = 2;
		roman = 0xfb;
	} else if ((*utf8)[0]==0xc2 && (*utf8)[1]==0xb8) {
		// CEDILLA
		nbytes = 2;
		roman = 0xfc;
	} else if ((*utf8)[0]==0xcb && (*utf8)[1]==0x9d) {
		// DOUBLE ACUTE ACCENT
		nbytes = 2;
		roman = 0xfd;
	} else if ((*utf8)[0]==0xcb && (*utf8)[1]==0x9b) {
		// OGONEK
		nbytes = 2;
		roman = 0xfe;
	} else if ((*utf8)[0]==0xcb && (*utf8)[1]==0x87) {
		// CARON
		nbytes = 2;
		roman = 0xff;
	} else {
		// unknown... absorb one byte
		nbytes = 1;
		roman = -1;
	}

	(*utf8) += nbytes;
	return roman;
}
