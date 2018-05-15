#include "font.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <boost/scope_exit.hpp>
#include <png.h>


namespace /* anonymous */ {

	void user_error_fn(png_structp /* png_ptr */, png_const_charp error_msg) {
		std::cout << "PNG error: " << error_msg << "\n";
	}

	void user_warning_fn(png_structp /* png_ptr */, png_const_charp warning_msg) {
		std::cout << "PNG warning: " << warning_msg << "\n";
	}

} /* anonymous */


Position::Position()
: x{0}
, y{0}
{}

Position::Position(std::size_t x, std::size_t y)
: x{x}
, y{y}
{}

Box::Box()
: m_width {0}
, m_height{0}
{}

Box::Box(std::size_t x, std::size_t y, std::size_t w, std::size_t h)
: m_position{x, y}
, m_width{w}
, m_height{h}
{}

std::size_t Box::left() const {
	return m_position.x;
}

std::size_t Box::right() const {
	return m_position.x + m_width - 1;
}

std::size_t Box::top() const {
	return m_position.y;
}

std::size_t Box::bottom() const {
	return m_position.y + m_height - 1;
}

std::size_t Box::width() const {
	return m_width;
}

std::size_t Box::height() const {
	return m_height;
}

std::size_t Box::size() const {
	return m_width * m_height;
}

bool operator==(const Box& lhs, const Box& rhs) {
	return lhs.left()   == rhs.left()  &&
	       lhs.top()    == rhs.top()   &&
	       lhs.width()  == rhs.width() &&
	       lhs.height() == rhs.height();
}

bool operator!=(const Box& lhs, const Box& rhs) {
	return !(lhs == rhs);
}

bool operator<(const Box& lhs, const Box& rhs) {
	return lhs.left()   < rhs.left()   || ((lhs.left()  == rhs.left()  &&
	       lhs.top()    < rhs.top())   || ((lhs.top()   == rhs.top()   &&
	       lhs.width()  < rhs.width()) || ((lhs.width() == rhs.width() &&
	       lhs.height() < rhs.height()))));
}

bool operator>(const Box& lhs, const Box& rhs) {
	return !(lhs < rhs) && !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& stream, const Box& box) {
	return stream << '[' << box.left() << ", " << box.top() << " | " << box.width() << "x" << box.height() << ']';
}

Glyph::Glyph()
: m_char  {0}
, m_empty {true}
{}

Glyph::Glyph(const std::vector<std::uint8_t>& data, std::uint8_t c)
: m_char  {c}
, m_data  {data}
, m_empty {true}
{
	std::size_t l = 7, r = 0, t = 7, b = 0;
	for(std::size_t x = 0; x < 8; ++x) {
		for(std::size_t y = 0; y < 8; ++y) {
			if(m_data[x+8*y]) {
				l = std::min(l, x);
				r = std::max(r, x);
				t = std::min(t, y);
				b = std::max(b, y);
			}
		}
	}
	// If char is empty
	if(r<l) {
		std::swap(l, r);
		std::swap(t, b);
	}
	else {
		m_empty = false;
	}

	m_box = Box{l, t, r - l + 1, b - t + 1};
}

const Box& Glyph::box() const {
	return m_box;
}

bool operator==(const Glyph& lhs, const Glyph& rhs) {
	return lhs.m_char == rhs.m_char && lhs.m_data == rhs.m_data;
}

bool operator!=(const Glyph& lhs, const Glyph& rhs) {
	return !(lhs == rhs);
}

bool operator<(const Glyph& lhs, const Glyph& rhs) {
	return lhs.m_char < rhs.m_char;
}

bool operator>(const Glyph& lhs, const Glyph& rhs) {
	return !(lhs < rhs) && !(lhs == rhs);
}

std::uint8_t Glyph::character() const {
	return m_char;
}

char Glyph::printable_character() const {
	return (m_char > 0x20 && m_char < 127) ? m_char : '.';
}

bool Glyph::empty() const {
	return m_empty;
}

std::uint8_t Glyph::row(std::size_t n) const {
	assert(n < 8);
	return m_data[n*8];
}

std::uint8_t Glyph::column(std::size_t n) const {
	assert(n < 8);

	std::uint8_t result{};
	for(std::size_t y = 0; y < 8; ++y) {
		if(m_data[n+8*y]) {
			result |= 1 << y;
		}
	}

	return result;
}


void Font::load(FILE* source) {
	std::vector<std::uint8_t> header(8);
	fread(&header[0], 1, header.size(), source);
	if(png_sig_cmp(&header[0], 0, header.size())) {
		throw std::runtime_error("file does not smell like PNG.");
	}

	png_structp png_ptr = png_create_read_struct(
		PNG_LIBPNG_VER_STRING,
		nullptr,
		user_error_fn,
		user_warning_fn
	);
	if(!png_ptr) {
		throw std::runtime_error("could not initialise PNG read struct.");
	}
	BOOST_SCOPE_EXIT(&png_ptr) {
		png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
	} BOOST_SCOPE_EXIT_END

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) {
		throw std::runtime_error("could not initialise PNG info struct.");
	}
	BOOST_SCOPE_EXIT(&png_ptr, &info_ptr) {
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
	} BOOST_SCOPE_EXIT_END

	png_infop end_info = png_create_info_struct(png_ptr);
	if(!end_info) {
		throw std::runtime_error("could not initialise PNG end-info struct.");
	}
	BOOST_SCOPE_EXIT(&png_ptr, &end_info) {
		png_destroy_read_struct(&png_ptr, &end_info, (png_infopp)NULL);
	} BOOST_SCOPE_EXIT_END

	png_init_io(png_ptr, source);
	png_set_sig_bytes(png_ptr, header.size());
	int png_transforms = PNG_TRANSFORM_STRIP_ALPHA
	                   | PNG_TRANSFORM_PACKING
	                   | PNG_TRANSFORM_EXPAND
	                   | PNG_TRANSFORM_STRIP_16
	                   ;
	png_read_png(png_ptr, info_ptr, png_transforms, NULL);
	png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

	const std::size_t width  = png_get_image_width(png_ptr, info_ptr);
	const std::size_t height = png_get_image_height(png_ptr, info_ptr);
	if(width % 8 != 0 || height % 8 != 0 || (width/8)*(height/8) > 256) {
		throw std::runtime_error("image has invalid dimensions: " + std::to_string(width) + "x" + std::to_string(height) + " (w&h should be multiple of 8, max 255 8x8 cells)");
	}
	const png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	if(bit_depth != 8) {
		throw std::runtime_error("image has invalid bit depth: " + std::to_string(bit_depth) + " != 8");
	}
	const png_byte color_type = png_get_color_type(png_ptr, info_ptr);
	if(color_type != PNG_COLOR_TYPE_RGB) {
		throw std::runtime_error("image has invalid colour depth: " + std::to_string(color_type) + " != RGBA");
	}
	const png_byte channels = png_get_channels(png_ptr, info_ptr);
	if(channels != 3) {
		throw std::runtime_error("image has invalid number of channels: " + std::to_string(channels) + " != 3");
	}

	std::size_t ascii = 0;
	for(std::size_t y = 0; y < height / 8; ++y) {
		for(std::size_t x = 0; x < width / 8; ++x) {
			std::vector<std::uint8_t> data(8*8);
			for(std::size_t j = 0; j < 8; ++j) {
				for(std::size_t i = 0; i < 8; ++i) {
//					const unsigned char r = row_pointers[8*y+j][3*(8*x+i)+0];
//					const unsigned char g = row_pointers[8*y+j][3*(8*x+i)+1];
					const unsigned char b = row_pointers[8*y+j][3*(8*x+i)+2];
					data[i+j*8] = b != 0;
				}
			}
			m_glyphs.emplace_back(data, ascii);
			++ascii;
		}
	}
}

const std::vector<Glyph>& Font::glyphs() const {
	return m_glyphs;
}
