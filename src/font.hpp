#pragma once
#include <iostream>
#include <cstdio>
#include <vector>
#include <cstdint>

struct Position {
	Position();
	Position(std::size_t x, std::size_t y);
	std::size_t x;
	std::size_t y;
};

class Box {
	public:
		Box();
		Box(std::size_t x, std::size_t y, std::size_t w, std::size_t h);
		std::size_t left() const;
		std::size_t right() const;
		std::size_t top() const;
		std::size_t bottom() const;
		std::size_t width() const;
		std::size_t height() const;

		std::size_t size() const;

	private:
		Position    m_position;
		std::size_t m_width;
		std::size_t m_height;
};
std::ostream& operator<<(std::ostream& stream, const Box& box);
bool operator==(const Box& lhs, const Box& rhs);
bool operator!=(const Box& lhs, const Box& rhs);
bool operator<(const Box& lhs, const Box& rhs);
bool operator>(const Box& lhs, const Box& rhs);


class Glyph {
	public:
		Glyph();
		Glyph(const std::vector<std::uint8_t>& data, const std::uint8_t c);
		const Box&                box() const;
		std::uint8_t              character() const;
		char                      printable_character() const;
		bool                      empty() const;

		std::uint8_t row(std::size_t n) const;
		std::uint8_t column(std::size_t n) const;

	private:
		std::uint8_t              m_char;
		Box                       m_box;
		std::vector<std::uint8_t> m_data;
		bool                      m_empty;
		friend bool operator==(const Glyph& lhs, const Glyph& rhs);
		friend bool operator<(const Glyph& lhs, const Glyph& rhs);
};
bool operator==(const Glyph& lhs, const Glyph& rhs);
bool operator!=(const Glyph& lhs, const Glyph& rhs);
bool operator<(const Glyph& lhs, const Glyph& rhs);
bool operator>(const Glyph& lhs, const Glyph& rhs);

class Font {
	public:
		void load(FILE* source);
		const std::vector<Glyph>& glyphs() const;

	private:
		std::vector<Glyph> m_glyphs;
};
