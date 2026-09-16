#pragma once

struct Colour {
	int r, g, b, a;
	Colour() : r(0), g(0), b(0), a(255) {}
	Colour(int r, int g, int b, int a = 255) : r(r), g(g), b(b), a(a) {}
};
